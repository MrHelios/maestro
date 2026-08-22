#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "clipboard/X11Clipboard.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <sys/select.h>
#include <unistd.h>

XErrorHandler X11Clipboard::previousHandler_ = nullptr;
int X11Clipboard::refCount_ = 0;
std::unordered_map<unsigned long, X11Clipboard::RequestorInfo> X11Clipboard::activeRequestors_;

bool X11Clipboard::isExpectedClipboardError(const XErrorEvent& error) {
    if (activeRequestors_.find(error.resourceid) == activeRequestors_.end()) return false;
    bool bad = error.error_code == BadWindow || error.error_code == BadAtom ||
               error.error_code == BadValue || error.error_code == BadMatch ||
               error.error_code == BadDrawable;
    if (!bad) return false;
    switch (error.request_code) {
        case 2:  // X_ChangeWindowAttributes (XSelectInput)
        case 18: // X_ChangeProperty
        case 25: // X_SendEvent
            return true;
        default:
            return false;
    }
}

void X11Clipboard::registerRequestor(unsigned long win) {
    auto &info = activeRequestors_[win];
    ++info.count;
    info.last = std::chrono::steady_clock::now();
}
void X11Clipboard::unregisterRequestor(unsigned long win) {
    auto it = activeRequestors_.find(win);
    if (it == activeRequestors_.end()) return;
    if (--it->second.count <= 0) activeRequestors_.erase(it);
    else it->second.last = std::chrono::steady_clock::now();
}
void X11Clipboard::purgeStaleRequestors() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = activeRequestors_.begin(); it != activeRequestors_.end(); ) {
        if (now - it->second.last > kIncrStaleTimeout) it = activeRequestors_.erase(it);
        else ++it;
    }
}

int X11Clipboard::handleX11Error(Display* display, XErrorEvent* error) {
    if (isExpectedClipboardError(*error)) return 0;
    if (previousHandler_) return previousHandler_(display, error);
    if (display) {
        char buf[256];
        XGetErrorText(display, error->error_code, buf, sizeof(buf));
        std::fprintf(stderr, "X11Clipboard: X error no esperado: %s (code=%d) req=%d\n",
                     buf, error->error_code, error->request_code);
    }
    return 0;
}

X11Clipboard::X11Clipboard() {
    if (refCount_ == 0) {
        previousHandler_ = XSetErrorHandler(handleX11Error);
    }
    ++refCount_;

    display_ = XOpenDisplay(nullptr);
    if (!display_) return;

    // XMaxRequestSize devuelve el limite en UNIDADES DE 4 BYTES (words de
    // 32 bits) que el servidor X acepta en una sola request. Se convierte
    // a bytes y se deja un margen generoso (1/4 del maximo) para el resto
    // del payload de XChangeProperty, en vez de usar el limite exacto;
    // es la practica habitual de otros clientes de clipboard (xclip/xsel
    // hacen algo equivalente). INCR arranca cuando el contenido no entra
    // en una sola request (incrThreshold_ == incrChunkSize_).
    long maxReqWords = XMaxRequestSize(display_);
    size_t maxReqBytes = static_cast<size_t>(maxReqWords) * 4;
    incrChunkSize_ = std::max<size_t>(4096, maxReqBytes / 4);
    incrThreshold_ = incrChunkSize_;

    int screen = DefaultScreen(display_);
    Window root = RootWindow(display_, screen);
    window_ = XCreateSimpleWindow(display_, root, 0, 0, 1, 1, 0, 0, 0);
    clipboardAtom_ = XInternAtom(display_, "CLIPBOARD", False);
    utf8Atom_ = XInternAtom(display_, "UTF8_STRING", False);
    textAtom_ = XInternAtom(display_, "TEXT", False);
    stringAtom_ = XInternAtom(display_, "STRING", False);
    targetsAtom_ = XInternAtom(display_, "TARGETS", False);
    incrAtom_ = XInternAtom(display_, "INCR", False);
    propertyAtom_ = XInternAtom(display_, "MAESTRO_CLIPBOARD", False);
    XSelectInput(display_, window_, PropertyChangeMask);
}

