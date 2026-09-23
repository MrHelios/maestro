#include "document/Cursor.h"

#include <algorithm>
#include <string>

#include "base/utf8.h"

namespace {
// true si el byte es whitespace ASCII separador de palabras (' ' o '\t').
bool isSeparator(char c) { return c == ' ' || c == '\t'; }
} // namespace

void Cursor::moveLeft(const Document& doc) {
    if (col > 0) {
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
    preferredCol_ = visualColumn(doc);
}

void Cursor::moveRight(const Document& doc) {
    const int len = doc.lineLength(line);
    if (col < len) {
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
    preferredCol_ = visualColumn(doc);
}

void Cursor::moveUp(const Document& doc) {
    if (line == 0) return;
    --line;
    applyPreferredCol(doc);
}

void Cursor::moveDown(const Document& doc) {
    if (line + 1 >= doc.lineCount()) return;
    ++line;
    applyPreferredCol(doc);
}

void Cursor::moveHome() {
    col = 0;
    preferredCol_ = 0;
}

void Cursor::moveEnd(const Document& doc) {
    col = doc.lineLength(line);
    preferredCol_ = visualColumn(doc);
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
            preferredCol_ = visualColumn(doc);
            return;
        }
        l++;
        c = 0;
    }
    preferredCol_ = visualColumn(doc);
}

// k: al COMIENZO del bloque anterior (atras), cruzando lineas. Devuelve
// el inicio de la corrida de no-separadores que termina justo antes de la
// posicion actual; si no hay nada en esta linea, sube buscando la ultima
// palabra de la linea anterior.
void Cursor::moveToPreviousWord(const Document& doc) {
    int l = line;
    int c = col;
    while (c > 0 && isSeparator(doc.lineAt(l)[c - 1])) c--;
    if (c > 0) {
        const std::string& ln = doc.lineAt(l);
        while (c > 0 && !isSeparator(ln[c - 1])) c--;
        line = l; col = c;
        preferredCol_ = visualColumn(doc);
        return;
    }
    l--;
    while (l >= 0) {
        const std::string& ln = doc.lineAt(l);
        const int len = static_cast<int>(ln.size());
        if (len > 0 && !isSeparator(ln[len - 1])) {
            int cc = len;
            while (cc > 0 && !isSeparator(ln[cc - 1])) cc--;
            line = l; col = cc;
            preferredCol_ = visualColumn(doc);
            return;
        }
        l--;
    }
    preferredCol_ = visualColumn(doc);
}

void Cursor::clampToLine(const Document& doc) {
    col = std::min(col, doc.lineLength(line));
    if (line < doc.lineCount()) col = utf8::alignStart(doc.lineAt(line), col);
}

void Cursor::applyPreferredCol(const Document& doc) {
    const std::string& ln = doc.lineAt(line);
    col = utf8::byteForColumn(ln, preferredCol_);
}
