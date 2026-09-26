#pragma once

#include <cstdlib>
#include <string>

// Frontier (15): TtyScroll.
//
// Política pura del scroll diferencial TTY: decide si el delta de
// viewport admite región de scroll (CSI S/T) o exige rebuild total.
// No emite nada por sí misma; TtyDiff la consulta y emite.
struct TtyScrollOp {
    bool useRegion = false; // false => rebuild total
    int absDelta = 0;       // filas a desplazar
    bool down = true;       // true: S (deltaTop>0), false: T
};

inline TtyScrollOp scrollOpFor(int contentH, int deltaTop) {
    TtyScrollOp op;
    const int absDelta = std::abs(deltaTop);
    if (contentH <= 0 || absDelta == 0 || absDelta >= contentH) return op;
    op.useRegion = true;
    op.absDelta = absDelta;
    op.down = deltaTop > 0;
    return op;
}

inline std::string scrollRegionPrefix(int contentH, const TtyScrollOp& op) {
    std::string out;
    out += "\x1b[1;";
    out += std::to_string(contentH);
    out += "r";
    out += "\x1b[";
    out += std::to_string(op.absDelta);
    out += op.down ? "S" : "T";
    out += "\x1b[r";
    return out;
}
