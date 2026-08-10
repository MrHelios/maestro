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
    e.text = std::string(1, c);
    return e;
}

static Event escapeEvent() {
    Event e;
    e.type = EventType::Escape;
    return e;
}

// v0.5: escribir requiere el modo Interaccion (letra 'i').
static void enterInteraccion(Editor& ed) {
    if (ed.state_ != State::Interaccion) {
        if (ed.state_ == State::Seleccion) {
            ed.handleEvent(escapeEvent());
        }
        ed.handleEvent(insert('i'));
    }
}

static void type(Editor& ed, const std::string& s) {
    if (s.empty()) return;
    enterInteraccion(ed);
    for (char c : s)
        ed.handleEvent(insert(c));
}

static void press(Editor& ed, EventType type) {
    Event e;
    e.type = type;
    ed.handleEvent(e);
}

// v0.5: la seleccion se activa con la letra 's' desde Navegacion. Este
// helper emula el flujo del usuario: si no estamos en modo seleccion,
// salimos de Interaccion si hace falta (ESC) y presionamos 's'; la
// flecha posterior extiende la seleccion.
static void enterSeleccion(Editor& ed) {
    if (ed.state_ != State::Seleccion) {
        if (ed.state_ == State::Interaccion) {
            ed.handleEvent(escapeEvent());
        }
        ed.handleEvent(insert('s'));
    }
}

static void selectPress(Editor& ed, EventType type) {
    enterSeleccion(ed);
    press(ed, type);
}

// v0.5: guardar pasa por el prefijo (Ctrl+K + Ctrl+S); un Save suelto no-op.
static void save(Editor& ed) {
    press(ed, EventType::Prefix);
    Event e; e.type = EventType::Save; ed.handleEvent(e);
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

TEST(selection_enter_select_mode_alone_is_empty) {
    // Regresion: al entrar al modo seleccion con la letra 's' sin mover
    // nada, no debe quedar seleccionado texto por encima del cursor.
    Editor ed;
    type(ed, "abc");                 // cursor (0,3)
    press(ed, EventType::InsertNewline);
    type(ed, "def");                 // cursor (1,3), cursor lejos de (0,0)
    const int undoBefore = static_cast<int>(ed.undoStack_.size());
    const bool modified = ed.modified_;

    enterSeleccion(ed);              // 's': solo entra al modo

    CHECK(!ed.hasSelection());
    CHECK(!ed.selection().has_value());
    // No es una edicion ni mueve el cursor.
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 3);
    CHECK_EQ(ed.undoStack_.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.modified_, modified);
}

TEST(selection_move_stays_empty_if_no_movement) {
    // Una flecha en modo seleccion que no mueve el cursor (absoluto inicio) no
    // produce texto seleccionado: anchor == position.
    Editor ed;
    selectPress(ed, EventType::MoveLeft);
    CHECK(!ed.hasSelection());
    CHECK(!ed.selection().has_value());
}

TEST(selection_forward) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveRight);  // (0,1)
    selectPress(ed, EventType::MoveRight);  // (0,2)
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
    selectPress(ed, EventType::MoveLeft);   // (0,2)
    selectPress(ed, EventType::MoveLeft);   // (0,1)
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
    selectPress(ed, EventType::MoveDown);   // (1,0)
    selectPress(ed, EventType::MoveRight);  // (1,1)
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
    selectPress(ed, EventType::MoveRight);
    selectPress(ed, EventType::MoveRight);
    selectPress(ed, EventType::MoveRight);  // cursor (0,3)

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
    selectPress(ed, EventType::MoveRight);
    selectPress(ed, EventType::MoveRight);  // (0,2)
    selectPress(ed, EventType::MoveRight);  // (0,3)
    selectPress(ed, EventType::MoveLeft);   // (0,2): vuelve sobre la seleccion

    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);           // anchor intacto
    CHECK_EQ(sel->end.line, 0);
    CHECK_EQ(sel->end.col, 2);
}

TEST(selection_cleared_by_escape_after_select) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    press(ed, EventType::Escape);       // ESC sale del modo seleccion
    CHECK(!ed.hasSelection());
}

