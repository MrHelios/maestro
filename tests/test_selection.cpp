#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_framework.h"

// Accedemos a las piezas internas (document_, cursor_) igual que en
// test_editor.cpp, para verificar que la seleccion se mantiene
// sincronizada con el cursor.
#include <string>
#include <vector>
#define private public
#include "Editor.h"
#undef private

static Event insert(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.ch = c;
    return e;
}

static void type(Editor& ed, const std::string& s) {
    for (char c : s)
        ed.handleEvent(insert(c));
}

static void press(Editor& ed, EventType type) {
    ed.handleEvent(Event{type});
}

static void shiftPress(Editor& ed, EventType type) {
    Event e;
    e.type = type;
    e.shift = true;
    ed.handleEvent(e);
}

// ---------------------------------------------------------------------------
// 15. Seleccion: modelo y contrato via eventos
// ---------------------------------------------------------------------------
TEST(selection_empty_by_default) {
    Editor ed;
    CHECK(!ed.hasSelection());
    CHECK(!ed.selection().has_value());
}

TEST(selection_not_started_by_plain_move) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);
    CHECK(!ed.hasSelection());
}

TEST(selection_shift_move_stays_empty_if_no_movement) {
    // Shift + una flecha que no mueve el cursor (absoluto inicio) no
    // produce texto seleccionado: anchor == position.
    Editor ed;
    shiftPress(ed, EventType::MoveLeft);
    CHECK(!ed.hasSelection());
    CHECK(!ed.selection().has_value());
}

TEST(selection_forward) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);  // (0,1)
    shiftPress(ed, EventType::MoveRight);  // (0,2)
    CHECK(ed.hasSelection());

    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.line, 0);
    CHECK_EQ(sel->end.col, 2);
}

TEST(selection_backward) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveEnd);         // (0,3)
    shiftPress(ed, EventType::MoveLeft);   // (0,2)
    shiftPress(ed, EventType::MoveLeft);   // (0,1)
    CHECK(ed.hasSelection());

    // La seleccion se normaliza: start antes que end, aunque se haya
    // hecho "para atras".
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.line, 0);
    CHECK_EQ(sel->end.col, 3);
}

TEST(selection_multiline) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);   // cursor (1,0)
    type(ed, "def");
    press(ed, EventType::MoveUp);          // (0,0)
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveDown);   // (1,0)
    shiftPress(ed, EventType::MoveRight);  // (1,1)
    CHECK(ed.hasSelection());

    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.line, 1);
    CHECK_EQ(sel->end.col, 1);
}

TEST(selection_anchor_unchanged) {
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::MoveHome);        // anchor sera (0,0)
    shiftPress(ed, EventType::MoveRight);
    shiftPress(ed, EventType::MoveRight);
    shiftPress(ed, EventType::MoveRight);  // cursor (0,3)

    auto sel = ed.selection();
    CHECK(sel.has_value());
    // El anchor sigue donde empezo, aunque el cursor se mueve.
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.col, 3);
}

TEST(selection_anchor_survives_reverse_direction) {
    // Seleccionar hacia adelante y volver hacia atras: el anchor no cambia,
    // la seleccion se encoge.
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    shiftPress(ed, EventType::MoveRight);  // (0,2)
    shiftPress(ed, EventType::MoveRight);  // (0,3)
    shiftPress(ed, EventType::MoveLeft);   // (0,2): vuelve sobre la seleccion

    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);           // anchor intacto
    CHECK_EQ(sel->end.line, 0);
    CHECK_EQ(sel->end.col, 2);
}

TEST(selection_cleared_by_plain_move) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    press(ed, EventType::MoveRight);       // sin shift: seleccion se descarta
    CHECK(!ed.hasSelection());
}

TEST(selection_cleared_by_insert) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    ed.handleEvent(insert('X'));
    CHECK(!ed.hasSelection());
}

TEST(selection_cleared_by_backspace) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    press(ed, EventType::Backspace);
    CHECK(!ed.hasSelection());
}

TEST(selection_cleared_by_delete) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    press(ed, EventType::Delete);
    CHECK(!ed.hasSelection());
}

TEST(selection_cleared_by_newline) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    press(ed, EventType::InsertNewline);
    CHECK(!ed.hasSelection());
}

TEST(selection_cleared_by_undo_redo) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    shiftPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());

    press(ed, EventType::Undo);
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.document_.lineAt(0), "ab");
}

TEST(selection_undo_redo_empty_history_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    press(ed, EventType::Redo);            // sin redo pendiente: no rompe nada
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.document_.lineAt(0), "abc");
}

