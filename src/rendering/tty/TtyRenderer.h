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
#include "rendering/Renderer.h"
#include "rendering/ScreenRenderer.h"
#include "rendering/Sink.h"
#include "rendering/StatusBarData.h"
#include "rendering/Style.h"
#include "rendering/tty/TtyThemeProvider.h"
#include "rendering/tty/TtyDiff.h"
#include "rendering/tty/TtyEncoder.h"
#include "syntax/SyntaxCache.h"

// ---------------------------------------------------------------------------
// TtyRenderer (Fase E paso 6): backend TTY completo. Dueño único del estado
// con estado del backend:
//
//   Renderer (puro, con EL FrameBuilder) + TtyEncoder + TtyDiff +
//   TtyThemeProvider. El diff comparte builder y encoder por referencia
//   (un solo dueño del estado de sintaxis y del tema).
//
// El Frame puro vive en rendering/ (FrameBuilder::buildFrame vía el
// Renderer); todo lo que produce bytes ANSI vive acá.
//
// Implementa el puerto neutro ScreenRenderer (lo que app/ conoce del
// rendering); el resto de la API pública (build*, theme(), primitivas del
// encoder) es observacional propia del backend: la usan sus tests/benches
// directamente, nunca a través del Editor.
// ---------------------------------------------------------------------------
class TtyRenderer : public ScreenRenderer {
public:
    TtyRenderer();

    // Tema (fuente única vía provider; setearlo invalida el diff porque los
    // caches guardan bytes del tema anterior).
    void setTheme(const TtyTheme& t);
    void toggleTheme() override;
    const TtyTheme& theme() const { return provider_.theme(); }
    bool isDarkTheme() const override { return provider_.isDark(); }
    void invalidateCache() override { diff_.invalidateCache(); }

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
                      Sink& sink,
                      const std::optional<Selection>& selection = std::nullopt,
                      const std::optional<Selection>& searchHighlight = std::nullopt,
                      const std::optional<BracketPair>& bracketPair = std::nullopt);

    // Sin defaults repetidos: los define la interfaz (ScreenRenderer); los
    // defaults se resuelven por tipo estático y ningún caller los usa sobre
    // el concreto (todos pasan args completos o llaman vía la interfaz).
    void renderScreenDiff(const Document& doc,
                          const Cursor& cursor,
                          const Viewport& viewport,
                          const std::string& filename,
                          bool modified,
                          const Message& message,
                          State state,
                          Sink& sink,
                          const std::optional<Selection>& selection,
                          const std::optional<Selection>& searchHighlight,
                          const std::optional<BracketPair>& bracketPair) override;

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
                          int height,
                          Sink& sink) override;

    // Precondición FileBrowser: 0 <= scroll <= items.size(), 0 <= selected < items.size() (si no vacío)
    // y selected en [scroll, scroll+height). El caller (Editor) debe clampear antes de renderizar.
    std::string buildFileListScreen(const std::vector<FileListItem>& items,
                                    int selected,
                                    int scroll,
                                    const std::string& path,
                                    const Message& message,
                                    int width,
                                    int height);

    void renderFileList(const std::vector<FileListItem>& items,
                        int selected,
                        int scroll,
                        const std::string& path,
                        const Message& message,
                        int width,
                        int height,
                        Sink& sink) override;

    void setExternalSyntaxCache(SyntaxCache* c) override {
        renderer_.setExternalSyntaxCache(c);
    }
    SyntaxCache* externalSyntaxCache() const {
        return renderer_.externalSyntaxCache();
    }
    SyntaxCache& activeCache() const { return renderer_.activeCache(); }

    // Observabilidad para tests (solo lectura): estado del cache diferencial.
    bool hasCache() const { return diff_.hasCache(); }
    int lastViewportH() const { return diff_.lastViewportH(); }

    // Primitivas del backend (las usan los benches).
    void renderEditorContent(std::string& out,
                             const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::optional<Normalized>& sel,
                             const Rect& area,
                             int gutterW) const;
    void renderStatusBar(std::string& out,
                         const Rect& area,
                         const StatusBarData& data,
                         StyleRole accent) const;
    void beginFrame(std::string& out) { encoder_.beginFrame(out); }
    void endFrame(std::string& out) { encoder_.endFrame(out); }
    void hideCursor(std::string& out) { encoder_.hideCursor(out); }
    void showCursor(std::string& out) { encoder_.showCursor(out); }
    void setCursorStyle(std::string& out, State state) {
        encoder_.setCursorStyle(out, state);
    }
    void setCursorStyle(std::string& out, FrameCursorShape shape) {
        encoder_.setCursorStyle(out, shape);
    }
    void moveCursorToRaw(std::string& out, int row, int col) {
        encoder_.moveCursorToRaw(out, row, col);
    }

private:
    Renderer renderer_;  // puro, con EL FrameBuilder
    TtyEncoder encoder_;
    TtyThemeProvider provider_;
    TtyDiff diff_;

    void renderBufferListContent(std::string& out,
                                 const std::vector<std::string>& names,
                                 int selected,
                                 const Rect& area);
    // Requiere las mismas invariantes de scroll/selected que buildFileListScreen().
    void renderFileListContent(std::string& out,
                               const std::vector<FileListItem>& items,
                               int selected,
                               int scroll,
                               const Rect& area);
};
