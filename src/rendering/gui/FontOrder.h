#pragma once

// FontOrder — política de selección de fuente en UN solo lugar (probada en
// tests/unit/test_fontorder.cpp para el orden y en
// tests/rendering/test_guifont.cpp para la política al abrir).
//
// PRECEDENCIA (contrato): argumento CLI (--font) > variable de entorno
// (MAESTRO_FONT) > candidatos del sistema > ~/.fonts.
//
// POLÍTICA DE ERRORES POR ORIGEN (no comparten la misma; ver tabla README):
//   - Cli inválida: error y salida != 0, SIN fallback (el diagnóstico lo
//     arma fontOpenDiagnostic en GuiOptions.h y lo imprime el loop).
//   - Env inválida: aviso por stderr UNA sola vez por proceso y se continúa
//     con el siguiente candidato (una recarga/resize no vuelve a avisar).
//   - System/Home: se prueba el siguiente; error terminal solo si ninguna
//     funciona.
//
// Cada candidato etiqueta su origen para que el cargador (GuiFont) trate
// una fuente explícita distinto de un candidato más: jamás cae al env una
// --font que falló al abrir.

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

enum class FontOrigin { Cli, Env, System, Home };

struct FontCandidate {
    FontOrigin origin;
    std::string path;
};

inline const char* fontOriginName(FontOrigin o) {
    switch (o) {
        case FontOrigin::Cli: return "cli(--font)";
        case FontOrigin::Env: return "MAESTRO_FONT";
        case FontOrigin::System: return "sistema";
        case FontOrigin::Home: return "~/.fonts";
    }
    return "?";
}

// Candidatos del sistema (orden de preferencia). Devuelto por
// resolveFontOrder después de Cli/Env.
inline const char* const kSystemFontCandidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
};

// La env vacía cuenta como ausente (comportamiento predeterminado).
inline bool envFontSet() {
    const char* e = std::getenv("MAESTRO_FONT");
    return e && *e;
}

// Orden de resolución completo. Lee el entorno en el momento de la llamada
// (MAESTRO_FONT y HOME): el cargador lo llama en cada load, así un cambio
// de env entre cargas se respeta como cambio del parámetro externo.
inline std::vector<FontCandidate> resolveFontOrder(
    const std::optional<std::string>& cliPath) {
    std::vector<FontCandidate> out;
    // Fuente explícita: EXACTAMENTE un candidato Cli y NADA más. Un --font
    // que no abre es error terminal: no existen fallbacks de otro origen
    // aunque MAESTRO_FONT o los candidatos del sistema sean válidos (así
    // el cargador jamás trata una explícita como "un candidato más").
    if (cliPath && !cliPath->empty()) {
        out.push_back({FontOrigin::Cli, *cliPath});
        return out;
    }
    if (const char* env = std::getenv("MAESTRO_FONT"); env && *env)
        out.push_back({FontOrigin::Env, env});
    for (const char* c : kSystemFontCandidates)
        out.push_back({FontOrigin::System, c});
    if (const char* home = std::getenv("HOME"); home && *home) {
        const std::string h = home;
        out.push_back({FontOrigin::Home,
                       h + "/.local/share/fonts/JetBrainsMono-Regular.ttf"});
        out.push_back({FontOrigin::Home, h + "/.fonts/JetBrainsMono-Regular.ttf"});
    }
    return out;
}