TEST(selection_cancelled_by_escape) {
    // ESC suelto cancela la seleccion y deja el documento intacto
    // (el cursor no se mueve y no hay texto borrado).
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    press(ed, EventType::Escape);
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.document_.lineAt(0), "abc");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 1);
}

TEST(selection_cleared_by_undo_redo) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveRight);
    selectPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());

    press(ed, EventType::Undo);
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.document_.lineAt(0), "ab");
}

TEST(selection_undo_redo_empty_history_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveRight);
    press(ed, EventType::Redo);            // sin redo pendiente: no rompe nada
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.document_.lineAt(0), "abc");
}

TEST(selection_does_not_clear_redo) {
    // Seleccionar es una operacion de ESTADO, no una edicion: no debe
    // tocar el historial de Undo/Redo. En particular, un redo pendiente
    // debe sobrevivir intacto a entrar en seleccion y extender.
    Editor ed;
    type(ed, "abc");               // undoStack: 'a','b','c'
    press(ed, EventType::Undo);    // -> "ab", el redo guarda "abc"
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    CHECK(!ed.redoStack_.empty());

    press(ed, EventType::MoveLeft); // cursor a (0,1) para poder avanzar un paso

    const size_t undoBefore = ed.undoStack_.size();
    const size_t redoBefore = ed.redoStack_.size();

    enterSeleccion(ed);               // 's': solo entra al modo
    press(ed, EventType::MoveRight);  // extiende: selecciona "b"
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.document_.lineAt(0), "ab"); // sin edicion

    // Seleccionar no agrega ni consume nada del historial.
    CHECK_EQ(ed.undoStack_.size(), undoBefore);
    CHECK_EQ(ed.redoStack_.size(), redoBefore);

    // El redo sigue vivo y reaplica el cambio.
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "abc");
}

TEST(selection_escape_does_not_alter_undo_redo) {
    // ESC cancela la seleccion (estado), pero NO es una edicion:
    //   - no agrega una entrada a undoStack_
    //   - no limpia el redoStack_ pendiente
    // El flujo completo type -> undo -> select -> escape -> redo debe
    // funcionar igual que si nunca hubiera existido la seleccion.
    Editor ed;
    type(ed, "abc");               // undoStack: 'a','b','c'
    press(ed, EventType::Undo);    // -> "ab", el redo guarda "abc"
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    CHECK(!ed.redoStack_.empty());

    const size_t undoBefore = ed.undoStack_.size();
    const size_t redoBefore = ed.redoStack_.size();

    enterSeleccion(ed);               // 's': entra al modo
    press(ed, EventType::MoveLeft);   // extiende (no edita)
    press(ed, EventType::Escape);     // cancela la seleccion

    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.document_.lineAt(0), "ab");

    // ESC no crea undo ni consume redo.
    CHECK_EQ(ed.undoStack_.size(), undoBefore);
    CHECK_EQ(ed.redoStack_.size(), redoBefore);

    // El redo sigue vivo y reaplica el cambio.
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "abc");
}

TEST(selection_normalized_forward_equals_stored) {
    // Para una seleccion hacia adelante, selection() coincide con el
    // estado interno (anchor = start).
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveRight);
    selectPress(ed, EventType::MoveRight);  // cursor (0,2)

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
    selectPress(ed, EventType::MoveLeft);   // (0,3)

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
// Paso 4: flechas en modo seleccion (primer comportamiento funcional)
// ---------------------------------------------------------------------------
// Ejemplo: "abcde" cursor en 2 (s, luego flechas para extender).
//   -> ab[c]de
//   -> ab[cd]e
//   -> ab[c]de
//   -> abcde (cursor en 2, sin seleccion)
// Las flechas en modo seleccion NUNCA modifican Document.
static void setupAbcde(Editor& ed) {
    type(ed, "abcde");          // cursor en (0,5)
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight); // cursor en (0,2)
}