TEST(selection_normalized_forward_equals_stored) {
    // Para una seleccion hacia adelante, selection() coincide con el
    // estado interno (anchor = start).
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    shiftPress(ed, EventType::MoveRight);  // cursor (0,2)

    CHECK(ed.selection_.has_value());
    CHECK_EQ(ed.selection_->anchor.line, 0);
    CHECK_EQ(ed.selection_->anchor.col, 0);
    CHECK_EQ(ed.selection_->position.line, 0);
    CHECK_EQ(ed.selection_->position.col, 2);
}

TEST(selection_normalized_backward_flips_stored) {
    // Para una seleccion hacia atras, selection() invierte el orden
    // interno (el anchor queda al final).
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::MoveEnd);         // (0,4)
    shiftPress(ed, EventType::MoveLeft);   // (0,3)

    CHECK(ed.selection_.has_value());
    CHECK_EQ(ed.selection_->anchor.line, 0);
    CHECK_EQ(ed.selection_->anchor.col, 4); // anchor: donde empezo (el final)
    CHECK_EQ(ed.selection_->position.line, 0);
    CHECK_EQ(ed.selection_->position.col, 3); // cursor: se movio para atras

    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.col, 3);
    CHECK_EQ(sel->end.col, 4);
}

// ---------------------------------------------------------------------------
// Paso 4: Shift + Left/Right (primer comportamiento funcional)
// ---------------------------------------------------------------------------
// Ejemplo: "abcde" cursor en 2.
//   Shift+Right -> ab[c]de
//   Shift+Right -> ab[cd]e
//   Shift+Left  -> ab[c]de
//   Shift+Left  -> abcde (cursor en 2, sin seleccion)
// Shift + Arrow NUNCA modifica Document.
static Editor setupAbcde() {
    Editor ed;
    type(ed, "abcde");          // cursor en (0,5)
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight); // cursor en (0,2)
    return ed;
}

TEST(editor_selection_shift_right) {
    Editor ed = setupAbcde();   // cursor (0,2), anchor (0,2)
    const std::string before = ed.document_.lineAt(0);

    shiftPress(ed, EventType::MoveRight); // -> (0,3)

    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection_->anchor.col, 2);  // anchor no cambia
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 3);
    CHECK_EQ(ed.document_.lineAt(0), before); // Document intacto
    CHECK_EQ(ed.document_.lineCount(), 1);
}

TEST(editor_selection_shift_right_twice) {
    Editor ed = setupAbcde();   // (0,2)
    const std::string before = ed.document_.lineAt(0);

    shiftPress(ed, EventType::MoveRight); // -> (0,3)
    shiftPress(ed, EventType::MoveRight); // -> (0,4): ab[cd]e

    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 4);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection_->anchor.col, 2);  // anchor intacto
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 4);
    CHECK_EQ(ed.document_.lineAt(0), before);
}

TEST(editor_selection_shift_left) {
    Editor ed = setupAbcde();   // (0,2)
    const std::string before = ed.document_.lineAt(0);

    shiftPress(ed, EventType::MoveLeft); // -> (0,1)

    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 1);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection_->anchor.col, 2);  // anchor donde empezo
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.col, 2);
    CHECK_EQ(ed.document_.lineAt(0), before);
}

TEST(editor_selection_shift_back_to_anchor) {
    Editor ed = setupAbcde();   // (0,2)
    const std::string before = ed.document_.lineAt(0);

    // Seleccionar dos veces hacia la derecha...
    shiftPress(ed, EventType::MoveRight); // (0,3) ab[c]de
    shiftPress(ed, EventType::MoveRight); // (0,4) ab[cd]e
    // ...y volver al anchor:
    shiftPress(ed, EventType::MoveLeft);  // (0,3) ab[c]de
    shiftPress(ed, EventType::MoveLeft);  // (0,2) cursor en el anchor

    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);
    // cursor == anchor: no hay texto seleccionado.
    CHECK(!ed.hasSelection());
    CHECK(!ed.selection().has_value());
    CHECK_EQ(ed.document_.lineAt(0), before);
}

TEST(editor_selection_reverses_direction) {
    Editor ed = setupAbcde();   // (0,2)
    const std::string before = ed.document_.lineAt(0);

    // Ir a la derecha y luego cruzar el anchor hacia la izquierda.
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4)
    shiftPress(ed, EventType::MoveLeft);  // (0,3)
    shiftPress(ed, EventType::MoveLeft);  // (0,2) == anchor, vacia
    shiftPress(ed, EventType::MoveLeft);  // (0,1): cruza, seleccion hacia atras

    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 1);
    CHECK(ed.hasSelection());
    // El anchor siguiُo fijo en 2; ahora la seleccion va de 1 a 2.
    CHECK_EQ(ed.selection_->anchor.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.col, 2);
    CHECK_EQ(ed.document_.lineAt(0), before);
}

