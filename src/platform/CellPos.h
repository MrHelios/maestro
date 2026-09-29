#pragma once

// A — Frontier (1): CellPos.
//
// Coordenada 0-based de celda (columna X, fila Y): dominio neutro entre el
// decoder TTY (convierte SGR 1-based restando 1) y ScreenToCursor (consume
// sin offsets). El render la produce igual (FrameBuilder resta 1) y el
// encoder suma 1 al emitir CUP (1-based ANSI). Nada de `int` de
// transporte (SGR) fuera del decoder.
struct CellPos {
    int col = 0; // 0-based, eje X
    int row = 0; // 0-based, eje Y

    CellPos() = default;
    CellPos(int c, int r) : col(c), row(r) {}

    bool valid() const { return col >= 0 && row >= 0; }
    bool operator==(const CellPos& o) const { return col == o.col && row == o.row; }
    bool operator!=(const CellPos& o) const { return !(*this == o); }
};
