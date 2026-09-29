#pragma once
#include <string>
#include <vector>
#include "platform/InputEvent.h"

namespace perf_helpers {

inline std::vector<std::string> makeLines(int n, int width) {
    return std::vector<std::string>(static_cast<size_t>(n), std::string(static_cast<size_t>(width), 'x'));
}

inline InputEvent moveEvent(InputEventType t) {
    InputEvent e;
    e.type = t;
    return e;
}

}