// ---------------------------------------------------------------------------
// Paso 5: cancelar la seleccion con flechas normales (sin Shift)
// ---------------------------------------------------------------------------
// Regla: una flecha sin Shift cancela la seleccion. Deja intactos el
// Document, modified_ y el undoStack (cancelar una seleccion NO es
// una edicion). La flecha derecha ademas mantiene la posicion del
// cursor (extremo de una seleccion hacia adelante); Left/Up/Down
// realizan su movimiento normal.
TEST(editor_selection_arrow_right_clears) {
    // Seleccion hacia adelante: [cde] con cursor al final (ej: "abcde"
    // con cursor en el extremo derecho de la seleccion).
    Editor ed = setupAbcde();     // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4)
    shiftPress(ed, EventType::MoveRight); // (0,5): [cde], cursor == anchor? no
    CHECK(ed.hasSelection());
    // Anchor fijo en 2, cursor adelante en 5.
    CHECK_EQ(ed.selection_->anchor.col, 2);
    CHECK_EQ(ed.selection_->position.col, 5);

    const std::string content = ed.document_.lineAt(0);
    const int undoBefore = static_cast<int>(ed.undoStack_.size());
    const bool modified = ed.modified_;

    press(ed, EventType::MoveRight);      // sin shift: cancelar
    CHECK(!ed.hasSelection());
    // Flecha derecha: el cursor MANTIENE su posicion (extremo derecho).
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 5);
    // No es una edicion.
    CHECK_EQ(ed.document_.lineAt(0), content);
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.undoStack_.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.modified_, modified);
}

TEST(editor_selection_arrow_left_clears) {
    Editor ed = setupAbcde();     // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveLeft);  // (0,1): seleccion hacia atras
    shiftPress(ed, EventType::MoveLeft);  // (0,0): [0..2)
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection_->anchor.col, 2);

    const std::string content = ed.document_.lineAt(0);
    const int undoBefore = static_cast<int>(ed.undoStack_.size());
    const bool modified = ed.modified_;

    press(ed, EventType::MoveLeft);       // sin shift
    CHECK(!ed.hasSelection());
    // Left realiza el movimiento normal (ya estaria en (0,0), noop).
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.document_.lineAt(0), content);
    CHECK_EQ(ed.undoStack_.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.modified_, modified);
}

TEST(editor_selection_arrow_up_clears) {
    Editor ed;
    type(ed, "aaa");
    ed.handleEvent(Event{EventType::InsertNewline}); // (1,0)
    ed.handleEvent(Event{EventType::MoveUp});        // (0,0)
    shiftPress(ed, EventType::MoveDown);             // seleccion -> (1,0)
    CHECK(ed.hasSelection());

    const std::string line0 = ed.document_.lineAt(0);
    const std::string line1 = ed.document_.lineAt(1);
    const int undoBefore = static_cast<int>(ed.undoStack_.size());
    const bool modified = ed.modified_;

    press(ed, EventType::MoveUp);          // sin shift: cancela, sube
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.document_.lineAt(0), line0);
    CHECK_EQ(ed.document_.lineAt(1), line1);
    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.undoStack_.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.modified_, modified);
}

TEST(editor_selection_arrow_down_clears) {
    Editor ed;
    type(ed, "aaa");
    ed.handleEvent(Event{EventType::InsertNewline}); // (1,0)
    ed.handleEvent(Event{EventType::MoveUp});         // (0,0)
    ed.handleEvent(Event{EventType::MoveHome});       // (0,0)

    shiftPress(ed, EventType::MoveDown);              // seleccion -> (1,0)
    CHECK(ed.hasSelection());

    const std::string line0 = ed.document_.lineAt(0);
    const std::string line1 = ed.document_.lineAt(1);
    const int undoBefore = static_cast<int>(ed.undoStack_.size());
    const bool modified = ed.modified_;

    press(ed, EventType::MoveDown);                   // sin shift
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 1);                     // baja una linea
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.document_.lineAt(0), line0);
    CHECK_EQ(ed.document_.lineAt(1), line1);
    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.undoStack_.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.modified_, modified);
}

// ---------------------------------------------------------------------------
// Paso 6: Shift + Up/Down (movimiento vertical)
// ---------------------------------------------------------------------------
// Reutiliza el comportamiento de preferredCol de Cursor: al moverse en
// vertical, el cursor conserva la columna deseada y se clampa a lineas
// mas cortas.
static Editor editorOfLines(const std::vector<std::string>& lines, int line, int col) {
    Editor ed;
    ed.document_.restore(lines);
    ed.cursor_.line = line;
    ed.cursor_.col = col;
    ed.cursor_.preferredCol_ = col;
    return ed;
}

