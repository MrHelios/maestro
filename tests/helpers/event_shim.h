#pragma once

// Shim de construcción de eventos para tests (Fase A).
//
// Objetivo: que el cambio 1-based SGR -> 0-based CellPos sea gradual y no
// contamine toda la suite de una sola vez.
//
//   Fase N -> shim -> tests verdes -> migración -> eliminar shim
//
// Provee una única capa de construcción:
//
//   makeKeyEvent(type, text="")
//   makeCharEvent(text)              // azúcar sobre InsertChar
//   makeMouseEvent(type, CellPos)    // Press/Drag/Release genérico, dominio destino
//   cellFromSgr(cx, cy)              // ÚNICO punto que sabe del offset 1-based SGR
//   makeMouseEventSgr(type, cx, cy)  // entrada 1-based (literales SGR de tests)
//   fakeResize(rows, cols)           // EventType::Resize autónomo
//
// Convención de coordenadas:
//   - Hoy CellPos es 1-based (lo que emite SGR: Cx, Cy).
//   - makeMouseEvent(type, CellPos) recibe un CellPos YA en el dominio
//     destino: es para tests ya migrados, NO es punto de conversión.
//     Cuando CellPos pase a 0-based, los literales 1-based que hoy se
//     escriben como CellPos{col, row} cambiarían de significado en
//     silencio si pasaran por acá: acá no hay nada que restar.
//   - El único punto de conversión es cellFromSgr(cx, cy): su firma se
//     declara 1-based (tal como lo emite SGR). La migración futura a
//     0-based toca SOLO cellFromSgr (luego {cx-1, cy-1}) + el decoder
//     (TtyMouse.h) + ScreenToCursor; los tests que usen
//     makeMouseEventSgr no cambian.
//
// Compatibilidad transitoria (deprecated):
//   - `event.mouseRow / event.mouseCol` siguen existiendo y siguen
//     pasando. Son API de transición: el código nuevo de tests debe usar
//     `setCellPos()/cellPos()` o este shim, no tocar los ints directo.
//   - `MouseEvent.h` ya expone `makeMousePress/Drag/ReleaseEvent(CellPos)`;
//     `makeMouseEvent` de acá delega en ellas (sin lógica TTY duplicada).
//   - Una vez migrados todos los callers a CellPos (y luego a 0-based),
//     ELIMINAR este shim junto con el overload legacy de ScreenToCursor
//     y los accesos directos a mouseRow/mouseCol en tests.
//   - NO cubre los screenToCursor(row, col) con ints crudos: ver inventario
//     en el overload legacy de ScreenToCursor.h.

#include <string>

#include "platform/CellPos.h"
#include "platform/Event.h"
#include "platform/MouseEvent.h"

namespace testshim {

inline Event makeKeyEvent(EventType type, const std::string& text = "") {
    Event e;
    e.type = type;
    e.text = text;
    return e;
}

inline Event makeCharEvent(const std::string& text) {
    return makeKeyEvent(EventType::InsertChar, text);
}

inline Event makeMouseEvent(EventType type, CellPos pos) {
    // Recibe un CellPos YA en el dominio destino (para tests migrados).
    // NO es punto de conversión: no restar 1 acá nunca.
    switch (type) {
        case EventType::MousePress:
            return makeMousePressEvent(pos);
        case EventType::MouseDrag:
            return makeMouseDragEvent(pos);
        case EventType::MouseRelease:
            return makeMouseReleaseEvent(pos);
        default: {
            Event e;
            e.type = type;
            e.setCellPos(pos);
            return e;
        }
    }
}

// Coordenadas tal como las emite SGR (1-based). Único lugar que sabe del offset.
inline CellPos cellFromSgr(int cx, int cy) { return CellPos{cx, cy}; }  // luego {cx-1, cy-1}
inline Event makeMouseEventSgr(EventType t, int cx, int cy) {
    return makeMouseEvent(t, cellFromSgr(cx, cy));
}

inline Event fakeResize(int rows, int cols) {
    Event e;
    e.type = EventType::Resize;
    e.resizeRows = rows;
    e.resizeCols = cols;
    return e;
}

}  // namespace testshim
