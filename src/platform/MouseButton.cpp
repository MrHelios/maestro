#include "platform/MouseButton.h"

#ifdef HAVE_X11
#include <X11/Xlib.h>
#endif

// Ver MouseButton.h por semantica del fallback (siempre true = conservar
// conducta) y ownership (Display con dueño explícito, sin static de proceso).
namespace platform {

#ifdef HAVE_X11
void X11MouseButtonQuery::ensureDisplay() const {
    if (opened_) return;
    opened_ = true;
    display_ = XOpenDisplay(nullptr);
}

X11MouseButtonQuery::~X11MouseButtonQuery() {
    if (display_) XCloseDisplay(static_cast<Display*>(display_));
}

bool X11MouseButtonQuery::held() const {
    ensureDisplay();
    Display* display = static_cast<Display*>(display_);
    if (!display) return true;
    int rootX = 0, rootY = 0, winX = 0, winY = 0;
    Window rootRet = 0, childRet = 0;
    unsigned int mask = 0;
    const int screen = DefaultScreen(display);
    const Window root = RootWindow(display, screen);
    if (!XQueryPointer(display, root, &rootRet, &childRet, &rootX, &rootY,
                       &winX, &winY, &mask)) {
        return true;
    }
    return (mask & Button1Mask) != 0;
}

#else

// Build portable sin X11: misma semantica del fallback ("presionado").
// display_ queda siempre nulo; opened_ solo evita trabajo repetido.
void X11MouseButtonQuery::ensureDisplay() const {
    opened_ = true;
}

X11MouseButtonQuery::~X11MouseButtonQuery() = default;

bool X11MouseButtonQuery::held() const {
    return true;
}

#endif

} // namespace platform