TEST(editor_selection_right) {
    Editor ed;
    setupAbcde(ed);   // cursor (0,2), anchor (0,2)
    const std::string before = ed.document_.lineAt(0);

    selectPress(ed, EventType::MoveRight); // -> (0,3)

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

TEST(editor_selection_right_twice) {
    Editor ed;
    setupAbcde(ed);   // (0,2)
    const std::string before = ed.document_.lineAt(0);

    selectPress(ed, EventType::MoveRight); // -> (0,3)
    selectPress(ed, EventType::MoveRight); // -> (0,4): ab[cd]e

    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 4);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection_->anchor.col, 2);  // anchor intacto
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 4);
    CHECK_EQ(ed.document_.lineAt(0), before);
}

TEST(editor_selection_left) {
    Editor ed;
    setupAbcde(ed);   // (0,2)
    const std::string before = ed.document_.lineAt(0);

    selectPress(ed, EventType::MoveLeft); // -> (0,1)

    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 1);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection_->anchor.col, 2);  // anchor donde empezo
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.col, 2);
    CHECK_EQ(ed.document_.lineAt(0), before);
}

TEST(editor_selection_back_to_anchor) {
    Editor ed;
    setupAbcde(ed);   // (0,2)
    const std::string before = ed.document_.lineAt(0);

    // Seleccionar dos veces hacia la derecha...
    selectPress(ed, EventType::MoveRight); // (0,3) ab[c]de
    selectPress(ed, EventType::MoveRight); // (0,4) ab[cd]e
    // ...y volver al anchor:
    selectPress(ed, EventType::MoveLeft);  // (0,3) ab[c]de
    selectPress(ed, EventType::MoveLeft);  // (0,2) cursor en el anchor

    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);
    // cursor == anchor: no hay texto seleccionado.
    CHECK(!ed.hasSelection());
    CHECK(!ed.selection().has_value());
    CHECK_EQ(ed.document_.lineAt(0), before);
}

TEST(editor_selection_reverses_direction) {
    Editor ed;
    setupAbcde(ed);   // (0,2)
    const std::string before = ed.document_.lineAt(0);

    // Ir a la derecha y luego cruzar el anchor hacia la izquierda.
    selectPress(ed, EventType::MoveRight); // (0,3)
    selectPress(ed, EventType::MoveRight); // (0,4)
    selectPress(ed, EventType::MoveLeft);  // (0,3)
    selectPress(ed, EventType::MoveLeft);  // (0,2) == anchor, vacia
    selectPress(ed, EventType::MoveLeft);  // (0,1): cruza, seleccion hacia atras

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
// Paso 5: cancelar la seleccion con ESC
// ---------------------------------------------------------------------------
// Regla (v0.5): el modo seleccion se cancela con ESC. Cancela deja intactos
// el Document, modified_ y el undoStack (cancelar no es una edicion).
// El cursor NO se mueve con ESC.
TEST(editor_selection_arrow_right_clears) {
    // Seleccion hacia adelante: [cde] con cursor al final (ej: "abcde"
    // con cursor en el extremo derecho de la seleccion).
    Editor ed;
    setupAbcde(ed);     // "abcde", cursor (0,2)
    selectPress(ed, EventType::MoveRight); // (0,3)
    selectPress(ed, EventType::MoveRight); // (0,4)
    selectPress(ed, EventType::MoveRight); // (0,5): [cde]
    CHECK(ed.hasSelection());
    // Anchor fijo en 2, cursor adelante en 5.
    CHECK_EQ(ed.selection_->anchor.col, 2);
    CHECK_EQ(ed.selection_->position.col, 5);

    const std::string content = ed.document_.lineAt(0);
    const int undoBefore = static_cast<int>(ed.undoStack_.size());
    const bool modified = ed.modified_;

    press(ed, EventType::Escape);         // cancelar seleccion (v0.5)
    CHECK(!ed.hasSelection());
    // ESC no mueve el cursor: queda en el extremo derecho.
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 5);
    // No es una edicion.
    CHECK_EQ(ed.document_.lineAt(0), content);
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.undoStack_.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.modified_, modified);
}