TEST(editor_selection_shift_down) {
    Editor ed = editorOfLines({"abcde", "12345", "wxyz"}, 0, 2);
    shiftPress(ed, EventType::MoveDown);  // -> (1,2)

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 2);
    // Anchor fijo donde empezo (0,2).
    CHECK_EQ(ed.selection_->anchor.line, 0);
    CHECK_EQ(ed.selection_->anchor.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.line, 1);
    CHECK_EQ(sel->end.col, 2);

    // Otro Shift+Down -> (2,2).
    shiftPress(ed, EventType::MoveDown);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 2);
    CHECK_EQ(ed.cursor_.col, 2);
    auto sel2 = ed.selection();
    CHECK_EQ(sel2->start.line, 0);
    CHECK_EQ(sel2->start.col, 2);
    CHECK_EQ(sel2->end.line, 2);
    CHECK_EQ(sel2->end.col, 2);
}

TEST(editor_selection_shift_up) {
    Editor ed = editorOfLines({"abcde", "12345", "wxyz"}, 1, 2);
    shiftPress(ed, EventType::MoveUp);   // -> (0,2)

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);
    // Anchor (donde empezo) en la linea 1.
    CHECK_EQ(ed.selection_->anchor.line, 1);
    CHECK_EQ(ed.selection_->anchor.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.line, 1);
    CHECK_EQ(sel->end.col, 2);
}

TEST(editor_selection_vertical_shorter_line) {
    // Linea de arriba mas corta: bajar desde una columna grande debe
    // aterrizar al final de la linea corta, conservando la preferida.
    Editor ed = editorOfLines({"abcdef", "xy"}, 0, 5);
    shiftPress(ed, EventType::MoveDown); // -> (1,2), preferredCol 5

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 5);
    CHECK_EQ(sel->end.line, 1);
    CHECK_EQ(sel->end.col, 2);
}

TEST(editor_selection_vertical_longer_line) {
    // Linea de arriba mas corta: subir hacia una linea larga recupera
    // la columna preferida (se clampa al largo).
    Editor ed = editorOfLines({"ab", "abcdef"}, 1, 3);
    shiftPress(ed, EventType::MoveUp); // -> (0,2): preferred 3 clampa a len 2

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.line, 1);
    CHECK_EQ(sel->end.col, 3);
}

TEST(editor_selection_multiline) {
    Editor ed = editorOfLines({"aaa", "bbb", "ccc", "ddd"}, 0, 1);
    shiftPress(ed, EventType::MoveDown); // (1,1)
    shiftPress(ed, EventType::MoveDown); // (2,1)
    shiftPress(ed, EventType::MoveDown); // (3,1)

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection_->anchor.line, 0);
    CHECK_EQ(ed.selection_->anchor.col, 1);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.line, 3);
    CHECK_EQ(sel->end.col, 1);
}

TEST(editor_selection_multiline_reverse) {
    Editor ed = editorOfLines({"aaa", "bbb", "ccc"}, 2, 1);
    shiftPress(ed, EventType::MoveUp);   // (1,1)
    shiftPress(ed, EventType::MoveUp);   // (0,1)

    CHECK(ed.hasSelection());
    // La seleccion es "hacia arriba": start = 0, end = 2.
    CHECK_EQ(ed.selection_->anchor.line, 2);
    CHECK_EQ(ed.selection_->anchor.col, 1);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.line, 2);
    CHECK_EQ(sel->end.col, 1);
}

// ---------------------------------------------------------------------------
// Paso 7: Shift + Home / End
// ---------------------------------------------------------------------------
TEST(editor_selection_shift_home) {
    Editor ed = setupAbcde();     // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveHome); // -> (0,0), anchor (0,2)

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.selection_->anchor.col, 2);   // anchor se mantiene
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.line, 0);
    CHECK_EQ(sel->end.col, 2);
}

TEST(editor_selection_shift_end) {
    Editor ed = setupAbcde();     // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveEnd); // -> (0,5), anchor (0,2)

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 5);          // lineLength("abcde") == 5
    CHECK_EQ(ed.selection_->anchor.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 5);
}

TEST(editor_selection_shift_home_reduces_selection) {
    // Seleccion hacia adelante [0..4): Shift+Home vuelve al anchor
    // (col 0) y la seleccion desaparece.
    Editor ed = setupAbcde();     // cursor (0,2)
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4): [2..4)
    CHECK(ed.hasSelection());

    shiftPress(ed, EventType::MoveHome);  // -> (0,0), cruza el anchor (2)
    CHECK(ed.hasSelection());
    // Se invierte: ahora va de 0 a 2.
    CHECK_EQ(ed.selection_->anchor.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.col, 2);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_selection_shift_end_reduces_selection) {
    // Seleccion hacia atras: cursor al inicio (0,0), anchor al final (0,5).
    // Shift+End mueve el cursor al anchor: la seleccion se reduce a nada.
    Editor ed = setupAbcde();     // cursor (0,2)
    press(ed, EventType::MoveEnd);        // (0,5)
    shiftPress(ed, EventType::MoveHome);  // -> (0,0): [0..5), anchor 5
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.selection_->anchor.col, 5);

    shiftPress(ed, EventType::MoveEnd);   // -> (0,5) == anchor: se reduce a nada
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.col, 5);
}

