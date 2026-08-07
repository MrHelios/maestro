#include <string>

#include "test_framework.h"

// Probamos buildScreen(), que es la parte pura del renderer: construye
// el frame ANSI completo sin tocar la terminal. Para montar un frame
// usamos construimos un Document/Cursor/Viewport de nivel bajo.
#include "Renderer.h"

// Secuencias ANSI usadas por el renderer.
#define ANSI_INV "\x1b[7m"   // video inverso
#define ANSI_RESET "\x1b[0m" // reset de estilo

namespace {

// Monta un frame con el contenido dado y devuelve la secuencia ANSI.
// `sel` opcional; si es std::nullopt, sin seleccion.
std::string frame(const std::vector<std::string>& lines,
                  const std::optional<Selection>& sel) {
    Document doc;
    doc.restore(lines);

    Viewport viewport;
    viewport.top = 0;
    viewport.height = static_cast<int>(lines.size()) + 1;
    viewport.width = 200;

    Cursor cursor;
    cursor.line = 0;
    cursor.col = 0;

    Renderer r;
    return r.buildScreen(doc, cursor, viewport, "test.txt", false, "", sel);
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

Selection selAt(Position a, Position p) {
    Selection s;
    s.anchor = a;
    s.position = p;
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Paso 9: renderizado visual de la seleccion
// ---------------------------------------------------------------------------
// El renderer marca el texto seleccionado con video inverso
// (\x1b[7m ... \x1b[0m). La direccion de la seleccion (hacia adelante o
// hacia atras) NO debe alterar el resultado visual.
TEST(renderer_no_selection) {
    std::string out = frame({"hello world"}, std::nullopt);

    // Sin seleccion no debe haber video inverso envolviendo al texto
    // del documento. (La barra de estado siempre usa video inverso,
    // por eso comprobamos que el texto en si no este "invertido".)
    CHECK(!contains(out, ANSI_INV "hello"));
    CHECK(contains(out, "hello world"));
}

TEST(renderer_single_line_selection) {
    // Seleccion [0,5) de "hello world" -> "hello" en video inverso.
    std::string out = frame({"hello world"}, selAt({0, 0}, {0, 5}));

    CHECK(contains(out, ANSI_INV "hello" ANSI_RESET));
    CHECK(contains(out, " world"));
}

TEST(renderer_multiline_selection) {
    // Seleccion desde (0,0) hasta (2,2) sobre:
    //   hello
    //   world
    //   foo
    // Debe invertir "hello", "world" y "fo".
    std::string out = frame({"hello", "world", "foo"}, selAt({0, 0}, {2, 2}));

    CHECK(contains(out, ANSI_INV "hello" ANSI_RESET));
    CHECK(contains(out, ANSI_INV "world" ANSI_RESET));
    CHECK(contains(out, ANSI_INV "fo" ANSI_RESET));
    CHECK(contains(out, "o\r\n")); // el resto de "foo" sin invertir
}

TEST(renderer_selection_reverse_direction) {
    // Mismo rango pero seleccionado "hacia atras" (cursor < anchor).
    std::string out = frame({"hello world"}, selAt({0, 5}, {0, 0}));

    // El resultado visual debe ser identico al de una seleccion hacia
    // delante: el renderer normaliza antes de dibujar.
    CHECK(contains(out, ANSI_INV "hello" ANSI_RESET));
    CHECK(contains(out, " world"));
}

TEST(renderer_empty_selection) {
    // anchor == position: no hay texto seleccionado.
    std::string out = frame({"hello world"}, selAt({0, 2}, {0, 2}));

    CHECK(!contains(out, ANSI_INV "hello"));
    CHECK(contains(out, "hello world"));
}

TEST(renderer_selection_partial_line_edges) {
    // Seleccion dentro de una linea: solo la parte media queda invertida.
    std::string out = frame({"hello world"}, selAt({0, 6}, {0, 9}));

    CHECK(!contains(out, ANSI_INV "hello"));
    CHECK(contains(out, ANSI_INV "wor" ANSI_RESET));
    CHECK(contains(out, "hello "));
    CHECK(contains(out, "ld"));
}

TEST(renderer_selection_clamps_to_line_end) {
    // Seleccion que termina mas alla del largo de la linea: se achica
    // al final real de la linea sin corromper el render.
    std::string out = frame({"abc"}, selAt({0, 1}, {0, 99}));

    CHECK(contains(out, ANSI_INV "bc" ANSI_RESET));
    CHECK(contains(out, "a"));
}