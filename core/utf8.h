#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <algorithm>
#include "core/Instrument.h"

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
// usa cellWidth() -> codepointWidth().
//
// Actualmente Maestro opera por codepoint/celda UTF-8.
// Las marcas combinantes se contabilizan como ancho 1.
// Las secuencias de grafemas (combining marks, ZWJ, variation selectors,
// regional-indicator flags, etc.) todavía no se modelan como una unidad
// visual.
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

// Maestro terminal-width policy (Unicode 17):
// - ASCII/control: 1
// - EAW W/F and selected emoji: 2
// - EAW A/N/H: 1
// - Combining marks: currently 1; grapheme-width handling is phase 2.
// - Regional Indicator pairs / ZWJ / VS16 remain outside the codepoint-width model and will be handled with grapheme segmentation.
struct WidthRange { uint32_t first; uint32_t last; };
inline int codepointWidth(uint32_t cp) {
    if (cp < 0x1100) return 1;
    static constexpr WidthRange wide[] = {
        {0x1100, 0x115F}, {0x2329, 0x232A}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0}, {0x23F3, 0x23F3},
        {0x25FD, 0x25FE}, {0x2614, 0x2615}, {0x2648, 0x2653}, {0x267F, 0x267F}, {0x2693, 0x2693}, {0x26A1, 0x26A1},
        {0x26AA, 0x26AB}, {0x26BD, 0x26BE}, {0x26C4, 0x26C5}, {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA},
        {0x26F2, 0x26F3}, {0x26F5, 0x26F5}, {0x26FA, 0x26FA}, {0x26FD, 0x26FD}, {0x2705, 0x2705}, {0x270A, 0x270B},
        {0x2728, 0x2728}, {0x2746, 0x2746}, {0x274C, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797},
        {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55}, {0x2E80, 0x303E},
        {0x3040, 0x3247}, {0x3250, 0x4DBF}, {0x4E00, 0xA4CF}, {0xA960, 0xA97C}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF},
        {0xFE10, 0xFE19}, {0xFE30, 0xFE6F}, {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6}, {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF},
        {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F1E6, 0x1F1FF}, {0x1F200, 0x1F202}, {0x1F210, 0x1F23B}, {0x1F240, 0x1F248}, {0x1F250, 0x1F251}, {0x1F260, 0x1F265}, {0x1F300, 0x1F320}, {0x1F32D, 0x1F335},
        {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393}, {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4},
        {0x1F3F8, 0x1F43E}, {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567},
        {0x1F57A, 0x1F57A}, {0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC},
        {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D5}, {0x1F6EB, 0x1F6EC}, {0x1F6F4, 0x1F6F8}, {0x1F7E0, 0x1F7E0}, {0x1F7F0, 0x1F7F0}, {0x1F90C, 0x1F90C}, {0x1F910, 0x1F93A}, {0x1F93C, 0x1F93E}, {0x1F940, 0x1F945}, {0x1F947, 0x1F96B},
        {0x1F980, 0x1F99E}, {0x1F99F, 0x1F99F}, {0x1F9C0, 0x1F9C0}, {0x1F9D0, 0x1F9E6}, {0x1FA70, 0x1FA7C}, {0x1FA80, 0x1FA8A}, {0x1FA8E, 0x1FAC6},
        {0x1FAEF, 0x1FAF8}, {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD},
    };
    for (auto r : wide) if (cp >= r.first && cp <= r.last) return 2;
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
    bool doInstr = instrument::enabled;
    uint64_t t0 = doInstr ? instrument::nowNanos() : 0;
    int n = static_cast<int>(line.size());
    int limit = byteCol < n ? byteCol : n; // input_bytes/scan_limit, aprox. O(n)
    if (limit <= 0) {
        if (doInstr) {
            uint64_t ns = instrument::nowNanos() - t0;
            instrument::recordColumnOf(0, ns);
        }
        return 0;
    }
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
    // Registra limit como input_bytes/scan_limit, no bytes iterados literales
    // (cellLen 1-4 y fast-path 8B hacen que iterados < limit en ASCII)
    if (doInstr) {
        uint64_t ns = instrument::nowNanos() - t0;
        instrument::recordColumnOf(static_cast<uint64_t>(limit), ns);
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
    bool doInstr = instrument::enabled;
    uint64_t t0 = doInstr ? instrument::nowNanos() : 0;
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
    // i = bytes escaneados hasta maxCols (no input_bytes total)
    if (doInstr) {
        uint64_t ns = instrument::nowNanos() - t0;
        instrument::recordTruncate(static_cast<uint64_t>(i), ns);
    }
    return std::string(line.substr(0, i));
}

// Devuelve los bytes de `line` cuyas COLUMNAS VISUALES caen dentro de
// [fromCol, toCol). No corta celdas por la mitad. Si el rango llega al
// final de la linea devuelve hasta el ultimo byte.
inline std::string expandTabs(std::string_view line) {
    bool doInstr = instrument::enabled;
    uint64_t t0 = doInstr ? instrument::nowNanos() : 0;
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
    if (doInstr) {
        uint64_t ns = instrument::nowNanos() - t0;
        instrument::recordExpandTabs(static_cast<uint64_t>(line.size()), ns);
    }
    return out;
}

inline std::string_view range(std::string_view line, int fromCol, int toCol) {
    bool doInstr = instrument::enabled;
    uint64_t t0 = doInstr ? instrument::nowNanos() : 0;
    if (toCol <= fromCol) {
        if (doInstr) {
            uint64_t ns = instrument::nowNanos() - t0;
            instrument::recordRange(0, ns);
        }
        return "";
    }
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
    // i = bytes escaneados hasta toCol (mas preciso que limit)
    if (doInstr) {
        uint64_t ns = instrument::nowNanos() - t0;
        instrument::recordRange(static_cast<uint64_t>(i), ns);
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
