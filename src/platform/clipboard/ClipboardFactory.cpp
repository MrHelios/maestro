#include "platform/clipboard/ClipboardFactory.h"

#include "platform/clipboard/NullClipboard.h"
#ifdef HAVE_X11
#include "platform/clipboard/X11Clipboard.h"
#endif

std::unique_ptr<SystemClipboard> makeNullClipboard() {
    return std::make_unique<NullClipboard>();
}

std::unique_ptr<SystemClipboard> makeSystemClipboard() {
    // X11Clipboard ya degrada solo (isAvailable/display_==nullptr),
    // así que el default puede pedirlo directo sin oler X11 acá afuera.
    // Si algún día hay Wayland/Qt, este es el único lugar que cambia.
    // Sin HAVE_X11 (build portable) se cae a NullClipboard.
#ifdef HAVE_X11
    return std::make_unique<X11Clipboard>();
#else
    return std::make_unique<NullClipboard>();
#endif
}
