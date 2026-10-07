#include "rendering/gui/GuiRenderer.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

GuiRenderer::GuiRenderer() = default;
GuiRenderer::~GuiRenderer() = default;

void GuiRenderer::setSdlRenderer(void* r) {
    // Las texturas del caché pertenecen al renderer anterior: liberación
    // ansiosa al cambiar. (Defensa en profundidad: GuiFont también vacía
    // el caché solo si textTexture() recibe un renderer distinto al activo.)
    if (sdlRenderer_ != r) font_.clearCache();
    sdlRenderer_ = r;
}

void GuiRenderer::initForRenderer(void* r, int fontPixels) {
    setSdlRenderer(r);
    if (r) font_.load(fontPixels);
}

void GuiRenderer::renderScreenDiff(const Document&,
                                   const Cursor&,
                                   const Viewport&,
                                   const std::string& filename,
                                   bool,
                                   const ChromeRequest&,
                                   Sink&,
                                   const std::optional<Selection>&,
                                   const std::optional<Selection>&,
                                   const std::optional<BracketPair>&) {
#ifdef HAVE_SDL2
#ifndef HAVE_SDL2_TTF
    (void)filename;  // solo lo usa la línea de prueba con fuente (TTF)
#endif
    if (!sdlRenderer_) return;
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
    // Fondo según tema (igual que el stub de Fase 0).
    const Uint8 bgR = dark_ ? 0x1E : 0xF5;
    const Uint8 bgG = dark_ ? 0x1E : 0xF5;
    const Uint8 bgB = dark_ ? 0x1E : 0xF5;
    SDL_SetRenderDrawColor(r, bgR, bgG, bgB, 0xFF);
    SDL_RenderClear(r);

#ifdef HAVE_SDL2_TTF
    // Fase 1: línea de prueba con la fuente real (nombre + métricas).
    if (font_.ok()) {
        const std::string name = filename.empty() ? "(SinNombre)" : filename;
        const std::string line =
            "Maestro --gui  " + name + "  celda " +
            std::to_string(font_.cellW()) + "x" + std::to_string(font_.cellH()) +
            "  " + font_.path();
        const Uint8 fg = dark_ ? 0xD4 : 0x1E;
        void* tex = font_.textTexture(r, line, fg, fg, fg);
        if (tex) {
            SDL_Texture* t = static_cast<SDL_Texture*>(tex);
            int tw = 0, th = 0;
            SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
            SDL_Rect dst{8, 8, tw, th};
            SDL_RenderCopy(r, t, nullptr, &dst);
        }
    }
#endif

    SDL_RenderPresent(r);
#else
    // Sin SDL2: no-op (el loop nunca llega acá, main avisa antes).
#endif
}

void GuiRenderer::renderBufferList(const std::vector<std::string>&, int, int, int,
                                   Sink&) {
    // FASE 1: el stub solo pinta fondo (+ línea de prueba) en
    // renderScreenDiff. Listas en Fase 4.
}

void GuiRenderer::renderFileList(const std::vector<FileListItem>&, int, int,
                                 const std::string&, const MessageBarData&, int, int,
                                 Sink&) {
}

void GuiRenderer::renderSaveAsFileList(const std::vector<FileListItem>&, int, int,
                                       const std::string&, const MessageBarData&, int,
                                       int, Sink&) {
}