TEST(editor_selection_shift_home_from_start) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);       // (0,0), sin seleccion
    shiftPress(ed, EventType::MoveHome);   // no se mueve: anchor == cursor

    // anchor == position: no hay nada seleccionado.
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_selection_shift_end_from_end) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveEnd);        // (0,3)
    shiftPress(ed, EventType::MoveEnd);   // ya esta al final

    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);
}

// ---------------------------------------------------------------------------
// Paso 10: borrar una seleccion con Backspace / Delete
// ---------------------------------------------------------------------------
// Seleccion -> borrar TODO el rango -> cursor al inicio -> selection = none.
// Es una UNICA operacion de Undo.
// Ej: "hello [world]!" + Delete -> "hello !" con cursor en el hueco.
TEST(editor_delete_selection) {
    Editor ed = setupAbcde();             // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4): seleccion [cd]
    CHECK(ed.hasSelection());

    const int undoBefore = static_cast<int>(ed.undoStack_.size());
    press(ed, EventType::Delete);

    // Rango borrado, cursor en el inicio (== anchor), sin seleccion.
    CHECK_EQ(ed.document_.lineAt(0), "abe");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);
    CHECK(!ed.hasSelection());
    CHECK(ed.modified_);

    // Una sola operacion de undo: un solo pushHistory.
    CHECK_EQ(ed.undoStack_.size(), static_cast<size_t>(undoBefore + 1));

    // Undo restaura todo el texto de una.
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "abcde");
}

TEST(editor_backspace_selection) {
    Editor ed = setupAbcde();             // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4): seleccion [cd]
    CHECK(ed.hasSelection());

    press(ed, EventType::Backspace);

    CHECK_EQ(ed.document_.lineAt(0), "abe");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);
    CHECK(!ed.hasSelection());
    CHECK(ed.modified_);
}

TEST(editor_delete_multiline_selection) {
    Editor ed = editorOfLines({"hola", "mundo", "jau"}, 0, 1);
    shiftPress(ed, EventType::MoveRight);  // (0,2)
    shiftPress(ed, EventType::MoveDown);   // (1,2)
    shiftPress(ed, EventType::MoveDown);   // (2,2)
    CHECK(ed.hasSelection());

    press(ed, EventType::Delete);

    // Rango [0,1 .. 2,2): "ola\nmundo\nja" se borra. Queda "h" + "u" = "hu".
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "hu");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 1);
    CHECK(!ed.hasSelection());
    CHECK(ed.modified_);
}

TEST(editor_backspace_multiline_selection) {
    Editor ed = editorOfLines({"hola", "mundo"}, 0, 2);
    shiftPress(ed, EventType::MoveDown);   // (1,2)
    shiftPress(ed, EventType::MoveRight);  // (1,3)
    CHECK(ed.hasSelection());

    press(ed, EventType::Backspace);

    // Seleccion [0,2 .. 1,4): "la\nmun" borrado. Resultado "hom do".
    // "ho" + "do" = "hodo".
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "hodo");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);
    CHECK(!ed.hasSelection());
}

TEST(editor_delete_selection_cursor_at_anchor) {
    Editor ed = setupAbcde();              // "abcde", cursor (0,2)
    // Seleccion hacia adelante que deja el cursor en el EXTREMO derecho.
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4) [cd]
    const bool reversed = ed.selection_->anchor.col > ed.cursor_.col;
    CHECK(!reversed); // anchor(2) < cursor(4)

    // Borrar: cursor debe quedar en el ANCHOR (2), no donde estaba (4).
    press(ed, EventType::Delete);
    CHECK_EQ(ed.cursor_.col, 2);
    CHECK_EQ(ed.document_.lineAt(0), "abe");
    CHECK(!ed.hasSelection());
}

TEST(editor_delete_reverse_selection) {
    Editor ed = setupAbcde();     // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveLeft);   // (0,1) [1..2)
    shiftPress(ed, EventType::MoveLeft);   // (0,0) [0..2), seleccion hacia atras
    CHECK(ed.hasSelection());
    CHECK(ed.selection_->anchor.col > ed.selection_->position.col); // reversa

    press(ed, EventType::Delete);

    // Se borra [0..2): "cd" de "abcde" -> "cde". Cursor va al inicio (0).
    CHECK_EQ(ed.document_.lineAt(0), "cde");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK(!ed.hasSelection());
}

