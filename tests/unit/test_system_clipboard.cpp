// Tests de backends reales del portapapeles del sistema (SystemClipboard).
//
// El Editor usa un SystemClipboard real segun el modo:
//   - GUI (--gui): makeSdlClipboard()    -> SdlClipboard
//   - por defecto: makeSystemClipboard() -> X11Clipboard (si HAVE_X11)
//                                         o NullClipboard (portable)
//
// Estado actual de cobertura:
//   - X11Clipboard: solo el camino de degradacion sin DISPLAY.
//   - SDLClipboard y X11Clipboard con display real: NO estan testeados aqui.
//   - NullClipboard: no tiene tests dedicados.

#include <cstdlib>
#include <string>

#include "test_framework.h"

#ifdef HAVE_X11
#include "platform/clipboard/X11Clipboard.h"

TEST(x11_clipboard_fallback_no_display) {
    const char* oldDisplay = std::getenv("DISPLAY");
    std::string saved = oldDisplay ? oldDisplay : "";
    bool hadDisplay = oldDisplay != nullptr;
    ::unsetenv("DISPLAY");
    X11Clipboard cb;

    if (hadDisplay)
        ::setenv("DISPLAY", saved.c_str(), 1);
    else
        ::unsetenv("DISPLAY");

    CHECK(!cb.isAvailable());
    CHECK(cb.copy("fallback"));
    CHECK(cb.ownsClipboard());

    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "fallback");

    cb.processEvents();
    CHECK(cb.ownsClipboard());
}

#endif
