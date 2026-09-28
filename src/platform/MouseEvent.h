#pragma once

#include "platform/CellPos.h"
#include "platform/InputEvent.h"

// A — Frontier (2): MouseEvent / InputEvent.
//
// Tipos comunes del borde de entrada, ANTES de la traducción a
// InputEvent semántico. El decoder TTY produce estos; Keymap
// los convierte a InputEventType. No mezclan Clipboard/Watcher/TTY:
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
// Punto único de conversión CellPos -> InputEvent (hoy identidad 1-based vía
// setCellPos; al migrar a 0-based se ajusta acá, no en cada caller).
inline InputEvent makeMousePressEvent(CellPos p) {
    InputEvent e;
    e.type = InputEventType::MousePress;
    e.setCellPos(p);
    return e;
}

inline InputEvent makeMouseDragEvent(CellPos p) {
    InputEvent e;
    e.type = InputEventType::MouseDrag;
    e.setCellPos(p);
    return e;
}

inline InputEvent makeMouseReleaseEvent(CellPos p) {
    InputEvent e;
    e.type = InputEventType::MouseRelease;
    e.setCellPos(p);
    return e;
}

// Nota: el nombre canónico es el vocabulario semántico de
// platform/InputEvent.h (`Event` en platform/Event.h es solo un alias
// legacy para tests). Este header solo conserva MouseEvent y las
// fábricas MouseEvent -> InputEvent.
