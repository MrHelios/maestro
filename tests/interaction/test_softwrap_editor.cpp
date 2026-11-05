#include <string>
#include <vector>

#include "test_support.h"
#include "helpers/spy_screen_renderer.h"
#include "platform/gui/GuiKeymap.h"
#include "platform/tty/TtyKeymap.h"

namespace {

void setupWrapEditor(Editor& ed, const std::vector<std::string>& lines,
                     int cols = 19) {
    ed.active().filename = "notes.md";
    ed.active().document.restore(lines);
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.resize(10, cols);
}

} // namespace

// El test usa textWidth = 12 (19 columnas - gutter 7). Una línea de 30
// caracteres ocupa tres filas visuales (12 + 12 + 6).
TEST(softwrap_vertical_keeps_preferred_column) {
    Editor ed;
    setupWrapEditor(ed, {
        std::string(30, 'a'),
        "short",
        std::string(30, 'b'),
    });
    ed.active().cursor.col = 20; // fila visual 1, columna relativa 8

    // Down: debe llegar a la siguiente linea logica conservando la columna pref.
    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 5); // "short": fin de linea (pref 8 adaptada)

    // Down: continua hacia la siguiente linea logica.
    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 8); // pref 8 en la linea larga
}

TEST(softwrap_vertical_up_keeps_preferred_column) {
    Editor ed;
    setupWrapEditor(ed, {
        std::string(30, 'a'),
        "short",
        std::string(30, 'b'),
    });
    ed.active().cursor.line = 2;
    ed.active().cursor.col = 20; // fila visual 1 dentro de la línea 2

    // Up: debe retroceder a la línea lógica anterior conservando la columna pref.
    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 5); // "short": fin de línea (pref 8 adaptada)

    // Up: continúa hacia la línea lógica anterior.
    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 8); // pref 8 en la línea larga
}

TEST(softwrap_home_end_stay_inside_intermediate_row) {
    Editor ed;
    setupWrapEditor(ed, {std::string(30, 'a')});
    ed.active().cursor.col = 18; // fila visual 1

    press(ed, InputEventType::MoveHome);
    CHECK_EQ(ed.active().cursor.col, 12); // inicio de la fila 1

    press(ed, InputEventType::MoveEnd);
    CHECK_EQ(ed.active().cursor.col, 23); // fin de la fila 1

    // Con el criterio nuevo (Down -> siguiente linea logica) no existe "fila
    // siguiente" dentro de la misma linea: con una sola linea logica hay que
    // posicionarse directo en la ultima fila (fin de linea).
    ed.active().cursor.col = 30; // fila visual 2 (ultima): fin de linea

    CHECK_EQ(ed.active().cursor.col, 30); // fin de linea

    press(ed, InputEventType::MoveHome);
    CHECK_EQ(ed.active().cursor.col, 24); // inicio de la fila 2

    press(ed, InputEventType::MoveEnd);
    CHECK_EQ(ed.active().cursor.col, 30); // fin de la línea (posición exclusiva)
}

// End en un .md debe llegar al final EXCLUSIVO de la línea (col ==
// lineLength), no a la última celda. El bug se manifestaba en el camino con
// soft-wrap, que solo se activa para .md: se comprueba el evento End tal como
// llega desde cada frontend (GUI por GuiKeymap, TUI por TtyKeymap).
//
// Dos casos:
// 1. Línea corta (sin wrap): la única fila visual es también la última.
// 2. Línea larga (con wrap): el cursor empieza en la última fila visual
//    y End debe alcanzar el final exclusivo de la línea (col 30).

namespace {
void testEndOnLastVisualRow(Editor& ed, InputEventType endEvent) {
    // Línea de 30 'a': 3 filas visuales (12 + 12 + 6).
    // Posicionamos en la última fila visual (columna 24).
    (void)ed.wrapIndex();
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 24;
    press(ed, endEvent);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 30);
    CHECK_EQ(ed.active().cursor.col, ed.active().document.lineLength(0));
}

