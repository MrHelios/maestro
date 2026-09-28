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
// Punto único de conversión CellPos -> Event (hoy identidad 1-based vía
// setCellPos; al migrar a 0-based se ajusta acá, no en cada caller).
inline Event makeMousePressEvent(CellPos p) {
    Event e;
    e.type = EventType::MousePress;
    e.setCellPos(p);
    return e;
}

inline Event makeMouseDragEvent(CellPos p) {
    Event e;
    e.type = EventType::MouseDrag;
    e.setCellPos(p);
    return e;
}

inline Event makeMouseReleaseEvent(CellPos p) {
    Event e;
    e.type = EventType::MouseRelease;
    e.setCellPos(p);
    return e;
}

// Nota: el envoltorio genérico antes llamado `InputEvent` vivía aquí como
// puente temporal; el nombre canónico ahora es el vocabulario semántico de
// platform/InputEvent.h (alias Event en platform/Event.h). Este header solo
// conserva MouseEvent y las fábricas MouseEvent -> Event.
