#include <chrono>

#include "test_framework.h"
#include "app/Editor.h"
#include "document/Document.h"
#include "layout/Layout.h"
#include "layout/ScreenToCursor.h"
#include "layout/Viewport.h"
#include "platform/CellPos.h"
#include "platform/InputEvent.h"
#include "platform/MouseEvent.h"
#include "platform/tty/Terminal.h"

namespace {

InputEvent mousePress(int col, int row) {
    return makeMousePressEvent(CellPos{col - 1, row - 1});
}

InputEvent mouseDrag(int col, int row) {
    return makeMouseDragEvent(CellPos{col - 1, row - 1});
}

InputEvent mouseRelease(int col, int row) {
    return makeMouseReleaseEvent(CellPos{col - 1, row - 1});
}

InputEvent insertCh(char c) {
    InputEvent e;
    e.type = InputEventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

// Geometria de test: 
// viewport chico, gutter 7 (docs chicos), content en (0,0).
// CellPos 0-based: celda = (relCol, relRow).
Layout testLayout(const Viewport& vp) {
    return computeLayout(vp.height + kChromeRows, vp.width);
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
// Select Graphic Rendition

TEST(mouse_sgr_right_press_basic) {
    InputEvent e;
    Terminal::parseMouseSgr("[<2;20;8M", e);

    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MousePress));
    CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Right));
    CHECK_EQ(e.cell.col, 19);
    CHECK_EQ(e.cell.row, 7);
}

TEST(mouse_sgr_left_press_basic) {
    InputEvent e;
    Terminal::parseMouseSgr("[<0;10;5M", e);
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MousePress));
    CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Left));
    CHECK_EQ(e.cell.col, 9);
    CHECK_EQ(e.cell.row, 4);
}

// con Shift(4)/Alt(8)/Ctrl(16):
// los modificadores no alteran el press izquierdo
TEST(mouse_sgr_left_press_with_modifiers) {
    for (int cb : {4, 8, 16}) {
        InputEvent e;
        
        std::string seq = "[<" + std::to_string(cb) + ";10;5M";
        Terminal::parseMouseSgr(seq, e);
        
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MousePress));
        CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Left));
        CHECK_EQ(e.cell.col, 9);
        CHECK_EQ(e.cell.row, 4);
    }
}

// con Shift(4)/Alt(8)/Ctrl(16):
// los modificadores no alteran el press derecho
TEST(mouse_sgr_right_press_with_modifiers) {
    for (int mod : {4, 8, 16}) {
        InputEvent e;

        int cb = 2 + mod;
        std::string seq = "[<" + std::to_string(cb) + ";20;8M";
        Terminal::parseMouseSgr(seq, e);

        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MousePress));
        CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Right));
        CHECK_EQ(e.cell.col, 19);
        CHECK_EQ(e.cell.row, 7);
    }
}

TEST(mouse_sgr_release_left) {
    InputEvent e;
    Terminal::parseMouseSgr("[<3;10;5m", e);
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MouseRelease));
    CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Left));
    CHECK_EQ(e.cell.col, 9);
    CHECK_EQ(e.cell.row, 4);
}

TEST(mouse_sgr_release_right) {
    InputEvent e;
    Terminal::parseMouseSgr("[<2;20;8m", e);

    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MouseRelease));
    CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Right));
    CHECK_EQ(e.cell.col, 19);
    CHECK_EQ(e.cell.row, 7);
}

TEST(mouse_sgr_left_drag_basic) {
    InputEvent e;
    Terminal::parseMouseSgr("[<32;10;5M", e);
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MouseDrag));
    CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Left));
    CHECK_EQ(e.cell.col, 9);
    CHECK_EQ(e.cell.row, 4);
}

TEST(mouse_sgr_right_drag_basic) {
    InputEvent e;
    Terminal::parseMouseSgr("[<34;20;8M", e);

    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MouseDrag));
    CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Right));
    CHECK_EQ(e.cell.col, 19);
    CHECK_EQ(e.cell.row, 7);
}

// 32 + Shift(4)/Alt(8)/Ctrl(16) sigue siendo drag izquierdo.
TEST(mouse_sgr_left_drag_with_modifiers) {
    for (int cb : {36, 40, 48}) {
        InputEvent e;
        std::string seq = "[<" + std::to_string(cb) + ";10;5M";
        Terminal::parseMouseSgr(seq, e);
        
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MouseDrag));
        CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Left));
        CHECK_EQ(e.cell.col, 9);
        CHECK_EQ(e.cell.row, 4);
    }
}

