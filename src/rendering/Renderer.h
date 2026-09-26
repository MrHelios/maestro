#pragma once

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
#include "rendering/Theme.h"
#include "rendering/frame/FrameBuilder.h"
#include "rendering/tty/TtyDiff.h"
#include "rendering/tty/TtyEncoder.h"
#include "syntax/SyntaxCache.h"

// ---------------------------------------------------------------------------
// Renderer: SHIM de compatibilidad (Fase B/C-1).
//
// La API pública se preserva intacta para no migrar la batería de tests en
// esta fase. Internamente delega en el pipeline nuevo:
//
//   buildScreen*    -> FrameBuilder::buildFrame + TtyEncoder::encodeFrame
//   buildDiffFrame* -> TtyDiff (dueño de rowCache_/statusCache_/scroll)
//
// Las pantallas de listas (buffers/archivos) siguen siendo TTY directo
// (pendientes de extracción a rendering/tty/). Los métodos privados al pie
// (beginFrame, renderEditorContent, ...) se conservan solo porque los
// perf-tests los usan; delegan en FrameBuilder/TtyEncoder.
// ---------------------------------------------------------------------------
class Renderer {
public:
    static void setTestMode(bool v) { s_testMode = v; }
    static bool isTestMode() { return s_testMode; }
    void setTheme(const Theme& t);
    const Theme& theme() const { return encoder_.theme(); }
    void invalidateCache() { diff_.invalidateCache(); }

    std::string buildScreen(const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::string& filename,
                             bool modified,
                             const Message& message,
                             State state,
                             const std::optional<Selection>& selection = std::nullopt,
                             const std::optional<Selection>& searchHighlight = std::nullopt,
                             const std::optional<BracketPair>& bracketPair = std::nullopt);

    void renderScreen(const Document& doc,
                      const Cursor& cursor,
                      const Viewport& viewport,
                      const std::string& filename,
                      bool modified,
                      const Message& message,
                      State state,
                      const std::optional<Selection>& selection = std::nullopt,
                      const std::optional<Selection>& searchHighlight = std::nullopt,
                      const std::optional<BracketPair>& bracketPair = std::nullopt);

    void renderScreenDiff(const Document& doc,
                          const Cursor& cursor,
                          const Viewport& viewport,
                          const std::string& filename,
                          bool modified,
                          const Message& message,
                          State state,
                          const std::optional<Selection>& selection = std::nullopt,
                          const std::optional<Selection>& searchHighlight = std::nullopt,
                          const std::optional<BracketPair>& bracketPair = std::nullopt);

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

    std::string buildBufferListScreen(const std::vector<std::string>& names,
                                       int selected,
                                       int width,
                                       int height);

    void renderBufferList(const std::vector<std::string>& names,
                          int selected,
                          int width,
                          int height);

    // Precondición FileBrowser: 0 <= scroll <= names.size(), 0 <= selected < names.size() (si no vacío)
    // y selected en [scroll, scroll+height). El caller (Editor) debe clampear antes de renderizar.
    std::string buildFileListScreen(const std::vector<std::string>& names,
                                     int selected,
                                     int scroll,
                                     const std::string& path,
                                     const Message& message,
                                     int width,
                                     int height);

    void renderFileList(const std::vector<std::string>& names,
                         int selected,
                         int scroll,
                         const std::string& path,
                         const Message& message,
                         int width,
                         int height);

    void setExternalSyntaxCache(SyntaxCache* c);
    SyntaxCache* externalSyntaxCache() const {
        return frameBuilder_.externalSyntaxCache();
    }
    SyntaxCache& activeCache() const { return frameBuilder_.activeCache(); }

    // Observabilidad para tests (solo lectura): estado del cache diferencial
    // (vive en TtyDiff). Reemplaza el acceso directo a los viejos campos
    // hasCache_/lastViewportH_ del Renderer monolítico.
    bool hasCache() const { return diff_.hasCache(); }
    int lastViewportH() const { return diff_.lastViewportH(); }

private:
    mutable FrameBuilder frameBuilder_;
    mutable TtyEncoder encoder_;
    mutable TtyDiff diff_;

    void renderBufferListContent(std::string& out,
                                   const std::vector<std::string>& names,
                                   int selected,
                                   const Rect& area) const;

    // Requiere las mismas invariantes de scroll/selected que buildFileListScreen().
    void renderFileListContent(std::string& out,
                                 const std::vector<std::string>& names,
                                 int selected,
                                 int scroll,
                                 const Rect& area) const;

    void renderStatusBar(std::string& out,
                           const Rect& area,
                           const StatusBarData& data) const;

    // Compatibilidad con perf-tests (delegan en TtyEncoder/FrameBuilder).
    void beginFrame(std::string& out) const { encoder_.beginFrame(out); }
    void endFrame(std::string& out) const { encoder_.endFrame(out); }
    void hideCursor(std::string& out) const { encoder_.hideCursor(out); }
    void showCursor(std::string& out) const { encoder_.showCursor(out); }
    void setCursorStyle(std::string& out, State state) const {
        encoder_.setCursorStyle(out, state);
    }
    void moveCursorTo(std::string& out, int row, int col) const {
        encoder_.moveCursorTo(out, row, col);
    }
    void renderEditorContent(std::string& out,
                               const Document& doc,
                               const Cursor& cursor,
                               const Viewport& viewport,
                               const std::optional<Normalized>& sel,
                               const Rect& area,
                               int gutterW) const;

    static inline bool s_testMode = false;
};
