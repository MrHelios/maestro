#pragma once
#include <string>
#include <vector>

namespace testutil {

inline std::string stripAnsi(const std::string& s) {
    std::string out;
    bool inEsc = false;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\x1b') {
            inEsc = true;
            if (i + 1 < s.size() && s[i + 1] == '[') i++;
        } else if (inEsc) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c >= 0x40 && c <= 0x7E) inEsc = false;
        } else {
            out += s[i];
        }
        i++;
    }
    return out;
}

inline int colWidth(const std::string& s) {
    int col = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) col++;
    return col;
}

inline bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

inline bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

inline std::vector<std::string> visibleRows(const std::string& frame) {
    std::string plain = stripAnsi(frame);
    std::vector<std::string> out;
    std::string cur;
    for (char c : plain) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    if (!cur.empty() || plain.empty() || plain.back() != '\n')
        out.push_back(cur);
    return out;
}

inline int gutterWidth(int totalLines) {
    int digits = 1;
    for (int n = totalLines; n >= 10; n /= 10) ++digits;
    return std::max(3, digits + 1);
}

// Valida estructura basica UTF-8 para tests: detecta truncamientos y
// bytes de continuacion invalidos. No pretende validar todos los casos
// Unicode (overlong/surrogates/>U+10FFFF) - suficiente para comprobar
// que truncate() no partio un caracter multibyte.
inline bool validUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int need;
        if ((c & 0x80) == 0) need = 0;
        else if ((c & 0xE0) == 0xC0) need = 1;
        else if ((c & 0xF0) == 0xE0) need = 2;
        else if ((c & 0xF8) == 0xF0) need = 3;
        else return false;
        if (i + static_cast<size_t>(need) >= s.size()) return false;
        for (int k = 1; k <= need; ++k)
            if ((static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0xC0) != 0x80)
                return false;
        i += static_cast<size_t>(need) + 1;
    }
    return true;
}

}
