#pragma once

#include <string_view>

#include "platform/CellPos.h"
#include "platform/Event.h"

// Frontier (11): decoder SGR → tipos comunes.
//
// Traduce "[<Cb;Cx;CyM/m" (sin ESC inicial) a Event + CellPos.
// Es la única función que entiende Cb: 64/65 rueda, 0 press, 32 drag,
// 3+m release. Terminal::parseMouseSgr delega acá para no duplicar.
inline bool decodeMouseSgr(std::string_view seq, Event& e, CellPos& pos) {
    size_t p1 = seq.find(';', 2);
    size_t p2 = (p1 == std::string_view::npos) ? std::string_view::npos
                                                : seq.find(';', p1 + 1);
    char finalCh = seq.empty() ? 0 : seq.back();

    if (p1 == std::string_view::npos || p2 == std::string_view::npos ||
        (finalCh != 'M' && finalCh != 'm')) {
        e.type = EventType::None;
        return true;
    }

    int cb = 0, cx = 0, cy = 0;
    try {
        cb = std::stoi(std::string(seq.substr(2, p1 - 2)));
        cx = std::stoi(std::string(seq.substr(p1 + 1, p2 - p1 - 1)));
        cy = std::stoi(std::string(seq.substr(p2 + 1, seq.size() - p2 - 2)));
    } catch (const std::exception&) {
        e.type = EventType::None;
        return true;
    }

    pos = CellPos{cx, cy};
    const int code = cb & ~0x1C; // preserva Shift/Alt/Ctrl en el match

    if (finalCh == 'm') {
        if (code == 3) {
            e.type = EventType::MouseRelease;
            e.setCellPos(pos);
            return true;
        }
        e.type = EventType::None;
        return true;
    }
    if (code == 64) { e.type = EventType::ScrollUp; return true; }
    if (code == 65) { e.type = EventType::ScrollDown; return true; }
    if (code == 0) {
        e.type = EventType::MousePress;
        e.setCellPos(pos);
        return true;
    }
    if (code == 32) {
        e.type = EventType::MouseDrag;
        e.setCellPos(pos);
        return true;
    }
    e.type = EventType::None;
    return true;
}
