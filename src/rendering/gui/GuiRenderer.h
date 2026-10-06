#pragma once

#include <optional>
#include <string>
#include <vector>

#include "document/Cursor.h"
#include "document/Document.h"
#include "document/Selection.h"
#include "layout/BracketMatcher.h"
#include "layout/Viewport.h"
#include "rendering/ChromeRequest.h"
#include "rendering/ScreenRenderer.h"
#include "rendering/Sink.h"
#include "syntax/SyntaxCache.h"

// ---------------------------------------------------------------------------
// GuiRenderer (Fase GUI-0, stub): backend SDL2 del puerto neutro
// ScreenRenderer.
//
// Paso inicial del binario único con --gui: implementa la interfaz para que
// el Editor no distinga backends, pero todavía NO dibuja texto. Cuando hay
// SDL2 disponible pinta un fondo sólido (prueba el camino
// Editor -> renderer -> ventana); sin SDL2 es no-op como NullScreenRenderer.
//
// El header NO incluye <SDL.h> a propósito: el puntero al renderer SDL se
// guarda opaco (void*) y solo el .cpp conoce SDL bajo #ifdef HAVE_SDL2.
// Así el mismo TU compila con WITH_SDL2=0 sin headers de desarrollo.
// ---------------------------------------------------------------------------
class GuiRenderer : public ScreenRenderer {
public:
    GuiRenderer();
    ~GuiRenderer() override;

    void invalidateCache() override {}
    void setExternalSyntaxCache(SyntaxCache* c) override { cache_ = c; }

    void toggleTheme() override { dark_ = !dark_; }
    bool isDarkTheme() const override { return dark_; }

    // El loop GUI inyecta el SDL_Renderer* real (no-owned). Sin inyectar,
    // los render* son no-op seguros (útil en tests sin ventana).
    void setSdlRenderer(void* r) { sdlRenderer_ = r; }

    void renderScreenDiff(const Document& doc,
                          const Cursor& cursor,
                          const Viewport& viewport,
                          const std::string& filename,
                          bool modified,
                          const ChromeRequest& chrome,
                          Sink& sink,
                          const std::optional<Selection>& selection,
                          const std::optional<Selection>& searchHighlight,
                          const std::optional<BracketPair>& bracketPair) override;

    void renderBufferList(const std::vector<std::string>& names,
                          int selected,
                          int width,
                          int height,
                          Sink& sink) override;

    void renderFileList(const std::vector<FileListItem>& items,
                        int selected,
                        int scroll,
                        const std::string& path,
                        const MessageBarData& message,
                        int width,
                        int height,
                        Sink& sink) override;

    void renderSaveAsFileList(const std::vector<FileListItem>& items,
                              int selected,
                              int scroll,
                              const std::string& path,
                              const MessageBarData& message,
                              int width,
                              int height,
                              Sink& sink) override;

private:
    bool dark_ = true;
    SyntaxCache* cache_ = nullptr;
    // SDL_Renderer* opaco (solo se toca en el .cpp bajo HAVE_SDL2).
    void* sdlRenderer_ = nullptr;
};
