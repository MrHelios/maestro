#pragma once

#include "platform/CellPos.h"
#include "platform/InputEvent.h"

// A — Frontier (2): MouseEvent / InputEvent.
//
// Tipos comunes del borde de entrada. El decoder TTY ya NO produce
// MouseEvent: produce InputEvent + CellPos directo (ver
// platform/tty/TtyMouse.h, decodeMouseSgr). Este header conserva el tipo
// clasico MouseEvent solo por compatibilidad (hoy sin productores
// internos) y las fabricas CellPos -> InputEvent, que son el camino
// canonico para construir eventos de mouse en dominio 0-based.
// No mezclan Clipboard/Watcher/TTY: son datos puros, sin fd ni terminal.

// Botón físico (lo que SGR distingue).
enum class MouseButton { Left, Middle, Right, None };

// Evento de mouse ya decodificado: posición común + botón.
// La rueda (64/65) viaja como ScrollUp/Down en InputEvent, no acá.
struct MouseEvent {
    CellPos cell;
    MouseButton button = MouseButton::Left;
    bool press = true;   // press(true) vs release(false)
    bool drag = false;   // Cb=32 (?1002h)
};

// Fábricas InputEvent <- tipos comunes (azúcar, sin lógica TTY).
// Punto único de construcción con CellPos 0-based: el decoder ya
// convirtió SGR; los callers trabajan siempre en dominio destino.
inline InputEvent makeMousePressEvent(CellPos cell) {
    InputEvent e;
    e.type = InputEventType::MousePress;
    e.cell = cell;
    return e;
}

inline InputEvent makeMouseDragEvent(CellPos cell) {
    InputEvent e;
    e.type = InputEventType::MouseDrag;
    e.cell = cell;
    return e;
}

inline InputEvent makeMouseReleaseEvent(CellPos cell) {
    InputEvent e;
    e.type = InputEventType::MouseRelease;
    e.cell = cell;
    return e;
}

// Nota: este header solo conserva MouseEvent y las fábricas
// MouseEvent -> InputEvent (vocabulario semántico de
// platform/InputEvent.h).
