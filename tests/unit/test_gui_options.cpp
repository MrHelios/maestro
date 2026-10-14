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
    CHECK(parseIntStrict("16", val, 1, 200));
    CHECK_EQ(val, 16);
    CHECK(parseIntStrict("1", val, 1, 200));
    CHECK_EQ(val, 1);
    CHECK(parseIntStrict("200", val, 1, 200));
    CHECK_EQ(val, 200);
    // Fuera de rango
    CHECK(!parseIntStrict("0", val, 1, 200));
    CHECK(!parseIntStrict("201", val, 1, 200));
    CHECK(!parseIntStrict("-1", val, 1, 200));
    // Sufijos inválidos
    CHECK(!parseIntStrict("16abc", val, 1, 200));
    CHECK(!parseIntStrict(" 16", val, 1, 200));
    CHECK(!parseIntStrict("16 ", val, 1, 200));
    // Vacío
    CHECK(!parseIntStrict("", val, 1, 200));
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