// 32 + Right(2) + Shift(4)/Alt(8)/Ctrl(16) sigue siendo drag derecho.
// 38 = 32+2+4, 42 = 32+2+8, 50 = 32+2+16
TEST(mouse_sgr_right_drag_with_modifiers_loop) {
    for (int cb : {38, 42, 50}) {
        InputEvent e;
        std::string seq = "[<" + std::to_string(cb) + ";20;8M";
        Terminal::parseMouseSgr(seq, e);

        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MouseDrag));
        CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Right));
        CHECK_EQ(e.cell.col, 19);
        CHECK_EQ(e.cell.row, 7);
    }
}

// 3 + mods + 'm' sigue siendo release (7=3+Shift, 11=3+Alt, 19=3+Ctrl).
TEST(mouse_sgr_left_release_with_modifiers) {
    for (int cb : {7, 11, 19}) {
        InputEvent e;
        std::string seq = "[<" + std::to_string(cb) + ";10;5m";
        Terminal::parseMouseSgr(seq, e);
        
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MouseRelease));
        CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Left));
        CHECK_EQ(e.cell.col, 9);
        CHECK_EQ(e.cell.row, 4);
    }
}

// Release derecho + mods + 'm' sigue siendo release.
// 6 = 2+Shift, 10 = 2+Alt, 18 = 2+Ctrl
TEST(mouse_sgr_right_release_with_modifiers) {
    for (int cb : {6, 10, 18}) {
        InputEvent e;
        std::string seq = "[<" + std::to_string(cb) + ";20;8m";
        Terminal::parseMouseSgr(seq, e);

        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MouseRelease));
        CHECK_EQ(static_cast<int>(e.button), static_cast<int>(MouseButton::Right));
        CHECK_EQ(e.cell.col, 19);
        CHECK_EQ(e.cell.row, 7);
    }
}

TEST(mouse_sgr_malformed_no_click) {
    InputEvent e;

    // Faltan campos: solo 2 en vez de 3 (Cb;Cx, sin Cy).
    Terminal::parseMouseSgr("[<0;10M", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));

    // Sin campos: nada entre '<' y 'M'.
    Terminal::parseMouseSgr("[<M", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
}

