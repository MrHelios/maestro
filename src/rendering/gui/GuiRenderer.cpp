#include "rendering/gui/GuiRenderer.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

GuiRenderer::GuiRenderer() = default;
GuiRenderer::~GuiRenderer() = default;

void GuiRenderer::renderScreenDiff(const Document&,
                                   const Cursor&,
                                   const Viewport&,
                                   const std::string&,
                                   bool,
                                   const ChromeRequest&,
                                   Sink&,
                                   const std::optional<Selection>&,
                                   const std::optional<Selection>&,
                                   const std::optional<BracketPair>&) {
#ifdef HAVE_SDL2
    if (!sdlRenderer_) return;
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
    // Fondo sólido según tema (stub FASE 0: aún sin texto).
    if (dark_) {
        SDL_SetRenderDrawColor(r, 0x1E, 0x1E, 0x1E, 0xFF);
    } else {
        SDL_SetRenderDrawColor(r, 0xF5, 0xF5, 0xF5, 0xFF);
    }
    SDL_RenderClear(r);
    SDL_RenderPresent(r);
#else
    // Sin SDL2: no-op (el loop nunca llega acá, main avisa antes).
#endif
}

void GuiRenderer::renderBufferList(const std::vector<std::string>&, int, int, int,
                                   Sink&) {
    // FASE 0: el stub solo pinta fondo en renderScreenDiff.
}

void GuiRenderer::renderFileList(const std::vector<FileListItem>&, int, int,
                                 const std::string&, const MessageBarData&, int, int,
                                 Sink&) {
}

void GuiRenderer::renderSaveAsFileList(const std::vector<FileListItem>&, int, int,
                                       const std::string&, const MessageBarData&, int,
                                       int, Sink&) {
}
