#include "app/Editor.h"
#include "document/Document.h"
#include "layout/Layout.h"
#include "layout/ScreenToCursor.h"
#include "layout/Viewport.h"
#include "platform/Event.h"
#include "platform/tty/Terminal.h"
#include "test_framework.h"

namespace {

Event mousePress(int col, int row) {
    Event e;
    e.type = EventType::MousePress;
    e.mouseCol = col;
    e.mouseRow = row;
    return e;
}

Event mouseDrag(int col, int row) {
    Event e;
    e.type = EventType::MouseDrag;
    e.mouseCol = col;
    e.mouseRow = row;
    return e;
}

Event mouseRelease(int col, int row) {
    Event e;
    e.type = EventType::MouseRelease;
    e.mouseCol = col;
    e.mouseRow = row;
    return e;
}

Event insertCh(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

// Geometria de test: viewport chico, gutter 3 (docs chicos), content en (0,0).
// mouseRow = relRow+1, mouseCol = relCol+1.
Layout testLayout(const Viewport& vp) {
    return computeLayout(vp.height + kStatusBarRows, vp.width);
}

Viewport testViewport(int height = 10, int width = 20) {
    Viewport vp;
    vp.top = 0;
    vp.left = 0;
    vp.height = height;
    vp.width = width;
    return vp;
}

} // namespace

// --- Parser SGR ---

TEST(mouse_sgr_left_press_basic) {
    Event e;
    Terminal::parseMouseSgr("[<0;10;5M", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MousePress));
    CHECK_EQ(e.mouseCol, 10);
    CHECK_EQ(e.mouseRow, 5);
}

TEST(mouse_sgr_left_press_with_modifiers) {
    // code == 0 con Shift(4)/Alt(8)/Ctrl(16): los modificadores no alteran
    // el press izquierdo, solo viajan enmascarados en Cb.
    for (int cb : {4, 8, 16}) {
        Event e;
        std::string seq = "[<" + std::to_string(cb) + ";10;5M";
        Terminal::parseMouseSgr(seq, e);
        CHECK_EQ(static_cast<int>(e.type),
                 static_cast<int>(EventType::MousePress));
        CHECK_EQ(e.mouseCol, 10);
        CHECK_EQ(e.mouseRow, 5);
    }
}

TEST(mouse_sgr_release_left) {
    Event e;
    Terminal::parseMouseSgr("[<3;10;5m", e);
    CHECK_EQ(static_cast<int>(e.type),
             static_cast<int>(EventType::MouseRelease));
    CHECK_EQ(e.mouseCol, 10);
    CHECK_EQ(e.mouseRow, 5);
}