TEST(editor_selection_arrow_left_clears) {
    Editor ed;
    setupAbcde(ed);     // "abcde", cursor (0,2)
    selectPress(ed, EventType::MoveLeft);  // (0,1): seleccion hacia atras
    selectPress(ed, EventType::MoveLeft);  // (0,0): [0..2)
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection_->anchor.col, 2);

    const std::string content = ed.document_.lineAt(0);
    const int undoBefore = static_cast<int>(ed.undoStack_.size());
    const bool modified = ed.modified_;

    press(ed, EventType::Escape);         // cancelar seleccion (v0.5)
    CHECK(!ed.hasSelection());
    // ESC no mueve el cursor: queda en (0,0).
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.document_.lineAt(0), content);
    CHECK_EQ(ed.undoStack_.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.modified_, modified);
}

TEST(editor_selection_arrow_up_clears) {
    Editor ed;
    type(ed, "aaa");
    press(ed, EventType::InsertNewline); // (1,0)
    press(ed, EventType::MoveUp);        // (0,0)
    selectPress(ed, EventType::MoveDown);             // seleccion -> (1,0)
    CHECK(ed.hasSelection());

    const std::string line0 = ed.document_.lineAt(0);
    const std::string line1 = ed.document_.lineAt(1);
    const int undoBefore = static_cast<int>(ed.undoStack_.size());
    const bool modified = ed.modified_;

    press(ed, EventType::Escape);         // cancelar seleccion (v0.5)
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 1);
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
    press(ed, EventType::InsertNewline); // (1,0)
    press(ed, EventType::MoveUp);         // (0,0)
    press(ed, EventType::MoveHome);       // (0,0)

    selectPress(ed, EventType::MoveDown);              // seleccion -> (1,0)
    CHECK(ed.hasSelection());

    const std::string line0 = ed.document_.lineAt(0);
    const std::string line1 = ed.document_.lineAt(1);
    const int undoBefore = static_cast<int>(ed.undoStack_.size());
    const bool modified = ed.modified_;

    press(ed, EventType::Escape);                   // cancelar seleccion (v0.5)
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 1);                     // ESC no mueve el cursor
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.document_.lineAt(0), line0);
    CHECK_EQ(ed.document_.lineAt(1), line1);
    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.undoStack_.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.modified_, modified);
}

// ---------------------------------------------------------------------------
// Paso 6: flechas verticales en modo seleccion (Up/Down)
// ---------------------------------------------------------------------------
// Reutiliza el comportamiento de preferredCol de Cursor: al moverse en
// vertical, el cursor conserva la columna deseada y se clampa a lineas
// mas cortas.
static void editorOfLines(const std::vector<std::string>& lines, int line, int col, Editor& ed) {
    ed.document_.restore(lines);
    ed.cursor_.line = line;
    ed.cursor_.col = col;
    ed.cursor_.preferredCol_ = col;
}

// ---------------------------------------------------------------------------
// Transicion: Seleccion activa -> ESC -> movimiento normal
// ---------------------------------------------------------------------------
// Despues de ESC el editor vuelve a Navegacion SIN seleccion (state_ y
// selection_ limpios). Una flecha posterior es, por tanto, movimiento
// normal: no extiende ni re-crea la seleccion cancelada.
TEST(selection_escape_then_right_moves_normally) {
    Editor ed;
    setupAbcde(ed);                  // "abcde", cursor (0,2)
    selectPress(ed, EventType::MoveRight); // (0,3): seleccion activa [c)
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));

    press(ed, EventType::Escape);   // cancela seleccion, -> Navegacion, cursor se queda
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.cursor_.col, 3);

    press(ed, EventType::MoveRight); // movimiento normal
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.col, 4);
}

TEST(selection_escape_then_left_moves_normally) {
    Editor ed;
    setupAbcde(ed);                  // cursor (0,2)
    selectPress(ed, EventType::MoveRight); // (0,3): seleccion activa
    press(ed, EventType::Escape);   // -> Navegacion, sin seleccion, cursor (0,3)
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    press(ed, EventType::MoveLeft);  // normal: una posicion a la izquierda
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);
}

TEST(selection_escape_then_up_moves_normally) {
    Editor ed;
    editorOfLines({"abc", "def", "ghi"}, 1, 1, ed);
    selectPress(ed, EventType::MoveDown); // (2,1): seleccion activa
    CHECK(ed.hasSelection());

    press(ed, EventType::Escape);       // -> Navegacion, sin seleccion, cursor (2,1)
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.cursor_.line, 2);

    press(ed, EventType::MoveUp);       // normal: sube una linea
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 1);
}

