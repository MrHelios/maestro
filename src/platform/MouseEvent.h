#pragma once

#include "platform/CellPos.h"
#include "platform/Event.h"

// A — Frontier (2): MouseEvent / InputEvent.
//
// Tipos comunes del borde de entrada, ANTES de la traducción a
// Event semántico. El decoder TTY produce estos; Keymap/Event
// los convierten a EventType. No mezclan Clipboard/Watcher/TTY:
// son datos puros, sin fd ni terminal.

// Botón físico (lo que SGR distingue).
enum class MouseButton { Left, Middle, Right, None };

// Evento de mouse ya decodificado: posición común + botón.
// La rueda (64/65) viaja como ScrollUp/Down en Event, no acá.
struct MouseEvent {
    CellPos pos;
    MouseButton button = MouseButton::Left;
    bool press = true;   // press(true) vs release(false)
    bool drag = false;   // Cb=32 (?1002h)
};

// Fábricas Event <- tipos comunes (azúcar, sin lógica TTY).
inline Event makeMousePressEvent(CellPos p) {
    Event e;
    e.type = EventType::MousePress;
    e.mouseCol = p.col;
    e.mouseRow = p.row;
    return e;
}

inline Event makeMouseDragEvent(CellPos p) {
    Event e;
    e.type = EventType::MouseDrag;
    e.mouseCol = p.col;
    e.mouseRow = p.row;
    return e;
}

inline Event makeMouseReleaseEvent(CellPos p) {
    Event e;
    e.type = EventType::MouseRelease;
    e.mouseCol = p.col;
    e.mouseRow = p.row;
    return e;
}

// InputEvent: envoltorio común mínimo (forma tipada de "algo llegó").
// Hoy el engine consume Event; esto documenta el paso previo
// InputEvent -> Event sin imponer variant/visit en el hot path.
struct InputEvent {
    enum class Kind { Key, Mouse, Resize, None };
    Kind kind = Kind::None;
    Event event; // forma ya traducida (puente temporal hasta el split total)
};