TEST(mouse_sgr_malformed_variants) {
    // Campo no numérico: 'abc' no es un código de botón válido.
    {
        InputEvent e;
        Terminal::parseMouseSgr("[<abc;10;5M", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
    }
    // Campos de más: SGR define exactamente 3.
    {
        InputEvent e;
        Terminal::parseMouseSgr("[<0;10;5;7M", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
    }
    // Campo vacío en el medio: ';;' deja la columna sin valor.
    {
        InputEvent e;
        Terminal::parseMouseSgr("[<0;;5M", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
    }
    // Terminador inválido: SGR solo acepta 'M' (press) o 'm' (release).
    {
        InputEvent e;
        Terminal::parseMouseSgr("[<0;10;5X", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
    }
    // Sin terminador: la secuencia quedó cortada.
    {
        InputEvent e;
        Terminal::parseMouseSgr("[<0;10;5", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
    }
    // Falta el '<' que identifica el modo SGR.
    {
        InputEvent e;
        Terminal::parseMouseSgr("[0;10;5M", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
    }
    // Coordenada cero: el protocolo cuenta desde 1, así que 0 daría -1 tras la conversión.
    {
        InputEvent e;
        Terminal::parseMouseSgr("[<0;0;5M", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
    }
    // Número negativo.
    {
        InputEvent e;
        Terminal::parseMouseSgr("[<0;-3;5M", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
    }
    // Cadena vacía.
    {
        InputEvent e;
        Terminal::parseMouseSgr("", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
    }
}

// --- Mapper puro ---

// un click en una fila y columna validad
// hay que tener en cuenta el gutter
TEST(mouse_map_text_click) {
    Document d;
    d.restore({"hello", "world"});
    
    Viewport vp = testViewport();
    Layout lo = testLayout(vp);
    
    auto p = screenToCursor(CellPos{8, 0}, lo, vp, d);
    CHECK(p.has_value());
    CHECK_EQ(p->line, 0);
    CHECK_EQ(p->col, 1);
}

// un click dentro del gutter
// mueve el cursor a la posicion inicial
TEST(mouse_map_gutter_goes_to_start) {
    Document d;
    d.restore({"hello"});
    
    Viewport vp = testViewport();
    Layout lo = testLayout(vp);
    
    auto p = screenToCursor(CellPos{0, 0}, lo, vp, d);
    CHECK(p.has_value());
    CHECK_EQ(p->line, 0);
    CHECK_EQ(p->col, 0);
}

TEST(mouse_map_gutter_text_border) {
    Document d;
    d.restore({"hello"});
    
    Viewport vp = testViewport();
    Layout lo = testLayout(vp);
    
    // teniendo en cuenta que el gutter mide 7
    // el cursor quedara en posicion (0,0)
    for(int i=0; i<7; i++) {
        auto g = screenToCursor(CellPos{i, 0}, lo, vp, d);
        CHECK(g.has_value());
        CHECK_EQ(g->col, 0);
        CHECK_EQ(g->line, 0);
    }
}

// click a la derecha del último carácter lleva el cursor al final de la línea
TEST(mouse_map_past_eol_clamps) {
    Document d;
    d.restore({"hi"});

    Viewport vp = testViewport();
    Layout lo = testLayout(vp);

    const int textStart = 7;               // ancho del gutter
    const int eol = textStart + 2;         // primera celda después de "hi"

    for (int c = eol; c < eol + 7; c++) {
        auto p = screenToCursor(CellPos{c, 0}, lo, vp, d);  // {col, fila}
        CHECK(p.has_value());
        CHECK_EQ(p->line, 0);
        CHECK_EQ(p->col, 2);   // posición justo después de 'i'
    }
}

// aca hacemos click en un emoji que ocupa dos posiciones
// pero la representacion real es una
TEST(mouse_map_wide_char_both_cells) {
    Document d;
    d.restore({"abc\xF0\x9F\x98\x80" "def"}); // abc😀def, emoji = 2 celdas
    
    Viewport vp = testViewport();
    Layout lo = testLayout(vp);
    
    // gutter=7: 'a'=7, 'b'=8, 'c'=9, 
    // emoji celdas=10,11, 
    // 'd'=12
    auto first = screenToCursor(CellPos{10, 0}, lo, vp, d);
    CHECK(first.has_value());
    CHECK_EQ(first->col, 3); // byte inicio del emoji
    
    auto second = screenToCursor(CellPos{11, 0}, lo, vp, d);    
    CHECK(second.has_value());
    CHECK_EQ(second->col, 3);
}

// verificamos el caso del \t que debe ocupar 4 espacio
// pero visualmente 1
TEST(mouse_map_tab_cells) {
    Document d;
    d.restore({"\tx"});
    
    Viewport vp = testViewport();
    Layout lo = testLayout(vp);
    
    // TAB = 4 celdas visuales (relCol 7..10), 'x' en visual 4
    for (int c = 7; c <= 10; ++c) {
        auto p = screenToCursor(CellPos{c, 0}, lo, vp, d);
        CHECK(p.has_value());
        CHECK_EQ(p->col, 0); // dentro del tab => byte 0
    }
    
    auto x = screenToCursor(CellPos{11, 0}, lo, vp, d);
    CHECK(x.has_value());
    CHECK_EQ(x->col, 1);
}

TEST(mouse_map_moving_viewport) {
    Document d;
    d.restore({"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7", "l8", "l9", "abcdefghij"});

    Viewport vp = testViewport();
    // movemos el viewport, no sale de (0,0)
    vp.top = 10;   // la fila de pantalla 0 muestra la línea 10 del documento
    vp.left = 2;   // scroll horizontal: la primera columna visible es el byte 2
    Layout lo = testLayout(vp);

    // CellPos{7, 0}: fila 0 de pantalla, y col 7 = primera celda de texto
    // (después del gutter de 7 columnas, si testLayout lo define así).
    // Equivale a SGR (Cx=8, Cy=1) tras restar 1.
    auto p = screenToCursor(CellPos{7, 0}, lo, vp, d);
    CHECK(p.has_value());
    CHECK_EQ(p->line, 10);  // vp.top + fila 0
    CHECK_EQ(p->col, 2);    // visual 0 + vp.left → 'c'
}

TEST(mouse_map_first_last_visible_row) {
    Document d;
    d.restore({"a", "b", "c", "d", "e"});
    
    // movemos el viewport a la posicion 4, 20
    Viewport vp = testViewport(4, 20);
    Layout lo = testLayout(vp);
    
    auto first = screenToCursor(CellPos{7, 0}, lo, vp, d);
    CHECK(first.has_value());
    CHECK_EQ(first->line, 0);
    
    auto last = screenToCursor(CellPos{7, 3}, lo, vp, d);
    CHECK(last.has_value());
    CHECK_EQ(last->line, 3);
}

TEST(mouse_map_outside_content_returns_nullopt) {
    Document d;
    d.restore({"a", "b"});
    
    Viewport vp = testViewport(10, 20);
    Layout lo = testLayout(vp);
    
    // fila visible 2 = docLine 2 >= lineCount => nullopt (fila `~`)
    auto below = screenToCursor(CellPos{9, 2}, lo, vp, d);
    CHECK(!below.has_value());
    
    // status bar (primera fila fuera de content): nullopt. 
    auto bar = screenToCursor(CellPos{9, 10}, lo, vp, d);
    CHECK(!bar.has_value());
    
    // mensaje del chrome: nullopt tambien.
    auto msg = screenToCursor(CellPos{9, 11}, lo, vp, d);
    CHECK(!msg.has_value());
    
    // Fuera de todo el chrome (relRow >= content.height + chrome.height) sigue siendo nullopt
    auto out = screenToCursor(CellPos{9, 12}, lo, vp, d);
    CHECK(!out.has_value());
}

// Verifica que screenToCursor rechace cualquier fila fuera de layout.content
TEST(mouse_map_chrome_with_content_row_nonzero) {
    Document d;
    d.restore({"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7", "l8", "l9"});
    
    Viewport vp = testViewport(10, 20);
    Layout lo = testLayout(vp);          // content = Rect{0,0,20,10}, chrome = Rect{10,0,20,2}
    lo.content.row = 5;                  // contenido empieza en la fila 5
    lo.chrome.row = 15;                  // chrome empieza en la fila 15 (5 + 10)
    lo.chrome.height = 2;

    // Fila anterior al contenido (cell.row = 4 < content.row = 5): nullopt.
    auto before = screenToCursor(CellPos{5, 4}, lo, vp, d);
    CHECK(!before.has_value());

    // Primera fila de contenido (cell.row = 5 == content.row): válido.
    auto first = screenToCursor(CellPos{5, 5}, lo, vp, d);
    CHECK(first.has_value());
    CHECK_EQ(first->line, 0);

    // StatusBar (cell.row = 15 == chrome.row): nullopt (fuera de content).
    auto status = screenToCursor(CellPos{5, 15}, lo, vp, d);
    CHECK(!status.has_value());

    // MessageBar (cell.row = 16 == chrome.row + 1): nullopt (fuera de content).
    auto message = screenToCursor(CellPos{5, 16}, lo, vp, d);
    CHECK(!message.has_value());

    // Última fila de contenido (cell.row = 14 == content.row + height - 1): válido.
    auto last = screenToCursor(CellPos{5, 14}, lo, vp, d);
    CHECK(last.has_value());
    CHECK_EQ(last->line, 9);

    // Fila posterior al chrome (cell.row = 17 >= chrome.row + chrome.height): nullopt.
    auto after = screenToCursor(CellPos{5, 17}, lo, vp, d);
    CHECK(!after.has_value());
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
    
    ed.processEventForTesting(mousePress(9, 2)); // fila 1, visual 1
    CHECK_EQ(b.cursor.line, 1);
    CHECK_EQ(b.cursor.col, 1);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),static_cast<int>(State::Navegacion));
}

// este test prueba que si estas en seleccion y haces click, se rompe
TEST(mouse_editor_seleccion_cancels_and_moves) {
    Editor ed;
    
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    
    ed.processEventForTesting(insertCh('s')); // entra a Seleccion
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),static_cast<int>(State::Seleccion));
    CHECK_EQ(b.cursor.line, 0);
    CHECK_EQ(b.cursor.col, 0);
    CHECK(b.selection.has_value());
    CHECK(!b.selectAllActive);
             
    // Press en Seleccion: cancela el highlight EN EL PRESS
    ed.processEventForTesting(mousePress(9, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),static_cast<int>(State::Navegacion));
    CHECK_EQ(b.cursor.line, 1);
    CHECK_EQ(b.cursor.col, 1);
    CHECK(!b.selection.has_value());
    CHECK(!b.selectAllActive);
    
    // Release sin drag: no-op, sigue en Navegacion sin rango.
    ed.processEventForTesting(mouseRelease(9, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),static_cast<int>(State::Navegacion));
    CHECK(!b.selection.has_value());
    CHECK(!b.selectAllActive);
    CHECK_EQ(b.cursor.line, 1);
    CHECK_EQ(b.cursor.col, 1);
}

TEST(mouse_editor_modal_ignored) {
    Editor ed;
    InputEvent prefix;
    
    Buffer& b = ed.getActiveBufferForTesting();
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    
    prefix.type = InputEventType::Prefix;
    ed.processEventForTesting(prefix);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Prefix));
             
    ed.processEventForTesting(mousePress(9, 2));
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
    
    ed.processEventForTesting(mousePress(9, 2));
    CHECK_EQ(b.cursor.line, 1);
    CHECK_EQ(b.cursor.col, 1);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK(!b.selection.has_value());
    CHECK(!ed.hasSelection());
}

// Seleccion por drag y luego click en OTRA parte del codigo (distinta
// linea/col del anchor): debe limpiar y mover el cursor al click.
TEST(mouse_click_after_mouse_drag_clears_on_other_text) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    
    b.document.restore({"hello world", "second line", "third line"});
    b.viewport.height = 10;
    b.viewport.width = 40;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    
    ed.processEventForTesting(mousePress(8, 1));        // anchor (0,0)
    ed.processEventForTesting(mouseDrag(12, 2));        // (1,4)
    ed.processEventForTesting(mouseRelease(12, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    CHECK(b.selection.has_value());
    
    // Click en otra parte: linea 2, visual 5 -> (2,5).
    ed.processEventForTesting(mousePress(13, 3));
    ed.processEventForTesting(mouseRelease(13, 3));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK(!b.selection.has_value());
    CHECK(!ed.hasSelection());
    CHECK_EQ(b.cursor.line, 2);
    CHECK_EQ(b.cursor.col, 5);
}

// Click simple en Navegacion, sigue en Navegacion.
TEST(mouse_click_navegacion_preserves_historic_behavior) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    
    ed.processEventForTesting(mousePress(9, 2));      // fila 1, visual 1
    ed.processEventForTesting(mouseRelease(9, 2));
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
    
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
         static_cast<int>(State::Navegacion));
    
    ed.processEventForTesting(mousePress(8, 1));     // fila 0, visual 0
    ed.processEventForTesting(mouseDrag(10, 2));     // fila 1, visual 2
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
    
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
        static_cast<int>(State::Navegacion));
    
    ed.processEventForTesting(mousePress(8, 3)); // fila 2
    ed.processEventForTesting(mouseDrag(8, 1));  // fila 0 (hacia arriba)
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
    
    ed.processEventForTesting(insertCh('s'));           // modo Seleccion
    ed.processEventForTesting(mousePress(8, 1));
    ed.processEventForTesting(mouseDrag(10, 1));
    ed.processEventForTesting(mouseRelease(10, 1));     // primera seleccion
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK(b.selection.has_value());
    
    // el press cancela el rango anterior de inmediato
    ed.processEventForTesting(mousePress(8, 3));        // fila 2
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK(!b.selection.has_value());
    CHECK_EQ(b.cursor.line, 2);
    CHECK_EQ(b.cursor.col, 0);
    
    ed.processEventForTesting(mouseDrag(9, 3));
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
    
    ed.processEventForTesting(mousePress(8, 1));
    ed.processEventForTesting(mouseDrag(10, 2));
    ed.processEventForTesting(mouseRelease(10, 2));
    
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
    
    ed.processEventForTesting(mouseDrag(10, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK(!b.selection.has_value());
    CHECK_EQ(b.cursor.line, 0);
    CHECK_EQ(b.cursor.col, 0);
}

// La ultima fila visible es seleccion exacta, no intencion de scroll
TEST(mouse_drag_last_visible_row_is_exact_no_scroll) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.filename = "test.cpp";  // sin soft-wrap: el test mide semantica sin wrap
    b.document.restore({"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7"});
    b.viewport.height = 4;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.resize(6, 20);
    
    ed.processEventForTesting(mousePress(8, 1)); // fila 0
    CHECK_EQ(b.viewport.top, 0);
    
    ed.processEventForTesting(mouseDrag(8, 4)); // ultima fila visible
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.viewport.top, 0); // sin scroll: endpoint exacto
    CHECK_EQ(b.cursor.line, 3);
    
    // Continuidad del gesto: empujar al chrome si scrollea.
    ed.processEventForTesting(mouseDrag(8, 5)); // statusbar (relRow=4)
    CHECK_EQ(b.viewport.top, 1);
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
    ed.resize(6, 20);

    ed.processEventForTesting(mousePress(8, 4));  // fila doc 7
    CHECK_EQ(b.viewport.top, 4);
    
    ed.processEventForTesting(mouseDrag(8, 1));   // primera fila visible
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.viewport.top, 3);                  // scrolleo 1 linea hacia arriba
    CHECK_EQ(b.cursor.line, 3);
}

