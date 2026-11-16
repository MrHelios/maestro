#pragma once

#include <string_view>

#include "platform/CellPos.h"
#include "platform/InputEvent.h"

// Frontier (11): decoder SGR → tipos comunes.
//
// Traduce "[<Cb;Cx;CyM/m" (sin ESC inicial, 1-based SGR) a InputEvent +
// CellPos 0-based (resta 1: único lugar que conoce el offset SGR en
// entrada; ver docs/architecture.md §2).
// Es la única función que entiende Cb: 64/65 rueda, 0 press, 32 drag,
// 3+m release. Terminal::parseMouseSgr delega acá para no duplicar.
inline bool decodeMouseSgr(std::string_view seq, InputEvent& e, CellPos& cell) {
    size_t p1 = seq.find(';', 2);
    size_t p2 = (p1 == std::string_view::npos) ? std::string_view::npos
                                                : seq.find(';', p1 + 1);
    char finalCh = seq.empty() ? 0 : seq.back();

    if (p1 == std::string_view::npos || p2 == std::string_view::npos ||
        (finalCh != 'M' && finalCh != 'm')) {
        e.type = InputEventType::None;
        return true;
    }

    // SGR define exactamente 3 campos: Cb;Cx;Cy. Un 4o campo invalida la secuencia.
    if (seq.find(';', p2 + 1) != std::string_view::npos) {
        e.type = InputEventType::None;
        return true;
    }

    int cb = 0, cx = 0, cy = 0;
    try {
        cb = std::stoi(std::string(seq.substr(2, p1 - 2)));
        cx = std::stoi(std::string(seq.substr(p1 + 1, p2 - p1 - 1)));
        cy = std::stoi(std::string(seq.substr(p2 + 1, seq.size() - p2 - 2)));
    } catch (const std::exception&) {
        e.type = InputEventType::None;
        return true;
    }

    // El protocolo SGR cuenta desde 1; coordenadas <= 0 son invalidas.
    if (cx <= 0 || cy <= 0) {
        e.type = InputEventType::None;
        return true;
    }

    cell = CellPos{cx - 1, cy - 1};
    const int btn = cb & 3;          // 0=left, 1=middle, 2=right, 3=release
    const int code = cb & ~0x1C;     // Cb sin modificadores (Shift/Alt/Ctrl)
    const bool isDrag = (cb & 32) != 0; // bit 5: motion/drag flag (?1002h)

    // Rueda: los modificadores no cambian el tipo de evento (64|mod sigue
    // siendo ScrollUp, 65|mod ScrollDown). El release de rueda (final 'm')
    // no representa una accion y se ignora.
    if (code == 64 || code == 65) {
        if (finalCh == 'm') {
            e.type = InputEventType::None;
            return true;
        }
        e.type = (code == 64) ? InputEventType::ScrollUp
                              : InputEventType::ScrollDown;
        return true;
    }

    if (finalCh == 'm') {
        // Release: SGR estandar usa cb==3, pero aceptamos cualquier cb con 'm'
        // para indicar que botón se solto (btn 0/1/2) o el estandar cb==3.
        if (btn == 3 || btn == 0) {
            e.type = InputEventType::MouseRelease;
            e.button = MouseButton::Left;
        } else if (btn == 1) {
            e.type = InputEventType::MouseRelease;
            e.button = MouseButton::Middle;
        } else { // btn == 2
            e.type = InputEventType::MouseRelease;
            e.button = MouseButton::Right;
        }
        e.cell = cell;
        return true;
    }
    if (finalCh == 'M') {
        if (btn == 0) {
            e.type = isDrag ? InputEventType::MouseDrag : InputEventType::MousePress;
            e.button = MouseButton::Left;
        } else if (btn == 1) {
            e.type = isDrag ? InputEventType::MouseDrag : InputEventType::MousePress;
            e.button = MouseButton::Middle;
        } else if (btn == 2) {
            e.type = isDrag ? InputEventType::MouseDrag : InputEventType::MousePress;
            e.button = MouseButton::Right;
        } else {
            e.type = InputEventType::None;
        }
        e.cell = cell;
        return true;
    }
    e.type = InputEventType::None;
    return true;
}
