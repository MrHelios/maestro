#pragma once

#include "platform/CellPos.h"
#include "platform/InputEvent.h"

// A — Frontier (2): MouseEvent / InputEvent.
//
// Tipos comunes del borde de entrada. El decoder TTY produce
// InputEvent + CellPos directo (ver platform/tty/TtyMouse.h); este header
// conserva el tipo clasico MouseEvent y las fabricas MouseEvent ->
// InputEvent. No mezclan Clipboard/Watcher/TTY:
// son datos puros, sin fd ni terminal.

// Botón físico (lo que SGR distingue).
enum class MouseButton { Left, Middle, Right, None };

// Evento de mouse ya decodificado: posición común + botón.
// La rueda (64/65) viaja como ScrollUp/Down en InputEvent, no acá.
struct MouseEvent {
    CellPos pos;
    MouseButton button = MouseButton::Left;
    bool press = true;   // press(true) vs release(false)
    bool drag = false;   // Cb=32 (?1002h)
};

// Fábricas InputEvent <- tipos comunes (azúcar, sin lógica TTY).
// Punto único de construcción con CellPos 0-based: el decoder ya
// convirtió SGR; los callers trabajan siempre en dominio destino.
inline InputEvent makeMousePressEvent(CellPos p) {
    InputEvent e;
    e.type = InputEventType::MousePress;
    e.cell = p;
    return e;
}

inline InputEvent makeMouseDragEvent(CellPos p) {
    InputEvent e;
    e.type = InputEventType::MouseDrag;
    e.cell = p;
    return e;
}

inline InputEvent makeMouseReleaseEvent(CellPos p) {
    InputEvent e;
    e.type = InputEventType::MouseRelease;
    e.cell = p;
    return e;
}

// Nota: este header solo conserva MouseEvent y las fábricas
// MouseEvent -> InputEvent (vocabulario semántico de
// platform/InputEvent.h).
