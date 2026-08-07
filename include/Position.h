#pragma once

// Un punto (linea, columna) dentro del documento. Las columnas son
// offsets en BYTES dentro de la linea, igual que en Document/Cursor.
// Es un struct de datos puro, sin logica.
struct Position {
    int line = 0;
    int col = 0;
};

inline bool operator==(const Position& a, const Position& b) {
    return a.line == b.line && a.col == b.col;
}

inline bool operator!=(const Position& a, const Position& b) {
    return !(a == b);
}

// Orden lexicografico por linea y luego columna.
inline bool operator<(const Position& a, const Position& b) {
    return a.line < b.line || (a.line == b.line && a.col < b.col);
}