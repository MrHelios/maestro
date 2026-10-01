#pragma once

#include <memory>
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
#include "rendering/Sink.h"
#include "rendering/StatusBarData.h"
#include "rendering/frame/FrameBuilder.h"
#include "syntax/SyntaxCache.h"

// Forward declarations del backend TTY (Fase E paso 1): el header común no
// incluye rendering/tty/* de forma directa ni transitiva. Los detalles viven
// solo en Renderer.cpp. `Theme` también se forward-declara por el mismo
// motivo (su definición ANSI vive en rendering/tty/Theme.h).
class TtyDiff;
class TtyEncoder;
struct Theme;

// Item estructurado de la lista de archivos (Alcance 1): `name` es el
// nombre base sin sufijos; `isDirectory` dice si es carpeta. El sufijo
// visual "/" lo pone el Renderer al pintar, no el app/.
struct FileListItem {
    std::string name;
    bool isDirectory = false;
};

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
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;
    // NOTA de alcance (honesta): acá no hay flag de test-mode ni estado
    // global, y build* es puro (FrameBuilder -> Frame -> string, sin I/O).
    // Pero la clase NO es backend-independent: posee TtyEncoder y TtyDiff
    // como miembros, su setTheme habla el Theme ANSI del backend TTY, y las
    // pantallas de listas siguen siendo TTY directo. Es el shim de
    // transición documentado arriba: conoce StyleRole + Frame, construye,
    // codifica vía el backend TTY que posee y entrega a un Sink inyectado.
    // La independencia total (Renderer puro + TtyRenderer en tty/) queda
    // pendiente y exigiría migrar la batería de tests.
    void setTheme(const Theme& t);
    const Theme& theme() const;
    void invalidateCache();

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

    void renderScreenDiff(const Document& doc,
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
                          Sink& sink);

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
                         Sink& sink);

    void setExternalSyntaxCache(SyntaxCache* c);
    SyntaxCache* externalSyntaxCache() const {
        return frameBuilder_.externalSyntaxCache();
    }
    SyntaxCache& activeCache() const { return frameBuilder_.activeCache(); }

    // Observabilidad para tests (solo lectura): estado del cache diferencial
    // (vive en TtyDiff). Reemplaza el acceso directo a los viejos campos
    // hasCache_/lastViewportH_ del Renderer monolítico.
    bool hasCache() const;
    int lastViewportH() const;

private:
    // frameBuilder_ es común y va por valor; encoder_/diff_ son el backend
    // TTY y van por puntero para no incluir tty/* en este header (paso 1).
    // Se definen en Renderer.cpp.
    mutable FrameBuilder frameBuilder_;
    std::unique_ptr<TtyEncoder> encoder_;
    std::unique_ptr<TtyDiff> diff_;

    void renderBufferListContent(std::string& out,
                                   const std::vector<std::string>& names,
                                   int selected,
                                   const Rect& area) const;

    // Requiere las mismas invariantes de scroll/selected que buildFileListScreen().
    void renderFileListContent(std::string& out,
                                  const std::vector<FileListItem>& items,
                                  int selected,
                                  int scroll,
                                  const Rect& area) const;

    void renderStatusBar(std::string& out,
                           const Rect& area,
                           const StatusBarData& data) const;

    // Compatibilidad con perf-tests (delegan en TtyEncoder/FrameBuilder).
    // Se definen out-of-line en Renderer.cpp para no exponer tty/* acá.
    void beginFrame(std::string& out) const;
    void endFrame(std::string& out) const;
    void hideCursor(std::string& out) const;
    void showCursor(std::string& out) const;
    void setCursorStyle(std::string& out, State state) const;
    void setCursorStyle(std::string& out, FrameCursorShape shape) const;
    void moveCursorToRaw(std::string& out, int row, int col) const;
    void renderEditorContent(std::string& out,
                               const Document& doc,
                               const Cursor& cursor,
                               const Viewport& viewport,
                               const std::optional<Normalized>& sel,
                                const Rect& area,
                                int gutterW) const;
};