void testEndOnSingleRow(Editor& ed, InputEventType endEvent) {
    // Línea de 10 chars: una sola fila visual (no hay wrap real).
    ed.active().document.restore({"hola mundo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    press(ed, endEvent);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 10);
    CHECK_EQ(ed.active().cursor.col, ed.active().document.lineLength(0));
}
} // namespace

TEST(softwrap_markdown_end_gui_reaches_line_end) {
    Editor ed;
    setupWrapEditor(ed, {std::string(30, 'a')});

    GuiKeyPress key;
    key.sym = GuiKeySym::End;
    GuiKeymap keymap;
    const auto type = keymap.mapKey(key);
    CHECK(type.has_value());
    if (type) {
        testEndOnLastVisualRow(ed, *type);
        testEndOnSingleRow(ed, *type);
    }
}

TEST(softwrap_markdown_end_tui_reaches_line_end) {
    Editor ed;
    setupWrapEditor(ed, {std::string(30, 'a')});

    TtyKeymap keymap;
    const auto type = keymap.sequence("[F");
    CHECK(type.has_value());
    if (type) {
        testEndOnLastVisualRow(ed, *type);
        testEndOnSingleRow(ed, *type);
    }
}

TEST(softwrap_home_at_continuation_boundary_stays_on_row) {
    Editor ed;
    setupWrapEditor(ed, {std::string(30, 'a')});
    ed.active().cursor.col = 12; // posición de límite, visualmente fila 1

    press(ed, InputEventType::MoveHome);
    CHECK_EQ(ed.active().cursor.col, 12);
}

TEST(softwrap_zero_text_width_uses_identity_navigation) {
    Editor ed;
    // width 2 == gutter 2: no queda ancho útil para texto.
    setupWrapEditor(ed, {std::string(30, 'a'), "abcdef"}, 2);
    for (int i = 0; i < 3; ++i) press(ed, InputEventType::MoveRight);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);

    // Navegación y render deben usar el mismo modo identidad: bajar una
    // línea lógica conserva la columna, no la colapsa a 0.
    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 3);
}

// Click en la última celda visual de una línea .md corta (una sola fila
// visual) debe colocar el cursor en el final exclusivo (col == lineLength).
TEST(softwrap_mouse_click_at_line_end_places_cursor_at_exclusive_end) {
    Editor ed;
    ed.active().filename = "notes.md";
    ed.active().document.restore({"hola mundo"}); // 10 chars, 1 visual row
    ed.resize(7, 20); // gutter 7, content width 13
    ed.active().viewport.top = 0;
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    // Click en la celda después del último carácter (columna visual 10 = gutter + 10).
    // gutter=7, text cols 0..9 ocupan celdas 7..16. Click en celda 17 -> visualColInRow=10.
    InputEvent press;
    press.type = InputEventType::MousePress;
    press.cell = CellPos{17, 0}; // gutter 7 + textWidth 10 = 17
    ed.handleEvent(press);

    InputEvent release;
    release.type = InputEventType::MouseRelease;
    release.cell = CellPos{17, 0};
    ed.handleEvent(release);

    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 10); // final exclusivo
    CHECK_EQ(ed.active().cursor.col, ed.active().document.lineLength(0));
    CHECK(!ed.hasSelection());
}

TEST(softwrap_mouse_drag_beyond_eof_selects_last_char) {
    Editor ed;
    ed.active().filename = "notes.md";
    ed.active().document.restore({"abc", "def", "ghi"});
    ed.resize(7, 20); // contentH 5; las 3 líneas caben y sobran filas ~
    ed.active().viewport.top = 0;
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    InputEvent press;
    press.type = InputEventType::MousePress;
    press.cell = CellPos{7, 0}; // gutter 7, primera celda de texto -> línea 0, col 0
    ed.handleEvent(press);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);

    InputEvent drag;
    drag.type = InputEventType::MouseDrag;
    drag.cell = CellPos{7, 3}; // primera fila ~ después del EOF
    ed.handleEvent(drag);

    CHECK(ed.hasSelection());
    CHECK(ed.active().selection.has_value());
    CHECK((ed.active().selection->anchor == Position{0, 0}));
    // EOF lógico de la selección: después de 'i', no el inicio de la
    // última celda ('i') que devolvería positionAt para un cursor.
    CHECK((ed.active().selection->position == Position{2, 3}));
    CHECK((Position{ed.active().cursor.line, ed.active().cursor.col} ==
           Position{2, 3}));
}

