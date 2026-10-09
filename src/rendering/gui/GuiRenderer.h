#pragma once

#include <chrono>
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
#include "rendering/ListLines.h"
#include "rendering/ScreenRenderer.h"
#include "rendering/Sink.h"
#include "rendering/frame/Frame.h"
#include "rendering/frame/FrameBuilder.h"
#include "rendering/gui/GuiChrome.h"
#include "rendering/gui/GuiFont.h"
#include "syntax/SyntaxCache.h"

// ---------------------------------------------------------------------------
// GuiRenderer: backend SDL2 del puerto neutro ScreenRenderer.
//
// Chrome: StatusBar (fila superior) + MessageBar (fila inferior) con el MISMO
// DTO y posicion de cursor que TTY (FrameBuilder + chrome::
// messageBarCursorCell). El cursor de los 4 prompts con input (Busqueda /
// IrAFila / SaveAs / Renombrar) va DENTRO del input del MessageBar
// (msg.cursor, antes de decoraciones), nunca al final ni en la lista.
// Contenido: filas del Frame (gutter + sintaxis + Selection, que fusiona
// selección y highlight de búsqueda + BracketMatch + fondo de línea actual)
// y listas modales (BufferSelector / FileBrowser / SaveAs) con la misma
// composición que TTY ("  nombre", "/" en carpetas, "~" de relleno).
// Sin SDL los render* son no-op seguros pero dejan snapshot testeable
// (lastContentRows_/lastListLines_).
// ---------------------------------------------------------------------------
// Snapshot testeable sin SDL: una fila de contenido como texto+roles.
// Es el DTO que la GUI pinta (segmentos del Frame) y lo que los tests
// afirman sin ventana: sintaxis, Selection (selección + highlight de
// búsqueda, fusionados en FrameBuilder), BracketMatch y CurrentLine.
struct GuiContentSeg {
    std::string text;
    StyleRole role = StyleRole::Default;
};
struct GuiContentRow {
    std::vector<GuiContentSeg> segs;
    bool isCurrentLine = false;
    std::string plain() const {
        std::string out;
        for (const auto& s : segs) out += s.text;
        return out;
    }
    bool hasRole(StyleRole r) const {
        for (const auto& s : segs)
            if (s.role == r) return true;
        return false;
    }
};

// Las líneas visibles de los modales son rendering::ListLine
// (ver rendering/ListLines.h: composición compartida con TTY).

// Rectángulo en píxeles de la celda del cursor (para SDL + tests sin SDL).
// `valid=false` si no hay cursor lógico (invisible o celda inválida).
struct GuiPixelRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool valid = false;
};

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
    // Contenido testeable sin SDL:
    //   - renderScreenDiff deja las filas del Frame (texto + rol por
    //     segmento, con Selection = selección o highlight de búsqueda).
    //   - renderBufferList/FileList/SaveAs dejan las líneas visibles del
    //     modal (exactamente content.height entradas ListLine: items +
    //     relleno con filler=true; FileBrowser respeta scroll,
    //     BufferSelector desde 0). Un archivo real llamado "~" llega con
    //     filler=false aunque su texto sea idéntico al del relleno.
    //   - lastListSelected: índice seleccionado tal cual lo recibió (-1 si
    //     vacío/inválido para no afirmar un cursor oculto).
    const std::vector<GuiContentRow>& lastContentRows() const {
        return lastContentRows_;
    }
    const std::vector<ListLine>& lastListLines() const {
        return lastListLines_;
    }
    int lastListSelected() const { return lastListSelected_; }
    int lastListScroll() const { return lastListScroll_; }

    // Cursor GUI: Bloque (navegación) fijo / Barra (inserción) con parpadeo.
    // Política: shape Bar parpadea (Interacción + 4 prompts con input, que
    // ya vienen como Bar desde cursorShapeFor); shape Block siempre visible.
    // CONTRATO DE RELOJ: el propietario debe llamar setBlinkNow(now) antes
    // de CADA render; el renderer nunca lee el reloj interno para la fase.
    // Sin ninguna inyección la fase es ON. Tras la primera inyección la
    // fase deriva solo del último tiempo inyectado: si el propietario deja
    // de inyectar, el parpadeo queda congelado (bug del propietario).
    // Reutilizar el renderer en otro contexto exige seguir inyectando.
    void setBlinkNow(std::chrono::steady_clock::time_point now);
    // Fase del parpadeo: true = pintar cursor. Block siempre true.
    bool blinkPhaseOn() const;
    // Visibilidad efectiva para pintar: visible lógico AND fase.
    bool cursorShown() const;
    // Geometría lógica del cursor (dónde se dibujaría: Bar = 2px de ancho,
    // Block = celda). Independiente de la fase de blink: es válida aunque
    // la fase esté OFF; la visibilidad efectiva para pintar la da
    // cursorShown(). No confundir con imeRectPx() (celda completa lógica
    // para la candidata IME).
    GuiPixelRect cursorPixelRect() const;
    // Ancla IME (SDL_SetTextInputRect): celda completa del cursor lógico,
    // aunque el blink lo tenga apagado (la ventana candidata sigue al
    // cursor lógico, no a la fase visible). Si no hay cursor válido
    // devuelve valid=false y el caller (GuiRunLoop::updateImeRect)
    // conserva a propósito el último rect de SDL (ver su política).
    GuiPixelRect imeRectPx() const;

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
    // Snapshots puros (ver getters): se rellenan SIEMPRE, con o sin SDL.
    mutable std::vector<GuiContentRow> lastContentRows_;
    mutable std::vector<ListLine> lastListLines_;
    mutable int lastListSelected_ = -1;
    mutable int lastListScroll_ = 0;
    // Parpadeo del cursor (solo Bar): ancla = último cambio de celda/forma
    // medido en el tiempo inyectado por el propietario (ver contrato en
    // setBlinkNow). Sin inyección no hay ancla y la fase es ON.
    mutable std::chrono::steady_clock::time_point blinkAnchor_{};
    mutable std::chrono::steady_clock::time_point blinkNow_{};
    mutable bool blinkAnchorSet_ = false;
    mutable bool blinkNowSet_ = false;
    static constexpr int kBlinkOnMs = 530;
    static constexpr int kBlinkPeriodMs = 1060;
    void noteCursorForBlink(const FrameCursor& next) const;
};
