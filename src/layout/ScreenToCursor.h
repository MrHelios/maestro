#pragma once

#include <algorithm>
#include <optional>

#include "base/utf8.h"
#include "document/Document.h"
#include "document/Position.h"
#include "layout/Gutter.h"
#include "layout/Layout.h"
#include "layout/Viewport.h"
#include "platform/CellPos.h"

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
//
// El ancho del gutter es layout/Gutter.h (unica fuente de verdad).

inline std::optional<Position> screenToCursor(CellPos pos,
                                              const Layout& layout,
                                              const Viewport& viewport,
                                              const Document& doc) {
    const int relRow = pos.row - 1 - layout.content.row;
    const int relCol = pos.col - 1 - layout.content.col;
    if (relRow < 0 || relRow >= layout.content.height) return std::nullopt;
    if (relCol < 0) return std::nullopt;

    const int count = doc.lineCount();
    if (count <= 0) return std::nullopt;
    const int docLine = viewport.top + relRow;
    if (docLine < 0 || docLine >= count) return std::nullopt;

    const int gutterW = gutterWidth(count, viewport.width);
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

// LEGACY (regla: existe solo mientras haya consumidores).
// Una vez migrados todos los callers a CellPos, ELIMINAR este wrapper.
// Pendientes: Editor::handleMousePress, Editor::resolveMouseDragPosition
// (pueden usar event.cellPos()) y tests/unit/test_mouse_press.cpp.
// Inventario test_mouse_press.cpp (ints crudos 1-based SGR, NO pasan por
// el shim event_shim.h ni por cellFromSgr — migrarlos a CellPos/cellFromSgr
// cuando CellPos pase a 0-based):
//   mouse_map_text_click (1,5) | mouse_map_gutter_goes_to_start (1,1) |
//   mouse_map_gutter_text_border (1,3),(1,4),(1,5) |
//   mouse_map_past_eol_clamps (1,50) |
//   mouse_map_wide_char_both_cells (1,1+6),(1,1+7) |
//   mouse_map_tab_cells loop (1,1+c) c=3..6 + (1,1+7) |
//   mouse_map_with_scroll_offsets (1,4) |
//   mouse_map_first_last_visible_row (1,4),(4,4) |
//   mouse_map_below_doc_and_statusbar_ignored (3,10),(11,10).
// Inventario completo de la migración: docs/migration-cellpos.md.
inline std::optional<Position> screenToCursor(int mouseRow, int mouseCol,
                                              const Layout& layout,
                                              const Viewport& viewport,
                                              const Document& doc) {
    return screenToCursor(CellPos{mouseCol, mouseRow}, layout, viewport, doc);
}