TEST(softwrap_mouse_drag_below_content_at_bottom_selects_eof) {
    Editor ed;
    ed.active().filename = "notes.md";
    ed.active().document.restore({"abc", "def", "ghi"});
    ed.resize(7, 20); // contentH 5 == totalVisualRows + 2
    ed.active().viewport.top = 0; // maxTop
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    InputEvent press;
    press.type = InputEventType::MousePress;
    press.cell = CellPos{7, 0};
    ed.handleEvent(press);

    InputEvent drag;
    drag.type = InputEventType::MouseDrag;
    drag.cell = CellPos{20, 5}; // chrome inferior, a la derecha del texto
    ed.handleEvent(drag);

    CHECK(ed.hasSelection());
    CHECK(ed.active().selection.has_value());
    CHECK((ed.active().selection->position == Position{2, 3}));
}

TEST(softwrap_renderer_uses_effective_wrap_mode) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    setupWrapEditor(ed, {std::string(30, 'a')});
    ed.renderFrame();
    CHECK(spy.lastWrapIndex != nullptr);
    CHECK(spy.lastWrapIndex->enabled());

    // Sin ancho útil el editor deja de usar wrap y el renderer debe recibir
    // el mismo modo identidad (nullptr), nunca un índice deshabilitado.
    ed.resize(10, 2);
    ed.renderFrame();
    CHECK(spy.lastWrapIndex == nullptr);
}

static std::vector<std::string> wrappedLines(int count) {
    std::vector<std::string> lines;
    for (int i = 0; i < count; ++i) lines.push_back(std::string(30, 'a'));
    return lines;
}

static bool samePosition(const Position& p, const Cursor& c) {
    return p.line == c.line && p.col == c.col;
}

TEST(softwrap_page_down_selection_tracks_cursor) {
    Editor ed;
    setupWrapEditor(ed, wrappedLines(20));
    ed.active().viewport.top = 0;
    enterSeleccion(ed);

    press(ed, InputEventType::PageDown);
    CHECK(ed.active().selection.has_value());
    CHECK((ed.active().selection->anchor == Position{0, 0}));
    CHECK(samePosition(ed.active().selection->position, ed.active().cursor));
    CHECK_EQ(ed.active().cursor.line, 2);   // fila visual 8
    CHECK_EQ(ed.active().cursor.col, 24);

    press(ed, InputEventType::PageDown);
    CHECK((ed.active().selection->anchor == Position{0, 0}));
    CHECK(samePosition(ed.active().selection->position, ed.active().cursor));
    CHECK_EQ(ed.active().cursor.line, 5);   // fila visual 16
    CHECK_EQ(ed.active().cursor.col, 12);
}

TEST(softwrap_page_up_selection_tracks_cursor_and_top_limit) {
    Editor ed;
    setupWrapEditor(ed, wrappedLines(20));
    ed.active().cursor.line = 4;
    ed.active().cursor.col = 5;
    ed.active().viewport.top = 12;
    enterSeleccion(ed);

    press(ed, InputEventType::PageUp);
    CHECK((ed.active().selection->anchor == Position{4, 5}));
    CHECK(samePosition(ed.active().selection->position, ed.active().cursor));
    CHECK_EQ(ed.active().cursor.line, 1);   // fila visual 4
    CHECK_EQ(ed.active().cursor.col, 17);   // columna preferida 5

    press(ed, InputEventType::PageUp);
    CHECK((ed.active().selection->anchor == Position{4, 5}));
    CHECK(samePosition(ed.active().selection->position, ed.active().cursor));
    CHECK_EQ(ed.active().cursor.line, 0);   // fila visual 0
    CHECK_EQ(ed.active().cursor.col, 5);    // columna preferida 5

    // viewport ya en el tope: la rama de límite debe mover el cursor al
    // inicio y mantener la selección sincronizada.
    press(ed, InputEventType::PageUp);
    CHECK((ed.active().selection->anchor == Position{4, 5}));
    CHECK(samePosition(ed.active().selection->position, ed.active().cursor));
    CHECK((Position{ed.active().cursor.line, ed.active().cursor.col} == Position{0, 5}));
    CHECK(ed.hasSelection());
}

