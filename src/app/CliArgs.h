#pragma once

#include <string>

#include "platform/gui/GuiOptions.h"

// Parser de argumentos CLI (único lugar, testeado en
// tests/unit/test_gui_options.cpp con el mismo parseArgs que usa main).
//
// REGLA --help (P1): si aparece -h/--help en CUALQUIER posición, help=true
// y se termina el parseo sin procesar el resto: main imprime ayuda y sale 0
// antes de inicializar SDL (main.cpp chequea help antes que bad y antes que
// la rama GUI). La ayuda prevalece sobre los errores de uso presentes en la
// línea (p. ej. `--font --help` o `--theme blue --help` muestran ayuda).
//
// REGLA DE GUIONES (P1): todo argumento que empieza con '-' es una opción,
// también como VALOR de otra opción. Un valor ausente o que parece otra
// opción marca bad con diagnóstico (badReason). La sintaxis inequívoca para
// valores con guion inicial es `--font=-mi-fuente.ttf` (aceptada para todas
// las opciones con valor).
//
// PRECEDENCIA DE CONFIGURACIÓN: argumento CLI > variable de entorno >
// valor predeterminado. En la práctica solo aplica a la fuente (la única
// con env): el orden real de resolución vive en FontOrder.h y la política
// de errores por origen se aplica en GuiFont (load/loadWithPath).

struct Args {
    bool gui = false;
    bool help = false;
    std::string file;
    bool bad = false;
    // Diagnóstico del primer error de uso (vacío si !bad). main lo imprime
    // antes del uso para que cada rechazo explique qué falló.
    std::string badReason;
    GuiOptions guiOptions;
    bool guiOptionsUsed = false;
};

namespace {
// Consume el valor de la opción en argv[i+1]. Devuelve nullptr si falta o
// si parece otra opción (empieza con '-'): sin esta guardia, `--font
// --theme` asignaría "--theme" como ruta de fuente en silencio.
inline const char* takeOptionValue(int argc, char* argv[], int i,
                                   const char* optName, Args& a) {
    if (i + 1 >= argc) {
        a.bad = true;
        a.badReason = std::string(optName) + " requiere un valor";
        return nullptr;
    }
    const char* v = argv[i + 1];
    if (v[0] == '-') {
        a.bad = true;
        a.badReason = std::string(optName) +
                      " requiere un valor (que no empiece por '-'; use " +
                      std::string(optName) + "=VALOR para rutas con guion)";
        return nullptr;
    }
    return v;
}
}  // namespace

inline Args parseArgs(int argc, char* argv[]) {
    Args a;
    // Pre-escaneo de ayuda (P1): gana sobre cualquier otro argumento,
    // válido o no, sin inicializar nada.
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "-h" || s == "--help") {
            a.help = true;
            return a;
        }
    }
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        // Forma --opt=VALOR: el valor puede empezar con '-' (es la vía
        // documentada para rutas como -mi-fuente.ttf).
        auto eqForm = [&](const char* opt) {
            const size_t n = std::string(opt).size();
            if (s.size() <= n || s.compare(0, n, opt) != 0 || s[n] != '=')
                return false;
            const std::string val = s.substr(n + 1);
            if (opt == std::string("--font")) {
                if (val.empty()) {
                    a.bad = true;
                    a.badReason = "--font= requiere una ruta";
                } else {
                    a.guiOptions.fontPath = val;
                    a.guiOptionsUsed = true;
                }
                return true;
            }
            if (opt == std::string("--font-size")) {
                int sz;
                if (!parseIntStrict(val, sz, 1, 200)) {
                    a.bad = true;
                    a.badReason =
                        "--font-size requiere un entero entre 1 y 200 (recibido '" +
                        val + "')";
                } else {
                    a.guiOptions.fontSize = sz;
                    a.guiOptionsUsed = true;
                }
                return true;
            }
            if (opt == std::string("--theme")) {
                if (!parseTheme(val, a.guiOptions.theme)) {
                    a.bad = true;
                    a.badReason =
                        "--theme requiere 'dark' o 'light' (recibido '" + val + "')";
                } else {
                    a.guiOptionsUsed = true;
                }
                return true;
            }
            if (opt == std::string("--geometry")) {
                int w, h;
                if (!parseGeometry(val, w, h)) {
                    a.bad = true;
                    a.badReason =
                        "--geometry requiere WxH con dimensiones entre 1 y 10000 y "
                        "área total <= 12000000 (ej. 1200x800; recibido '" + val + "')";
                } else {
                    a.guiOptions.windowWidth = w;
                    a.guiOptions.windowHeight = h;
                    a.guiOptionsUsed = true;
                }
                return true;
            }
            return false;
        };
        if (eqForm("--font") || eqForm("--font-size") || eqForm("--theme") ||
            eqForm("--geometry")) {
            if (a.bad) break;
            continue;
        }
        if (s == "--gui") {
            a.gui = true;
        } else if (s == "-h" || s == "--help") {
            a.help = true;  // inalcanzable (pre-escaneo); defensa futura
        } else if (s == "--font") {
            if (const char* v = takeOptionValue(argc, argv, i, "--font", a)) {
                a.guiOptions.fontPath = v;
                a.guiOptionsUsed = true;
                ++i;
            } else break;
        } else if (s == "--font-size") {
            if (const char* v =
                    takeOptionValue(argc, argv, i, "--font-size", a)) {
                int sz;
                if (!parseIntStrict(v, sz, 1, 200)) {
                    a.bad = true;
                    a.badReason =
                        "--font-size requiere un entero entre 1 y 200 (recibido '" +
                        std::string(v) + "')";
                } else {
                    a.guiOptions.fontSize = sz;
                    a.guiOptionsUsed = true;
                }
                ++i;
            } else break;
        } else if (s == "--theme") {
            if (const char* v = takeOptionValue(argc, argv, i, "--theme", a)) {
                if (!parseTheme(v, a.guiOptions.theme)) {
                    a.bad = true;
                    a.badReason =
                        "--theme requiere 'dark' o 'light' (recibido '" +
                        std::string(v) + "')";
                } else {
                    a.guiOptionsUsed = true;
                }
                ++i;
            } else break;
        } else if (s == "--no-hidpi") {
            a.guiOptions.enableHiDpi = false;
            a.guiOptionsUsed = true;
        } else if (s == "--geometry") {
            if (const char* v =
                    takeOptionValue(argc, argv, i, "--geometry", a)) {
                int w, h;
                if (!parseGeometry(v, w, h)) {
                    a.bad = true;
                    a.badReason =
                        "--geometry requiere WxH con dimensiones entre 1 y 10000 y "
                        "área total <= 12000000 (ej. 1200x800; recibido '" +
                        std::string(v) + "')";
                } else {
                    a.guiOptions.windowWidth = w;
                    a.guiOptions.windowHeight = h;
                    a.guiOptionsUsed = true;
                }
                ++i;
            } else break;
        } else if (!s.empty() && s[0] == '-') {
            a.bad = true;
            a.badReason = "opción desconocida '" + s + "'";
        } else if (a.file.empty()) {
            a.file = s;
        } else {
            a.bad = true;
            a.badReason = "argumento inesperado '" + s + "'";
        }
        if (a.bad) break;
    }
    if (!a.bad && a.guiOptionsUsed && !a.gui) {
        a.bad = true;
        a.badReason = "las opciones GUI (--font, --font-size, --theme, "
                      "--no-hidpi, --geometry) requieren --gui";
    }
    return a;
}
