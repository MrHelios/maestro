#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <algorithm>

// Utilitarios UTF-8 puros (sin dependencias del renderer): el renderer
// los usa y los tests los ejercitan en su propio archivo.

namespace utf8 {

// ---------------------------------------------------------------------------
// Celda (modelo byte-safe, decision documentada en README y Document.h)
// ---------------------------------------------------------------------------
// Una "celda" es la unidad minima que el editor trata como un caracter al
// navegar, borrar y contar columnas. Bajo el modelo B (Document = bytes,
// UTF-8 = presentacion), una celda es:
//
//   - una secuencia UTF-8 VALIDA (lead + todas sus continuaciones presentes), o
//   - cualquier byte que NO forme una secuencia valida (lead invalido,
//     continuacion huerfana, overlong, ...) como celda propia de 1 byte.
//
// Asi un archivo con bytes invalidos sigue siendo navegable byte a byte,
// y ninguna celda "se traga" bytes que no le pertenecen (un byte de
// continuacion huerfano es una celda propia, no se pega al vecino).
inline bool isCellStart(std::string_view line, int i) {
    if (i < 0 || i >= static_cast<int>(line.size())) return false;
    unsigned char c = static_cast<unsigned char>(line[i]);
    if (c < 0x80) return true;               // ASCII
    if ((c & 0xC0) != 0x80) return true;     // lead (valido o invalido)
    // Continuacion: es "huerfana" (inicio de celda) si el lead valido mas
    // cercano a la izquierda ya esta completo o no la cubre.
    int j = i - 1;
    while (j >= 0 && (static_cast<unsigned char>(line[j]) & 0xC0) == 0x80) {
        j--;
    }
    if (j < 0) return true;                  // sin lead previo
    unsigned char lead = static_cast<unsigned char>(line[j]);
    int expect = 0;                          // continuaciones que declara el lead
    if ((lead & 0xE0) == 0xC0) expect = 1;
    else if ((lead & 0xF0) == 0xE0) expect = 2;
    else if ((lead & 0xF8) == 0xF0) expect = 3;
    const int conts = i - j - 1;             // continuaciones entre lead y `i`
    return conts >= expect;                  // cubierta por el lead si conts < expect
}

// Cuenta cuantas COLUMNAS VISUALES ocupan los primeros `byteCol` bytes
// de `line`. Es necesario porque un caracter UTF-8 puede ocupar varios
// bytes en el std::string pero UNA sola columna en la terminal. Si
// contamos bytes en vez de columnas, el cursor termina posicionado mas
// a la derecha de donde realmente esta el texto, dejando un hueco.
//
// LIMITACION DOCUMENTADA (v0.x): una celda NO equivale a una columna
// de terminal. Aqui se cuenta ancho 1 por celda, lo que es correcto
// para texto latino (acentos, "—", "€", ...) pero aproximado para el
// resto. En concreto, quedan fuera dos ejes independientes:
//
//   (1) Ancho de celda: hay codepoints que la terminal pinta en 2
//       columnas: CJK (中), emojis (🙂). Aqui se les cuenta 1, asi que
//       se renderizan apiñados y `columnOf`/`truncate`/`range` no los
//       alinean contra el borde derecho. La solucion correcta seria por
//       tabla de ancho (East Asian Width -> 2), como wcwidth()/wcswidth,
//       y arrastra el modelo de columna de TODO el editor (Cursor,
//       Viewport, seleccion), por eso se deja documentado y no resuelto.
//
//   (2) Cluster de grafemas: una UNIDAD VISUAL puede ser varias
//       celdas que se combinan, p.ej. "a" + codigo de combinacion
//       (a + U+0301 = á), secuencias ZWJ (👩👩👧), o variation selectors
//       (e + U+FE0F). Aqui cada celda se cuenta como una columna y un
//       salto de cursor; lo correcto seria segmentar por grafemas (UAX #29)
//       para que el cursor y la seleccion naveguen por unidades visuales.
//
// Ambos ejes se dejan como limitacion de alcance cierta: el modelo
// actual (bytes, ancho 1 por celda) es una aproximacion que no parte
// secuencias UTF-8 validas y funciona para texto occidental, pero no
// pretende ser un render de texto completo.
inline int cellLen(std::string_view line, int pos, int n) {
    unsigned char c = static_cast<unsigned char>(line[pos]);
    if (c < 0x80) return 1;
    if ((c & 0xC0) == 0x80) return 1;
    int expect = 0;
    if ((c & 0xE0) == 0xC0) expect = 1;
    else if ((c & 0xF0) == 0xE0) expect = 2;
    else if ((c & 0xF8) == 0xF0) expect = 3;
    int len = 1;
    while (len <= expect && pos + len < n && (static_cast<unsigned char>(line[pos + len]) & 0xC0) == 0x80) ++len;
    return len;
}

// Encuentra el byte de inicio de la celda que contiene pos.
// Si pos==0 devuelve 0; si pos apunta dentro de una celda multibyte
// devuelve el lead de esa celda; si es continuación huérfana la trata
// como celda propia (modelo byte-safe).
inline int cellStartBefore(std::string_view line, int pos) {
    if (pos <= 0) return 0;
    int start = pos - 1;
    int i = start;
    while (i > 0 && (static_cast<unsigned char>(line[i]) & 0xC0) == 0x80) --i;
    if ((static_cast<unsigned char>(line[i]) & 0xC0) == 0x80) {
        return i;
    }
    unsigned char lead = static_cast<unsigned char>(line[i]);
    int expect = 0;
    if ((lead & 0xE0) == 0xC0) expect = 1;
    else if ((lead & 0xF0) == 0xE0) expect = 2;
    else if ((lead & 0xF8) == 0xF0) expect = 3;
    int conts = start - i;
    if (conts > expect) return start;
    return i;
}

inline int columnOf(std::string_view line, int byteCol) {
    int n = static_cast<int>(line.size());
    int limit = byteCol < n ? byteCol : n;
    if (limit <= 0) return 0;
    int col = 0;
    int i = 0;
    while (i < limit) {
        if (i + 8 <= limit) {
            uint64_t v;
            std::memcpy(&v, line.data() + i, 8);
            if ((v & 0x8080808080808080ULL) == 0) { col += 8; i += 8; continue; }
        }
        ++col;
        i += cellLen(line, i, n);
    }
    return col;
}

// Trunca `line` a lo sumo `maxCols` COLUMNAS VISUALES, sin cortar una
// celda por la mitad (lo que generaria bytes invalidos y corromperia el
// resto del render).
inline std::string truncate(std::string_view line, int maxCols) {
    int col = 0;
    size_t i = 0;
    while (i < line.size()) {
        if (isCellStart(line, static_cast<int>(i))) {
            if (col >= maxCols) break;
            col++;
        }
        i++;
    }
    return std::string(line.substr(0, i));
}

// Devuelve los bytes de `line` cuyas COLUMNAS VISUALES caen dentro de
// [fromCol, toCol). No corta celdas por la mitad. Si el rango llega al
// final de la linea devuelve hasta el ultimo byte.
inline std::string_view range(std::string_view line, int fromCol, int toCol) {
    if (toCol <= fromCol) return "";
    int col = 0;
    size_t startByte = line.size();
    size_t i = 0;
    while (i < line.size()) {
        if (isCellStart(line, static_cast<int>(i))) {
            if (col == fromCol) startByte = i;
            if (col >= toCol) break;
            col++;
        }
        i++;
    }
    if (col >= toCol) return line.substr(startByte, i - startByte);
    return line.substr(startByte); // hasta el final de la linea
}

// Normaliza `col` al inicio de la celda que contiene.
// Si `col` ya es inicio de celda (o fuera de rango), lo deja intacto.
inline int alignStart(std::string_view line, int col) {
    if (col <= 0 || col >= static_cast<int>(line.size())) return col;
    if (isCellStart(line, col)) return col;
    return cellStartBefore(line, col);
}

// Normaliza `col` al final (exclusivo) de la celda que contiene.
// Si `col` ya es inicio de celda, devuelve `col` (rango vacio).
inline int alignEnd(std::string_view line, int col) {
    if (col <= 0 || col >= static_cast<int>(line.size())) return col;
    if (isCellStart(line, col)) return col;
    int start = cellStartBefore(line, col);
    unsigned char c = static_cast<unsigned char>(line[start]);
    int expect = 0;
    if ((c & 0xE0) == 0xC0) expect = 1;
    else if ((c & 0xF0) == 0xE0) expect = 2;
    else if ((c & 0xF8) == 0xF0) expect = 3;
    int end = start + 1;
    while (expect > 0 && end < static_cast<int>(line.size()) && (static_cast<unsigned char>(line[end]) & 0xC0) == 0x80) {
        --expect;
        ++end;
    }
    return end;
}

inline bool isValid(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int need;
        if ((c & 0x80) == 0) need = 0;
        else if ((c & 0xE0) == 0xC0) need = 1;
        else if ((c & 0xF0) == 0xE0) need = 2;
        else if ((c & 0xF8) == 0xF0) need = 3;
        else return false;
        if (need == 1 && c < 0xC2) return false; // overlong 2 bytes (C0/C1)
        if (need == 3 && c >= 0xF5) return false; // >U+10FFFF (F5-FF)
        if (i + static_cast<size_t>(need) >= s.size()) return false;
        for (int k = 1; k <= need; ++k) if ((static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0xC0) != 0x80) return false;
        unsigned char c1 = need >= 1 ? static_cast<unsigned char>(s[i + 1]) : 0;
        if (need == 2) {
            if (c == 0xE0 && c1 < 0xA0) return false; // overlong 3 bytes (U+0000-U+07FF)
            if (c == 0xED && c1 > 0x9F) return false; // surrogates U+D800-U+DFFF
        } else if (need == 3) {
            if (c == 0xF0 && c1 < 0x90) return false; // overlong 4 bytes
            if (c == 0xF4 && c1 > 0x8F) return false; // >U+10FFFF
        }
        uint32_t cp = 0;
        if (need == 1) cp = ((c & 0x1F) << 6) | (c1 & 0x3F);
        else if (need == 2) cp = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        else if (need == 3) cp = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        if (need > 0) {
            if (cp < 0x80 && need != 0) return false;
            if (cp < 0x800 && need > 1) return false;
            if (cp < 0x10000 && need > 2) return false;
            if (cp >= 0xD800 && cp <= 0xDFFF) return false;
            if (cp > 0x10FFFF) return false;
        }
        i += static_cast<size_t>(need) + 1;
    }
    return true;
}

} // namespace utf8
