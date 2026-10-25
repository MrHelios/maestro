#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <sys/stat.h>

#include "app/Editor.h"
#include "base/utf8.h"
#include "layout/Gutter.h"
#include "layout/ScreenToCursor.h"

namespace editor_internal {

// Mensajes de ayuda fijos por modo (v?.?): centralizados aca para que
// cambiar el texto sea tocar un solo lugar, no buscarlo repetido por el
// archivo. Antes "ABRIR: ..." estaba duplicado en startFileBrowser() y
// fileBrowserEnterSelected(); con esto solo hay una fuente de verdad.
constexpr const char* kHelpBufferSelector = "ENTER: open | ESC: cancel | \u2191/\u2193: move";
constexpr const char* kHelpBusqueda = "Find: ";
constexpr const char* kHelpNavegacion =
    "NAVEGACION: i escribir | s seleccionar | c/x copiar/cortar | p pegar | "
    "Ctrl+K buffer/guardar/salir | Ctrl+U/Y deshacer/rehacer";
constexpr const char* kHelpEmpty = "";
constexpr const char* kHelpPrefix = "command: Ctrl+k";
constexpr const char* kHelpIrAFila = "ir a fila: ";
constexpr const char* kHelpNombrar = "Nombre del Archivo: ";
constexpr const char* kHelpSaveAsSuffix = " (Enter para Guardar)";

// Byte offset del cursor de edición al final del input de un prompt
// (prompt + query), antes de cualquier decoración posterior
// (" - not found", " (i/N)", " [max ...]", " (Control+S...)"): el input se
// escribe ahí aunque el mensaje siga. Los prompts son ASCII, la query puede
// ser UTF-8: el offset en bytes lo traduce a columnas messageBarCursorCell.
inline int promptCursor(const char* prompt, const std::string& query) {
    return static_cast<int>(std::string(prompt).size() + query.size());
}
// Longitud en bytes de la etiqueta del prompt ("Find: ", "ir a fila: ",
// ...): es lo unico que va en negrita; el input del usuario y los sufijos
// decorativos van sin negrita (ver Message::boldPrefix).
inline int promptBoldPrefix(const char* prompt) {
    return static_cast<int>(std::string(prompt).size());
}
// Limite superior del nombre (basename): NAME_MAX en ext4 = 255 bytes.
// Se cuenta en bytes (no caracteres) porque el filesystem cuenta bytes;
// UTF-8 multibyte cuenta multiple. El path completo ademas se valida
// contra PATH_MAX en commitRename.
constexpr size_t kMaxRenameBytes = 255;
constexpr int kIndentLen = 4;
inline int shiftColumn(int col, int delta) {
    return delta > 0 ? col + delta : std::max(0, col + delta);
}

// textWidthFor sigue local a proposito: es una derivacion usada por el
// Editor, no politica compartida. El gutter viene de layout/Gutter.h.
inline int textWidthFor(const Viewport& vp, int totalLines) {
    int gutterW = gutterWidth(totalLines, vp.width);
    int w = vp.width - gutterW;
    return w < 0 ? 0 : w;
}

// Helpers de soft-wrap para el Editor.
// Condición EFECTIVA compartida por navegación, mouse y renderizado: un
// archivo Markdown no usa soft-wrap si no queda ancho útil para el texto.
// El índice aplica la misma regla (wantWrap = enabled && textWidth > 0).
inline bool isWrapEnabledFor(const Buffer& b) {
    return softwrap::isMarkdownFilename(b.filename) &&
           textWidthFor(b.viewport, b.document.lineCount()) > 0;
}

// Asegura que el índice de wrap del buffer activo esté actualizado.
inline const softwrap::Index& wrapFor(Editor* ed, const Buffer& b) {
    int tw = textWidthFor(b.viewport, b.document.lineCount());
    ed->wrapIndex().ensure(b.document, tw, isWrapEnabledFor(b));
    return ed->wrapIndex();
}

// Marca una línea como modificada para actualización incremental del índice de wrap
inline void markWrapLineDirty(Editor* ed, int line) {
    ed->wrapIndex().markLineDirty(line);
}

// Columna visual relativa dentro de la fila visual (0..columns-1)
inline int visualColumnOf(const softwrap::Index& idx, const Document& doc, const Position& pos) {
    return idx.visualColumnOf(doc, pos);
}

// screenToCursor con wrap: mapea celda visual -> posicion logica
inline std::optional<Position> screenToCursorWrap(const softwrap::Index& idx,
                                                   CellPos cell,
                                                   const Layout& layout,
                                                   const Viewport& viewport,
                                                   const Document& doc,
                                                   int gutterW) {
    const int relRow = cell.row - layout.content.row;
    const int relCol = cell.col - layout.content.col;
    if (relRow < 0 || relRow >= layout.content.height) return std::nullopt;
    if (relCol < 0) return std::nullopt;

    int visualRow = viewport.top + relRow;
    if (visualRow >= idx.totalRows()) return std::nullopt;

    if (relCol < gutterW) {
        // Click en gutter: inicio de la fila visual
        return idx.positionAt(doc, visualRow, 0);
    }

    int visualColInRow = relCol - gutterW;  // viewport.left es 0 en wrap
    return idx.positionAt(doc, visualRow, visualColInRow);
}

inline void updateModified(Buffer& b) {
    b.recalcModified();
}

// Posicion visible clampada al borde para el drag del mouse (autoscroll y
// filas `~`). Funcion PURA: no muta el viewport, solo lo lee.
//
// Es la unica copia de la logica "relCol -> byte col" fuera de
// screenToCursor(): gutter -> inicio de fila, si no visTarget = left +
// (col - gutterW) via byteForColumn + alignStart + clamp a EOL. Tanto el
// camino tilde como el post-scroll pasan por aca, asi gutter/tabs/emoji
// /EOL no pueden divergir entre ambos.
//
// Requiere doc.lineCount() > 0, h > 0 y totalW > 0 (los callers lo
// garantizan). La linea se clampdea a [0, count): en filas `~` eso da
// EOF sin codigo especial.
// EOF lógico de una selección: una posición DESPUÉS del último carácter.
// Distinto de la posición que devuelve End/positionAt para el cursor (última
// celda de texto): un extremo de selección half-open necesita este byte.
inline Position dragEofPosition(const Document& doc) {
    const int line = doc.lineCount() - 1;
    return Position{line, doc.lineLength(line)};
}

inline Position dragEdgePosition(int relRow, int relCol, const Viewport& vp,
                                 const Document& doc, int h, int totalW) {
    const int count = doc.lineCount();
    const int gutterW = gutterWidth(count, totalW);
    const int clampedRow = std::min(std::max(relRow, 0), h - 1);
    const int clampedCol = std::min(std::max(relCol, 0), totalW - 1);
    int line = vp.top + clampedRow;
    if (line < 0) line = 0;
    if (line >= count) line = count - 1;
    const std::string& lineStr = doc.lineAt(line);
    if (clampedCol < gutterW) return Position{line, 0};
    int visTarget = vp.left + (clampedCol - gutterW);
    if (visTarget < 0) visTarget = 0;
    int byteCol = utf8::byteForColumn(lineStr, visTarget);
    byteCol = utf8::alignStart(lineStr, byteCol);
    if (byteCol < 0) byteCol = 0;
    if (byteCol > static_cast<int>(lineStr.size()))
        byteCol = static_cast<int>(lineStr.size());
    return Position{line, byteCol};
}

// Convierte una ruta a su forma CANONICA-LEXICA: absoluta contra cwd() y
// con "." / ".." reducidos ("foo/../bar" -> "bar"). Unifica rutas que
// apuntan al mismo archivo pero se escriben distinto, de modo que el
// chequeo de duplicados de buffers funcione ("a/../b" == "b").
//
// NO resuelve symlinks: eso requiere filesystem::canonical(), que exige
// que el archivo EXISTA y falla sobre archivos nuevos (que el editor
// tambien abre). Resolver symlinks queda como limitacion documentada.
inline std::string resolveAbsolutePath(const std::string& path) {
    if (path.empty()) return path;
    std::error_code ec;
    return std::filesystem::absolute(path, ec).lexically_normal().string();
}

inline Buffer::FileIdentity captureIdentity(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return Buffer::FileIdentity{};
    Buffer::FileIdentity id;
    id.valid = true;
    id.dev = st.st_dev;
    id.ino = st.st_ino;
    id.size = st.st_size;
    id.mtime = st.st_mtim;
    return id;
}

} // namespace editor_internal