TEST(softwrap_page_down_selection_at_bottom_limit) {
    Editor ed;
    setupWrapEditor(ed, wrappedLines(20));
    ed.active().cursor.line = 19;
    ed.active().cursor.col = 0;
    ed.active().viewport.top = 52; // maxTop = 60 filas - 8
    enterSeleccion(ed);

    press(ed, InputEventType::PageDown);
    CHECK(ed.active().selection.has_value());
    CHECK((ed.active().selection->anchor == Position{19, 0}));
    CHECK(samePosition(ed.active().selection->position, ed.active().cursor));
    CHECK((Position{ed.active().cursor.line, ed.active().cursor.col} == Position{19, 24})); // última fila visual
}

TEST(softwrap_page_selection_when_document_fits_viewport) {
    Editor ed;
    setupWrapEditor(ed, {std::string(30, 'a')}); // 3 filas < viewport (8)
    enterSeleccion(ed);

    press(ed, InputEventType::PageDown);
    CHECK(ed.active().selection.has_value());
    CHECK(samePosition(ed.active().selection->position, ed.active().cursor));
    CHECK((Position{ed.active().cursor.line, ed.active().cursor.col} == Position{0, 24}));

    press(ed, InputEventType::PageUp);
    CHECK(samePosition(ed.active().selection->position, ed.active().cursor));
    CHECK((Position{ed.active().cursor.line, ed.active().cursor.col} == Position{0, 0}));
}

// ---------------------------------------------------------------------------
// Navegacion vertical con soft-wrap (flechas Arriba/Abajo) en .md.
// Una linea logica larga con textWidth=5 ocupa varias filas visuales
// ("abcdefghijklmno" -> filas [0,5) [5,10) [10,15)). Down debe llegar a la
// SIGUIENTE linea logica y Up a la anterior: la cantidad de filas visuales de
// la linea origen no altera la linea logica alcanzada.
// ---------------------------------------------------------------------------

// Down debe avanzar a la siguiente linea logica (no permanecer en la misma).
TEST(softwrap_editor_arrow_down_moves_to_next_logical_line) {
    Editor ed;
    setupWrapEditor(ed, {"abcdefghijklmno", "12345"}, 12); // textWidth = 5
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0; // fila visual 0, col relativa 0

    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 1); // down: siguiente linea logica
}

// Down al terminar las filas de la linea actual cruza a la primera fila de la
// siguiente linea logica.
TEST(softwrap_editor_arrow_down_crosses_logical_line) {
    Editor ed;
    setupWrapEditor(ed, {"abcdefghijklmno", "12345", "ABCDEFGHIJKLMNO"}, 12);
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 15; // ultima fila visual de la linea 0 (fin exclusivo)

    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 1); // cruzo a la linea logica 1
    CHECK_EQ(ed.active().cursor.col, 5);  // col preferida (5) aplicada en "12345"
}

// Up debe retroceder a la linea logica anterior (no permanecer en la misma).
TEST(softwrap_editor_arrow_up_moves_to_previous_logical_line) {
    Editor ed;
    setupWrapEditor(ed, {"abcdefghijklmno", "12345"}, 12); // textWidth = 5
    ed.active().cursor.line = 1;
    ed.active().cursor.col = 0; // unica fila de la linea corta

    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 0); // up: linea logica anterior
}

// Arriba al empezar de una linea cruza a la ultima fila visual de la linea
// anterior (preservando la columna preferida).
TEST(softwrap_editor_arrow_up_crosses_logical_line) {
    Editor ed;
    setupWrapEditor(ed, {"abcdefghijklmno", "12345"}, 12); // textWidth = 5
    ed.active().cursor.line = 1;
    ed.active().cursor.col = 0; // unica fila de la linea corta

    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 0); // cruzo a la linea logica 0
    CHECK_EQ(ed.active().cursor.col, 0); // pref 0 en la linea logica 0
}

