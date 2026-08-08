#include <string>

#include "test_framework.h"

// Probamos buildScreen(), que es la parte pura del renderer: construye
// el frame ANSI completo sin tocar la terminal. Para montar un frame
// usamos construimos un Document/Cursor/Viewport de nivel bajo.
#include "Renderer.h"
#include "Editor.h"

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
    return r.buildScreen(doc, cursor, viewport, "test.txt", false, "",
                         State::Normal, sel);
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

// ---------------------------------------------------------------------------
// v0.4: barra de estado de dos filas
// ---------------------------------------------------------------------------
namespace {

// Monta un frame solo con la barra de estado, controlando la ruta, el
// estado de guardado, el estado de la maquina de estados, el mensaje y
// el ancho de la terminal. El documento es una unica linea vacia.
std::string barFrame(const std::string& file, bool modified,
                     const std::string& msg, State state, int width) {
    Document doc;
    doc.restore({""});

    Viewport v;
    v.top = 0;
    v.height = 1;
    v.width = width;

    Cursor c;
    c.line = 0;
    c.col = 0;

    Renderer r;
    return r.buildScreen(doc, c, v, file, modified, msg, state, std::nullopt);
}

} // namespace

TEST(statusbar_two_rows_present) {
    // Debe haber exactamente una fila fija (video inverso) y una fila de
    // mensajes (sin inverso). El texto del mensaje no debe estar invertido.
    std::string out = barFrame("/a/b.txt", false, "hola", State::Normal, 200);
    CHECK(contains(out, ANSI_INV));
    CHECK(contains(out, "hola"));
    CHECK(!contains(out, ANSI_INV "hola"));
}

TEST(statusbar_left_format_name_path_estado) {
    // <nombre> - <ruta> - <ESTADO>, en ese orden, dentro del bloque inverso.
    std::string out = barFrame("/home/alice/proyecto/odo.txt", false, "",
                               State::Normal, 200);
    CHECK(contains(out, "odo.txt - /home/alice/proyecto - NORMAL"));
}

TEST(statusbar_modified_indicator) {
    // Un cambio sin guardar agrega [modificado] junto al nombre.
    std::string out = barFrame("/home/a/x.cc", true, "", State::Normal, 200);
    CHECK(contains(out, "x.cc [modificado] - /home/a - NORMAL"));
}

TEST(statusbar_state_labels) {
    // SELECCION y COMANDO se mapean 1 a 1 con los estados Select y Prefix.
    std::string sel = barFrame("/a.txt", false, "", State::Select, 200);
    CHECK(contains(sel, "a.txt - / - SELECCION"));

    std::string pre = barFrame("/a.txt", false, "", State::Prefix, 200);
    CHECK(contains(pre, "a.txt - / - COMANDO"));
}

TEST(statusbar_right_block_always_visible) {
    // Linea/Col siempre se muestra, anclado a la derecha, incluso en una
    // terminal estrecha: no es un derrota del sacrificio.
    std::string out = barFrame(
        "/data/muy/largo/dir/de/archivos/nombre.txt", false, "",
        State::Normal, 20);
    CHECK(contains(out, "Linea: 1 Col: 1"));
}

TEST(statusbar_path_sacrificed_before_name) {
    // Terminal no muy ancha: la ruta cede con "..." al inicio, el nombre
    // y el bloque Ln/Col se mantienen enteros.
    std::string out = barFrame(
        "/a/very/long/directory/chain/for/the/path/iz/archivo.txt",
        false, "", State::Normal, 55);
    CHECK(contains(out, "archivo.txt"));   // nombre intacto
    CHECK(contains(out, "..."));           // la ruta se corto con "..." al inicio
    CHECK(contains(out, "NORMAL"));        // etiqueta de estado intacta
    CHECK(contains(out, "Linea: 1 Col: 1"));
}

TEST(statusbar_second_row_message_independent) {
    // La fila de mensajes es propia: se muestra tal cual y se trunca al
    // ancho de la terminal sin competir con la barra fija.
    std::string out = barFrame("/a.txt", false, "mensaje de estado", State::Normal, 15);
    CHECK(contains(out, "mensaje de esta")); // truncado a 15 columnas
    CHECK(!contains(out, "mensaje de estado ")); // no cabe el final
    // La barra fija tiene su propio ancho: en 15 columnas el bloque
    // Linea/Col cabe (Linea+Col no depende del mensaje).
    CHECK(contains(out, "Linea: 1 Col: 1"));
}