X11Clipboard::~X11Clipboard() {
    if (display_) {
        if (window_) XDestroyWindow(display_, (Window)window_);
        XCloseDisplay(display_);
    }
    // Xlib mantiene el error handler a nivel global de proceso.
    // Este contador asume que la creación/destrucción de X11Clipboard
    // ocurre desde un único thread.
    assert(refCount_ > 0);
    --refCount_;
    if (refCount_ == 0) {
        XSetErrorHandler(previousHandler_);
        previousHandler_ = nullptr;
    }
}

bool X11Clipboard::ownsClipboard() const {
    if (!display_) return ownsClipboard_;
    Window owner = XGetSelectionOwner(display_, (Atom)clipboardAtom_);
    return owner == (Window)window_ && ownsClipboard_;
}

bool X11Clipboard::copy(const std::string& text) {
    ownedText_ = text;
    if (!display_) {
        ownsClipboard_ = true;
        return true;
    }
    XSetSelectionOwner(display_, (Atom)clipboardAtom_, (Window)window_, CurrentTime);
    XFlush(display_);
    Window owner = XGetSelectionOwner(display_, (Atom)clipboardAtom_);
    ownsClipboard_ = (owner == (Window)window_);
    return ownsClipboard_;
}

void X11Clipboard::handleSelectionRequest(void* evPtr) {
    auto* req = static_cast<XSelectionRequestEvent*>(evPtr);
    registerRequestor(req->requestor);
    XSelectionEvent ev{};
    ev.type = SelectionNotify;
    ev.display = req->display;
    ev.requestor = req->requestor;
    ev.selection = req->selection;
    ev.target = req->target;
    ev.time = req->time;
    ev.property = req->property;
    if (req->property == None) {
        ev.property = None;
    } else if (req->selection != (Atom)clipboardAtom_) {
        ev.property = None;
    } else if (req->target == (Atom)targetsAtom_) {
        Atom supported[] = {(Atom)targetsAtom_, (Atom)utf8Atom_, (Atom)textAtom_, (Atom)stringAtom_, XA_STRING};
        int n = (Atom)stringAtom_ == XA_STRING ? 4 : 5;
        XChangeProperty(display_, req->requestor, req->property, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(supported), n);
    } else if (req->target == (Atom)utf8Atom_ || req->target == (Atom)textAtom_ || req->target == (Atom)stringAtom_ || req->target == XA_STRING) {
        if (ownedText_.size() > incrThreshold_) {
            long total = static_cast<long>(ownedText_.size());
            XSelectInput(display_, req->requestor, PropertyChangeMask);

            XChangeProperty(display_, req->requestor, req->property, (Atom)incrAtom_, 32, PropModeReplace,
                            reinterpret_cast<unsigned char*>(&total), 1);
            IncrSend s;
            s.requestor = req->requestor;
            s.property = req->property;
            s.target = req->target;
            s.data = ownedText_;
            s.offset = 0;
            s.lastActivity = std::chrono::steady_clock::now();
            incrSends_.push_back(std::move(s));
        } else {
            Atom propType = req->target;
            XChangeProperty(display_, req->requestor, req->property, propType, 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(ownedText_.data()), static_cast<int>(ownedText_.size()));
        }
    } else {
        ev.property = None;
    }
    XSendEvent(display_, req->requestor, False, 0, reinterpret_cast<XEvent*>(&ev));
    XFlush(display_);
}

void X11Clipboard::handlePropertyNotify(void* evPtr) {
    auto* ev = static_cast<XPropertyEvent*>(evPtr);
    if (ev->state != PropertyDelete)
        return;
    for (auto it = incrSends_.begin(); it != incrSends_.end(); ++it) {
        if (ev->window != static_cast<Window>(it->requestor))
            continue;
        if (ev->atom != static_cast<Atom>(it->property))
            continue;
        if (it->offset < it->data.size()) {
            size_t chunk = std::min(incrChunkSize_, it->data.size() - it->offset);
            XChangeProperty(
                display_,
                static_cast<Window>(it->requestor),
                static_cast<Atom>(it->property),
                static_cast<Atom>(it->target),
                8,
                PropModeReplace,
                reinterpret_cast<const unsigned char*>(it->data.data() + it->offset),
                static_cast<int>(chunk));
            XFlush(display_);
            it->offset += chunk;
            it->lastActivity = std::chrono::steady_clock::now();
            auto it2 = activeRequestors_.find(it->requestor);
            if (it2 != activeRequestors_.end()) it2->second.last = it->lastActivity;
        } else {
            XChangeProperty(
                display_,
                static_cast<Window>(it->requestor),
                static_cast<Atom>(it->property),
                static_cast<Atom>(it->target),
                8,
                PropModeReplace,
                nullptr,
                0);
            XFlush(display_);
            unregisterRequestor(it->requestor);
            incrSends_.erase(it);
        }
        return;
    }
}

