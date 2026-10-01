#pragma once

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "app/EditorState.h"
#include "app/Message.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "document/Selection.h"
#include "layout/BracketMatcher.h"
#include "layout/Layout.h"
#include "layout/Viewport.h"
#include "rendering/StatusBarData.h"
#include "rendering/frame/FrameBuilder.h"
#include "rendering/tty/TtyEncoder.h"

// ---------------------------------------------------------------------------
// TtyDiff: render diferencial del backend TTY.
//
// Dueño EXCLUSIVO de los caches codificados (rowCache_/statusCache_ + estado
// de viewport/cursor/versión). El contrato común (Frame) nunca los ve:
// el diff compara filas YA codificadas por TtyEncoder y emite solo CSI
// puntuales (scroll de región, rewrites de fila, cursor).
//
// La lógica es la del viejo Renderer::buildDiffFrame & cía., movida acá sin
// cambios de comportamiento.
//
// EXCEPCION ARQUITECTONICA (documentada, no un bug): el contrato comun
// FrameCursor { cell, visible, shape } solo lo materializa
// FrameBuilder::buildFrame(). Los fast paths de este diff
// (buildCursorMoveFrame / buildScrollFrame / rebuild / slow path) no
// construyen un Frame: resuelven el cursor directamente con
// FrameBuilder::editorCursorPos() —el mismo resolver, sin clamp duplicado—
// y traducen su bool a "posicionar+mostrar" o "dejar oculto".
// Modelo mental:
//
//   FrameBuilder::buildFrame()
//       └─ contrato FrameCursor comun (TTY/GUI)
//
//   TtyDiff fast paths
//       └─ bypass de Frame por performance,
//          pero usan el mismo resolver de posicion/visibilidad
// ---------------------------------------------------------------------------
class TtyDiff {
public:
    void setTheme(const Theme& t) {
        encoder_.setTheme(t);
        hasCache_ = false;
        hasLastStatusData_ = false;
    }
    void invalidateCache() {
        hasCache_ = false;
        hasLastStatusData_ = false;
    }
    const Theme& theme() const { return encoder_.theme(); }

    void setExternalSyntaxCache(SyntaxCache* c) {
        builder_.setExternalSyntaxCache(c);
    }

    // Observabilidad para tests (solo lectura): estado del cache diferencial.
    bool hasCache() const { return hasCache_; }
    int lastViewportW() const { return lastViewportW_; }
    int lastViewportH() const { return lastViewportH_; }

    std::string buildDiffFrame(const Document& doc,
                               const Cursor& cursor,
                               const Viewport& viewport,
                               const std::string& filename,
                               bool modified,
                               const Message& message,
                               State state,
                               const std::optional<Selection>& selection = std::nullopt,
                               const std::optional<Selection>& searchHighlight = std::nullopt,
                               const std::optional<BracketPair>& bracketPair = std::nullopt);

    // Requiere rowCache_[i] <=> línea viewport.top+i y size==contentH.
    std::string buildScrollFrame(const Document& doc,
                                 const Cursor& cursor,
                                 const Viewport& viewport,
                                 const std::string& filename,
                                 bool modified,
                                 const Message& message,
                                 State state,
                                 const std::optional<Selection>& selection,
                                 const std::optional<Selection>& searchHighlight,
                                 const std::optional<BracketPair>& bracketPair,
                                 int deltaTop);

    static void splitRows(const std::string& body,
                          std::vector<std::string_view>* rows);

private:
    FrameBuilder builder_;
    TtyEncoder encoder_;

    std::deque<std::string> rowCache_; // una entrada por fila: "\x1b[K" + bytes
    std::string statusCache_;          // status bar codificada, filas con "\r\n"
    bool hasCache_ = false;
    int cachedContentH_ = -1;
    int lastViewportW_ = -1;
    int lastViewportH_ = -1;
    int lastViewportTop_ = 0;
    int lastViewportLeft_ = 0;
    int lastCursorLine_ = 0;
    int lastCursorCol_ = 0;
    uint64_t lastVersion_ = 0;
    int lastLineCount_ = 0;
    StatusBarData lastStatusData_;
    bool hasLastStatusData_ = false;
    std::optional<BracketPair> lastBracketPair_;
    bool hasLastBracketPair_ = false;

    std::string buildCursorMoveFrame(const Document& doc,
                                     const Cursor& cursor,
                                     const Viewport& viewport,
                                     const std::string& filename,
                                     bool modified,
                                     const Message& message,
                                     State state);

    void rebuildCache(const Document& doc, const Cursor& cursor,
                      const Viewport& viewport, const std::string& filename,
                      bool modified, const Message& message, State state,
                      const std::optional<Selection>& selection,
                      const std::optional<Selection>& searchHighlight,
                      const std::optional<BracketPair>& bracketPair);

    bool patchContentRow(std::string& out, const Document& doc,
                         const Cursor& cursor, const Viewport& viewport,
                         const std::optional<Normalized>& sel,
                         const std::optional<Normalized>& searchSel,
                         const std::optional<Normalized>& bracketOpen,
                         const std::optional<Normalized>& bracketClose,
                         int docLine, int gutterW, int textWidth, int contentH);

    void patchStatusBar(std::string& out, const Document& doc,
                        const Cursor& cursor, const std::string& filename,
                        bool modified, const Message& message, State state,
                        const Layout& layout, int contentH);

    // Resuelve el cursor con el resolver común (sin clamp duplicado) y lo
    // posiciona/muestra. Busqueda o fuera de viewport => lo deja oculto
    // (contrato visible==false). endFrame=true cierra con endFrame en vez
    // de showCursor (camino de frame completo). Única versión con el bloque
    // resolve -> CUP -> style -> show/end (4 caminos lo usan).
    void placeCursor(std::string& out, const Document& doc,
                     const Cursor& cursor, const Viewport& viewport,
                     State state, bool endFrame);
    void placeCursor(std::string& out, const Document& doc,
                     const Cursor& cursor, const Viewport& viewport,
                     const FrameBuilder::EditorGeometry& g, State state,
                     bool endFrame);
    void emitCursor(std::string& out, CellPos pos, bool visible, State state,
                    bool endFrame);

    void updateCacheState(const Viewport& viewport, const Cursor& cursor,
                          const Document& doc);
};