// ---------------------------------------------------------------------------
// Paso 13: seleccionar NO modifica el documento (modified_)
// ---------------------------------------------------------------------------
// Seleccion es estado del editor, no una edicion al texto.
// Simulamos un "guardado" fijando savedLines_/modified_ directamente.
static void markSaved(Editor& ed) {
    ed.savedLines_ = ed.document_.snapshot();
    ed.modified_ = false;
}

TEST(selection_does_not_set_modified) {
    Editor ed;
    type(ed, "hello");
    markSaved(ed);

    // Shift + flechas solo seleccionan.
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    shiftPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    CHECK(!ed.modified_);
}

TEST(selection_cancel_does_not_set_modified) {
    Editor ed;
    type(ed, "hello");
    markSaved(ed);
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    shiftPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());

    // Cancelar la seleccion con una flecha normal tampoco modifica.
    press(ed, EventType::MoveRight);
    CHECK(!ed.hasSelection());
    CHECK(!ed.modified_);
}

TEST(delete_selection_sets_modified) {
    Editor ed;
    type(ed, "hello");
    markSaved(ed);
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight); // [1..)
    shiftPress(ed, EventType::MoveRight);

    press(ed, EventType::Delete);
    CHECK_EQ(ed.document_.lineAt(0), "llo");
    CHECK(ed.modified_);
}

TEST(replace_selection_sets_modified) {
    Editor ed;
    type(ed, "hello");
    markSaved(ed);
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight); // [h] seleccionada
    ed.handleEvent(insert('H'));

    CHECK_EQ(ed.document_.lineAt(0), "Hello");
    CHECK(ed.modified_);
}

TEST(undo_delete_selection_restores_modified) {
    Editor ed;
    type(ed, "hello");
    markSaved(ed);
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight); // seleccion "h"
    press(ed, EventType::Delete);         // "ello"
    CHECK(ed.modified_);

    press(ed, EventType::Undo);           // vuelve a "hello" == guardado
    CHECK_EQ(ed.document_.lineAt(0), "hello");
    CHECK(!ed.modified_);
}

TEST(redo_delete_selection_sets_modified) {
    Editor ed;
    type(ed, "hello");
    markSaved(ed);
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight); // seleccion "h"
    press(ed, EventType::Delete);         // "ello"
    press(ed, EventType::Undo);           // "hello"
    CHECK(!ed.modified_);

    press(ed, EventType::Redo);           // "ello" de nuevo
    CHECK_EQ(ed.document_.lineAt(0), "ello");
    CHECK(ed.modified_);
}

// ---------------------------------------------------------------------------
// Paso 14: la seleccion no se guarda (solo el documento)
// ---------------------------------------------------------------------------
static std::string selSavedContent(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

TEST(editor_save_with_selection) {
    using testfw::TempFile;
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hello");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight); // [h]
    shiftPress(ed, EventType::MoveRight); // [he]
    CHECK(ed.hasSelection());

    press(ed, EventType::Save);

    // El archivo guarda SOLO el documento, sin nada de seleccion.
    CHECK_EQ(selSavedContent(f.path), "hello");
    CHECK(!ed.modified_);
    CHECK(ed.hasSelection()); // la seleccion en pantalla sigue intacta
}

TEST(editor_save_after_selection_cancel) {
    using testfw::TempFile;
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hello");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight); // cancelar seleccion

    press(ed, EventType::Save);

    CHECK_EQ(selSavedContent(f.path), "hello");
    CHECK(!ed.modified_);
}

TEST(editor_save_after_replacement) {
    using testfw::TempFile;
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hello");
    press(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveRight); // [h]
    ed.handleEvent(insert('H'));          // "Hello"

    press(ed, EventType::Save);

    CHECK_EQ(selSavedContent(f.path), "Hello");
    CHECK(!ed.modified_);
}

// ---------------------------------------------------------------------------
// Paso 15: edge cases de seleccion
// ---------------------------------------------------------------------------
TEST(selection_empty_document) {
    // Documento vacio (una linea sin caracteres). Shift+flechas no puede
    // seleccionar nada, no debe crashear y el cursor sigue valido.
    Editor ed; // linea vacia, cursor (0,0)
    shiftPress(ed, EventType::MoveLeft);
    shiftPress(ed, EventType::MoveRight);
    shiftPress(ed, EventType::MoveUp);
    shiftPress(ed, EventType::MoveDown);

    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.document_.lineAt(0), "");
}

TEST(selection_single_character) {
    Editor ed = setupAbcde();     // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveRight); // (0,3) solo "c"
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 3);

    press(ed, EventType::Delete);
    CHECK_EQ(ed.document_.lineAt(0), "abde");
    CHECK_EQ(ed.cursor_.col, 2);
    CHECK(!ed.hasSelection());
}