TEST(mouse_sgr_release_nonleft_ignored) {
    Event e;
    Terminal::parseMouseSgr("[<0;10;5m", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
}

TEST(mouse_sgr_left_drag_basic) {
    Event e;
    Terminal::parseMouseSgr("[<32;10;5M", e);
    CHECK_EQ(static_cast<int>(e.type),
             static_cast<int>(EventType::MouseDrag));
    CHECK_EQ(e.mouseCol, 10);
    CHECK_EQ(e.mouseRow, 5);
}

TEST(mouse_sgr_left_drag_with_modifiers) {
    // 32 + Shift(4)/Alt(8)/Ctrl(16) sigue siendo drag izquierdo.
    for (int cb : {36, 40, 48}) {
        Event e;
        std::string seq = "[<" + std::to_string(cb) + ";10;5M";
        Terminal::parseMouseSgr(seq, e);
        CHECK_EQ(static_cast<int>(e.type),
                 static_cast<int>(EventType::MouseDrag));
    }
}

TEST(mouse_sgr_left_release_with_modifiers) {
    // 3 + mods + 'm' sigue siendo release (7=3+Shift, 11=3+Alt, 19=3+Ctrl).
    for (int cb : {7, 11, 19}) {
        Event e;
        std::string seq = "[<" + std::to_string(cb) + ";10;5m";
        Terminal::parseMouseSgr(seq, e);
        CHECK_EQ(static_cast<int>(e.type),
                 static_cast<int>(EventType::MouseRelease));
    }
}

TEST(mouse_sgr_drag_middle_right_ignored) {
    Event e;
    Terminal::parseMouseSgr("[<33;10;5M", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
    Terminal::parseMouseSgr("[<1;10;5M", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
    Terminal::parseMouseSgr("[<2;10;5M", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
}

TEST(mouse_sgr_malformed_no_click) {
    Event e;
    Terminal::parseMouseSgr("[<0;10M", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
    Terminal::parseMouseSgr("[<M", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
}

// --- Mapper puro ---

TEST(mouse_map_text_click) {
    Document d;
    d.restore({"hello", "world"});
    Viewport vp = testViewport();
    Layout lo = testLayout(vp);
    // gutter=3: texto de fila 0 col visual 1 => relCol=4 => mouseCol=5
    auto p = screenToCursor(1, 5, lo, vp, d);
    CHECK(p.has_value());
    CHECK_EQ(p->line, 0);
    CHECK_EQ(p->col, 1);
}

TEST(mouse_map_gutter_goes_to_start) {
    Document d;
    d.restore({"hello"});
    Viewport vp = testViewport();
    Layout lo = testLayout(vp);
    auto p = screenToCursor(1, 1, lo, vp, d); // dentro del gutter
    CHECK(p.has_value());
    CHECK_EQ(p->line, 0);
    CHECK_EQ(p->col, 0);
}

TEST(mouse_map_gutter_text_border) {
    Document d;
    d.restore({"hello"});
    Viewport vp = testViewport();
    Layout lo = testLayout(vp);
    // gutterW=3: relCol 2 = ultima de gutter, relCol 3 = primera de texto
    auto g = screenToCursor(1, 3, lo, vp, d);
    CHECK(g.has_value());
    CHECK_EQ(g->col, 0);
    auto t = screenToCursor(1, 4, lo, vp, d);
    CHECK(t.has_value());
    CHECK_EQ(t->col, 0);
    auto t1 = screenToCursor(1, 5, lo, vp, d);
    CHECK(t1.has_value());
    CHECK_EQ(t1->col, 1);
}

TEST(mouse_map_past_eol_clamps) {
    Document d;
    d.restore({"hi"});
    Viewport vp = testViewport();
    Layout lo = testLayout(vp);
    auto p = screenToCursor(1, 50, lo, vp, d); // muy a la derecha
    CHECK(p.has_value());
    CHECK_EQ(p->line, 0);
    CHECK_EQ(p->col, 2);
}

TEST(mouse_map_wide_char_both_cells) {
    Document d;
    d.restore({"abc\xF0\x9F\x98\x80" "def"}); // abc😀def, emoji = 2 celdas
    Viewport vp = testViewport();
    Layout lo = testLayout(vp);
    // gutter=3: 'a'=relCol3, 'b'=4, 'c'=5, emoji celdas=6,7, 'd'=8
    auto first = screenToCursor(1, 1 + 6, lo, vp, d);
    auto second = screenToCursor(1, 1 + 7, lo, vp, d);
    CHECK(first.has_value());
    CHECK(second.has_value());
    CHECK_EQ(first->col, 3); // byte inicio del emoji
    CHECK_EQ(second->col, 3);
}

TEST(mouse_map_tab_cells) {
    Document d;
    d.restore({"\tx"});
    Viewport vp = testViewport();
    Layout lo = testLayout(vp);
    // TAB = 4 celdas visuales (relCol 3..6), 'x' en visual 4
    for (int c = 3; c <= 6; ++c) {
        auto p = screenToCursor(1, 1 + c, lo, vp, d);
        CHECK(p.has_value());
        CHECK_EQ(p->col, 0); // dentro del tab => byte 0
    }
    auto x = screenToCursor(1, 1 + 7, lo, vp, d);
    CHECK(x.has_value());
    CHECK_EQ(x->col, 1);
}

TEST(mouse_map_with_scroll_offsets) {
    Document d;
    d.restore({"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7", "l8", "l9",
               "abcdefghij"});
    Viewport vp = testViewport();
    vp.top = 10;
    vp.left = 2;
    Layout lo = testLayout(vp);
    // fila visible 0 = docLine 10; visual 0 => byte 2 ("c")
    auto p = screenToCursor(1, 4, lo, vp, d);
    CHECK(p.has_value());
    CHECK_EQ(p->line, 10);
    CHECK_EQ(p->col, 2);
}

TEST(mouse_map_first_last_visible_row) {
    Document d;
    d.restore({"a", "b", "c", "d", "e"});
    Viewport vp = testViewport(4, 20);
    Layout lo = testLayout(vp);
    auto first = screenToCursor(1, 4, lo, vp, d);
    CHECK(first.has_value());
    CHECK_EQ(first->line, 0);
    auto last = screenToCursor(4, 4, lo, vp, d);
    CHECK(last.has_value());
    CHECK_EQ(last->line, 3);
}

TEST(mouse_map_below_doc_and_statusbar_ignored) {
    Document d;
    d.restore({"a", "b"});
    Viewport vp = testViewport(10, 20);
    Layout lo = testLayout(vp);
    // fila visible 2 = docLine 2 >= lineCount => nullopt (fila `~`)
    auto below = screenToCursor(3, 10, lo, vp, d);
    CHECK(!below.has_value());
    // status bar: primera fila fuera de content (relRow=10 => mouseRow=11)
    auto bar = screenToCursor(11, 10, lo, vp, d);
    CHECK(!bar.has_value());
}

// --- Editor ---

TEST(mouse_editor_navegacion_moves) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.processEventForTesting(mousePress(5, 2)); // fila 1, visual 1
    CHECK_EQ(b.cursor.line, 1);
    CHECK_EQ(b.cursor.col, 1);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
}

TEST(mouse_editor_seleccion_cancels_and_moves) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    ed.processEventForTesting(insertCh('s')); // entra a Seleccion
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    // Press solo arma: todavia no cancela ni cambia de modo.
    ed.processEventForTesting(mousePress(5, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.cursor.line, 1);
    CHECK_EQ(b.cursor.col, 1);
    // Release sin drag: conducta historica de click (cancela a Navegacion).
    ed.processEventForTesting(mouseRelease(5, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK(!b.selection.has_value());
    CHECK(!b.selectAllActive);
    CHECK_EQ(b.cursor.line, 1);
    CHECK_EQ(b.cursor.col, 1);
}

TEST(mouse_editor_modal_ignored) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    Event prefix;
    prefix.type = EventType::Prefix;
    ed.processEventForTesting(prefix);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Prefix));
    ed.processEventForTesting(mousePress(5, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Prefix));
    CHECK_EQ(b.cursor.line, 0);
    CHECK_EQ(b.cursor.col, 0);
}

// --- Gesto press/drag/release ---

TEST(mouse_press_arms_without_mode_change) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.processEventForTesting(mousePress(5, 2));
    // Mueve el cursor pero no entra a Seleccion ni crea rango.
    CHECK_EQ(b.cursor.line, 1);
    CHECK_EQ(b.cursor.col, 1);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK(!b.selection.has_value());
}

TEST(mouse_click_navegacion_preserves_historic_behavior) {
    // Click simple en Navegacion (press + release sin drag): preserva la
    // conducta historica -> sigue en Navegacion, cursor movido, sin rango.
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.processEventForTesting(mousePress(5, 2)); // fila 1, visual 1
    ed.processEventForTesting(mouseRelease(5, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK_EQ(b.cursor.line, 1);
    CHECK_EQ(b.cursor.col, 1);
    CHECK(!b.selection.has_value());
    CHECK(!ed.hasSelection());
}

TEST(mouse_drag_from_navegacion_enters_seleccion) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.processEventForTesting(mousePress(4, 1)); // fila 0, visual 0
    ed.processEventForTesting(mouseDrag(6, 2));  // fila 1, visual 2
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK(b.selection.has_value());
    CHECK_EQ(b.selection->anchor.line, 0);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.cursor.line, 1);
    CHECK_EQ(b.cursor.col, 2);
    CHECK(ed.hasSelection());
}

TEST(mouse_drag_upwards_selects) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world", "test"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 2;
    b.cursor.col = 0;
    ed.processEventForTesting(mousePress(4, 3)); // fila 2
    ed.processEventForTesting(mouseDrag(4, 1));  // fila 0 (hacia arriba)
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->end.line, 2);
}

TEST(mouse_drag_resets_existing_selection) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world", "test"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    ed.processEventForTesting(insertCh('s')); // modo Seleccion
    ed.processEventForTesting(mousePress(4, 1));
    ed.processEventForTesting(mouseDrag(6, 1));
    ed.processEventForTesting(mouseRelease(6, 1)); // primera seleccion
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK(b.selection.has_value());
    // Nuevo gesto: el rango anterior se resetea con el nuevo anchor.
    ed.processEventForTesting(mousePress(4, 3)); // fila 2
    // Todavia conserva el rango viejo hasta el primer drag.
    CHECK(b.selection.has_value());
    ed.processEventForTesting(mouseDrag(5, 3));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.selection->anchor.line, 2);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.cursor.line, 2);
    CHECK_EQ(b.cursor.col, 1);
}

