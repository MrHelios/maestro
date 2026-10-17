// Tests para GuiOptions y parsing de argumentos CLI.

#include <vector>

#include "app/CliArgs.h"
#include "test_framework.h"
#include "platform/gui/GuiOptions.h"

TEST(gui_options_defaults) {
    GuiOptions opts;
    CHECK(!opts.fontPath.has_value());
    CHECK(!opts.fontSize.has_value());
    CHECK(opts.theme == GuiTheme::Dark);
    CHECK(opts.enableHiDpi == true);
    CHECK(!opts.windowWidth.has_value());
    CHECK(!opts.windowHeight.has_value());
}

TEST(gui_options_equality) {
    GuiOptions a;
    GuiOptions b;
    CHECK(a == b);

    a.fontPath = "/custom/font.ttf";
    CHECK(!(a == b));
    b.fontPath = "/custom/font.ttf";
    CHECK(a == b);

    a.fontSize = 18;
    CHECK(!(a == b));
    b.fontSize = 18;
    CHECK(a == b);

    a.theme = GuiTheme::Light;
    CHECK(!(a == b));
    b.theme = GuiTheme::Light;
    CHECK(a == b);

    a.enableHiDpi = false;
    CHECK(!(a == b));
    b.enableHiDpi = false;
    CHECK(a == b);

    a.windowWidth = 1200;
    CHECK(!(a == b));
    b.windowWidth = 1200;
    CHECK(a == b);

    a.windowHeight = 800;
    CHECK(!(a == b));
    b.windowHeight = 800;
    CHECK(a == b);
}

TEST(gui_theme_values) {
    CHECK_EQ(static_cast<int>(GuiTheme::Dark), 0);
    CHECK_EQ(static_cast<int>(GuiTheme::Light), 1);
}

TEST(gui_parse_geometry_valid) {
    int w, h;
    CHECK(parseGeometry("1200x800", w, h));
    CHECK_EQ(w, 1200);
    CHECK_EQ(h, 800);

    CHECK(parseGeometry("960x600", w, h));
    CHECK_EQ(w, 960);
    CHECK_EQ(h, 600);

    CHECK(parseGeometry("1x1", w, h));
    CHECK_EQ(w, 1);
    CHECK_EQ(h, 1);
}

TEST(gui_parse_geometry_invalid) {
    int w, h;
    CHECK(!parseGeometry("", w, h));
    CHECK(!parseGeometry("1200", w, h));
    CHECK(!parseGeometry("x800", w, h));
    CHECK(!parseGeometry("1200x", w, h));
    CHECK(!parseGeometry("abcxdef", w, h));
    CHECK(!parseGeometry("0x800", w, h));
    CHECK(!parseGeometry("1200x0", w, h));
    CHECK(!parseGeometry("-1x800", w, h));
    CHECK(!parseGeometry("10001x800", w, h));
    CHECK(!parseGeometry("1200x10001", w, h));
}

TEST(gui_parse_theme_valid) {
    GuiTheme t;
    CHECK(parseTheme("dark", t));
    CHECK_EQ(static_cast<int>(t), static_cast<int>(GuiTheme::Dark));
    CHECK(parseTheme("light", t));
    CHECK_EQ(static_cast<int>(t), static_cast<int>(GuiTheme::Light));
}

TEST(gui_parse_theme_invalid) {
    GuiTheme t;
    CHECK(!parseTheme("", t));
    CHECK(!parseTheme("Dark", t));
    CHECK(!parseTheme("LIGHT", t));
    CHECK(!parseTheme("blue", t));
    CHECK(!parseTheme("invalid", t));
}

TEST(gui_parse_geometry_strict) {
    int w, h;
    // Sufijos inválidos
    CHECK(!parseGeometry("1200x800junk", w, h));
    CHECK(!parseGeometry("1200x800x600", w, h));
    CHECK(!parseGeometry("1200junkx800", w, h));
    // Espacios
    CHECK(!parseGeometry(" 1200x800", w, h));
    CHECK(!parseGeometry("1200x800 ", w, h));
    // Desbordamiento de long en una dimensión: rechazo antes de cualquier
    // conversión peligrosa (stol lanza y parseIntStrict devuelve false).
    CHECK(!parseGeometry("999999999999x800", w, h));
    CHECK(!parseGeometry("1200x999999999999", w, h));
    CHECK(!parseGeometry("-1x-1", w, h));
}

