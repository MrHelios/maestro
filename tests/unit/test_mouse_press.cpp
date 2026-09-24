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

TEST(mouse_sgr_release_ignored) {
    Event e;
    Terminal::parseMouseSgr("[<0;10;5m", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
}

TEST(mouse_sgr_wheel_release_ignored) {
    Event e;
    Terminal::parseMouseSgr("[<64;10;5m", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
}

TEST(mouse_sgr_drag_middle_right_ignored) {
    Event e;
    Terminal::parseMouseSgr("[<32;10;5M", e);
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
    ed.processEventForTesting(mousePress(5, 2));
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
