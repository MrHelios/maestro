// Tests de la política de orden de fuentes (FontOrder.h): precedencia CLI >
// MAESTRO_FONT > sistema > ~/.fonts y orígenes etiquetados. Puro y sin SDL:
// solo decide el ORDEN; la política al abrir la usa GuiFont
// (test_guifont.cpp). El entorno (MAESTRO_FONT/HOME) se manipula con guard
// RAII para no filtrar estado a otros tests del proceso.

#include <cstdlib>
#include <string>
#include <vector>

#include "rendering/gui/FontOrder.h"
#include "test_framework.h"

namespace {
// Guard del entorno del proceso: restaura TODAS las variables tocadas.
struct EnvGuard {
    bool hadFont = false;
    std::string fontOld;
    bool hadHome = false;
    std::string homeOld;

    EnvGuard() {
        if (const char* f = std::getenv("MAESTRO_FONT")) {
            hadFont = true;
            fontOld = f;
        }
        if (const char* h = std::getenv("HOME")) {
            hadHome = true;
            homeOld = h;
        }
    }
    ~EnvGuard() {
        if (hadFont)
            setenv("MAESTRO_FONT", fontOld.c_str(), 1);
        else
            unsetenv("MAESTRO_FONT");
        if (hadHome)
            setenv("HOME", homeOld.c_str(), 1);
        else
            unsetenv("HOME");
    }

    void clearEnv() {
        unsetenv("MAESTRO_FONT");
        unsetenv("HOME");
    }
};

int countOrigin(const std::vector<FontCandidate>& v, FontOrigin o) {
    int n = 0;
    for (const auto& c : v)
        if (c.origin == o) ++n;
    return n;
}
}  // namespace

TEST(fontorder_sin_cli_orden_env_sistema_home) {
    // Régimen SIN --font: env > sistema > ~/.fonts, cada origen en su
    // bloque. (El régimen CON --font es cli-exclusivo, ver
    // fontorder_cli_excluye_todos_los_otros_origenes.)
    EnvGuard g;
    g.clearEnv();
    setenv("MAESTRO_FONT", "/env/fuente.ttf", 1);
    setenv("HOME", "/fakehome", 1);

    const auto order = resolveFontOrder(std::nullopt);
    CHECK(!order.empty());
    CHECK_EQ(countOrigin(order, FontOrigin::Cli), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::Env), 1);
    CHECK_EQ(countOrigin(order, FontOrigin::System), 3);
    CHECK_EQ(countOrigin(order, FontOrigin::Home), 2);
    // El primero es la env y el último es ~/.fonts.
    CHECK(order.front().origin == FontOrigin::Env);
    CHECK_EQ(order.front().path, std::string("/env/fuente.ttf"));
    CHECK(order.back().origin == FontOrigin::Home);
    // Los System están en el medio, antes de los Home.
    size_t firstSystem = order.size(), firstHome = order.size();
    for (size_t i = 0; i < order.size(); ++i) {
        if (firstSystem == order.size() && order[i].origin == FontOrigin::System)
            firstSystem = i;
        if (firstHome == order.size() && order[i].origin == FontOrigin::Home)
            firstHome = i;
    }
    CHECK(firstSystem < firstHome);
}

TEST(fontorder_sin_cli_con_env) {
    EnvGuard g;
    g.clearEnv();
    setenv("MAESTRO_FONT", "/env/fuente.ttf", 1);
    unsetenv("HOME");

    const auto order = resolveFontOrder(std::nullopt);
    CHECK_EQ(countOrigin(order, FontOrigin::Cli), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::Env), 1);
    CHECK(order.front().origin == FontOrigin::Env);
    CHECK_EQ(countOrigin(order, FontOrigin::Home), 0);
}