TEST(gui_parse_geometry_area_limit) {
    int w, h;
    // 4000x3000 = 12M (límite exacto) -> válido
    CHECK(parseGeometry("4000x3000", w, h));
    CHECK_EQ(w, 4000);
    CHECK_EQ(h, 3000);
    // 4000x3001 = 12M+4000 -> inválido
    CHECK(!parseGeometry("4000x3001", w, h));
    // 5000x2500 = 12.5M -> inválido
    CHECK(!parseGeometry("5000x2500", w, h));
    // 3500x3500 = 12.25M -> inválido
    CHECK(!parseGeometry("3500x3500", w, h));
}

TEST(gui_parse_int_strict) {
    int val;
    // GRAMÁTICA congelada: solo dígitos decimales (ver parseIntStrict).
    // '+16' y '-1' se rechazan por la gramática, no por el rango.
    CHECK(parseIntStrict("16", val, 1, 200));
    CHECK_EQ(val, 16);
    CHECK(parseIntStrict("1", val, 1, 200));
    CHECK_EQ(val, 1);
    CHECK(parseIntStrict("200", val, 1, 200));
    CHECK_EQ(val, 200);
    // Fuera de rango
    CHECK(!parseIntStrict("0", val, 1, 200));
    CHECK(!parseIntStrict("201", val, 1, 200));
    // Gramática: sin signo, sin espacios, sin sufijos, sin vacío
    CHECK(!parseIntStrict("+16", val, 1, 200));
    CHECK(!parseIntStrict("-1", val, 1, 200));
    CHECK(!parseIntStrict("1e2", val, 1, 200));
    CHECK(!parseIntStrict("16abc", val, 1, 200));
    CHECK(!parseIntStrict(" 16", val, 1, 200));
    CHECK(!parseIntStrict("16 ", val, 1, 200));
    CHECK(!parseIntStrict("", val, 1, 200));
}

TEST(gui_parse_int_strict_desbordamiento) {
    int val;
    // stol lanza out_of_range con dígitos fuera del rango de long: el
    // desbordamiento se rechaza sin crash ni truncamiento.
    CHECK(!parseIntStrict("999999999999", val, 1, 200));
    CHECK(!parseIntStrict("99999999999999999999", val, 1, 200));
}

// Ejercita el parseArgs real de src/app/CliArgs.h (el mismo que usa main):
// construye argv y verifica el Args resultante, sin duplicar la lógica.

namespace {
Args callParse(const std::vector<std::string>& parts) {
    std::vector<char*> argv;
    argv.reserve(parts.size());
    for (const std::string& s : parts) argv.push_back(const_cast<char*>(s.c_str()));
    return parseArgs(static_cast<int>(argv.size()), argv.data());
}
}  // namespace

TEST(gui_cli_gui_options_require_gui_flag) {
    // Cada opción GUI sin --gui es error; con --gui es OK.
    CHECK(callParse({"prog", "--font", "/x.ttf"}).bad);
    CHECK(callParse({"prog", "--font-size", "18"}).bad);
    CHECK(callParse({"prog", "--theme", "light"}).bad);
    CHECK(callParse({"prog", "--no-hidpi"}).bad);
    CHECK(callParse({"prog", "--geometry", "1200x800"}).bad);

    CHECK(!callParse({"prog", "--gui", "--font", "/x.ttf"}).bad);
    CHECK(!callParse({"prog", "--gui", "--font-size", "18"}).bad);
    CHECK(!callParse({"prog", "--gui", "--theme", "light"}).bad);
    CHECK(!callParse({"prog", "--gui", "--no-hidpi"}).bad);
    CHECK(!callParse({"prog", "--gui", "--geometry", "1200x800"}).bad);

    // Sin opciones GUI el modo TTY sigue siendo válido.
    CHECK(!callParse({"prog"}).bad);
    CHECK(!callParse({"prog", "archivo.txt"}).bad);
}

TEST(gui_cli_parses_option_values) {
    Args a = callParse({"prog", "--gui", "--font", "/x.ttf", "--font-size",
                        "18", "--theme", "light", "--no-hidpi", "--geometry",
                        "1200x800", "archivo.txt"});
    CHECK(!a.bad);
    CHECK(a.gui);
    CHECK(a.guiOptionsUsed);
    CHECK(a.guiOptions.fontPath.has_value());
    CHECK(*a.guiOptions.fontPath == "/x.ttf");
    CHECK(a.guiOptions.fontSize.has_value());
    CHECK_EQ(*a.guiOptions.fontSize, 18);
    CHECK(a.guiOptions.theme == GuiTheme::Light);
    CHECK(!a.guiOptions.enableHiDpi);
    CHECK(a.guiOptions.windowWidth.has_value());
    CHECK_EQ(*a.guiOptions.windowWidth, 1200);
    CHECK(a.guiOptions.windowHeight.has_value());
    CHECK_EQ(*a.guiOptions.windowHeight, 800);
    CHECK(a.file == "archivo.txt");
}

