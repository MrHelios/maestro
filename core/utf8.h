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
// de `line`. TAB se expande a la siguiente tabstop (TAB_WIDTH), el resto
// usa cellWidth() -> codepointWidth(): 1 para latin, 2 para CJK/emoji
// ancho (EastAsianWidth W/F) via tabla Unicode determinista.
//
// LIMITACION DOCUMENTADA (v0.x): `width 0` (combinantes) aun no se
// soporta: p.ej. "e" + U+0301 se cuenta como 2 cols en vez de 1.
// Requiere segmentacion por grafemas (UAX #29) y afecta cursor/seleccion,
// por eso se deja como segunda fase despues de 1/2.
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

inline int codepointWidth(uint32_t cp) {
    if (cp < 0x1100) return 1;
    if (cp <= 0x115F) return 2;
    if (cp == 0x2329 || cp == 0x232A) return 2;
    if (cp >= 0x23E9 && cp <= 0x23EC) return 2;
    if (cp == 0x23F0 || cp == 0x23F3) return 2;
    if (cp >= 0x25FD && cp <= 0x25FE) return 2;
    if (cp >= 0x2614 && cp <= 0x2615) return 2;
    if (cp >= 0x2648 && cp <= 0x2653) return 2;
    if (cp == 0x267F || cp == 0x2693 || cp == 0x26A1) return 2;
    if (cp >= 0x26AA && cp <= 0x26AB) return 2;
    if (cp >= 0x26BD && cp <= 0x26BE) return 2;
    if (cp >= 0x26C4 && cp <= 0x26C5) return 2;
    if (cp == 0x26CE || cp == 0x26D4 || cp == 0x26EA) return 2;
    if (cp >= 0x26F2 && cp <= 0x26F3) return 2;
    if (cp == 0x26F5 || cp == 0x26FA || cp == 0x26FD) return 2;
    if (cp == 0x2705 || (cp >= 0x270A && cp <= 0x270B) || cp == 0x2728 || cp == 0x2746) return 2;
    if (cp >= 0x274C && cp <= 0x274E) return 2;
    if (cp >= 0x2753 && cp <= 0x2755) return 2;
    if (cp == 0x2757 || (cp >= 0x2795 && cp <= 0x2797) || cp == 0x27B0 || cp == 0x27BF) return 2;
    if (cp >= 0x2B1B && cp <= 0x2B1C) return 2;
    if (cp == 0x2B50 || cp == 0x2B55) return 2;
    if (cp >= 0x2E80 && cp <= 0x303E) return 2;
    if (cp >= 0x3040 && cp <= 0x3247) return 2;
    if (cp >= 0x3250 && cp <= 0x4DBF) return 2;
    if (cp >= 0x4E00 && cp <= 0xA4CF) return 2;
    if (cp >= 0xA960 && cp <= 0xA97C) return 2;
    if (cp >= 0xAC00 && cp <= 0xD7A3) return 2;
    if (cp >= 0xF900 && cp <= 0xFAFF) return 2;
    if (cp >= 0xFE10 && cp <= 0xFE19) return 2;
    if (cp >= 0xFE30 && cp <= 0xFE6F) return 2;
    if (cp >= 0xFF00 && cp <= 0xFF60) return 2;
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return 2;
    if (cp == 0x1F004 || cp == 0x1F0CF) return 2;
    if (cp >= 0x1F18E && cp <= 0x1F18E) return 2;
    if (cp >= 0x1F191 && cp <= 0x1F19A) return 2;
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return 2;
    if (cp >= 0x1F201 && cp <= 0x1F27A) return 2;
    if (cp >= 0x1F30D && cp <= 0x1F335) return 2;
    if (cp >= 0x1F337 && cp <= 0x1F37C) return 2;
    if (cp >= 0x1F37E && cp <= 0x1F393) return 2;
    if (cp >= 0x1F3A0 && cp <= 0x1F3CA) return 2;
    if (cp >= 0x1F3CF && cp <= 0x1F3D3) return 2;
    if (cp >= 0x1F3E0 && cp <= 0x1F3F0) return 2;
    if (cp == 0x1F3F4 || (cp >= 0x1F3F8 && cp <= 0x1F43E) || cp == 0x1F440) return 2;
    if (cp >= 0x1F442 && cp <= 0x1F4FC) return 2;
    if (cp >= 0x1F4FF && cp <= 0x1F53D) return 2;
    if (cp >= 0x1F54B && cp <= 0x1F54E) return 2;
    if (cp >= 0x1F550 && cp <= 0x1F567) return 2;
    if (cp == 0x1F57A || (cp >= 0x1F595 && cp <= 0x1F596) || cp == 0x1F5A4) return 2;
    if (cp >= 0x1F5FB && cp <= 0x1F64F) return 2;
    if (cp >= 0x1F680 && cp <= 0x1F6C5) return 2;
    if (cp == 0x1F6CC || (cp >= 0x1F6D0 && cp <= 0x1F6D2) || (cp >= 0x1F6EB && cp <= 0x1F6EC)) return 2;
    if (cp >= 0x1F6F4 && cp <= 0x1F6F8) return 2;
    if (cp >= 0x1F910 && cp <= 0x1F93E) return 2;
    if (cp >= 0x1F940 && cp <= 0x1F96B) return 2;
    if (cp >= 0x1F980 && cp <= 0x1F99E) return 2;
    if (cp == 0x1F9C0 || (cp >= 0x1F9D0 && cp <= 0x1F9E6)) return 2;
    if (cp >= 0x20000 && cp <= 0x2FFFD) return 2;
    if (cp >= 0x30000 && cp <= 0x3FFFD) return 2;
    return 1;
}

