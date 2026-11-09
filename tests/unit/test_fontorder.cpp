#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/gui/GuiOptions.h"
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

// no agregamos argumento al comand line
// verificamos orden de ejecucion
// verificamos path en cada orden de ejecucion
TEST(fontorder_sin_cli_orden_env_sistema_home) {
    EnvGuard g;
    g.clearEnv();
    setenv("MAESTRO_FONT", "/env/fuente.ttf", 1);
    setenv("HOME", "/fakehome", 1);
    
    // no le pasamos linea de comando: --font ruta.ttf
    const auto order = resolveFontOrder(std::nullopt);
    
    // el orden de ejecucion:
    // argumento de comando de linea
    // variable de entorno
    // home
    // sistemas
    CHECK(!order.empty());
    CHECK_EQ(countOrigin(order, FontOrigin::Cli), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::Env), 1);
    CHECK_EQ(countOrigin(order, FontOrigin::Home), 2);
    CHECK_EQ(countOrigin(order, FontOrigin::System), 3);
    
    // verificamos path de cada orden
    CHECK(order.front().origin == FontOrigin::Env);
    CHECK_EQ(order.front().path, std::string("/env/fuente.ttf"));
    
    CHECK(order[1].origin == FontOrigin::System);
    CHECK_EQ(order[1].path, std::string("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"));
    
    CHECK(order.back().origin == FontOrigin::Home);
    CHECK_EQ(order.back().path, std::string("/fakehome/.fonts/JetBrainsMono-Regular.ttf"));
}

// no agregamos argemento al command line
// no establecemos home font
// solo probamos variable de entorno
// con la direccion del font
TEST(fontorder_sin_cli_con_env) {
    EnvGuard g;
    g.clearEnv();
    setenv("MAESTRO_FONT", "/env/fuente.ttf", 1);
    unsetenv("HOME");

    const auto order = resolveFontOrder(std::nullopt);
    CHECK_EQ(countOrigin(order, FontOrigin::Cli), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::Home), 0);
    
    CHECK_EQ(countOrigin(order, FontOrigin::Env), 1);
    CHECK_EQ(order.front().path, std::string("/env/fuente.ttf"));
}

TEST(fontorder_env_ausente_es_comportamiento_default) {
    EnvGuard g;
    g.clearEnv();
    unsetenv("HOME");

    auto order = resolveFontOrder(std::nullopt);
    CHECK_EQ(countOrigin(order, FontOrigin::Cli), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::Env), 0);
    CHECK_EQ(order.size(), size_t(3));
}

TEST(fontorder_env_vacia_es_comportamiento_default) {
    EnvGuard g;
    g.clearEnv();
    setenv("MAESTRO_FONT", "", 1);
    
    auto order = resolveFontOrder(std::nullopt);
    CHECK_EQ(countOrigin(order, FontOrigin::Cli), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::Env), 0);
    CHECK_EQ(order.size(), size_t(3));
}

TEST(fontorder_cli_excluye_todos_los_otros_origenes) {
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

// La forma --font= ya es rechazada por el parser; 
TEST(fontorder_cli_vacia_se_trata_como_ausente) {
    EnvGuard g;
    g.clearEnv();
    unsetenv("HOME");

    const auto order = resolveFontOrder(std::optional<std::string>(""));
    CHECK_EQ(countOrigin(order, FontOrigin::Cli), 0);
    CHECK_EQ(order.size(), size_t(3));
}

// Una ruta --font inválida no se valida en el resolver: queda como ÚNICO
// candidato Cli para que el cargador falle SIN fallback (error terminal).
TEST(fontorder_cli_ruta_invalida_es_error_terminal_sin_fallback) {
    EnvGuard g;
    g.clearEnv();
    setenv("MAESTRO_FONT", "/env/fuente.ttf", 1);
    setenv("HOME", "/fakehome", 1);

    const std::string bad = "/no/existe/maestro-test.ttf";
    CHECK(!std::filesystem::exists(bad));

    const auto order = resolveFontOrder(std::optional<std::string>(bad));
    // El resolver no comprueba la existencia: la ruta inválida es el único
    // candidato y no se agrega ningún origen de respaldo.
    CHECK_EQ(order.size(), size_t(1));
    CHECK(order.front().origin == FontOrigin::Cli);
    CHECK_EQ(order.front().path, bad);
    CHECK_EQ(countOrigin(order, FontOrigin::Cli), 1);
    CHECK_EQ(countOrigin(order, FontOrigin::Env), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::System), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::Home), 0);

    // El diagnóstico de la CLI conserva la ruta en el mensaje de error.
    GuiOptions opts;
    opts.fontPath = bad;
    const std::string diag = fontOpenDiagnostic(opts);
    CHECK_EQ(diag, std::string("maestro: error al abrir la fuente '") + bad +
                       "' especificada con --font\n");
}

// Sin --font, sin MAESTRO_FONT y sin HOME solo quedan los candidatos del sistema. 
// El resolver no comprueba si existen: el fallo se detecta al
// abrirlos y, si ninguno abre, el loop imprime el diagnóstico de error
// terminal y sale con código != 0.
TEST(fontorder_sin_cli_env_home_ni_sistema_reporta_error_terminal) {
    EnvGuard g;
    g.clearEnv();
    CHECK(std::getenv("MAESTRO_FONT") == nullptr);
    CHECK(std::getenv("HOME") == nullptr);

    const auto order = resolveFontOrder(std::nullopt);
    CHECK_EQ(countOrigin(order, FontOrigin::Cli), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::Env), 0);
    CHECK_EQ(countOrigin(order, FontOrigin::Home), 0);
    
    // Únicos candidatos restantes: los del sistema
    CHECK_EQ(countOrigin(order, FontOrigin::System), 3);
    CHECK_EQ(order.size(), size_t(3));

    // Sin fuente explícita, este es el diagnóstico terminal que imprime
    // GuiRunLoop cuando ninguna fuente se pudo abrir.
    GuiOptions opts;
    CHECK_EQ(fontOpenDiagnostic(opts),
             std::string("maestro: no se encontró ninguna fuente TTF utilizable\n"));
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

// Los candidatos del sistema son parte del contrato README
TEST(fontorder_rutas_de_sistema_congeladas) {    
    CHECK_EQ(std::string(kSystemFontCandidates[0]),
             std::string("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"));
    CHECK_EQ(std::string(kSystemFontCandidates[1]),
             std::string("/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"));
}

// Para diagnósticos legibles (warnings usan el origen).
TEST(fontorder_nombres_de_origen) {
    CHECK(std::string(fontOriginName(FontOrigin::Cli)).find("cli") != std::string::npos);
    CHECK(std::string(fontOriginName(FontOrigin::Env)) == std::string("MAESTRO_FONT"));
    CHECK(std::string(fontOriginName(FontOrigin::System)) == std::string("sistema"));
    CHECK(std::string(fontOriginName(FontOrigin::Home)) == std::string("~/.fonts"));
}
