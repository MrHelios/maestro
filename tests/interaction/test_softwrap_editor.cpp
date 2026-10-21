#include <string>
#include <vector>

#include "test_support.h"
#include "helpers/spy_screen_renderer.h"

namespace {

void setupWrapEditor(Editor& ed, const std::vector<std::string>& lines,
                     int cols = 15) {
    ed.active().filename = "notes.md";
    ed.active().document.restore(lines);
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.resize(10, cols);
}

} // namespace

// El test usa textWidth = 12 (15 columnas - gutter 3). Una línea de 30
// caracteres ocupa tres filas visuales (12 + 12 + 6).
TEST(softwrap_vertical_keeps_preferred_column) {
    Editor ed;
    setupWrapEditor(ed, {
        std::string(30, 'a'),
        "short",
        std::string(30, 'b'),
    });
    ed.active().cursor.col = 20; // fila visual 1, columna relativa 8

    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 29); // fila corta: se limita

    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 4);  // "short" también es corta

    // La preferencia original (8) sobrevive a las filas cortas.
    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 8);
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

    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 8); // primera fila visual de la línea 2

    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 4);

    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 29);

    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 20); // preferencia 8 en la fila 1
}

TEST(softwrap_home_end_stay_inside_intermediate_row) {
    Editor ed;
    setupWrapEditor(ed, {std::string(30, 'a')});
    ed.active().cursor.col = 18; // fila visual 1

    press(ed, InputEventType::MoveHome);
    CHECK_EQ(ed.active().cursor.col, 12); // inicio de la fila 1

    press(ed, InputEventType::MoveEnd);
    CHECK_EQ(ed.active().cursor.col, 23); // fin de la fila 1

    press(ed, InputEventType::MoveDown);   // a la fila 2 (última)
    CHECK_EQ(ed.active().cursor.col, 29);

    press(ed, InputEventType::MoveHome);
    CHECK_EQ(ed.active().cursor.col, 24); // inicio de la fila 2

    press(ed, InputEventType::MoveEnd);
    CHECK_EQ(ed.active().cursor.col, 29); // fin de la última fila
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
    press.cell = CellPos{3, 0}; // gutter 3, visual 0 -> línea 0, col 0
    ed.handleEvent(press);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);

    InputEvent drag;
    drag.type = InputEventType::MouseDrag;
    drag.cell = CellPos{3, 3}; // primera fila ~ después del EOF
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
    press.cell = CellPos{3, 0};
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