TEST(fontorder_env_ausente_o_vacia_es_comportamiento_default) {
    EnvGuard g;
    g.clearEnv();
    unsetenv("HOME");

    // Ausente
    auto order = resolveFontOrder(std::nullopt);
    CHECK_EQ(countOrigin(order, FontOrigin::Cli), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::Env), 0);
    // Solo candidatos de sistema (3) sin HOME.
    CHECK_EQ(order.size(), size_t(3));

    // Vacía cuenta como ausente (la env vacía nunca es una ruta útil).
    setenv("MAESTRO_FONT", "", 1);
    order = resolveFontOrder(std::nullopt);
    CHECK_EQ(countOrigin(order, FontOrigin::Env), 0);
    CHECK_EQ(order.size(), size_t(3));
}

TEST(fontorder_cli_excluye_todos_los_otros_origenes) {
    // GARANTÍA P1: una --font que no abre NO cae al env ni al sistema. El
    // cargador no puede "tratar a la explícita como un candidato más":
    // con Cli presente el orden TERMINA en él (política de error aparte,
    // probada abriendo en test_guifont.cpp).
    EnvGuard g;
    g.clearEnv();
    setenv("MAESTRO_FONT", "/env/fuente.ttf", 1);
    setenv("HOME", "/fakehome", 1);

    const auto order = resolveFontOrder(std::optional<std::string>("/cli.ttf"));
    CHECK_EQ(order.size(), size_t(1));
    CHECK(order.front().origin == FontOrigin::Cli);
    CHECK_EQ(order.front().path, std::string("/cli.ttf"));
    CHECK_EQ(countOrigin(order, FontOrigin::Env), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::System), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::Home), 0);
}

TEST(fontorder_home_agrega_rutas_extra) {
    EnvGuard g;
    g.clearEnv();
    setenv("HOME", "/fakehome", 1);

    const auto order = resolveFontOrder(std::nullopt);
    const FontCandidate* h1 = nullptr;
    const FontCandidate* h2 = nullptr;
    int homes = 0;
    for (const auto& c : order) {
        if (c.origin != FontOrigin::Home) continue;
        ++homes;
        if (homes == 1)
            h1 = &c;
        else if (homes == 2)
            h2 = &c;
    }
    CHECK_EQ(homes, 2);
    if (h1) CHECK_EQ(h1->path, std::string("/fakehome/.local/share/fonts/JetBrainsMono-Regular.ttf"));
    if (h2) CHECK_EQ(h2->path, std::string("/fakehome/.fonts/JetBrainsMono-Regular.ttf"));
}

TEST(fontorder_cli_vacia_se_trata_como_ausente) {
    EnvGuard g;
    g.clearEnv();
    unsetenv("HOME");

    // La forma --font= ya es rechazada por el parser; por robustez el
    // cargador tampoco la admite como candidato Cli (nunca abriría).
    const auto order = resolveFontOrder(std::optional<std::string>(""));
    CHECK_EQ(countOrigin(order, FontOrigin::Cli), 0);
    CHECK_EQ(order.size(), size_t(3));
}

TEST(fontorder_rutas_de_sistema_congeladas) {
    // Los candidatos del sistema son parte del contrato README (DejaVu
    // primero); cambiarlos silenciosamente rompería installs sin X11.
    CHECK_EQ(std::string(kSystemFontCandidates[0]),
             std::string("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"));
    CHECK_EQ(std::string(kSystemFontCandidates[1]),
             std::string("/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"));
}

TEST(fontorder_nombres_de_origen) {
    // Para diagnósticos legibles (warnings usan el origen).
    CHECK(std::string(fontOriginName(FontOrigin::Cli)).find("cli") != std::string::npos);
    CHECK(std::string(fontOriginName(FontOrigin::Env)) == std::string("MAESTRO_FONT"));
    CHECK(std::string(fontOriginName(FontOrigin::System)) == std::string("sistema"));
    CHECK(std::string(fontOriginName(FontOrigin::Home)) == std::string("~/.fonts"));
}
