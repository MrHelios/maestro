#pragma once

#include <cctype>
#include <optional>
#include <string>

enum class GuiTheme { Dark, Light };

struct GuiOptions {
    std::optional<std::string> fontPath;
    std::optional<int> fontSize;
    GuiTheme theme = GuiTheme::Dark;
    bool enableHiDpi = true;
    std::optional<int> windowWidth;
    std::optional<int> windowHeight;
};

// Límite de área total en puntos (w * h) para evitar buffers excesivos con HiDPI.
// 4000x3000 = 12M puntos -> a 2x = 48M píxeles, razonable para buffers persistentes.
inline constexpr int kMaxWindowArea = 12000000;
inline constexpr int kMaxDimension = 10000;

inline bool parseIntStrict(const std::string& s, int& out, int minVal, int maxVal) {
    if (s.empty()) return false;
    // Rechazar espacios en blanco iniciales o finales (stol los acepta).
    if (std::isspace(static_cast<unsigned char>(s.front()))) return false;
    if (std::isspace(static_cast<unsigned char>(s.back()))) return false;
    try {
        size_t pos = 0;
        long val = std::stol(s, &pos);
        if (pos != s.size()) return false;
        if (val < minVal || val > maxVal) return false;
        out = static_cast<int>(val);
        return true;
    } catch (...) {
        return false;
    }
}

inline bool parseGeometry(const std::string& s, int& outW, int& outH) {
    auto pos = s.find('x');
    if (pos == std::string::npos || pos == 0 || pos + 1 >= s.size()) return false;
    std::string ws = s.substr(0, pos);
    std::string hs = s.substr(pos + 1);
    if (ws.find('x') != std::string::npos || hs.find('x') != std::string::npos) return false;
    int w, h;
    if (!parseIntStrict(ws, w, 1, kMaxDimension)) return false;
    if (!parseIntStrict(hs, h, 1, kMaxDimension)) return false;
    // Límite de área total para evitar buffers excesivos (considera HiDPI 2x-3x).
    if (static_cast<long long>(w) * static_cast<long long>(h) > kMaxWindowArea) return false;
    outW = w;
    outH = h;
    return true;
}

inline bool operator==(const GuiOptions& a, const GuiOptions& b) {
    return a.fontPath == b.fontPath &&
           a.fontSize == b.fontSize &&
           a.theme == b.theme &&
           a.enableHiDpi == b.enableHiDpi &&
           a.windowWidth == b.windowWidth &&
           a.windowHeight == b.windowHeight;
}

inline bool parseTheme(const std::string& s, GuiTheme& out) {
    if (s == "dark") { out = GuiTheme::Dark; return true; }
    if (s == "light") { out = GuiTheme::Light; return true; }
    return false;
}