int X11Clipboard::fd() const {
    if (!display_) return -1;
    return ConnectionNumber(display_);
}

void X11Clipboard::purgeStaleIncrSends() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = incrSends_.begin(); it != incrSends_.end(); ) {
        if (now - it->lastActivity > kIncrStaleTimeout) {
            unregisterRequestor(it->requestor);
            it = incrSends_.erase(it);
        } else {
            ++it;
        }
    }
    purgeStaleRequestors();
}

void X11Clipboard::processEvents() {
    if (!display_) return;
    while (XPending(display_)) {
        XEvent ev;
        XNextEvent(display_, &ev);
        if (ev.type == SelectionRequest) {
            handleSelectionRequest(&ev.xselectionrequest);
        } else if (ev.type == SelectionClear) {
            if (ev.xselectionclear.selection == (Atom)clipboardAtom_) {
                ownsClipboard_ = false;
                ownedText_.clear();
            }
        } else if (ev.type == PropertyNotify) {
            handlePropertyNotify(&ev.xproperty);
        }
    }
    purgeStaleIncrSends();
}

std::optional<std::string> X11Clipboard::readProperty(unsigned long win, unsigned long prop) {
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nitems = 0, bytesAfter = 0;
    unsigned char* data = nullptr;
    int res = XGetWindowProperty(display_, (Window)win, (Atom)prop, 0, 1UL << 20, False, AnyPropertyType,
                                 &actualType, &actualFormat, &nitems, &bytesAfter, &data);
    if (res != Success) {
        if (data) XFree(data);
        return std::nullopt;
    }
    if (actualType == (Atom)incrAtom_) {
        if (data) XFree(data);
        return std::nullopt;
    }
    if (actualType == None) {
        if (data) XFree(data);
        return std::optional<std::string>{""};
    }
    std::string result;
    if (data) {
        result.assign(reinterpret_cast<char*>(data), nitems * (actualFormat / 8));
        XFree(data);
    }
    unsigned long offset = (nitems * actualFormat + 31) / 32;
    while (bytesAfter > 0) {
        Atom t2 = None; int f2 = 0; unsigned long n2 = 0, b2 = 0; unsigned char* d2 = nullptr;
        int r2 = XGetWindowProperty(display_, (Window)win, (Atom)prop, offset, 1UL << 20, False, AnyPropertyType, &t2, &f2, &n2, &b2, &d2);
        if (r2 != Success) {
            if (d2) XFree(d2);
            return std::nullopt;
        }
        if (d2) {
            result.append(reinterpret_cast<char*>(d2), n2 * (f2 / 8));
            XFree(d2);
        }
        if (b2 == 0) break;
        offset += (n2 * f2 + 31) / 32;
        bytesAfter = b2;
    }
    return result;
}

void X11Clipboard::deleteProperty(unsigned long win, unsigned long prop) {
    XDeleteProperty(display_, (Window)win, (Atom)prop);
}

std::optional<std::string> X11Clipboard::fetchProperty(unsigned long win, unsigned long prop) {
    auto result = readProperty(win, prop);
    if (result) deleteProperty(win, prop);
    return result;
}

bool X11Clipboard::waitForSelectionNotify(unsigned long target, unsigned long property, int timeoutMs) {
    (void)target;
    (void)property;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    int fd = ConnectionNumber(display_);
    while (std::chrono::steady_clock::now() < deadline) {
        while (XPending(display_)) {
            XEvent ev;
            XNextEvent(display_, &ev);
            if (ev.type == SelectionNotify) {
                if (ev.xselection.selection != (Atom)clipboardAtom_) continue;
                if (ev.xselection.property == None) return false;
                return true;
            }
        }
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        struct timeval tv{};
        tv.tv_sec = remain / 1000;
        tv.tv_usec = (remain % 1000) * 1000;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        int r = select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (r < 0) break;
    }
    return false;
}

