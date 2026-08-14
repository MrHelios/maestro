#include "Cursor.h"

#include <algorithm>
#include <string>

#include "utf8.h"

namespace {
// true si el byte es whitespace ASCII separador de palabras (' ' o '\t').
bool isSeparator(char c) { return c == ' ' || c == '\t'; }
} // namespace

void Cursor::moveLeft(const Document& doc) {
    if (col > 0) {
        // Retrocede una celda COMPLETA: el cursor siempre apunta al INICIO
        // de una celda, asi que saltamos los bytes de la celda anterior
        // (continuaciones que pertenecen a su lead) para caer en su inicio.
        // Con utf8::isCellStart, una continuacion huerfana es su propia
        // celda (modelo byte-safe): nunca se "pega" a un vecino invalido.
        const std::string& ln = doc.lineAt(line);
        int i = col - 1;
        while (i > 0 && !utf8::isCellStart(ln, i)) {
            i--;
        }
        col = i;
    } else if (line > 0) {
        line--;
        col = doc.lineLength(line);
    }
    preferredCol_ = col;
}

void Cursor::moveRight(const Document& doc) {
    const int len = doc.lineLength(line);
    if (col < len) {
        // Avanza una celda COMPLETA: col apunta al inicio de la celda
        // actual; la cruzamos y seguimos saltando bytes hasta el inicio
        // de la siguiente celda (asi no caemos dentro de una celda).
        const std::string& ln = doc.lineAt(line);
        int i = col + 1;
        while (i < len && !utf8::isCellStart(ln, i)) {
            i++;
        }
        col = i;
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

// j: al FINAL del siguiente bloque (adelante), cruzando lineas.
// De la posicion actual se salta el gap de separadores y se avanza hasta
// el fin de la palabra que encuentre; si la linea no tiene mas palabra,
// se continua con la primera de la siguiente. Si no hay un bloque mas
// adelante en todo el documento, el cursor se queda donde esta.
void Cursor::moveToNextWord(const Document& doc) {
    int l = line;
    int c = col;
    const int n = doc.lineCount();
    while (l < n) {
        const std::string& ln = doc.lineAt(l);
        const int len = static_cast<int>(ln.size());
        while (c < len && isSeparator(ln[c])) c++;
        if (c < len) {
            while (c < len && !isSeparator(ln[c])) c++;
            line = l; col = c;
            preferredCol_ = c;
            return;
        }
        // No hay mas palabra en esta linea desde c: pasar a la siguiente.
        l++;
        c = 0;
    }
    // No encontro ningun bloque posterior al cursor; dejamos el cursor como
    // estaba (EOF).
    preferredCol_ = col;
}

// k: al COMIENZO del bloque anterior (atras), cruzando lineas. Devuelve
// el inicio de la corrida de no-separadores que termina justo antes de la
// posicion actual; si no hay nada en esta linea, sube buscando la ultima
// palabra de la linea anterior.
void Cursor::moveToPreviousWord(const Document& doc) {
    int l = line;
    int c = col;
    // Saltar el gap de separadores inmediatamente anterior al cursor.
    while (c > 0 && isSeparator(doc.lineAt(l)[c - 1])) c--;
    if (c > 0) {
        // Hay una corrida de no-separadores terminando en c: ir a su inicio.
        const std::string& ln = doc.lineAt(l);
        while (c > 0 && !isSeparator(ln[c - 1])) c--;
        line = l; col = c;
        preferredCol_ = c;
        return;
    }
    // Nada antes en esta linea: buscar la ultima palabra de la linea anterior.
    l--;
    while (l >= 0) {
        const std::string& ln = doc.lineAt(l);
        const int len = static_cast<int>(ln.size());
        if (len > 0 && !isSeparator(ln[len - 1])) {
            int cc = len;
            while (cc > 0 && !isSeparator(ln[cc - 1])) cc--;
            line = l; col = cc;
            preferredCol_ = cc;
            return;
        }
        l--;
    }
    // No hay ningun bloque anterior en el documento: el cursor se queda.
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
