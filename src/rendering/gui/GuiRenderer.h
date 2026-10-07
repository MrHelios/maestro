#pragma once

#include <optional>
#include <string>
#include <vector>

#include "document/Cursor.h"
#include "document/Document.h"
#include "document/Selection.h"
#include "layout/BracketMatcher.h"
#include "layout/Viewport.h"
#include "rendering/ChromeData.h"
#include "rendering/ChromeRequest.h"
#include "rendering/ScreenRenderer.h"
#include "rendering/Sink.h"
#include "rendering/frame/Frame.h"
#include "rendering/frame/FrameBuilder.h"
#include "rendering/gui/GuiChrome.h"
#include "rendering/gui/GuiFont.h"
#include "syntax/SyntaxCache.h"

// ---------------------------------------------------------------------------
// GuiRenderer (Fase GUI-2 chrome): backend SDL2 del puerto neutro
// ScreenRenderer.
//
// El header NO incluye <SDL.h> a propósito: el puntero al renderer SDL se
// guarda opaco (void*) y solo el .cpp conoce SDL bajo #ifdef HAVE_SDL2.
// Así el mismo TU compila con WITH_SDL2=0 sin headers de desarrollo.
//
// Chrome: StatusBar (fila superior) + MessageBar (fila inferior) con el MISMO
// DTO y posicion de cursor que TTY (FrameBuilder + chrome::
// messageBarCursorCell). El cursor de los 4 prompts con input (Busqueda /
// IrAFila / SaveAs / Renombrar) va DENTRO del input del MessageBar
// (msg.cursor, antes de decoraciones), nunca al final ni en la lista.
// El pintado completo del contenido (filas, gutter, sintaxis) sigue en
// Fase 3; el area de contenido queda en fondo liso por ahora.
// ---------------------------------------------------------------------------
class GuiRenderer : public ScreenRenderer {
public:
    GuiRenderer();
    ~GuiRenderer() override;

    void invalidateCache() override {}
    void setExternalSyntaxCache(SyntaxCache* c) override {
        frameBuilder_.setExternalSyntaxCache(c);
    }

    void toggleTheme() override { dark_ = !dark_; }
    bool isDarkTheme() const override { return dark_; }

    // Frame puro del editor (mismo que TTY: geometria + chrome + cursor).
    // Publico para tests sin SDL: verifica StatusBar/MessageBar y que el
    // cursor de los prompts caiga dentro del input del MessageBar.
    // No-const a propósito (igual que TtyRenderer::buildScreen): el
    // FrameBuilder muta sus cachés internos al construir.
    Frame buildFrame(const Document& doc, const Cursor& cursor,
                     const Viewport& viewport, const std::string& filename,
                     bool modified, const ChromeRequest& chrome,
                     const std::optional<Selection>& selection = std::nullopt,
                     const std::optional<Selection>& searchHighlight = std::nullopt,
                     const std::optional<BracketPair>& bracketPair = std::nullopt);

    // ChromeData de las pantallas de listas (misma composicion que
    // TtyRenderer: nombre/estado/right + MessageBar). Publico para tests.
    ChromeData bufferListChrome(const std::vector<std::string>& names,
                                int selected) const;
    ChromeData fileListChrome(const std::string& path,
                              const MessageBarData& message,
                              const std::vector<FileListItem>& items,
                              int selected, int scroll,
                              const char* estado) const;

    // El loop GUI inyecta el SDL_Renderer* real (no-owned). Sin inyectar,
    // los render* son no-op seguros (útil en tests sin ventana).
    void setSdlRenderer(void* r);
    // Variante completa: inyecta renderer + abre la fuente y mide la celda.
    // Debe llamarse tras crear el SDL_Renderer y antes del primer resize.
    void initForRenderer(void* r, int fontPixels = 16);

    // Métricas de celda en píxeles (de la fuente; 9x18 estimados si no hay).
    // El loop las usa para rows/cols = píxeles/celda.
    int cellW() const { return font_.cellW(); }
    int cellH() const { return font_.cellH(); }
    bool hasFont() const { return font_.ok(); }
    const std::string& fontPath() const { return font_.path(); }

    // Ultimo frame calculado por los render* (para tests sin SDL: sin
    // ventana no hay pixeles que afirmar, pero si DTO + cursor).
    const ChromeData& lastChrome() const { return lastChrome_; }
    StyleRole lastAccent() const { return lastAccent_; }
    FrameCursor lastCursor() const { return lastCursor_; }
    Layout lastLayout() const { return lastLayout_; }

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
    FrameBuilder frameBuilder_;
    // SDL_Renderer* opaco (solo se toca en el .cpp bajo HAVE_SDL2).
    void* sdlRenderer_ = nullptr;
    GuiFont font_;

    // Ultimo chrome/cursor calculados (para inspeccion en tests sin SDL y
    // para no recalcular en el pintado cuando hay SDL).
    mutable ChromeData lastChrome_;
    mutable StyleRole lastAccent_ = StyleRole::StatusAccentDefault;
    mutable FrameCursor lastCursor_;
    mutable Layout lastLayout_;
};