TEST(mouse_drag_autoscroll_horizontal) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.filename = "test.cpp";  // sin soft-wrap: el test mide semantica sin wrap
    b.document.restore({"0123456789ABCDEFGHIJ0123456789"});
    b.viewport.height = 10;
    b.viewport.width = 16; // gutter 7 + 9 de texto
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.resize(12, 16);
    
    ed.processEventForTesting(mousePress(8, 1)); // visual 0
    
    const int lastCol = 16; // ultima celda visible (1-based)
    ed.processEventForTesting(mouseDrag(lastCol, 1));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.viewport.left, 1); // 1 celda de scroll horizontal
    
    // visTarget = left(1) + (relCol(15) - gutter(7)) = 9, linea ASCII.
    CHECK_EQ(b.cursor.line, 0);
    CHECK_EQ(b.cursor.col, 9);
    CHECK(b.selection.has_value());
    CHECK_EQ(b.selection->anchor.line, 0);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 0);
    CHECK_EQ(b.selection->position.col, 9);
    CHECK(ed.hasSelection());
}

// Gesto sobre el chrome (relRow >= h) con documento debajo:
// scrollea 1 linea y extiende la seleccion, nunca nullopt silencioso.
TEST(mouse_drag_chrome_scrolls_when_content_below) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    b.filename = "test.cpp";  // sin soft-wrap: el test mide semantica sin wrap
    b.document.restore({"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7"});
    b.viewport.height = 4;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.resize(6, 20);
    
    ed.processEventForTesting(mousePress(8, 1));         // fila doc 0
    ed.processEventForTesting(mouseDrag(8, 5));          // statusbar (relRow=4)
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.viewport.top, 1);                         // hubo scroll, no se ignoro
    CHECK_EQ(b.cursor.line, 4);                          // borde inferior revelado (1+3)
    CHECK(b.selection.has_value());
    CHECK(ed.hasSelection());
}