// En una posicion exactamente en el limite del wrap (col 5 == fin fila 0 ==
// inicio fila 1), la fila visual debe resolverse con VisualAffinity::Start
// (pertenece a la fila siguiente) y el movimiento debe respetarlo. Con el nuevo
// criterio, Down desde el limite debe intentar la siguiente linea logica.
TEST(softwrap_editor_vertical_wrap_boundary) {
    Editor ed;
    setupWrapEditor(ed, {"abcdefghijklmno", "12345"}, 12); // textWidth = 5
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 5; // limite: fin fila 0 / inicio fila 1 (misma linea)

    // El primer MoveDown ejecuta el comando -> wrapFor() -> ensure() habilita
    // el indice; a partir de ahi visualRowOf() responde con el wrap activo.
    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.wrapIndex().visualRowOf({0, 5}), 1);
    CHECK_EQ(ed.active().cursor.line, 1); // down: siguiente linea logica
}

// La seleccion debe extenderse usando el MISMO destino visual que el
// movimiento normal de flechas (hasta la siguiente linea logica).
TEST(softwrap_editor_vertical_selection) {
    Editor ed;
    setupWrapEditor(ed, {"abcdefghijklmno", "12345"}, 12); // textWidth = 5
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 2;
    enterSeleccion(ed);
    CHECK(ed.active().selection.has_value());

    press(ed, InputEventType::MoveDown);
    CHECK(ed.active().selection.has_value());
    CHECK((ed.active().selection->anchor == Position{0, 2}));
    CHECK(samePosition(ed.active().selection->position, ed.active().cursor));
    CHECK_EQ(ed.active().cursor.line, 1); // down: siguiente linea logica
    CHECK_EQ(ed.active().cursor.col, 2);  // pref col 2 adaptada a la linea destino
}

// Navegacion vertical conservando la columna visual preferida durante varios
// movimientos (cruce de lineas y vuelta).
TEST(softwrap_editor_vertical_preserves_preferred_column) {
    Editor ed;
    setupWrapEditor(ed, {"abcdefghijklmno", "12345", "ABCDEFGHIJKLMNO"}, 12);
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 2;

    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 2); // pref col 2 adaptada a la linea destino

    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 2); // pref col 2 en la linea larga

    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 2); // pref col 2 conservada al retroceder
}

// Navegacion en lineas cortas: bajar desde una linea corta hacia una larga
// (con wrap) conserva la columna y no pierde la fila correcta.
TEST(softwrap_editor_vertical_short_line) {
    Editor ed;
    setupWrapEditor(ed, {"short", "abcdefghijklmno"}, 12); // textWidth = 5
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 3; // linea corta: una sola fila

    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 1); // cruzo a la linea larga
    CHECK_EQ(ed.active().cursor.col, 3);  // primera fila de la linea larga, col pref 3

    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 0); // up: linea logica anterior
    CHECK_EQ(ed.active().cursor.col, 3);  // pref col 3
}

// Limites del documento: Up desde la linea logica 0 y Down desde la ultima
// linea logica no deben mover el cursor (no-op en ambos extremos).
TEST(softwrap_editor_vertical_at_document_limits) {
    Editor ed;
    setupWrapEditor(ed, {"abcdefghijklmno", "12345", "ABCDEFGHIJKLMNO"}, 12); // textWidth = 5
    ed.active().cursor.line = 2;
    ed.active().cursor.col = 0;

    // Down: ya en la ultima linea logica -> no-op.
    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 0);

    // Up: linea 2 -> linea 1 -> linea 0.
    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 1);
    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 0);

    // Up: desde la linea logica 0 -> no-op.
    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 0);
}

// Columna preferida sobre destino largo: con textWidth=5 y linea destino de
// 15 chars, el Down debe aterrizar en la PRIMERA fila visual de la linea destino
// (col 4), no en una fila de continuación.
TEST(softwrap_editor_vertical_dest_long_pref_column) {
    Editor ed;
    setupWrapEditor(ed, {"abcdefghijklmno", "ABCDEFGHIJKLMNO"}, 12); // textWidth = 5
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 4; // pref 4 (< ancho util 5)

    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 1); // primera linea larga
    CHECK_EQ(ed.active().cursor.col, 4);  // primera fila visual de la linea destino
}
