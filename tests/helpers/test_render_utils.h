#pragma once
#include <string>
#include <vector>

// Constantes legacy del Theme dark (antes en core/Theme.h).
// Fuente unica para los tests que verifican colores ANSI: test_renderer,
// test_theme y los tests de interaction via test_support.h (que incluye
// este helper en vez de redefinirlas).
constexpr const char* kCurrentLineStyle = "\x1b[48;5;237m";
constexpr const char* kListSelectedStyle = "\x1b[48;5;237m";
constexpr const char* kSelectionStyle = "\x1b[48;5;60m";
constexpr const char* kLineNumberStyle = "\x1b[38;5;242m";
constexpr const char* kGutterCurrentStyle = "\x1b[1m\x1b[38;5;81;48;5;237m";
constexpr const char* kMarkerStyle = "\x1b[38;5;65m";
constexpr const char* kEditorBackground = "\x1b[48;2;18;19;20m";
constexpr const char* kDarkBackground = "\x1b[48;2;18;19;20m";
constexpr const char* kDarkReset = "\x1b[0m\x1b[48;2;18;19;20m";
constexpr const char* kStatusBarBackground = "\x1b[48;2;25;26;27m";
constexpr const char* kUiGrayText = "\x1b[38;2;140;140;140m";
constexpr const char* kStatusBarStyle = "\x1b[38;2;140;140;140m\x1b[48;2;25;26;27m";
constexpr const char* kStatusBarName = "\x1b[38;2;140;140;140m";
constexpr const char* kStatusBarPath = "\x1b[38;2;140;140;140m";
constexpr const char* kStatusBarCommand = "\x1b[1m\x1b[38;5;178m";
constexpr const char* kStatusBarModified = "\x1b[1;38;5;221m";
constexpr const char* kPromptStyle = "\x1b[1m";
constexpr const char* kMessageSuccess = "\x1b[38;5;250m";
constexpr const char* kMessageWarning = "\x1b[38;5;250m";
constexpr const char* kMessageError = "\x1b[38;5;250m";
constexpr const char* kMessageReset = "\x1b[0m";
constexpr const char* kAccentNavegacion = "\x1b[1m\x1b[38;5;81m";
constexpr const char* kAccentInteraccion = "\x1b[1m\x1b[38;5;81m";
constexpr const char* kAccentSeleccion = "\x1b[1m\x1b[38;5;81m";
constexpr const char* kAccentComando = "\x1b[1m\x1b[38;5;81m";
constexpr const char* kAccentBuffers = "\x1b[1m\x1b[38;5;81m";
constexpr const char* kAccentGuardar = "\x1b[1m\x1b[38;5;81m";
constexpr const char* kAccentAbrir = "\x1b[1m\x1b[38;5;81m";

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