// Gesto sobre el chrome ya en el ultimo viewport (top == maxTop):
// no hay nada que desplazar (scroll imposible)
TEST(mouse_drag_chrome_at_maxTop_clamps_to_edge) {
    Editor ed;
    ed.getActiveBufferForTesting().filename = "test.txt";  // no-wrap: camino identidad
    Buffer& b = ed.getActiveBufferForTesting();
    
    b.document.restore({"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7"});
    b.viewport.height = 4;
    b.viewport.width = 20;
    b.viewport.top = 4; // maxTop: lineas 4..7 visibles
    b.viewport.left = 0;
    b.cursor.line = 7;
    b.cursor.col = 0;
    ed.resize(6, 20);
    
    ed.processEventForTesting(mousePress(8, 4));           // fila doc 7, arma
    ed.processEventForTesting(mouseDrag(8, 3));            // fila doc 6, inicia rango
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.cursor.line, 6);
    
    ed.processEventForTesting(mouseDrag(8, 5));            // chrome: clamp al borde
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.viewport.top, 4);                           // sin scroll: ya en el limite
    CHECK_EQ(b.cursor.line, 7);                            // marca la ultima fila
    CHECK_EQ(b.cursor.col, 0);
    CHECK(b.selection.has_value());
    CHECK_EQ(b.selection->anchor.line, 7);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 7);
    CHECK_EQ(b.selection->position.col, 0);
}

