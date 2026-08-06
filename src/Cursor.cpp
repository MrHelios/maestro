#include "Cursor.h"

#include <algorithm>

void Cursor::moveLeft(const Document& doc) {
    if (col > 0) {
        col--;
    } else if (line > 0) {
        line--;
        col = doc.lineLength(line);
    }
    preferredCol_ = col;
}

void Cursor::moveRight(const Document& doc) {
    if (col < doc.lineLength(line)) {
        col++;
    } else if (line + 1 < doc.lineCount()) {
        line++;
        col = 0;
    }
    preferredCol_ = col;
}

void Cursor::moveUp(const Document& doc) {
    if (line == 0) return;
    line--;
    applyPreferredCol(doc);
}

void Cursor::moveDown(const Document& doc) {
    if (line + 1 >= doc.lineCount()) return;
    line++;
    applyPreferredCol(doc);
}

void Cursor::moveHome() {
    col = 0;
    preferredCol_ = col;
}

void Cursor::moveEnd(const Document& doc) {
    col = doc.lineLength(line);
    preferredCol_ = col;
}

void Cursor::clampToLine(const Document& doc) {
    col = std::min(col, doc.lineLength(line));
}

void Cursor::applyPreferredCol(const Document& doc) {
    // Esta es la regla descrita en el enunciado: si la linea nueva es
    // mas corta que la columna preferida, el cursor va al final de esa
    // linea, pero preferredCol_ NO se pisa -- asi, si seguimos subiendo
    // (o bajando) a una linea mas larga, el cursor "recupera" la
    // columna original.
    int len = doc.lineLength(line);
    col = std::min(preferredCol_, len);
}