TEST(selection_escape_then_down_moves_normally) {
    Editor ed;
    editorOfLines({"abc", "def", "ghi", "jkl"}, 1, 1, ed);
    selectPress(ed, EventType::MoveDown); // (2,1): seleccion activa
    CHECK(ed.hasSelection());

    press(ed, EventType::Escape);       // -> Navegacion, sin seleccion, cursor (2,1)
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.cursor_.line, 2);

    press(ed, EventType::MoveDown);     // normal: una sola linea hacia abajo
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 3);
    CHECK_EQ(ed.cursor_.col, 1);
}

TEST(editor_selection_down) {
    Editor ed;
    editorOfLines({"abcde", "12345", "wxyz"}, 0, 2, ed);
    selectPress(ed, EventType::MoveDown);  // -> (1,2)

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

    // Otro Down en modo seleccion -> (2,2).
    selectPress(ed, EventType::MoveDown);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 2);
    CHECK_EQ(ed.cursor_.col, 2);
    auto sel2 = ed.selection();
    CHECK_EQ(sel2->start.line, 0);
    CHECK_EQ(sel2->start.col, 2);
    CHECK_EQ(sel2->end.line, 2);
    CHECK_EQ(sel2->end.col, 2);
}

TEST(editor_selection_up) {
    Editor ed;
    editorOfLines({"abcde", "12345", "wxyz"}, 1, 2, ed);
    selectPress(ed, EventType::MoveUp);   // -> (0,2)

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
    Editor ed;
    editorOfLines({"abcdef", "xy"}, 0, 5, ed);
    selectPress(ed, EventType::MoveDown); // -> (1,2), preferredCol 5

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
    Editor ed;
    editorOfLines({"ab", "abcdef"}, 1, 3, ed);
    selectPress(ed, EventType::MoveUp); // -> (0,2): preferred 3 clampa a len 2

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
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc", "ddd"}, 0, 1, ed);
    selectPress(ed, EventType::MoveDown); // (1,1)
    selectPress(ed, EventType::MoveDown); // (2,1)
    selectPress(ed, EventType::MoveDown); // (3,1)

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
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 2, 1, ed);
    selectPress(ed, EventType::MoveUp);   // (1,1)
    selectPress(ed, EventType::MoveUp);   // (0,1)

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
// Paso 7: Home / End en modo seleccion
// ---------------------------------------------------------------------------
TEST(editor_selection_home) {
    Editor ed;
    setupAbcde(ed);     // "abcde", cursor (0,2)
    selectPress(ed, EventType::MoveHome); // -> (0,0), anchor (0,2)

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

TEST(editor_selection_end) {
    Editor ed;
    setupAbcde(ed);     // "abcde", cursor (0,2)
    selectPress(ed, EventType::MoveEnd); // -> (0,5), anchor (0,2)

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 5);          // lineLength("abcde") == 5
    CHECK_EQ(ed.selection_->anchor.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 5);
}

TEST(editor_selection_home_reduces_selection) {
    // Seleccion hacia adelante [0..4): Home en modo seleccion vuelve al
    // anchor (col 0) y la seleccion desaparece.
    Editor ed;
    setupAbcde(ed);     // cursor (0,2)
    selectPress(ed, EventType::MoveRight); // (0,3)
    selectPress(ed, EventType::MoveRight); // (0,4): [2..4)
    CHECK(ed.hasSelection());

    selectPress(ed, EventType::MoveHome);  // -> (0,0), cruza el anchor (2)
    CHECK(ed.hasSelection());
    // Se invierte: ahora va de 0 a 2.
    CHECK_EQ(ed.selection_->anchor.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.col, 2);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_selection_end_reduces_selection) {
    // Seleccion hacia atras: cursor al inicio (0,0), anchor al final (0,5).
    // End en modo seleccion mueve el cursor al anchor: la seleccion se
    // reduce a nada.
    Editor ed;
    setupAbcde(ed);     // cursor (0,2)
    press(ed, EventType::MoveEnd);        // (0,5)
    selectPress(ed, EventType::MoveHome);  // -> (0,0): [0..5), anchor 5
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.selection_->anchor.col, 5);

    selectPress(ed, EventType::MoveEnd);   // -> (0,5) == anchor: se reduce a nada
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.col, 5);
}

