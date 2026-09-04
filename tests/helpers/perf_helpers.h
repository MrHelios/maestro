#pragma once
#include <string>
#include <vector>
#include "terminal/Event.h"

namespace perf_helpers {

inline std::vector<std::string> makeLines(int n, int width) {
    return std::vector<std::string>(static_cast<size_t>(n), std::string(static_cast<size_t>(width), 'x'));
}

inline Event moveEvent(EventType t) {
    Event e;
    e.type = t;
    return e;
}

}
