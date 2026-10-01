#pragma once

#include <optional>
#include <string>
#include <vector>

#include "app/EditorState.h"
#include "app/Message.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "document/Selection.h"
#include "layout/Layout.h"
#include "layout/Viewport.h"
#include "rendering/Sink.h"
#include "rendering/StatusBarData.h"
#include "rendering/Style.h"
#include "rendering/frame/FrameBuilder.h"
#include "rendering/tty/Theme.h"
#include "rendering/tty/TtyEncoder.h"

// Item estructurado de la lista de archivos (Alcance 1): `name` es el
// nombre base sin sufijos; `isDirectory` dice si es carpeta. El sufijo
// visual "/" lo pone el backend al pintar, no el app/.
struct FileListItem {
    std::string name;
    bool isDirectory = false;
};

// ---------------------------------------------------------------------------
// TtyLists (Fase E paso 5): pantallas TTY-directo del backend TTY.
//
// Todo lo que en el Renderer monolítico era TTY-directo (listas de
// buffers/archivos, renderEditorContent con "\r\n", helpers de fila)
// vive acá, movido tal cual sin cambios de bytes. El Renderer común ya no
// las conoce: el dueño (Editor / tests) usa esta clase para los modales.
//
// NOTA: render* NO invalida ningún cache diferencial (no posee el TtyDiff).
// El caller invalida en las transiciones hacia/desde el modal
// (Editor::renderFrame lo hace una sola vez por transición, ver wasModal_).
// ---------------------------------------------------------------------------
class TtyLists {
public:
    TtyLists() = default;
    explicit TtyLists(const Theme& t) : encoder_(t) {}

    void setTheme(const Theme& t) { encoder_.setTheme(t); }
    const Theme& theme() const { return encoder_.theme(); }

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

    // Primitivas del backend (las usan los benches; en el paso 6 las
    // consume TtyRenderer). Delegan en TtyEncoder/FrameBuilder.
    void beginFrame(std::string& out) const { encoder_.beginFrame(out); }
    void endFrame(std::string& out) const { encoder_.endFrame(out); }
    void hideCursor(std::string& out) const { encoder_.hideCursor(out); }
    void showCursor(std::string& out) const { encoder_.showCursor(out); }
    void setCursorStyle(std::string& out, State state) const {
        encoder_.setCursorStyle(out, state);
    }
    void setCursorStyle(std::string& out, FrameCursorShape shape) const {
        encoder_.setCursorStyle(out, shape);
    }
    void moveCursorToRaw(std::string& out, int row, int col) const {
        encoder_.moveCursorToRaw(out, row, col);
    }

private:
    FrameBuilder frameBuilder_;
    TtyEncoder encoder_;

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
};
