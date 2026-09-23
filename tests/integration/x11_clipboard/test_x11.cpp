#include "test_framework.h"
#define private public
#include "platform/clipboard/X11Clipboard.h"
#undef private
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <unistd.h>
#include <thread>
#include <atomic>

static std::atomic<bool> g_origCalled{false};
static int testOrigHandler(Display*, XErrorEvent*) { g_origCalled.store(true, std::memory_order_relaxed); return 0; }
constexpr unsigned long kTestRequestor = 0xdeadbeef;

static XErrorHandler currentXErrorHandler() {
    XErrorHandler cur = XSetErrorHandler(testOrigHandler);
    XSetErrorHandler(cur);
    return cur;
}
static void expectHandlerInstalled() { CHECK(currentXErrorHandler() != testOrigHandler); }
static void expectHandlerInstalledNotOrig(XErrorHandler orig) {
    auto cur = currentXErrorHandler();
    CHECK(cur != testOrigHandler);
    CHECK(cur != orig);
}
static void expectHandlerRestored() { CHECK_EQ(currentXErrorHandler(), testOrigHandler); }

// CLIPBOARD es global al X server. Sin aislamiento, el orden de tests importa:
// un test que deja owner o requestors pendientes contamina al siguiente.
// Esta función limpia el estado global para aislar cada test.
static void isolateClipboard() {
    X11Clipboard tmp;
    if (!tmp.isAvailable()) return;
    tmp.copy("");
    for (int i = 0; i < 5; ++i) { tmp.processEvents(); usleep(5000); }
    for (int i = 0; i < 10 && (!X11Clipboard::activeRequestors_.empty() || !tmp.incrSends_.empty()); ++i) {
        tmp.processEvents();
        tmp.purgeStaleIncrSends();
        usleep(5000);
    }
    CHECK(X11Clipboard::activeRequestors_.empty());
    CHECK(tmp.incrSends_.empty());
}

template <typename Pred>
static bool pollUntilX11(Pred pred, int timeoutMs = 500) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        usleep(10000);
    }
    return pred();
}

struct DisplayGuard {
    Display* d = nullptr;
    explicit DisplayGuard(Display* display) : d(display) {}
    ~DisplayGuard() { if (d) XCloseDisplay(d); }
    Display* get() const { return d; }
    operator Display*() const { return d; }
    DisplayGuard(const DisplayGuard&) = delete;
    DisplayGuard& operator=(const DisplayGuard&) = delete;
};

struct WindowGuard {
    Display* d = nullptr;
    Window w = 0;
    WindowGuard() = default;
    WindowGuard(Display* display, Window window) : d(display), w(window) {}
    ~WindowGuard() { if (d && w != 0) { XDestroyWindow(d, w); XFlush(d); } }
    Window get() const { return w; }
    operator Window() const { return w; }
    void release() { w = 0; }
    WindowGuard(const WindowGuard&) = delete;
    WindowGuard& operator=(const WindowGuard&) = delete;
};

// El thread de pump existe únicamente para satisfacer el event loop de X11
// durante paste(): el requestor espera SelectionNotify mientras el owner
// debe procesar SelectionRequest. Sin pump, paste() bloquearía.
static std::optional<std::string> pasteWithPump(X11Clipboard& requestor, X11Clipboard& owner) {
    std::atomic<bool> done{false};
    std::thread pump([&]{
        while (!done.load(std::memory_order_relaxed)) {
            requestor.processEvents();
            owner.processEvents();
            usleep(5000);
        }
    });
    auto result = requestor.paste();
    done.store(true, std::memory_order_relaxed);
    pump.join();
    return result;
}

// XSetErrorHandler es estado global de Xlib. Si un test instala un handler
// y un CHECK falla antes del cleanup, el handler queda contaminando los
// tests siguientes (dependencia de orden). Este guard RAII garantiza
// la restauración incluso con early return vía SKIP o con CHECK fallido.
// Problema grave para la estabilidad del suite.
class XErrorHandlerGuard {
public:
    explicit XErrorHandlerGuard(XErrorHandler h) : previous_(XSetErrorHandler(h)) {}
    ~XErrorHandlerGuard() { XSetErrorHandler(previous_); }
    XErrorHandler previous() const { return previous_; }
private:
    XErrorHandler previous_;
};