// Gesto real: press + varios drags (incluye cambio de direccion) +
// release. El anchor permanece fijo en el press y position sigue al
// ultimo drag; el Selection no se recrea en cada evento.
TEST(mouse_drag_multiple_anchor_fixed_position_follows) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    
    b.document.restore({"hello world foo", "second line here", "third line here"});
    b.viewport.height = 10;
    b.viewport.width = 40;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.resize(12, 40);
    
    // gutter=7: celda col = 7 + visual (0-based).
    ed.processEventForTesting(mousePress(8, 1)); // anchor (0,0)
    ed.processEventForTesting(mouseDrag(10, 2));  // (1,2)
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.selection->anchor.line, 0);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 1);
    CHECK_EQ(b.selection->position.col, 2);
    
    ed.processEventForTesting(mouseDrag(12, 3)); // (2,4)
    CHECK_EQ(b.selection->anchor.line, 0);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 2);
    CHECK_EQ(b.selection->position.col, 4);
    
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.line, 2);
    CHECK_EQ(sel->end.col, 4);
    
    ed.processEventForTesting(mouseDrag(9, 1)); // vuelta a (0,1)
    CHECK_EQ(b.selection->anchor.line, 0);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 0);
    CHECK_EQ(b.selection->position.col, 1);
    CHECK_EQ(b.cursor.line, 0);
    CHECK_EQ(b.cursor.col, 1);
    
    ed.processEventForTesting(mouseRelease(9, 1));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK_EQ(b.selection->anchor.line, 0);
    CHECK_EQ(b.selection->anchor.col, 0);
    CHECK_EQ(b.selection->position.line, 0);
    CHECK_EQ(b.selection->position.col, 1);
    CHECK(ed.hasSelection());
}