TEST(selection_empty_line) {
    // Seleccion en una linea vacia: no hay nada seleccionable.
    Editor ed;
    type(ed, "xy");
    press(ed, EventType::InsertNewline);  // (1,0): linea vacia
    press(ed, EventType::MoveUp);
    press(ed, EventType::MoveEnd);
    press(ed, EventType::MoveDown);       // cursor (1,0)
    shiftPress(ed, EventType::MoveRight); // linea vacia: no se mueve

    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(selection_entire_document) {
    Editor ed = editorOfLines({"linea1", "linea2", "linea3"}, 0, 0);
    shiftPress(ed, EventType::MoveDown); // (1,0)
    shiftPress(ed, EventType::MoveDown); // (2,0)
    shiftPress(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveEnd);   // (2,6): fin de la ultima linea
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.line, 2);
    CHECK_EQ(sel->end.col, 6);
}

TEST(delete_entire_document_selection) {
    Editor ed = editorOfLines({"linea1", "linea2", "linea3"}, 0, 0);
    shiftPress(ed, EventType::MoveDown);
    shiftPress(ed, EventType::MoveDown);
    shiftPress(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveEnd);

    press(ed, EventType::Delete);

    // Borrar todo deja el doc en su estado minimo: una linea vacia.
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK(!ed.hasSelection());
    CHECK(ed.modified_);
}

TEST(replace_entire_document_selection) {
    Editor ed = editorOfLines({"linea1", "linea2", "linea3"}, 0, 0);
    shiftPress(ed, EventType::MoveDown);
    shiftPress(ed, EventType::MoveDown);
    shiftPress(ed, EventType::MoveHome);
    shiftPress(ed, EventType::MoveEnd);

    ed.handleEvent(insert('Z'));

    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "Z");
    CHECK_EQ(ed.cursor_.col, 1);
    CHECK(!ed.hasSelection());
}

// ---------------------------------------------------------------------------
// Paso 12: Undo/Redo de operaciones sobre seleccion
// ---------------------------------------------------------------------------
// El historial debe restaurar contenido, cursor, y seleccion, todo junto.
TEST(editor_undo_delete_selection) {
    Editor ed = setupAbcde();     // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4): [cd]
    CHECK(ed.hasSelection());

    press(ed, EventType::Delete);
    CHECK_EQ(ed.document_.lineAt(0), "abe");
    CHECK(!ed.hasSelection());

    press(ed, EventType::Undo);

    CHECK_EQ(ed.document_.lineAt(0), "abcde");
    // La seleccion [cd] se restaura.
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 4);
}

TEST(editor_redo_delete_selection) {
    Editor ed = setupAbcde();
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4) [cd]
    press(ed, EventType::Delete);         // "abe"
    press(ed, EventType::Undo);           // "abcde" + seleccion
    CHECK(ed.hasSelection());

    press(ed, EventType::Redo);

    // Redo re-aplica el borrado: sin seleccion y "abe".
    CHECK_EQ(ed.document_.lineAt(0), "abe");
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.col, 2);
}

TEST(editor_undo_replace_selection) {
    Editor ed = setupAbcde();
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4) [cd]
    ed.handleEvent(insert('X'));          // "abXe"
    CHECK_EQ(ed.document_.lineAt(0), "abXe");

    press(ed, EventType::Undo);

    CHECK_EQ(ed.document_.lineAt(0), "abcde");
    // Vuelve la seleccion [cd] que fue reemplazada.
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 4);
}

TEST(editor_redo_replace_selection) {
    Editor ed = setupAbcde();
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4) [cd]
    ed.handleEvent(insert('X'));          // "abXe"
    press(ed, EventType::Undo);           // "abcde" + seleccion
    CHECK(ed.hasSelection());

    press(ed, EventType::Redo);

    CHECK_EQ(ed.document_.lineAt(0), "abXe");
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.col, 3); // tras la X
}

TEST(editor_undo_multiline_selection) {
    // Seleccion [0,1 .. 1,1) sobre "aaa"/"bbb".
    Editor ed = editorOfLines({"aaa", "bbb"}, 0, 1);
    shiftPress(ed, EventType::MoveDown);  // (1,1)
    ed.handleEvent(insert('Q'));          // borra rango y pone Q -> "aQbb"
    CHECK_EQ(ed.document_.lineAt(0), "aQbb");
    CHECK_EQ(ed.cursor_.col, 2);
    CHECK(!ed.hasSelection());

    press(ed, EventType::Undo);

    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.document_.lineAt(0), "aaa");
    CHECK_EQ(ed.document_.lineAt(1), "bbb");
    // La seleccion multilinea original se restaura.
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.line, 1);
    CHECK_EQ(sel->end.col, 1);
}

