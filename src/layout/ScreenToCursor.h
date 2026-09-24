#pragma once

#include <algorithm>
#include <optional>

#include "base/utf8.h"
#include "document/Document.h"
#include "document/Position.h"
#include "layout/Layout.h"
#include "layout/Viewport.h"

// Mapeo inverso pantalla -> buffer para MousePress (funcion pura).
//
// Convierte coordenadas 1-based de terminal (las que emite SGR) a una
// Position del documento, usando la MISMA geometria que el Renderer para
// buffer -> pantalla (Layout::content + gutter + viewport.top/left).
// No depende de Editor/Renderer: testeable sin construir el engine.
//
// Reglas:
//   - Fuera de layout.content (status bar, etc.) => nullopt.
//   - Fila `~` (docLine >= lineCount) => nullopt.
//   - Click en gutter (relCol < gutterW) => inicio de la fila (col 0).
//   - Click en texto o mas alla de EOL => utf8::byteForColumn + alignStart
//     (mas alla de EOL clampa naturalmente a EOL).
inline int screenToCursorGutterWidth(int totalLines, int viewportWidth) {
    int digits = 1;
    for (int n = totalLines; n >= 10; n /= 10) ++digits;
    return std::min(std::max(3, digits + 1), viewportWidth);
}

inline std::optional<Position> screenToCursor(int mouseRow, int mouseCol,
                                              const Layout& layout,
                                              const Viewport& viewport,
                                              const Document& doc) {
    const int relRow = mouseRow - 1 - layout.content.row;
    const int relCol = mouseCol - 1 - layout.content.col;
    if (relRow < 0 || relRow >= layout.content.height) return std::nullopt;
    if (relCol < 0) return std::nullopt;

    const int count = doc.lineCount();
    if (count <= 0) return std::nullopt;
    const int docLine = viewport.top + relRow;
    if (docLine < 0 || docLine >= count) return std::nullopt;

    const int gutterW = screenToCursorGutterWidth(count, viewport.width);
    if (relCol < gutterW) return Position{docLine, 0};

    const int visTarget = viewport.left + (relCol - gutterW);
    const std::string& line = doc.lineAt(docLine);
    const int target = visTarget < 0 ? 0 : visTarget;
    int byteCol = utf8::byteForColumn(line, target);
    byteCol = utf8::alignStart(line, byteCol);
    if (byteCol < 0) byteCol = 0;
    if (byteCol > static_cast<int>(line.size())) byteCol = static_cast<int>(line.size());
    return Position{docLine, byteCol};
}