// Caso base: seleccion por drag y luego click en otro texto valido.
TEST(mouse_click_after_mouse_drag_clears_on_text) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    
    ed.processEventForTesting(mousePress(8, 1));      // (0,0)
    ed.processEventForTesting(mouseDrag(10, 2));      // (1,2)
    ed.processEventForTesting(mouseRelease(10, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    
    // Click en otro texto valido: debe limpiar y volver a Navegacion.
    ed.processEventForTesting(mousePress(8, 1));
    ed.processEventForTesting(mouseRelease(8, 1));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK(!b.selection.has_value());
    CHECK(!ed.hasSelection());
}

// Click en fila `~` (viewport pero mas alla del EOF) tambien debe limpiar.
TEST(mouse_click_after_mouse_drag_on_tilde_clears) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    
    b.document.restore({"a", "b"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    
    ed.processEventForTesting(mousePress(8, 1)); // (0,0)
    ed.processEventForTesting(mouseDrag(9, 1));  // (0,1)
    ed.processEventForTesting(mouseRelease(9, 1));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    
    // Fila visible 4 = docLine 4 >= lineCount (2): fila `~`.
    ed.processEventForTesting(mousePress(14, 5));
    ed.processEventForTesting(mouseRelease(14, 5));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK(!b.selection.has_value());
    CHECK(!ed.hasSelection());
}

// Click en el chrome es un no-op: no mueve el cursor, no toca la
// seleccion, no cambia el estado ni arma el gesto de mouse.
TEST(mouse_click_after_mouse_drag_on_chrome_does_not_change_state) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.resize(12, 20);
    
    ed.processEventForTesting(mousePress(8, 1));        // (0,0)
    ed.processEventForTesting(mouseDrag(10, 2));        // (1,2)
    ed.processEventForTesting(mouseRelease(10, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    
    // Contenido = filas 1..10 (1-based); 11 es la primera de statusbar.
    ed.processEventForTesting(mousePress(14, 11));      // StatusBar: no-op
    ed.processEventForTesting(mouseRelease(14, 11));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));       // sin cambios
    CHECK(b.selection.has_value());                     // seleccion mantiene
    CHECK(ed.hasSelection());
    CHECK_EQ(b.cursor.line, 1);                         // cursor sin mover
    CHECK_EQ(b.cursor.col, 2);
}

// Verifica que un click en el MessageBar tampoco modifica el estado.
TEST(mouse_click_on_messagebar_does_not_change_state) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    
    b.document.restore({"hello", "world"});
    b.viewport.height = 10;
    b.viewport.width = 20;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.resize(12, 20);
    
    ed.processEventForTesting(mousePress(8, 1));          // (0,0)
    ed.processEventForTesting(mouseDrag(10, 2));          // (1,2)
    ed.processEventForTesting(mouseRelease(10, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    
    // 12 es la segunda fila del chrome (MessageBar).
    ed.processEventForTesting(mousePress(14, 12));        // MessageBar: no-op
    ed.processEventForTesting(mouseRelease(14, 12));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));         // sin cambios
    CHECK(b.selection.has_value());                       // seleccion mantiene
    CHECK(ed.hasSelection());
    CHECK_EQ(b.cursor.line, 1);                           // cursor sin mover
    CHECK_EQ(b.cursor.col, 2);
}

// Escenario del reporte: abrir archivo con contenido, seleccionar
// arrastrando, soltar, y luego hacer click en otra parte del codigo
// para ir a otra posicion. El highlight debe borrarse EN EL PRESS,
// sin esperar al release.
TEST(mouse_press_after_mouse_drag_clears_highlight_immediately) {
    Editor ed;
    Buffer& b = ed.getActiveBufferForTesting();
    
    b.document.restore({"hello world", "second line", "third line"});
    b.viewport.height = 10;
    b.viewport.width = 40;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    ed.resize(12, 40);
    
    ed.processEventForTesting(mousePress(8, 1)); // anchor (0,0)
    ed.processEventForTesting(mouseDrag(12, 2));  // (1,4)
    ed.processEventForTesting(mouseRelease(12, 2));
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    
    // Click en otra posicion: solo el press, todavia sin release.
    ed.processEventForTesting(mousePress(13, 3)); // (2,5)
    CHECK_EQ(b.cursor.line, 2);
    CHECK_EQ(b.cursor.col, 5);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK(!b.selection.has_value());
    CHECK(!ed.hasSelection());
}

// ScreenToCursor acepta CellPos 0-based.
TEST(mouse_screen_to_cursor_cellpos) {
    Document doc;
    Layout layout = computeLayout(24, 80);
    Viewport vp;
    
    doc.restore({"hello", "world"});
    vp.top = 0;
    vp.left = 0;
    vp.width = 80;
    vp.height = 22;
    
    // CellPos 0-based: 
    // (col=7, row=0) = primera celda de texto
    auto p = screenToCursor(CellPos{7, 0}, layout, vp, doc);
    CHECK(p.has_value());
    CHECK_EQ(p->line, 0);
    CHECK_EQ(p->col, 0);
    
    // Fuera del contenido: nullopt.
    CHECK(!screenToCursor(CellPos{1000, 1000}, layout, vp, doc).has_value());
}

