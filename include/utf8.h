#pragma once

#include <string>
#include <algorithm>

// Utilitarios UTF-8 puros (sin dependencias del renderer): el renderer
// los usa y los tests los ejercitan en su propio archivo.

namespace utf8 {

// Cuenta cuantas COLUMNAS VISUALES ocupan los primeros `byteCol` bytes
// de `line`. Es necesario porque un caracter UTF-8 puede ocupar varios
// bytes en el std::string pero UNA sola columna en la terminal. Si
// contamos bytes en vez de columnas, el cursor termina posicionado mas
// a la derecha de donde realmente esta el texto, dejando un hueco.
//
// Nota: esto asume caracteres de ancho 1 por codepoint (correcto para
// acentos, "—", etc). Caracteres de ancho doble (CJK, emojis) cuentan
// como 1 columna y quedan fuera del alcance de v0.1.
inline int columnOf(const std::string& line, int byteCol) {
    int col = 0;
    int limit = std::min<int>(byteCol, static_cast<int>(line.size()));
    for (int i = 0; i < limit; ++i) {
        unsigned char c = static_cast<unsigned char>(line[i]);
        // Un byte es "inicio de caracter" si NO es un byte de
        // continuacion UTF-8 (los bytes de continuacion tienen el
        // patron 10xxxxxx).
        if ((c & 0xC0) != 0x80) {
            col++;
        }
    }
    return col;
}

// Trunca `line` a lo sumo `maxCols` COLUMNAS VISUALES, sin cortar un
// caracter multibyte por la mitad (lo que generaria bytes invalidos
// y corromperia el resto del render).
inline std::string truncate(const std::string& line, int maxCols) {
    int col = 0;
    size_t i = 0;
    while (i < line.size()) {
        unsigned char c = static_cast<unsigned char>(line[i]);
        if ((c & 0xC0) != 0x80) {
            if (col >= maxCols) break;
            col++;
        }
        i++;
    }
    return line.substr(0, i);
}

// Devuelve los bytes de `line` cuyas COLUMNAS VISUALES caen dentro de
// [fromCol, toCol). No corta caracteres multibyte por la mitad. Si el
// rango llega al final de la linea devuelve hasta el ultimo byte.
inline std::string range(const std::string& line, int fromCol, int toCol) {
    if (toCol <= fromCol) return "";
    int col = 0;
    size_t startByte = line.size();
    size_t i = 0;
    while (i < line.size()) {
        unsigned char c = static_cast<unsigned char>(line[i]);
        if ((c & 0xC0) != 0x80) {
            if (col == fromCol) startByte = i;
            if (col >= toCol) break;
            col++;
        }
        i++;
    }
    if (col >= toCol) return line.substr(startByte, i - startByte);
    return line.substr(startByte); // hasta el final de la linea
}

} // namespace utf8