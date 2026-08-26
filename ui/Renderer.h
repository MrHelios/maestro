#pragma once

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

class Renderer {
public:
    void setTheme(const Theme& t) { theme_ = t; }
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

    std::string lastEditorBody_;
    bool hasLastEditorBody_ = false;
    int lastViewportW_ = -1;
    int lastViewportH_ = -1;

    Layout calculateLayout(int contentRows, int width) const;

    void renderEditorContent(std::string& out,
                              const Document& doc,
                              const Cursor& cursor,
                              const Viewport& viewport,
                              const std::optional<Normalized>& sel,
                              const Rect& area,
                              int gutterW) const;
    void renderEditorContent(std::string& out,
                              const Document& doc,
                              const Cursor& cursor,
                              const Viewport& viewport,
                              const std::optional<Normalized>& sel,
                              const std::optional<Normalized>& searchSel,
                              const Rect& area,
                              int gutterW) const;

    void renderBufferListContent(std::string& out,
                                  const std::vector<std::string>& names,
                                  int selected,
                                  const Rect& area) const;

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

    void editorCursorPos(const Document& doc,
                          const Cursor& cursor,
                          const Viewport& viewport,
                          int& outRow, int& outCol) const;

    void moveCursorTo(std::string& out, int row, int col) const;

    void beginFrame(std::string& out) const;
    void endFrame(std::string& out) const;
    void hideCursor(std::string& out) const;
    void showCursor(std::string& out) const;
};