std::optional<std::string> X11Clipboard::readIncrProperty(unsigned long win, unsigned long prop) {
    std::string result;
    constexpr int TRANSFER_TIMEOUT_MS = 5000;
    constexpr int CHUNK_TIMEOUT_MS = 1000;
    auto transferDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TRANSFER_TIMEOUT_MS);
    auto chunkDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(CHUNK_TIMEOUT_MS);
    int fd = ConnectionNumber(display_);
    while (std::chrono::steady_clock::now() < transferDeadline) {
        while (XPending(display_)) {
            XEvent ev;
            XNextEvent(display_, &ev);
            if (ev.type == PropertyNotify && ev.xproperty.window == (Window)win && ev.xproperty.atom == (Atom)prop && ev.xproperty.state == PropertyNewValue) {
                Atom t2 = None; int f2 = 0; unsigned long n2 = 0, b2 = 0; unsigned char* d2 = nullptr;
                int r2 = XGetWindowProperty(display_, (Window)win, (Atom)prop, 0, 1UL << 20, True, AnyPropertyType, &t2, &f2, &n2, &b2, &d2);
                if (r2 != Success) {
                    if (d2) XFree(d2);
                    return std::nullopt;
                }
                if (n2 == 0) {
                    if (d2) XFree(d2);
                    return result;
                }
                if (d2) {
                    result.append(reinterpret_cast<char*>(d2), n2 * (f2 / 8));
                    XFree(d2);
                }
                chunkDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(CHUNK_TIMEOUT_MS);
            }
        }
        auto now = std::chrono::steady_clock::now();
        if (now >= transferDeadline || now >= chunkDeadline) break;
        auto remainTransfer = std::chrono::duration_cast<std::chrono::milliseconds>(transferDeadline - now).count();
        auto remainChunk = std::chrono::duration_cast<std::chrono::milliseconds>(chunkDeadline - now).count();
        long remain = std::min(remainTransfer, remainChunk);
        struct timeval tv{};
        tv.tv_sec = remain / 1000;
        tv.tv_usec = (remain % 1000) * 1000;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        int r = select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (r < 0) break;
    }
    return std::nullopt;
}

std::optional<std::string> X11Clipboard::paste() {
    if (!display_) {
        if (ownsClipboard_) return ownedText_;
        return std::optional<std::string>{""};
    }
    if (ownsClipboard()) {
        return ownedText_;
    }
    unsigned long targets[] = {utf8Atom_, stringAtom_, textAtom_};
    for (unsigned long tgt : targets) {
        XConvertSelection(display_, (Atom)clipboardAtom_, (Atom)tgt, (Atom)propertyAtom_, (Window)window_, CurrentTime);
        XFlush(display_);
        if (!waitForSelectionNotify(tgt, propertyAtom_, 500)) {
            XDeleteProperty(display_, (Window)window_, (Atom)propertyAtom_);
            continue;
        }
        Atom actualType = None; int actualFormat = 0; unsigned long nitems = 0, bytesAfter = 0; unsigned char* data = nullptr;
        int res = XGetWindowProperty(display_, (Window)window_, (Atom)propertyAtom_, 0, 1UL << 20, False, AnyPropertyType, &actualType, &actualFormat, &nitems, &bytesAfter, &data);
        if (res != Success) {
            if (data) XFree(data);
            XDeleteProperty(display_, (Window)window_, (Atom)propertyAtom_);
            continue;
        }
        if (actualType == (Atom)incrAtom_) {
            if (data) XFree(data);
            XDeleteProperty(display_, (Window)window_, (Atom)propertyAtom_);
            XFlush(display_);
            auto incrResult = readIncrProperty(window_, propertyAtom_);
            if (incrResult) return incrResult;
            XDeleteProperty(display_, (Window)window_, (Atom)propertyAtom_);
            continue;
        }
        if (data) XFree(data);
        auto result = fetchProperty(window_, propertyAtom_);
        if (result) return result;
        XDeleteProperty(display_, (Window)window_, (Atom)propertyAtom_);
    }
    return std::optional<std::string>{""};
}
