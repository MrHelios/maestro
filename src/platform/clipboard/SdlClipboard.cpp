#include "platform/clipboard/SdlClipboard.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

#ifdef HAVE_SDL2
bool SdlClipboard::sdlReady() {
    // SDL clipboard exige video init. SDL_WasInit es barato y no toca
    // nada si SDL no arrancó (tests headless -> fallback memoria).
    return (SDL_WasInit(SDL_INIT_VIDEO) != 0);
}
#endif

bool SdlClipboard::copy(const std::string& text) {
    // El respaldo local siempre se actualiza (modo memoria cuando SDL no
    // está listo). El testigo de publicación, en cambio, SOLO avanza con
    // éxito de SDL: si SDL_SetClipboardText() falla, copy() informa del
    // fallo y ownsClipboard() sigue respondiendo sobre la última copia
    // realmente publicada, sin dar true por coincidencia externa.
    localText_ = text;
    localHas_ = true;
#ifdef HAVE_SDL2
    if (!sdlReady()) return true;  // headless: memoria, éxito local
    if (SDL_SetClipboardText(text.c_str()) != 0) return false;
    publishedText_ = text;
    publishedHas_ = true;
    return true;
#else
    return true;
#endif
}

std::optional<std::string> SdlClipboard::paste() {
#ifdef HAVE_SDL2
    if (sdlReady()) {
        if (SDL_HasClipboardText() == SDL_FALSE) return std::optional<std::string>{""};
        char* p = SDL_GetClipboardText();
        if (!p) return std::nullopt;  // error SDL real
        std::string out(p);
        SDL_free(p);
        return out;
    }
#endif
    if (localHas_) return localText_;
    return std::optional<std::string>{""};
}

bool SdlClipboard::ownsClipboard() const {
#ifdef HAVE_SDL2
    if (sdlReady()) {
        if (!publishedHas_) return false;
        if (SDL_HasClipboardText() == SDL_FALSE) return false;
        // SDL_GetClipboardText aloca: hay que liberarlo con SDL_free.
        // No muta estado lógico (solo el buffer temporal), así que el
        // método sigue siendo const.
        char* p = SDL_GetClipboardText();
        if (!p) return false;
        std::string cur(p);
        SDL_free(p);
        return cur == publishedText_;
    }
#endif
    return localHas_;
}