TEST(mouse_release_after_drag_stays_in_seleccion) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.processEventForTesting(mousePress(4, 1));
    ed.processEventForTesting(mouseDrag(6, 2));
    ed.processEventForTesting(mouseRelease(6, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK(b.selection.has_value());
    CHECK(ed.hasSelection());
}

TEST(mouse_drag_without_press_ignored) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.processEventForTesting(mouseDrag(6, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK(!b.selection.has_value());
    CHECK_EQ(b.cursor.line, 0);
    CHECK_EQ(b.cursor.col, 0);
}

TEST(mouse_drag_autoscroll_down) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7"});
    b.viewport.height = 4;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.processEventForTesting(mousePress(4, 1)); // fila 0
    CHECK_EQ(b.viewport.top, 0);
    ed.processEventForTesting(mouseDrag(4, 4)); // ultima fila visible
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.viewport.top, 1); // scrolleo 1 linea
    // La fila borde ahora muestra la linea recien revelada (1+3).
    CHECK_EQ(b.cursor.line, 4);
}

TEST(mouse_drag_autoscroll_up) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7"});
    b.viewport.height = 4;
    b.viewport.width = 20;
    b.viewport.top = 4;
    b.viewport.left = 0;
    b.cursor.line = 7;
    b.cursor.col = 0;
    ed.processEventForTesting(mousePress(4, 4)); // fila doc 7
    CHECK_EQ(b.viewport.top, 4);
    ed.processEventForTesting(mouseDrag(4, 1)); // primera fila visible
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.viewport.top, 3); // scrolleo 1 linea hacia arriba
    CHECK_EQ(b.cursor.line, 3);
}

