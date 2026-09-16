#pragma once

#include <deque>
#include <optional>
#include <string>
#include <vector>
#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Layout.h"
#include "core/Selection.h"
#include "core/Theme.h"
#include "core/Viewport.h"
#include "ui/EditorState.h"
#include "ui/Message.h"
#include "ui/StatusBar.h"
#include "syntax/SyntaxHighlighter.h"
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"

class Renderer {
public:
    static void setTestMode(bool v) { s_testMode = v; }
    static bool isTestMode() { return s_testMode; }
    void setTheme(const Theme& t) {
        theme_ = t;
        hasCache_ = false;
        hasLastStatusData_ = false;
    }
    void invalidateCache() {
        hasCache_ = false;
        hasLastStatusData_ = false;
    }
    const Theme& theme() const { return theme_; }

    std::string buildScreen(const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::string& filename,
                             bool modified,
                             const Message& message,
                             State state,
                             const std::optional<Selection>& selection = std::nullopt,
                             const std::optional<Selection>& searchHighlight = std::nullopt);

    void renderScreen(const Document& doc,
                      const Cursor& cursor,
                      const Viewport& viewport,
                      const std::string& filename,
                      bool modified,
                      const Message& message,
                      State state,
                      const std::optional<Selection>& selection = std::nullopt,
                      const std::optional<Selection>& searchHighlight = std::nullopt);

    void renderScreenDiff(const Document& doc,
                          const Cursor& cursor,
                          const Viewport& viewport,
                          const std::string& filename,
                          bool modified,
                          const Message& message,
                          State state,
                          const std::optional<Selection>& selection = std::nullopt,
                          const std::optional<Selection>& searchHighlight = std::nullopt);

    std::string buildDiffFrame(const Document& doc,
                               const Cursor& cursor,
                               const Viewport& viewport,
                               const std::string& filename,
                               bool modified,
                               const Message& message,
                               State state,
                               const std::optional<Selection>& selection = std::nullopt,
                               const std::optional<Selection>& searchHighlight = std::nullopt);

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

private:
    Theme theme_ = defaultTheme();
    mutable SyntaxHighlighter syntaxHighlighter_;
    // Mutable cache: syntax state is derived from Document identity,
    // version() and language; rebuilding it does not change rendering semantics.
    mutable std::vector<SyntaxState> syntaxStates_;
    mutable uint64_t syntaxStatesVersion_ = UINT64_MAX;
    mutable SyntaxLanguage syntaxStatesLang_ = SyntaxLanguage::None;
    mutable const Document* syntaxStatesDoc_ = nullptr;

    mutable std::deque<std::string> rowCache_;   // una entrada por fila de contenido: "\x1b[K" + bytes
    mutable std::string statusCache_;            // status bar cacheado, filas separadas por "\r\n"
    mutable bool hasCache_ = false;
    mutable int cachedContentH_ = -1;
    mutable int lastViewportW_ = -1;
    mutable int lastViewportH_ = -1;
    mutable int lastViewportTop_ = 0;
    mutable int lastViewportLeft_ = 0;
    mutable int lastCursorLine_ = 0;
    mutable int lastCursorCol_ = 0;
    mutable uint64_t lastVersion_ = 0;
    mutable int lastLineCount_ = 0;
    mutable StatusBarData lastStatusData_;
    mutable bool hasLastStatusData_ = false;

    Layout calculateLayout(int contentRows, int width) const;

    // Sobrecarga para tests/bench sin searchHighlight; delega en la de 7 args con searchSel = nullopt.
    void renderEditorContent(std::string& out,
                              const Document& doc,
                              const Cursor& cursor,
                              const Viewport& viewport,
                              const std::optional<Normalized>& sel,
                              const Rect& area,
                              int gutterW) const;
    // Núcleo: renderiza el contenido en `area` con gutter `gutterW` aplicando sel y searchSel.
    void renderEditorContent(std::string& out,
                              const Document& doc,
                              const Cursor& cursor,
                              const Viewport& viewport,
                              const std::optional<Normalized>& sel,
                              const std::optional<Normalized>& searchSel,
                              const Rect& area,
                              int gutterW) const;

    void renderEditorRow(std::string& out,
                         const Document& doc,
                         const Cursor& cursor,
                         const Viewport& viewport,
                         const std::optional<Normalized>& sel,
                         const std::optional<Normalized>& searchSel,
                         int docLine,
                         int gutterW,
                         int textWidth) const;
    void renderEditorRow(std::string& out,
                         const Document& doc,
                         const Cursor& cursor,
                         const Viewport& viewport,
                         const std::optional<Normalized>& sel,
                         const std::optional<Normalized>& searchSel,
                         int docLine,
                         int gutterW,
                         int textWidth,
                         const std::vector<SyntaxSpan>& spans) const;

    SyntaxState syntaxStateAt(const Document& doc, int targetLine) const;
    void updateSyntaxLanguage(const std::string& filename) const;
    const std::string& syntaxStyleFor(SyntaxToken tok) const;

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

    struct EditorGeometry {
        Layout layout;
        int gutterW = 0;
    };
    EditorGeometry editorGeometry(const Document& doc,
                                  const Viewport& viewport) const;

    std::string buildEditorBody(const Document& doc,
                                 const Cursor& cursor,
                                 const Viewport& viewport,
                                 const std::string& filename,
                                 bool modified,
                                 const Message& message,
                                 State state,
                                 const std::optional<Selection>& selection,
                                 const std::optional<Selection>& searchHighlight = std::nullopt) const;

    std::string buildScrollFrame(const Document& doc,
                                 const Cursor& cursor,
                                 const Viewport& viewport,
                                 const std::string& filename,
                                 bool modified,
                                 const Message& message,
                                 State state,
                                 const std::optional<Selection>& selection,
                                 const std::optional<Selection>& searchHighlight,
                                 int deltaTop);

    std::string buildCursorMoveFrame(const Document& doc,
                                     const Cursor& cursor,
                                     const Viewport& viewport,
                                     const std::string& filename,
                                     bool modified,
                                     const Message& message,
                                     State state);

    void rebuildCache(const Document& doc, const Cursor& cursor, const Viewport& viewport,
                      const std::string& filename, bool modified, const Message& message,
                      State state, const std::optional<Selection>& selection,
                      const std::optional<Selection>& searchHighlight);

    bool patchContentRow(std::string& out, const Document& doc, const Cursor& cursor,
                         const Viewport& viewport, const std::optional<Normalized>& sel,
                         const std::optional<Normalized>& searchSel, int docLine,
                         int gutterW, int textWidth, int contentH);

    void patchStatusBar(std::string& out, const Document& doc, const Cursor& cursor,
                        const std::string& filename, bool modified, const Message& message,
                        State state, const Layout& layout, int contentH);

    void editorCursorPos(const Document& doc,
                          const Cursor& cursor,
                          const Viewport& viewport,
                          int& outRow, int& outCol) const;
    void editorCursorPos(const Document& doc,
                          const Cursor& cursor,
                          const Viewport& viewport,
                          const EditorGeometry& g,
                          int& outRow, int& outCol) const;

    void moveCursorTo(std::string& out, int row, int col) const;

    void beginFrame(std::string& out) const;
    void endFrame(std::string& out) const;
    void hideCursor(std::string& out) const;
    void showCursor(std::string& out) const;
    void setCursorStyle(std::string& out, State state) const;
    void updateCacheState(const Viewport& viewport, const Cursor& cursor,
                          const Document& doc);

    static void splitRows(const std::string& body, std::vector<std::string_view>* rows);

    static inline bool s_testMode = false;
};
