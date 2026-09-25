#include "platform/MouseButton.h"

#include <X11/Xlib.h>

// Ver MouseButton.h por semantica del fallback (siempre true = conservar
// conducta) y del ciclo de vida del Display (static de proceso).
namespace platform {

bool leftMouseButtonHeld() {
    static Display* display = XOpenDisplay(nullptr);
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

} // namespace platform
