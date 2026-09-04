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

}