TEST(editor_selection_home_from_start) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);       // (0,0), sin seleccion
    selectPress(ed, EventType::MoveHome);   // no se mueve: anchor == cursor

    // anchor == position: no hay nada seleccionado.
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_selection_end_from_end) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveEnd);        // (0,3)
    selectPress(ed, EventType::MoveEnd);   // ya esta al final

    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);
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

    // Entrar en modo seleccion y mover las flechas solo selecciona.
    press(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveRight);
    selectPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    CHECK(!ed.modified_);
}

TEST(selection_cancel_does_not_set_modified) {
    Editor ed;
    type(ed, "hello");
    markSaved(ed);
    press(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveRight);
    selectPress(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());

    // Cancelar la seleccion con ESC tampoco modifica.
    press(ed, EventType::Escape);
    CHECK(!ed.hasSelection());
    CHECK(!ed.modified_);
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
    selectPress(ed, EventType::MoveRight); // [h]
    selectPress(ed, EventType::MoveRight); // [he]
    CHECK(ed.hasSelection());

    save(ed);   // Ctrl+K + Ctrl+S

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
    selectPress(ed, EventType::MoveRight);
    press(ed, EventType::Escape); // cancelar seleccion (v0.5)

    save(ed);   // Ctrl+K + Ctrl+S

    CHECK_EQ(selSavedContent(f.path), "hello");
    CHECK(!ed.modified_);
}

// ---------------------------------------------------------------------------
// Paso 15: edge cases de seleccion
// ---------------------------------------------------------------------------
TEST(selection_empty_document) {
    // Documento vacio (una linea sin caracteres). Las flechas en modo
    // seleccion no pueden seleccionar nada, no deben crashear y el cursor
    // sigue valido.
    Editor ed; // linea vacia, cursor (0,0)
    selectPress(ed, EventType::MoveLeft);
    selectPress(ed, EventType::MoveRight);
    selectPress(ed, EventType::MoveUp);
    selectPress(ed, EventType::MoveDown);

    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.document_.lineAt(0), "");
}

TEST(selection_empty_line) {
    // Seleccion en una linea vacia: no hay nada seleccionable.
    Editor ed;
    type(ed, "xy");
    press(ed, EventType::InsertNewline);  // (1,0): linea vacia
    press(ed, EventType::MoveUp);
    press(ed, EventType::MoveEnd);
    press(ed, EventType::MoveDown);       // cursor (1,0)
    selectPress(ed, EventType::MoveRight); // linea vacia: no se mueve

    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(selection_entire_document) {
    Editor ed;
    editorOfLines({"linea1", "linea2", "linea3"}, 0, 0, ed);
    selectPress(ed, EventType::MoveDown); // (1,0)
    selectPress(ed, EventType::MoveDown); // (2,0)
    selectPress(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveEnd);   // (2,6): fin de la ultima linea
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.line, 2);
    CHECK_EQ(sel->end.col, 6);
}

// v0.5: salir de seleccion es siempre a Navegacion. 'c' (como 'x', y como
// ESC) termina la seleccion sin efecto sobre el documento.
TEST(selection_c_exits_to_navegacion) {
    Editor ed;
    setupAbcde(ed);
    selectPress(ed, EventType::MoveRight); // [c]
    CHECK(ed.hasSelection());
    ed.handleEvent(insert('c'));
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.document_.lineAt(0), "abcde");
}

TEST(selection_other_char_ignored) {
    // En v0.5 una letra que no sea c/x ya NO reemplaza la seleccion: se
    // ignora y el modo sigue activo.
    Editor ed;
    setupAbcde(ed);
    selectPress(ed, EventType::MoveRight); // [c]
    CHECK(ed.hasSelection());
    ed.handleEvent(insert('Z'));
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK_EQ(ed.document_.lineAt(0), "abcde");
}