TEST(mouse_drag_autoscroll_horizontal) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"0123456789ABCDEFGHIJ0123456789"});
    b.viewport.height = 10;
    b.viewport.width = 12; // gutter 3 + 9 de texto
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.processEventForTesting(mousePress(4, 1)); // visual 0
    const int lastCol = 12; // ultima celda visible (1-based)
    ed.processEventForTesting(mouseDrag(lastCol, 1));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.viewport.left, 1); // 1 celda de scroll horizontal
    // visTarget = left(1) + (relCol(11) - gutter(3)) = 9, linea ASCII.
    CHECK_EQ(b.cursor.line, 0);
    CHECK_EQ(b.cursor.col, 9);
    CHECK(b.selection.has_value());
    CHECK_EQ(b.selection->anchor.line, 0);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 0);
    CHECK_EQ(b.selection->position.col, 9);
    CHECK(ed.hasSelection());
}

TEST(mouse_drag_statusbar_scrolls_when_content_below) {
    // Gesto sobre la statusbar (relRow >= h) con documento debajo:
    // scrollea 1 linea y extiende la seleccion, nunca nullopt silencioso.
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7"});
    b.viewport.height = 4;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.processEventForTesting(mousePress(4, 1)); // fila doc 0
    ed.processEventForTesting(mouseDrag(4, 5));  // statusbar (relRow=4)
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.viewport.top, 1); // hubo scroll, no se ignoro
    CHECK_EQ(b.cursor.line, 4);  // borde inferior revelado (1+3)
    CHECK(b.selection.has_value());
    CHECK(ed.hasSelection());
}