inline int cellWidth(std::string_view line, int pos, int n) {
    unsigned char c = static_cast<unsigned char>(line[pos]);
    if (c < 0x80) return 1;
    if ((c & 0xC0) == 0x80) return 1;
    int expect = 0;
    if ((c & 0xE0) == 0xC0) expect = 1;
    else if ((c & 0xF0) == 0xE0) expect = 2;
    else if ((c & 0xF8) == 0xF0) expect = 3;
    else return 1;
    if (pos + expect >= n) return 1;
    for (int k = 1; k <= expect; ++k) if ((static_cast<unsigned char>(line[pos + k]) & 0xC0) != 0x80) return 1;
    uint32_t cp = 0;
    if (expect == 1) {
        if (c < 0xC2) return 1;
        cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(line[pos + 1]) & 0x3F);
    } else if (expect == 2) {
        unsigned char c1 = static_cast<unsigned char>(line[pos + 1]);
        if (c == 0xE0 && c1 < 0xA0) return 1;
        if (c == 0xED && c1 > 0x9F) return 1;
        cp = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (static_cast<unsigned char>(line[pos + 2]) & 0x3F);
    } else {
        unsigned char c1 = static_cast<unsigned char>(line[pos + 1]);
        if (c == 0xF0 && c1 < 0x90) return 1;
        if (c == 0xF4 && c1 > 0x8F) return 1;
        if (c >= 0xF5) return 1;
        cp = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((static_cast<unsigned char>(line[pos + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(line[pos + 3]) & 0x3F);
        if (cp > 0x10FFFF) return 1;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) return 1;
    return codepointWidth(cp);
}

inline constexpr int TAB_WIDTH = 4;
inline constexpr int kTabWidth = TAB_WIDTH;

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
            if ((v & 0x8080808080808080ULL) == 0) {
                constexpr uint64_t kTab = 0x0909090909090909ULL;
                uint64_t x = v ^ kTab;
                bool hasTab = ((x - 0x0101010101010101ULL) & ~x & 0x8080808080808080ULL) != 0;
                if (!hasTab) { col += 8; i += 8; continue; }
            }
        }
        if (line[i] == '\t') {
            col = ((col / TAB_WIDTH) + 1) * TAB_WIDTH;
        } else {
            col += cellWidth(line, i, n);
        }
        i += cellLen(line, i, n);
    }
    return col;
}

inline int byteForColumn(std::string_view line, int targetCol) {
    int n = static_cast<int>(line.size());
    if (targetCol <= 0) return 0;
    int col = 0;
    int i = 0;
    while (i < n) {
        int w = cellWidth(line, i, n);
        if (line[i] == '\t') w = ((col / TAB_WIDTH) + 1) * TAB_WIDTH - col;
        if (col + w > targetCol) return i;
        col += w;
        int next = i + cellLen(line, i, n);
        if (col >= targetCol) return next;
        i = next;
    }
    return n;
}

// Trunca `line` a lo sumo `maxCols` COLUMNAS VISUALES, sin cortar una
// celda por la mitad (lo que generaria bytes invalidos y corromperia el
// resto del render).
inline std::string truncate(std::string_view line, int maxCols) {
    int col = 0;
    size_t i = 0;
    int n = static_cast<int>(line.size());
    while (i < line.size()) {
        if (!isCellStart(line, static_cast<int>(i))) { ++i; continue; }
        int w = cellWidth(line, static_cast<int>(i), n);
        if (line[i] == '\t') w = ((col / TAB_WIDTH) + 1) * TAB_WIDTH - col;
        if (col + w > maxCols) break;
        col += w;
        i += cellLen(line, static_cast<int>(i), n);
    }
    return std::string(line.substr(0, i));
}

// Devuelve los bytes de `line` cuyas COLUMNAS VISUALES caen dentro de
// [fromCol, toCol). No corta celdas por la mitad. Si el rango llega al
// final de la linea devuelve hasta el ultimo byte.
inline std::string expandTabs(std::string_view line) {
    std::string out;
    out.reserve(line.size() + 8);
    int col = 0;
    int n = static_cast<int>(line.size());
    int i = 0;
    while (i < n) {
        if (line[i] == '\t') {
            int w = ((col / TAB_WIDTH) + 1) * TAB_WIDTH - col;
            out.append(w, ' ');
            col += w;
            i += 1;
        } else {
            int len = cellLen(line, i, n);
            out.append(line.data() + i, len);
            col += cellWidth(line, i, n);
            i += len;
        }
    }
    return out;
}

inline std::string_view range(std::string_view line, int fromCol, int toCol) {
    if (toCol <= fromCol) return "";
    int n = static_cast<int>(line.size());
    int col = 0;
    size_t startByte = line.size();
    size_t endByte = line.size();
    int i = 0;
    while (i < n) {
        if (col >= fromCol && startByte == line.size()) startByte = i;
        if (col >= toCol) { endByte = i; break; }
        int w = cellWidth(line, i, n);
        if (line[i] == '\t') w = ((col / TAB_WIDTH) + 1) * TAB_WIDTH - col;
        col += w;
        i += cellLen(line, i, n);
        if (col >= toCol && endByte == line.size()) { endByte = i; break; }
    }
    if (startByte == line.size()) return "";
    if (endByte == line.size() && col < toCol) endByte = line.size();
    if (col < fromCol) return "";
    if (startByte > endByte) return "";
    return line.substr(startByte, endByte - startByte);
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