// Sin gesto de mouse armado debe ser no-op observable:
// no mueve cursor ni viewport, no crea selección, no detiene el loop.
// El camino armado sigue cubierto por mouse_tick_* (vía tickMouseAutoscroll).
TEST(mouse_fase_c_tick_without_gesture_is_noop) {
    Editor ed;
    const int lineBefore = ed.getActiveBufferForTesting().cursor.line;
    const int colBefore = ed.getActiveBufferForTesting().cursor.col;
    const int topBefore = ed.getActiveBufferForTesting().viewport.top;
    const auto t = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    
    ed.tick(t); // fake timeout
    
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, lineBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, colBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, topBefore);
    CHECK(!ed.hasSelection());
    CHECK(ed.isRunning());
    
    // El bombeo externo sin fuentes listas también es no-op.
    ed.pollExternalEvents();
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, lineBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, colBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, topBefore);
    CHECK(!ed.hasSelection());
    CHECK(ed.isRunning());
}

// El camino de eventos solo expira mensajes: con autoscroll armado,
// clearExpiredMessages() no consulta el oráculo ni mueve nada (eso
// pertenece al timeout, vía tick()). Pinea la corrección del loop:
// eventos y wakeups externos ya no dan pasos extra ni dejan la pantalla
// desactualizada.
TEST(mouse_fase_c_event_path_does_not_autoscroll) {
    Editor ed;
    InputEvent press, drag;
    std::vector<std::string> lines;
    
    for (int i = 0; i < 20; ++i) 
        lines.push_back("l" + std::to_string(i));
    
    ed.getActiveBufferForTesting().document.restore(lines);
    ed.getActiveBufferForTesting().viewport.height = 4;
    ed.getActiveBufferForTesting().viewport.width = 20;
    ed.getActiveBufferForTesting().viewport.top = 10;
    ed.getActiveBufferForTesting().viewport.left = 0;
    ed.getActiveBufferForTesting().cursor.line = 11;
    ed.getActiveBufferForTesting().cursor.col = 0;
    ed.resize(6, 20);

    press.type = InputEventType::MousePress;
    press.cell = CellPos{7, 1}; // línea 11, arma gesto
    ed.handleEvent(press);

    drag.type = InputEventType::MouseDrag;
    drag.cell = CellPos{7, 4}; // chrome: fuera por abajo
    ed.handleEvent(drag);
    
    CHECK(ed.getStateForTesting() == State::Seleccion);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, 11); // paso del evento
    CHECK(ed.hasSelection());

    int oracleCalls = 0;
    ed.setMouseButtonPressedQuery([&] { ++oracleCalls; return true; });
    const int topBefore = ed.getActiveBufferForTesting().viewport.top;
    const int lineBefore = ed.getActiveBufferForTesting().cursor.line;
    const auto now = std::chrono::steady_clock::now();

    ed.clearExpiredMessages(now);
    CHECK_EQ(oracleCalls, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, topBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, lineBefore);
    CHECK(ed.hasSelection());
    CHECK(ed.getStateForTesting() == State::Seleccion);
    CHECK(ed.isRunning());

    // El timeout sí avanza: un paso, con consulta al oráculo.
    ed.tick(std::chrono::steady_clock::now() + std::chrono::seconds(10));
    CHECK(oracleCalls > 0);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, topBefore + 1);
}

// --- Headless extra: celda inválida -> Editor no arma gesto ---
// El píxel fuera de métrica da CellPos inválida (nunca {0,0} espurio) y el
// Editor la trata como fuera de contenido: cancela selección si la había y
// no arma gesto (el drag posterior sin press se ignora).
TEST(mouse_gui_celda_invalida_no_arma_gesto_headless) {
    InputEvent pressInv = makeMousePressEvent(CellPos{});
    
    CHECK(!pressInv.cell.valid());
    CHECK_EQ(static_cast<int>(pressInv.type),
             static_cast<int>(InputEventType::MousePress));
    // Editor: press inválido desde Navegación no mueve el cursor ni arma.
    {
        Editor ed;
        ed.getActiveBufferForTesting().document.restore({"hola", "mundo"});
        InputEvent press = makeMousePressEvent(CellPos{});
        ed.handleEvent(press);
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::Navegacion));
        CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
        CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 0);
        // Drag sin press previo: ignorado (sin selección, sin crash).
        InputEvent drag = makeMouseDragEvent(CellPos{5, 0});
        ed.handleEvent(drag);
        CHECK(!ed.hasSelection());
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::Navegacion));
    }
    // Editor: press inválido desde Selección cancela a Navegación (igual que
    // click en el chrome), sin dejar gesto armado.
    {
        Editor ed;
        ed.getActiveBufferForTesting().document.restore({"hola mundo"});
        InputEvent s;
        s.type = InputEventType::InsertChar;
        s.text = "s";
        ed.handleEvent(s);
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::Seleccion));
        ed.handleEvent(makeMousePressEvent(CellPos{}));
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::Navegacion));
        CHECK(!ed.hasSelection());
        ed.handleEvent(makeMouseDragEvent(CellPos{5, 0}));
        CHECK(!ed.hasSelection());
    }
}