TEST(gui_cli_no_hidpi_establece_opt_y_used) {
    // El flag solo toca enableHiDpi (el resto queda en default) y marca
    // guiOptionsUsed: el efecto real en la máscara de SDL_CreateWindow se
    // prueba aparte en guiwindowflags_* (test_guisdlmapper.cpp).
    Args a = callParse({"prog", "--gui", "--no-hidpi"});
    CHECK(!a.bad);
    CHECK(a.gui);
    CHECK(a.guiOptionsUsed);
    CHECK(!a.guiOptions.enableHiDpi);
    // Lo no tocado permanece en default.
    CHECK(!a.guiOptions.fontPath.has_value());
    CHECK(!a.guiOptions.fontSize.has_value());
    CHECK(a.guiOptions.theme == GuiTheme::Dark);
    CHECK(!a.guiOptions.windowWidth.has_value());
    CHECK(!a.guiOptions.windowHeight.has_value());
    // Sin el flag (cualquier otro --gui): HiDPI activo por defecto.
    Args b = callParse({"prog", "--gui"});
    CHECK(!b.bad);
    CHECK(b.guiOptions.enableHiDpi);
    CHECK(!b.guiOptionsUsed);
}

TEST(gui_cli_rejects_invalid_option_values) {
    // Valor faltante, fuera de rango o malformado marca bad.
    CHECK(callParse({"prog", "--gui", "--font"}).bad);
    CHECK(callParse({"prog", "--gui", "--font-size"}).bad);
    CHECK(callParse({"prog", "--gui", "--font-size", "0"}).bad);
    CHECK(callParse({"prog", "--gui", "--font-size", "201"}).bad);
    CHECK(callParse({"prog", "--gui", "--font-size", "16abc"}).bad);
    CHECK(callParse({"prog", "--gui", "--theme"}).bad);
    CHECK(callParse({"prog", "--gui", "--theme", "blue"}).bad);
    CHECK(callParse({"prog", "--gui", "--theme", "Dark"}).bad);
    CHECK(callParse({"prog", "--gui", "--geometry"}).bad);
    CHECK(callParse({"prog", "--gui", "--geometry", "1200x800x600"}).bad);
    CHECK(callParse({"prog", "--gui", "--geometry", "5000x2500"}).bad);
    // Flag desconocido sigue siendo error.
    CHECK(callParse({"prog", "--gui", "--no-existe"}).bad);
}

TEST(gui_cli_help_gana_en_cualquier_posicion) {
    // Regla --help (P1): ayuda en cualquier posición, incluso si los demás
    // argumentos serían inválidos. main la imprime y sale 0 antes de
    // inicializar SDL; acá solo se verifica que el parser la reporte.
    Args a = callParse({"prog", "--font", "--help"});
    CHECK(a.help);
    CHECK(!a.bad);
    a = callParse({"prog", "--theme", "blue", "--help"});
    CHECK(a.help);
    CHECK(!a.bad);
    a = callParse({"prog", "--gui", "--help"});
    CHECK(a.help);
    CHECK(!a.bad);
    a = callParse({"prog", "--gui", "--theme", "blue", "--help"});
    CHECK(a.help);
    CHECK(!a.bad);
    a = callParse({"prog", "archivo.txt", "-h"});
    CHECK(a.help);
    CHECK(!a.bad);
    a = callParse({"prog", "-h", "archivo.txt"});
    CHECK(a.help);
    CHECK(!a.bad);
}