TEST(x11_maestro_to_maestro) {
    X11Clipboard cb;
    if (!cb.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");

    isolateClipboard();
    CHECK(cb.copy("hello x11"));
    CHECK(cb.ownsClipboard());
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "hello x11");
}

TEST(x11_utf8) {
    X11Clipboard cb;
    if (!cb.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");

    isolateClipboard();    std::string s = std::string("caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x98\x80 \xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF");
    CHECK(cb.copy(s));
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, s);
}

TEST(x11_multiline) {
    X11Clipboard cb;
    if (!cb.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");

    isolateClipboard();    std::string multi = "linea 1\nlinea 2\nlinea 3";
    CHECK(cb.copy(multi));
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, multi);
}

TEST(x11_ownership) {
    X11Clipboard a, b;
    if (!a.isAvailable() || !b.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");

    isolateClipboard();
    CHECK(a.copy("A"));
    CHECK(pollUntilX11([&]{ a.processEvents(); return a.ownsClipboard(); }));
    CHECK(b.copy("B"));
    CHECK(pollUntilX11([&]{ b.processEvents(); return b.ownsClipboard(); }));
    CHECK(pollUntilX11([&]{ a.processEvents(); b.processEvents(); return !a.ownsClipboard(); }));
    auto p = pasteWithPump(a, b);
    CHECK(p.has_value());
    CHECK_EQ(*p, "B");
}

TEST(x11_maestro_to_firefox_simulated) {
    X11Clipboard maestro, firefox;
    if (!maestro.isAvailable() || !firefox.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");

    isolateClipboard();
    CHECK(maestro.copy("from maestro"));
    CHECK(pollUntilX11([&]{ maestro.processEvents(); firefox.processEvents(); return maestro.ownsClipboard(); }));
    auto p = pasteWithPump(firefox, maestro);
    CHECK(p.has_value());
    CHECK_EQ(*p, "from maestro");
}

TEST(x11_firefox_to_maestro_simulated) {
    X11Clipboard maestro, firefox;
    if (!maestro.isAvailable() || !firefox.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");

    isolateClipboard();
    CHECK(firefox.copy("from firefox"));
    CHECK(pollUntilX11([&]{ maestro.processEvents(); firefox.processEvents(); return firefox.ownsClipboard(); }));
    auto p = pasteWithPump(maestro, firefox);
    CHECK(p.has_value());
    CHECK_EQ(*p, "from firefox");
}

TEST(x11_targets_supported) {
    X11Clipboard cb;
    if (!cb.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");

    isolateClipboard();
    CHECK(cb.copy("targets test"));
    cb.processEvents();
    CHECK(cb.ownsClipboard());
}

TEST(x11_empty) {
    X11Clipboard cb;
    if (!cb.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");

    isolateClipboard();
    CHECK(cb.copy(""));
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "");
}

TEST(clipboard_multiple_instances_keep_handler_alive) {
    XErrorHandlerGuard guard(testOrigHandler);
    XErrorHandler orig = guard.previous();
    {
        auto* a = new X11Clipboard();
        auto* b = new X11Clipboard();
        CHECK_EQ(X11Clipboard::refCount_, 2);
        delete a;
        CHECK_EQ(X11Clipboard::refCount_, 1);
        expectHandlerInstalledNotOrig(orig);
        delete b;
        CHECK_EQ(X11Clipboard::refCount_, 0);
    }
    expectHandlerRestored();
}

TEST(clipboard_last_instance_restores_previous_handler) {
    // The clipboard must restore the handler that existed before the first
    // X11Clipboard instance, not Xlib's default or our own handler.
    XErrorHandlerGuard guard(testOrigHandler);
    {
        X11Clipboard a;
        CHECK_EQ(X11Clipboard::refCount_, 1);
    }
    CHECK_EQ(X11Clipboard::refCount_, 0);
    expectHandlerRestored();
}

TEST(clipboard_instances_destruction_order_is_irrelevant) {
    XErrorHandlerGuard guard(testOrigHandler);
    {
        auto* a = new X11Clipboard();
        auto* b = new X11Clipboard();
        delete b;
        CHECK_EQ(X11Clipboard::refCount_, 1);
        expectHandlerInstalled();
        delete a;
        CHECK_EQ(X11Clipboard::refCount_, 0);
    }
    expectHandlerRestored();
}

TEST(clipboard_three_instances_keep_handler_until_last_destroyed) {
    XErrorHandlerGuard guard(testOrigHandler);
    auto* a = new X11Clipboard();
    auto* b = new X11Clipboard();
    auto* c = new X11Clipboard();
    CHECK_EQ(X11Clipboard::refCount_, 3);
    delete b;
    CHECK_EQ(X11Clipboard::refCount_, 2);
    expectHandlerInstalled();
    delete a;
    CHECK_EQ(X11Clipboard::refCount_, 1);
    expectHandlerInstalled();
    delete c;
    CHECK_EQ(X11Clipboard::refCount_, 0);
    expectHandlerRestored();
}

TEST(clipboard_x11_error_handler_absorbs_expected_and_delegates_unexpected) {
    XErrorHandlerGuard guard(testOrigHandler);
    X11Clipboard cb;
    if (!cb.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");
    g_origCalled.store(false, std::memory_order_relaxed);
    const unsigned long fakeReq = kTestRequestor;
    X11Clipboard::registerRequestor(fakeReq);
    int before = X11Clipboard::absorbedErrorCount_;
    XErrorEvent ev{};
    ev.error_code = BadWindow;
    ev.request_code = X_ChangeProperty;
    ev.resourceid = fakeReq;
    int ret = X11Clipboard::handleX11Error(nullptr, &ev);
    CHECK(ret == 0);
    CHECK(!g_origCalled.load(std::memory_order_relaxed));
    CHECK(X11Clipboard::absorbedErrorCount_ > before);
    g_origCalled.store(false, std::memory_order_relaxed);
    XErrorEvent ev2{};
    ev2.error_code = BadWindow;
    ev2.request_code = X_ChangeProperty;
    ev2.resourceid = 0x12345678;
    X11Clipboard::handleX11Error(nullptr, &ev2);
    CHECK(g_origCalled.load(std::memory_order_relaxed));
    X11Clipboard::unregisterRequestor(fakeReq);
    g_origCalled.store(false, std::memory_order_relaxed);
    CHECK(cb.copy("still alive after X error"));
}

TEST(clipboard_survives_other_instance_destruction) {
    auto* a = new X11Clipboard();
    auto* b = new X11Clipboard();
    if (!b->isAvailable()) { delete a; delete b; SKIP("X11 not available - requires X11/Xvfb with DISPLAY"); }
    isolateClipboard();
    CHECK(b->copy("hello"));
    delete a;
    CHECK_EQ(X11Clipboard::refCount_, 1);
    CHECK(b->ownsClipboard());
    auto p = b->paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "hello");
    delete b;
}

TEST(clipboard_error_not_from_requestor_delegates) {
    CHECK_EQ(X11Clipboard::refCount_, 0);
    CHECK(X11Clipboard::activeRequestors_.empty());
    XErrorHandlerGuard guard(testOrigHandler);
    auto* cb = new X11Clipboard();
    g_origCalled.store(false, std::memory_order_relaxed);
    XErrorEvent ev{};
    ev.error_code = BadWindow;
    ev.request_code = X_ChangeProperty;
    ev.resourceid = 0x12345678;
    // The requestor is not registered, so this error is unexpected.
    // The clipboard must delegate it to the previously installed handler.
    X11Clipboard::handleX11Error(nullptr, &ev);
    CHECK(g_origCalled.load(std::memory_order_relaxed));
    delete cb;
    g_origCalled.store(false, std::memory_order_relaxed);
}

TEST(clipboard_error_from_requestor_absorbed) {
    CHECK_EQ(X11Clipboard::refCount_, 0);
    XErrorHandlerGuard guard(testOrigHandler);
    auto* cb = new X11Clipboard();
    X11Clipboard::registerRequestor(kTestRequestor);
    g_origCalled.store(false, std::memory_order_relaxed);
    XErrorEvent ev{};
    ev.error_code = BadWindow;
    ev.request_code = X_ChangeProperty;
    ev.resourceid = kTestRequestor;
    X11Clipboard::handleX11Error(nullptr, &ev);
    CHECK(!g_origCalled.load(std::memory_order_relaxed));
    X11Clipboard::unregisterRequestor(kTestRequestor);
    delete cb;
}

TEST(clipboard_requestor_disappears_during_response_does_not_crash) {
    CHECK_EQ(X11Clipboard::refCount_, 0);
    g_origCalled.store(false, std::memory_order_relaxed);
    XErrorHandlerGuard guard(testOrigHandler);
    X11Clipboard cb;
    if (!cb.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");
    isolateClipboard();
    DisplayGuard d2(XOpenDisplay(nullptr));
    if (!d2.get()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");
    std::string large(cb.incrThreshold_ + 8192, 'x');
    if (large.size() <= cb.incrChunkSize_) large.resize(cb.incrChunkSize_ + 4096);
    CHECK(cb.copy(large));
    WindowGuard req(d2.get(), XCreateSimpleWindow(d2.get(), RootWindow(d2.get(), DefaultScreen(d2.get())), 0, 0, 10, 10, 0, 0, 0));
    Atom clip = XInternAtom(d2.get(), "CLIPBOARD", False);
    Atom utf8 = XInternAtom(d2.get(), "UTF8_STRING", False);
    Atom prop = XInternAtom(d2.get(), "TEST_PROP_DISAPPEAR", False);
    XConvertSelection(d2.get(), clip, utf8, prop, req.get(), CurrentTime);
    XFlush(d2.get());
    XSync(d2.get(), False);
    for (int i = 0; i < 100 && (X11Clipboard::activeRequestors_.find(req.get()) == X11Clipboard::activeRequestors_.end() || cb.incrSends_.empty()); ++i) {
        cb.processEvents();
        usleep(5000);
    }
    CHECK(X11Clipboard::activeRequestors_.find(req.get()) != X11Clipboard::activeRequestors_.end());
    CHECK(!cb.incrSends_.empty());
    Window reqVal = req.get();
    XDestroyWindow(d2.get(), reqVal);
    req.release();
    XFlush(d2.get());
    XSync(d2.get(), False);
    XPropertyEvent pe{};
    pe.type = PropertyNotify;
    pe.display = cb.display_;
    pe.window = reqVal;
    pe.atom = prop;
    pe.state = PropertyDelete;
    pe.time = CurrentTime;
    g_origCalled.store(false, std::memory_order_relaxed);
    int absorbedBefore = X11Clipboard::absorbedErrorCount_;
    cb.handlePropertyNotify(&pe);
    XSync(cb.display_, False);
    CHECK(!g_origCalled.load(std::memory_order_relaxed));
    CHECK(X11Clipboard::absorbedErrorCount_ > absorbedBefore);
    // Requestor was destroyed; real code should have cleaned via XGetWindowAttributes check
    CHECK(X11Clipboard::activeRequestors_.find(reqVal) == X11Clipboard::activeRequestors_.end());
    bool stillHasIncrForReq = false;
    for (auto &s : cb.incrSends_) if (s.requestor == reqVal) stillHasIncrForReq = true;
    CHECK(!stillHasIncrForReq);
    CHECK(cb.copy("still alive"));
    CHECK(!g_origCalled.load(std::memory_order_relaxed));
}

TEST(x11_large_incr_happy_path) {
    X11Clipboard owner, requestor;
    if (!owner.isAvailable() || !requestor.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");

    isolateClipboard();
    std::string large(owner.incrThreshold_ + 8192, 'x');
    if (large.size() <= owner.incrChunkSize_) large.resize(owner.incrChunkSize_ + 4096);
    // Make content non-trivial to catch truncation
    for (size_t i = 0; i < large.size(); ++i) large[i] = static_cast<char>('a' + (i % 26));
    CHECK(owner.copy(large));
    CHECK(pollUntilX11([&]{ owner.processEvents(); requestor.processEvents(); return owner.ownsClipboard(); }));
    auto result = pasteWithPump(requestor, owner);
    CHECK(result.has_value());
    CHECK_EQ(result->size(), large.size());
    CHECK_EQ(*result, large);
    CHECK(owner.copy("small after large"));
    CHECK(pollUntilX11([&]{ owner.processEvents(); requestor.processEvents(); return owner.ownsClipboard(); }));
    auto smallResult = pasteWithPump(requestor, owner);
    CHECK(smallResult.has_value());
    CHECK_EQ(*smallResult, "small after large");
}

TEST(x11_incr_threshold_boundaries) {
    // Functional: paste correctness at threshold boundaries. Does not assert
    // INCR vs normal transfer (would couple to impl); INCR protocol covered by x11_large_incr_*.
    X11Clipboard owner, requestor;
    if (!owner.isAvailable() || !requestor.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");

    isolateClipboard();
    size_t threshold = owner.incrThreshold_;
    struct Case { size_t size; };
    std::vector<Case> cases = {
        {threshold > 0 ? threshold - 1 : 0},
        {threshold},
        {threshold + 1},
    };
    for (auto &c : cases) {
        std::string data(c.size, 'x');
        for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>('a' + (i % 26));
        CHECK(owner.copy(data));
        CHECK(pollUntilX11([&]{ owner.processEvents(); requestor.processEvents(); return owner.ownsClipboard(); }));
        auto result = pasteWithPump(requestor, owner);
        CHECK(result.has_value());
        CHECK_EQ(result->size(), data.size());
        CHECK_EQ(*result, data);
    }
}

TEST(x11_large_utf8_incr) {
    X11Clipboard owner, requestor;
    if (!owner.isAvailable() || !requestor.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");

    isolateClipboard();    std::string pattern = "caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x98\x80 \xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF ";
    std::string large;
    while (large.size() < owner.incrThreshold_ + 8192) large += pattern;
    CHECK(owner.copy(large));
    CHECK(pollUntilX11([&]{ owner.processEvents(); requestor.processEvents(); return owner.ownsClipboard(); }));
    auto result = pasteWithPump(requestor, owner);
    CHECK(result.has_value());
    CHECK_EQ(result->size(), large.size());
    CHECK_EQ(*result, large);
}

TEST(clipboard_multiple_requestors_one_disappears_other_continues) {
    isolateClipboard();
    CHECK_EQ(X11Clipboard::refCount_, 0);
    g_origCalled.store(false, std::memory_order_relaxed);
    XErrorHandlerGuard guard(testOrigHandler);
    X11Clipboard owner;
    if (!owner.isAvailable()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");
    DisplayGuard dA(XOpenDisplay(nullptr));
    DisplayGuard dB(XOpenDisplay(nullptr));
    if (!dA.get() || !dB.get()) SKIP("X11 not available - requires X11/Xvfb with DISPLAY");
    std::string large(owner.incrThreshold_ + 8192, 'y');
    if (large.size() <= owner.incrChunkSize_) large.resize(owner.incrChunkSize_ + 4096);
    CHECK(owner.copy(large));
    WindowGuard reqA(dA.get(), XCreateSimpleWindow(dA.get(), RootWindow(dA.get(), DefaultScreen(dA.get())), 0, 0, 10, 10, 0, 0, 0));
    WindowGuard reqB(dB.get(), XCreateSimpleWindow(dB.get(), RootWindow(dB.get(), DefaultScreen(dB.get())), 0, 0, 10, 10, 0, 0, 0));
    Atom clipA = XInternAtom(dA.get(), "CLIPBOARD", False);
    Atom utf8A = XInternAtom(dA.get(), "UTF8_STRING", False);
    Atom propA = XInternAtom(dA.get(), "PROP_A_MULTI", False);
    Atom clipB = XInternAtom(dB.get(), "CLIPBOARD", False);
    Atom utf8B = XInternAtom(dB.get(), "UTF8_STRING", False);
    Atom propB = XInternAtom(dB.get(), "PROP_B_MULTI", False);
    XConvertSelection(dA.get(), clipA, utf8A, propA, reqA.get(), CurrentTime);
    XConvertSelection(dB.get(), clipB, utf8B, propB, reqB.get(), CurrentTime);
    XFlush(dA.get()); XFlush(dB.get());
    XSync(dA.get(), False); XSync(dB.get(), False);
    for (int i = 0; i < 100 && (X11Clipboard::activeRequestors_.find(reqA.get()) == X11Clipboard::activeRequestors_.end() || X11Clipboard::activeRequestors_.find(reqB.get()) == X11Clipboard::activeRequestors_.end() || owner.incrSends_.size() < 2); ++i) {
        owner.processEvents();
        usleep(5000);
    }
    CHECK(X11Clipboard::activeRequestors_.find(reqA.get()) != X11Clipboard::activeRequestors_.end());
    CHECK(X11Clipboard::activeRequestors_.find(reqB.get()) != X11Clipboard::activeRequestors_.end());
    CHECK(owner.incrSends_.size() >= 2);
    Window reqAVal = reqA.get();
    XDestroyWindow(dA.get(), reqAVal);
    XFlush(dA.get()); XSync(dA.get(), False);
    reqA.release();
    XPropertyEvent peA{};
    peA.type = PropertyNotify;
    peA.display = owner.display_;
    peA.window = reqAVal;
    peA.atom = propA;
    peA.state = PropertyDelete;
    peA.time = CurrentTime;
    int absorbedBefore = X11Clipboard::absorbedErrorCount_;
    owner.handlePropertyNotify(&peA);
    XSync(owner.display_, False);
    CHECK(!g_origCalled.load(std::memory_order_relaxed));
    CHECK(X11Clipboard::absorbedErrorCount_ > absorbedBefore);
    CHECK(X11Clipboard::activeRequestors_.find(reqAVal) == X11Clipboard::activeRequestors_.end());
    bool hasAIncr = false;
    for (auto &s : owner.incrSends_) if (s.requestor == reqAVal) hasAIncr = true;
    CHECK(!hasAIncr);
    CHECK(X11Clipboard::activeRequestors_.find(reqB.get()) != X11Clipboard::activeRequestors_.end());
    bool hasBIncr = false;
    for (auto &s : owner.incrSends_) if (s.requestor == reqB.get()) hasBIncr = true;
    CHECK(hasBIncr);
    // B must still be able to complete the transfer via real code
    for (int i = 0; i < 20 && X11Clipboard::activeRequestors_.find(reqB.get()) != X11Clipboard::activeRequestors_.end(); ++i) {
        XPropertyEvent peB{};
        peB.type = PropertyNotify;
        peB.display = owner.display_;
        peB.window = reqB.get();
        peB.atom = propB;
        peB.state = PropertyDelete;
        peB.time = CurrentTime;
        owner.handlePropertyNotify(&peB);
        XSync(owner.display_, False);
        CHECK(!g_origCalled.load(std::memory_order_relaxed));
        usleep(5000);
        owner.processEvents();
    }
    CHECK(X11Clipboard::activeRequestors_.find(reqB.get()) == X11Clipboard::activeRequestors_.end());
    bool hasBIncrAfter = false;
    for (auto &s : owner.incrSends_) if (s.requestor == reqB.get()) hasBIncrAfter = true;
    CHECK(!hasBIncrAfter);
    CHECK(owner.copy("still alive after multi"));
}
