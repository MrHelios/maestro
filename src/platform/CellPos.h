#pragma once

// A — Frontier (1): CellPos.
//
// Coordenada 1-based de terminal (lo que emite SGR: Cx, Cy).
// Es el tipo común entre el decoder TTY (produce) y
// ScreenToCursor (consume). Nada de `int mouseRow/mouseCol`
// sueltos fuera de Event (compat legacy).
struct CellPos {
    int col = 0; // 1-based, eje X
    int row = 0; // 1-based, eje Y

    CellPos() = default;
    CellPos(int c, int r) : col(c), row(r) {}

    bool valid() const { return col > 0 && row > 0; }
    bool operator==(const CellPos& o) const { return col == o.col && row == o.row; }
    bool operator!=(const CellPos& o) const { return !(*this == o); }
};
