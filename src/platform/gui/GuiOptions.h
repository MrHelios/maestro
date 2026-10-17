#pragma once

#include <cctype>
#include <optional>
#include <string>

enum class GuiTheme { Dark, Light };

struct GuiOptions {
    std::optional<std::string> fontPath;
    // CONTRATO DE UNIDADES: píxeles lógicos (== píxeles del drawable a escala
    // 1; no puntos tipográficos de 1/72in). El loop la convierte a píxeles
    // del drawable con guiScaledFontPixels antes de TTF_OpenFont, así el
    // mismo valor da la misma altura visual en cualquier monitor.
    std::optional<int> fontSize;
    GuiTheme theme = GuiTheme::Dark;
    bool enableHiDpi = true;
    // CONTRATO DE UNIDADES: coordenadas de ventana (puntos, no píxeles
    // físicos). Se pasan tal cual a SDL_CreateWindow; con HiDPI el drawable
    // puede ser N× mayor.
    std::optional<int> windowWidth;
    std::optional<int> windowHeight;
};

// Límite de área total en puntos (w * h) para evitar buffers excesivos con HiDPI.
// 4000x3000 = 12M puntos -> a 2x = 48M píxeles, razonable para buffers persistentes.
inline constexpr int kMaxWindowArea = 12000000;
inline constexpr int kMaxDimension = 10000;

// GRAMÁTICA (congelada y testeada en test_gui_options.cpp): solo dígitos
// decimales, sin signo ('+' y '-' no son parte de la gramática; stol los
// aceptaría y abriría la puerta a "+16" o "-0" que para un argumento CLI
// numérico es ambiguo), sin espacios ni sufijos ("16abc", "1e2" → no). El
// rango [minVal, maxVal] sigue aplicando por encima de la gramática.
inline bool parseIntStrict(const std::string& s, int& out, int minVal, int maxVal) {
    if (s.empty()) return false;
    if (!std::isdigit(static_cast<unsigned char>(s.front()))) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    try {
        size_t pos = 0;
        long val = std::stol(s, &pos);
        if (pos != s.size()) return false;
        if (val < minVal || val > maxVal) return false;
        out = static_cast<int>(val);
        return true;
    } catch (...) {
        // Única forma de llegar acá: dígitos puros fuera de rango de long
        // (desbordamiento), p. ej. "999999999999". stol lanza y se rechaza.
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

// Diagnóstico de inicialización de fuente (lo consume GuiRunLoop cuando
// initForRenderer falla). Distingue la ruta explícita (--font: error sin
// fallback, la salida es != 0) de la ausencia de fuente utilizable cuando
// nadie fijó una (la resolución de orden está en FontOrder.h).
inline std::string fontOpenDiagnostic(const GuiOptions& opts) {
    if (opts.fontPath.has_value()) {
        return "maestro: error al abrir la fuente '" + *opts.fontPath +
               "' especificada con --font (sin fallback: una fuente "
               "explícita que no abre es error terminal)\n";
    }
    return "maestro: no se encontró ninguna fuente TTF utilizable "
           "(MAESTRO_FONT, candidatos del sistema o ~/.fonts; instale por "
           "ejemplo fonts-dejavu-core)\n";
}