TEST(editor_redo_multiline_selection)  {
    // Seleccion [0,1 .. 1,1) sobre "aaa"/"bbb".
    Editor ed = editorOfLines({"aaa", "bbb"}, 0, 1);
    shiftPress(ed, EventType::MoveDown);  // (1,1)
    ed.handleEvent(insert('Q'));
    press(ed, EventType::Undo);
    CHECK(ed.hasSelection());

    press(ed, EventType::Redo);

    // Redo re-hace el reemplazo multilinea -> "a" + "bb" = "aQbb".
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "aQbb");
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.col, 2);
}

// Ciclo seleccion -> delete -> undo -> redo -> undo, sin corromper estado.
TEST(editor_undo_redo_delete_cycle) {
    Editor ed = setupAbcde();
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4) [cd]
    press(ed, EventType::Delete);         // "abe"
    press(ed, EventType::Undo);           // "abcde" + sel
    CHECK(ed.hasSelection());
    press(ed, EventType::Redo);           // "abe", sin sel
    CHECK_EQ(ed.document_.lineAt(0), "abe");
    CHECK(!ed.hasSelection());
    press(ed, EventType::Undo);           // vuelve "abcde" + sel

    CHECK_EQ(ed.document_.lineAt(0), "abcde");
    CHECK(ed.hasSelection());
    CHECK(ed.modified_);
}

// ---------------------------------------------------------------------------
// Paso 11: escribir un caracter reemplaza la seleccion
// ---------------------------------------------------------------------------
// InsertChar con seleccion => borrar rango + insertar caracter, como UNA
// unica operacion de Undo.
// Ej: "hello [world]" + X -> "hello X" con cursor despues de la X.
TEST(editor_replace_selection_char) {
    Editor ed = setupAbcde();     // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4): [cd]
    CHECK(ed.hasSelection());

    const int undoBefore = static_cast<int>(ed.undoStack_.size());
    ed.handleEvent(insert('X'));

    // Rango borrado y X insertada en su lugar.
    CHECK_EQ(ed.document_.lineAt(0), "abXe");
    CHECK_EQ(ed.cursor_.col, 3);   // despues de la X
    CHECK(!ed.hasSelection());
    CHECK(ed.modified_);
    CHECK_EQ(ed.undoStack_.size(), static_cast<size_t>(undoBefore + 1));

    // Undo restaura YA el texto original en una sola operacion.
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "abcde");
}

TEST(editor_replace_reverse_selection_char) {
    // Seleccion hacia atras (cursor < anchor): el resultado debe ser el
    // mismo, el caracter se inserta en el inicio del rango.
    Editor ed = setupAbcde();     // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveLeft);  // (0,1)
    shiftPress(ed, EventType::MoveLeft);  // (0,0) [0..2)
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('Y'));

    // [0..2) -> "cd" reemplazado por "Y": "Ycde". Cursor tras la Y.
    CHECK_EQ(ed.document_.lineAt(0), "Ycde");
    CHECK_EQ(ed.cursor_.col, 1);
    CHECK(!ed.hasSelection());
}

TEST(editor_replace_multiline_selection_char) {
    Editor ed = editorOfLines({"hola", "mundo"}, 0, 2);
    shiftPress(ed, EventType::MoveDown);   // (1,2)
    shiftPress(ed, EventType::MoveRight);  // (1,3)
    ed.handleEvent(insert('Z'));

    // Rango [0,2..1,4): "la\nmun" -> reemplazado por "Z". "ho"+"do"="hodo",
    // luego 'Z' -> "hoZdo". Cursor tras la Z (indice 3).
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "hoZdo");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);
    CHECK(!ed.hasSelection());
}

TEST(editor_replace_selection_cursor_at_start) {
    // El cursor queda donde empezo el reemplazo (inicio del rango + 1).
    Editor ed = setupAbcde();     // "abcde", cursor (0,2)
    shiftPress(ed, EventType::MoveRight); // (0,3): [2..3)
    ed.handleEvent(insert('Y'));

    CHECK_EQ(ed.document_.lineAt(0), "abYde");
    CHECK_EQ(ed.cursor_.col, 3);
    CHECK(!ed.hasSelection());
}

TEST(editor_replace_selection_undo_single_op) {
    // Undo restaura todo de una vez (una sola operacion historica).
    Editor ed = setupAbcde();
    shiftPress(ed, EventType::MoveRight); // (0,3)
    shiftPress(ed, EventType::MoveRight); // (0,4) [cd]
    ed.handleEvent(insert('W'));          // "abWe"
    CHECK_EQ(ed.document_.lineAt(0), "abWe");

    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "abcde");
    CHECK_EQ(ed.cursor_.col, 4); // undo restaura la posicion previa al borrado
}