TEST(mouse_drag_statusbar_at_maxTop_keeps_selection) {
    // Gesto sobre la statusbar ya en el ultimo viewport (top == maxTop):
    // no hay nada que desplazar; el evento se ignora y la seleccion
    // vigente queda intacta (cursor, rango y viewport sin cambios).
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7"});
    b.viewport.height = 4;
    b.viewport.width = 20;
    b.viewport.top = 4; // maxTop: lineas 4..7 visibles
    b.viewport.left = 0;
    b.cursor.line = 7;
    b.cursor.col = 0;
    ed.processEventForTesting(mousePress(4, 4)); // fila doc 7, arma
    ed.processEventForTesting(mouseDrag(4, 3));  // fila doc 6, inicia rango
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.cursor.line, 6);
    ed.processEventForTesting(mouseDrag(4, 5)); // statusbar, sin scroll
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.viewport.top, 4); // sin cambios
    CHECK_EQ(b.cursor.line, 6);  // sin cambios
    CHECK_EQ(b.cursor.col, 0);
    CHECK(b.selection.has_value());
    CHECK_EQ(b.selection->anchor.line, 7);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 6);
    CHECK_EQ(b.selection->position.col, 0);
    CHECK(ed.hasSelection());
}

TEST(mouse_drag_multiple_anchor_fixed_position_follows) {
    // Gesto real: press + varios drags (incluye cambio de direccion) +
    // release. El anchor permanece fijo en el press y position sigue al
    // ultimo drag; el Selection no se recrea en cada evento.
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello world foo", "second line here", "third line here"});
    b.viewport.height = 10;
    b.viewport.width = 40;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    // gutter=3: mouseCol = 1 + 3 + visual.
    ed.processEventForTesting(mousePress(4, 1)); // anchor (0,0)
    ed.processEventForTesting(mouseDrag(6, 2));  // (1,2)
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.selection->anchor.line, 0);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 1);
    CHECK_EQ(b.selection->position.col, 2);
    ed.processEventForTesting(mouseDrag(8, 3)); // (2,4)
    CHECK_EQ(b.selection->anchor.line, 0);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 2);
    CHECK_EQ(b.selection->position.col, 4);
    {
        auto sel = ed.selection();
        CHECK(sel.has_value());
        CHECK_EQ(sel->start.line, 0);
        CHECK_EQ(sel->start.col, 0);
        CHECK_EQ(sel->end.line, 2);
        CHECK_EQ(sel->end.col, 4);
    }
    ed.processEventForTesting(mouseDrag(5, 1)); // vuelta a (0,1)
    CHECK_EQ(b.selection->anchor.line, 0);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 0);
    CHECK_EQ(b.selection->position.col, 1);
    CHECK_EQ(b.cursor.line, 0);
    CHECK_EQ(b.cursor.col, 1);
    ed.processEventForTesting(mouseRelease(5, 1));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.selection->anchor.line, 0);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 0);
    CHECK_EQ(b.selection->position.col, 1);
    CHECK(ed.hasSelection());
}