TEST(gui_cli_forma_igual_acepta_valores_con_guion) {
    // Sintaxis inequívoca documentada: --opt=VALOR admite valores que
    // empiezan con '-' (la forma separada por espacio no).
    Args a = callParse({"prog", "--gui", "--font=-mi-fuente.ttf"});
    CHECK(!a.bad);
    CHECK(a.guiOptions.fontPath.has_value());
    CHECK_EQ(*a.guiOptions.fontPath, std::string("-mi-fuente.ttf"));

    a = callParse({"prog", "--gui", "--font=/ruta con espacios/f.ttf"});
    CHECK(!a.bad);
    CHECK_EQ(*a.guiOptions.fontPath, std::string("/ruta con espacios/f.ttf"));

    a = callParse({"prog", "--gui", "--font-size=18", "--theme=light",
                   "--geometry=1200x800"});
    CHECK(!a.bad);
    CHECK_EQ(*a.guiOptions.fontSize, 18);
    CHECK(a.guiOptions.theme == GuiTheme::Light);
    CHECK_EQ(*a.guiOptions.windowWidth, 1200);
    CHECK_EQ(*a.guiOptions.windowHeight, 800);

    // Valor vacío con la forma '=' es error con diagnóstico.
    a = callParse({"prog", "--gui", "--font="});
    CHECK(a.bad);
    CHECK(!a.badReason.empty());
}

TEST(gui_cli_valor_que_parece_otra_opcion_es_error) {
    // Un valor que empieza con '-' se trata como opción (regla P1): sin la
    // guardia, `--font --theme` asignaría "--theme" como ruta en silencio.
    Args a = callParse({"prog", "--gui", "--font", "--theme"});
    CHECK(a.bad);
    CHECK(!a.badReason.empty());

    a = callParse({"prog", "--gui", "--font", "-x.ttf"});
    CHECK(a.bad);
    CHECK(!a.badReason.empty());
    // La vía válida para ese valor es la forma '='.
    CHECK(!callParse({"prog", "--gui", "--font=-x.ttf"}).bad);

    a = callParse({"prog", "--gui", "--font-size", "-5"});
    CHECK(a.bad);
    a = callParse({"prog", "--gui", "--theme", "--light"});
    CHECK(a.bad);
    a = callParse({"prog", "--gui", "--geometry", "-1x800"});
    CHECK(a.bad);
}

TEST(gui_cli_theme_invalido_via_parseargs) {
    // parseTheme ya cubre los valores puros; acá se prueba a través del
    // parser real, que debe marcar bad con diagnóstico.
    for (const char* bad : {"Dark", "LIGHT", "blue", "", "dark "}) {
        Args a = callParse({"prog", "--gui", "--theme", bad});
        CHECK(a.bad);
        CHECK(!a.badReason.empty());
    }
    Args ok = callParse({"prog", "--gui", "--theme", "light"});
    CHECK(!ok.bad);
    CHECK(ok.guiOptions.theme == GuiTheme::Light);
}

TEST(gui_cli_font_size_gramatica_via_parseargs) {
    // Gramática elegida para --font-size: dígitos decimales positivos.
    CHECK(callParse({"prog", "--gui", "--font-size", "+16"}).bad);  // sin signo
    CHECK(callParse({"prog", "--gui", "--font-size", "1e2"}).bad);
    CHECK(callParse({"prog", "--gui", "--font-size", "999999999999"}).bad);  // overflow
    CHECK(callParse({"prog", "--gui", "--font-size", "0"}).bad);
    CHECK(callParse({"prog", "--gui", "--font-size", "201"}).bad);  // excesivo
    Args a = callParse({"prog", "--gui", "--font-size", "200"});
    CHECK(!a.bad);
    CHECK_EQ(*a.guiOptions.fontSize, 200);
    a = callParse({"prog", "--gui", "--font-size", "1"});
    CHECK(!a.bad);
    CHECK_EQ(*a.guiOptions.fontSize, 1);
}

TEST(gui_cli_bad_reason_por_cada_falla) {
    struct Case {
        std::vector<std::string> parts;
        const char* needle;
    };
    const Case cases[] = {
        {{"prog", "--gui", "--font"}, "--font"},
        {{"prog", "--gui", "--font-size", "abc"}, "--font-size"},
        {{"prog", "--gui", "--theme", "blue"}, "--theme"},
        {{"prog", "--gui", "--geometry", "1200"}, "--geometry"},
        {{"prog", "--gui", "--no-existe"}, "opción desconocida"},
        {{"prog", "--font", "/x.ttf"}, "requieren --gui"},
        {{"prog", "a.txt", "b.txt"}, "inesperado"},
    };
    for (const Case& c : cases) {
        Args a = callParse(c.parts);
        CHECK(a.bad);
        if (a.bad) {
            CHECK(a.badReason.find(c.needle) != std::string::npos);
        }
    }
}