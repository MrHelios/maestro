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
#include "rendering/StatusBar.h"
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

    void updateCacheState(const Viewport& viewport, const Cursor& cursor,
                          const Document& doc);
};
