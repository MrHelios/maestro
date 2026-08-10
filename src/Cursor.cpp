#include "Cursor.h"

#include <algorithm>
#include <string>

namespace {
// true si el byte es de continuacion UTF-8 (patron 10xxxxxx).
bool isContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }
} // namespace

void Cursor::moveLeft(const Document& doc) {
    if (col > 0) {
        // Retrocede un caracter COMPLETO: el cursor siempre apunta al BYTE
        // de inicio de un caracter, asi que saltamos los bytes de
        // continuacion del caracter anterior para caer en su lead byte.
        const std::string& ln = doc.lineAt(line);
        col--;
        while (col > 0 && isContinuation(static_cast<unsigned char>(ln[col]))) {
            col--;
        }
    } else if (line > 0) {
        line--;
        col = doc.lineLength(line);
    }
    preferredCol_ = col;
}

void Cursor::moveRight(const Document& doc) {
    const int len = doc.lineLength(line);
    if (col < len) {
        // Avanza un caracter COMPLETO: col apunta al lead byte del actual;
        // lo cruzamos y seguimos salteando bytes de continuacion hasta el
        // lead byte del siguiente caracter (asi no caemos dentro de un
        // caracter multibyte).
        const std::string& ln = doc.lineAt(line);
        col++;
        while (col < len && isContinuation(static_cast<unsigned char>(ln[col]))) {
            col++;
        }
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
