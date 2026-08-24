#pragma once

#include <string>
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
inline bool isCellStart(const std::string& line, int i) {
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
inline int columnOf(const std::string& line, int byteCol) {
    int col = 0;
    int limit = std::min<int>(byteCol, static_cast<int>(line.size()));
    for (int i = 0; i < limit; ++i) {
        if (isCellStart(line, i)) {
            col++;
        }
    }
    return col;
}

// Trunca `line` a lo sumo `maxCols` COLUMNAS VISUALES, sin cortar una
// celda por la mitad (lo que generaria bytes invalidos y corromperia el
// resto del render).
inline std::string truncate(const std::string& line, int maxCols) {
    int col = 0;
    size_t i = 0;
    while (i < line.size()) {
        if (isCellStart(line, static_cast<int>(i))) {
            if (col >= maxCols) break;
            col++;
        }
        i++;
    }
    return line.substr(0, i);
}

// Devuelve los bytes de `line` cuyas COLUMNAS VISUALES caen dentro de
// [fromCol, toCol). No corta celdas por la mitad. Si el rango llega al
// final de la linea devuelve hasta el ultimo byte.
inline std::string range(const std::string& line, int fromCol, int toCol) {
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

} // namespace utf8