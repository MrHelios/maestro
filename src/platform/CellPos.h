#pragma once

// A — Frontier (1): CellPos.
//
// Coordenada 0-based de celda (columna X, fila Y): dominio neutro entre el
// decoder TTY (convierte SGR 1-based restando 1) y ScreenToCursor (consume
// sin offsets). El render la produce igual (FrameBuilder resta 1) y el
// encoder suma 1 al emitir CUP (1-based ANSI). Nada de `int` de
// transporte (SGR) fuera del decoder.
//
// Default inválido (-1,-1): un CellPos recién construido NO es una celda
// válida (valid() == false). Esto evita que un evento que no es de mouse
// cargue una "celda válida" espuria en {0,0}. Solo los eventos de mouse
// (MousePress/MouseDrag/MouseRelease) portan una celda válida; el resto
// conserva el default inválido.
struct CellPos {
    int col = -1; // 0-based, eje X; -1 = inválido
    int row = -1; // 0-based, eje Y; -1 = inválido

    CellPos() = default;
    CellPos(int c, int r) : col(c), row(r) {}

    bool valid() const { return col >= 0 && row >= 0; }
    bool operator==(const CellPos& o) const { return col == o.col && row == o.row; }
    bool operator!=(const CellPos& o) const { return !(*this == o); }
};
