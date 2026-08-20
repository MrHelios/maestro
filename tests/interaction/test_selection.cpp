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
#include "ui/Editor.h"
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
    const int undoBefore = static_cast<int>(ed.active().undoStack.size());
    const bool modified = ed.active().modified;

    enterSeleccion(ed);              // 's': solo entra al modo

    CHECK(!ed.hasSelection());
    CHECK(!ed.selection().has_value());
    // No es una edicion ni mueve el cursor.
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK_EQ(ed.active().undoStack.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.active().modified, modified);
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
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 1);
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
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
}

TEST(selection_undo_redo_empty_history_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveRight);
    press(ed, EventType::Redo);            // sin redo pendiente: no rompe nada
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
}

TEST(selection_does_not_clear_redo) {
    // Seleccionar es una operacion de ESTADO, no una edicion: no debe
    // tocar el historial de Undo/Redo. En particular, un redo pendiente
    // debe sobrevivir intacto a entrar en seleccion y extender.
    Editor ed;
    type(ed, "abc");               // undoStack: 'a','b','c'
    press(ed, EventType::Undo);    // -> "ab", el redo guarda "abc"
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    CHECK(!ed.active().redoStack.empty());

    press(ed, EventType::MoveLeft); // cursor a (0,1) para poder avanzar un paso

    const size_t undoBefore = ed.active().undoStack.size();
    const size_t redoBefore = ed.active().redoStack.size();

    enterSeleccion(ed);               // 's': solo entra al modo
    press(ed, EventType::MoveRight);  // extiende: selecciona "b"
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().document.lineAt(0), "ab"); // sin edicion

    // Seleccionar no agrega ni consume nada del historial.
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK_EQ(ed.active().redoStack.size(), redoBefore);

    // El redo sigue vivo y reaplica el cambio.
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
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
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    CHECK(!ed.active().redoStack.empty());

    const size_t undoBefore = ed.active().undoStack.size();
    const size_t redoBefore = ed.active().redoStack.size();

    enterSeleccion(ed);               // 's': entra al modo
    press(ed, EventType::MoveLeft);   // extiende (no edita)
    press(ed, EventType::Escape);     // cancela la seleccion

    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "ab");

    // ESC no crea undo ni consume redo.
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK_EQ(ed.active().redoStack.size(), redoBefore);

    // El redo sigue vivo y reaplica el cambio.
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
}

TEST(selection_normalized_forward_equals_stored) {
    // Para una seleccion hacia adelante, selection() coincide con el
    // estado interno (anchor = start).
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveRight);
    selectPress(ed, EventType::MoveRight);  // cursor (0,2)

    CHECK(ed.active().selection.has_value());
    CHECK_EQ(ed.active().selection->anchor.line, 0);
    CHECK_EQ(ed.active().selection->anchor.col, 0);
    CHECK_EQ(ed.active().selection->position.line, 0);
    CHECK_EQ(ed.active().selection->position.col, 2);
}

TEST(selection_normalized_backward_flips_stored) {
    // Para una seleccion hacia atras, selection() invierte el orden
    // interno (el anchor queda al final).
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::MoveEnd);         // (0,4)
    selectPress(ed, EventType::MoveLeft);   // (0,3)

    CHECK(ed.active().selection.has_value());
    CHECK_EQ(ed.active().selection->anchor.line, 0);
    CHECK_EQ(ed.active().selection->anchor.col, 4); // anchor: donde empezo (el final)
    CHECK_EQ(ed.active().selection->position.line, 0);
    CHECK_EQ(ed.active().selection->position.col, 3); // cursor: se movio para atras

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
    const std::string before = ed.active().document.lineAt(0);

    selectPress(ed, EventType::MoveRight); // -> (0,3)

    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 2);  // anchor no cambia
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 3);
    CHECK_EQ(ed.active().document.lineAt(0), before); // Document intacto
    CHECK_EQ(ed.active().document.lineCount(), 1);
}

TEST(editor_selection_right_twice) {
    Editor ed;
    setupAbcde(ed);   // (0,2)
    const std::string before = ed.active().document.lineAt(0);

    selectPress(ed, EventType::MoveRight); // -> (0,3)
    selectPress(ed, EventType::MoveRight); // -> (0,4): ab[cd]e

    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 4);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 2);  // anchor intacto
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 4);
    CHECK_EQ(ed.active().document.lineAt(0), before);
}

TEST(editor_selection_left) {
    Editor ed;
    setupAbcde(ed);   // (0,2)
    const std::string before = ed.active().document.lineAt(0);

    selectPress(ed, EventType::MoveLeft); // -> (0,1)

    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 2);  // anchor donde empezo
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.col, 2);
    CHECK_EQ(ed.active().document.lineAt(0), before);
}

TEST(editor_selection_back_to_anchor) {
    Editor ed;
    setupAbcde(ed);   // (0,2)
    const std::string before = ed.active().document.lineAt(0);

    // Seleccionar dos veces hacia la derecha...
    selectPress(ed, EventType::MoveRight); // (0,3) ab[c]de
    selectPress(ed, EventType::MoveRight); // (0,4) ab[cd]e
    // ...y volver al anchor:
    selectPress(ed, EventType::MoveLeft);  // (0,3) ab[c]de
    selectPress(ed, EventType::MoveLeft);  // (0,2) cursor en el anchor

    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 2);
    // cursor == anchor: no hay texto seleccionado.
    CHECK(!ed.hasSelection());
    CHECK(!ed.selection().has_value());
    CHECK_EQ(ed.active().document.lineAt(0), before);
}

TEST(editor_selection_reverses_direction) {
    Editor ed;
    setupAbcde(ed);   // (0,2)
    const std::string before = ed.active().document.lineAt(0);

    // Ir a la derecha y luego cruzar el anchor hacia la izquierda.
    selectPress(ed, EventType::MoveRight); // (0,3)
    selectPress(ed, EventType::MoveRight); // (0,4)
    selectPress(ed, EventType::MoveLeft);  // (0,3)
    selectPress(ed, EventType::MoveLeft);  // (0,2) == anchor, vacia
    selectPress(ed, EventType::MoveLeft);  // (0,1): cruza, seleccion hacia atras

    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK(ed.hasSelection());
    // El anchor siguiُo fijo en 2; ahora la seleccion va de 1 a 2.
    CHECK_EQ(ed.active().selection->anchor.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.col, 2);
    CHECK_EQ(ed.active().document.lineAt(0), before);
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
    CHECK_EQ(ed.active().selection->anchor.col, 2);
    CHECK_EQ(ed.active().selection->position.col, 5);

    const std::string content = ed.active().document.lineAt(0);
    const int undoBefore = static_cast<int>(ed.active().undoStack.size());
    const bool modified = ed.active().modified;

    press(ed, EventType::Escape);         // cancelar seleccion (v0.5)
    CHECK(!ed.hasSelection());
    // ESC no mueve el cursor: queda en el extremo derecho.
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 5);
    // No es una edicion.
    CHECK_EQ(ed.active().document.lineAt(0), content);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().undoStack.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.active().modified, modified);
}

TEST(editor_selection_arrow_left_clears) {
    Editor ed;
    setupAbcde(ed);     // "abcde", cursor (0,2)
    selectPress(ed, EventType::MoveLeft);  // (0,1): seleccion hacia atras
    selectPress(ed, EventType::MoveLeft);  // (0,0): [0..2)
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 2);

    const std::string content = ed.active().document.lineAt(0);
    const int undoBefore = static_cast<int>(ed.active().undoStack.size());
    const bool modified = ed.active().modified;

    press(ed, EventType::Escape);         // cancelar seleccion (v0.5)
    CHECK(!ed.hasSelection());
    // ESC no mueve el cursor: queda en (0,0).
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK_EQ(ed.active().document.lineAt(0), content);
    CHECK_EQ(ed.active().undoStack.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.active().modified, modified);
}

TEST(editor_selection_arrow_up_clears) {
    Editor ed;
    type(ed, "aaa");
    press(ed, EventType::InsertNewline); // (1,0)
    press(ed, EventType::MoveUp);        // (0,0)
    selectPress(ed, EventType::MoveDown);             // seleccion -> (1,0)
    CHECK(ed.hasSelection());

    const std::string line0 = ed.active().document.lineAt(0);
    const std::string line1 = ed.active().document.lineAt(1);
    const int undoBefore = static_cast<int>(ed.active().undoStack.size());
    const bool modified = ed.active().modified;

    press(ed, EventType::Escape);         // cancelar seleccion (v0.5)
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK_EQ(ed.active().document.lineAt(0), line0);
    CHECK_EQ(ed.active().document.lineAt(1), line1);
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().undoStack.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.active().modified, modified);
}

TEST(editor_selection_arrow_down_clears) {
    Editor ed;
    type(ed, "aaa");
    press(ed, EventType::InsertNewline); // (1,0)
    press(ed, EventType::MoveUp);         // (0,0)
    press(ed, EventType::MoveHome);       // (0,0)

    selectPress(ed, EventType::MoveDown);              // seleccion -> (1,0)
    CHECK(ed.hasSelection());

    const std::string line0 = ed.active().document.lineAt(0);
    const std::string line1 = ed.active().document.lineAt(1);
    const int undoBefore = static_cast<int>(ed.active().undoStack.size());
    const bool modified = ed.active().modified;

    press(ed, EventType::Escape);                   // cancelar seleccion (v0.5)
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 1);                     // ESC no mueve el cursor
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK_EQ(ed.active().document.lineAt(0), line0);
    CHECK_EQ(ed.active().document.lineAt(1), line1);
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().undoStack.size(), static_cast<size_t>(undoBefore));
    CHECK_EQ(ed.active().modified, modified);
}

// ---------------------------------------------------------------------------
// Paso 6: flechas verticales en modo seleccion (Up/Down)
// ---------------------------------------------------------------------------
// Reutiliza el comportamiento de preferredCol de Cursor: al moverse en
// vertical, el cursor conserva la columna deseada y se clampa a lineas
// mas cortas.
static void editorOfLines(const std::vector<std::string>& lines, int line, int col, Editor& ed) {
    ed.active().document.restore(lines);
    ed.active().cursor.line = line;
    ed.active().cursor.col = col;
    ed.active().cursor.preferredCol_ = col;
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
    CHECK_EQ(ed.active().cursor.col, 3);

    press(ed, EventType::MoveRight); // movimiento normal
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().cursor.col, 4);
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
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 2);
}

TEST(selection_escape_then_up_moves_normally) {
    Editor ed;
    editorOfLines({"abc", "def", "ghi"}, 1, 1, ed);
    selectPress(ed, EventType::MoveDown); // (2,1): seleccion activa
    CHECK(ed.hasSelection());

    press(ed, EventType::Escape);       // -> Navegacion, sin seleccion, cursor (2,1)
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, 2);

    press(ed, EventType::MoveUp);       // normal: sube una linea
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 1);
}

TEST(selection_escape_then_down_moves_normally) {
    Editor ed;
    editorOfLines({"abc", "def", "ghi", "jkl"}, 1, 1, ed);
    selectPress(ed, EventType::MoveDown); // (2,1): seleccion activa
    CHECK(ed.hasSelection());

    press(ed, EventType::Escape);       // -> Navegacion, sin seleccion, cursor (2,1)
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, 2);

    press(ed, EventType::MoveDown);     // normal: una sola linea hacia abajo
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 3);
    CHECK_EQ(ed.active().cursor.col, 1);
}

TEST(editor_selection_down) {
    Editor ed;
    editorOfLines({"abcde", "12345", "wxyz"}, 0, 2, ed);
    selectPress(ed, EventType::MoveDown);  // -> (1,2)

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 2);
    // Anchor fijo donde empezo (0,2).
    CHECK_EQ(ed.active().selection->anchor.line, 0);
    CHECK_EQ(ed.active().selection->anchor.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.line, 1);
    CHECK_EQ(sel->end.col, 2);

    // Otro Down en modo seleccion -> (2,2).
    selectPress(ed, EventType::MoveDown);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 2);
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
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 2);
    // Anchor (donde empezo) en la linea 1.
    CHECK_EQ(ed.active().selection->anchor.line, 1);
    CHECK_EQ(ed.active().selection->anchor.col, 2);
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
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 2);
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
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 2);
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
    CHECK_EQ(ed.active().selection->anchor.line, 0);
    CHECK_EQ(ed.active().selection->anchor.col, 1);
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
    CHECK_EQ(ed.active().selection->anchor.line, 2);
    CHECK_EQ(ed.active().selection->anchor.col, 1);
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
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK_EQ(ed.active().selection->anchor.col, 2);   // anchor se mantiene
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
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 5);          // lineLength("abcde") == 5
    CHECK_EQ(ed.active().selection->anchor.col, 2);
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
    CHECK_EQ(ed.active().selection->anchor.col, 2);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.col, 2);
    CHECK_EQ(ed.active().cursor.col, 0);
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
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK_EQ(ed.active().selection->anchor.col, 5);

    selectPress(ed, EventType::MoveEnd);   // -> (0,5) == anchor: se reduce a nada
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().cursor.col, 5);
}

TEST(editor_selection_home_from_start) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);       // (0,0), sin seleccion
    selectPress(ed, EventType::MoveHome);   // no se mueve: anchor == cursor

    // anchor == position: no hay nada seleccionado.
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(editor_selection_end_from_end) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveEnd);        // (0,3)
    selectPress(ed, EventType::MoveEnd);   // ya esta al final

    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
}

// ---------------------------------------------------------------------------
// Matriz: cada movimiento (Left/Right/Up/Down/Home/End) dentro de
// Seleccion debe: dejar el anchor fijo, mover el cursor, actualizar la
// seleccion, y permitir crecer, reducirse y desaparecer al volver al anchor.
// ---------------------------------------------------------------------------
TEST(selection_right_grows_shrinks_disappears) {
    Editor ed;
    setupAbcde(ed);           // anchor (0,2)
    const int anchor = ed.active().cursor.col;

    selectPress(ed, EventType::MoveRight); // (0,3)
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK_EQ(ed.active().selection->anchor.col, anchor); // anchor fijo
    CHECK(ed.hasSelection());

    selectPress(ed, EventType::MoveRight); // (0,4): crece
    CHECK_EQ(ed.active().selection->anchor.col, anchor);
    CHECK_EQ(ed.active().selection->position.col, 4);

    selectPress(ed, EventType::MoveLeft);  // (0,3): se reduce
    CHECK_EQ(ed.active().selection->anchor.col, anchor);
    CHECK_EQ(ed.active().selection->position.col, 3);
    CHECK(ed.hasSelection());

    selectPress(ed, EventType::MoveLeft);  // (0,2): de vuelta al anchor
    CHECK_EQ(ed.active().cursor.col, anchor);
    CHECK_EQ(ed.active().selection->anchor.col, anchor);
    CHECK(!ed.hasSelection());
}

TEST(selection_left_grows_shrinks_disappears) {
    Editor ed;
    setupAbcde(ed);           // anchor (0,2)
    const int anchor = ed.active().cursor.col;

    selectPress(ed, EventType::MoveLeft); // (0,1): hacia atras
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK_EQ(ed.active().selection->anchor.col, anchor); // anchor fijo
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.col, 2);

    selectPress(ed, EventType::MoveLeft); // (0,0): crece
    CHECK_EQ(ed.active().selection->anchor.col, anchor);
    sel = ed.selection();
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.col, 2);

    selectPress(ed, EventType::MoveRight); // (0,1): se reduce
    CHECK_EQ(ed.active().selection->anchor.col, anchor);
    sel = ed.selection();
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.col, 2);

    selectPress(ed, EventType::MoveRight); // (0,2): de vuelta al anchor
    CHECK_EQ(ed.active().cursor.col, anchor);
    CHECK_EQ(ed.active().selection->anchor.col, anchor);
    CHECK(!ed.hasSelection());
}

TEST(selection_down_grows_shrinks_disappears) {
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 0, 1, ed); // anchor (0,1)
    const int anchorLine = ed.active().cursor.line;
    const int anchorCol = ed.active().cursor.col;

    selectPress(ed, EventType::MoveDown); // (1,1)
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().selection->anchor.line, anchorLine);
    CHECK_EQ(ed.active().selection->anchor.col, anchorCol);
    CHECK(ed.hasSelection());

    selectPress(ed, EventType::MoveDown); // (2,1): crece
    CHECK_EQ(ed.active().selection->anchor.line, anchorLine);
    CHECK_EQ(ed.active().selection->position.line, 2);

    selectPress(ed, EventType::MoveUp);   // (1,1): se reduce
    CHECK_EQ(ed.active().selection->anchor.line, anchorLine);
    CHECK_EQ(ed.active().selection->position.line, 1);

    selectPress(ed, EventType::MoveUp);   // (0,1): de vuelta al anchor
    CHECK_EQ(ed.active().cursor.line, anchorLine);
    CHECK_EQ(ed.active().cursor.col, anchorCol);
    CHECK_EQ(ed.active().selection->anchor.line, anchorLine);
    CHECK(!ed.hasSelection());
}

TEST(selection_up_grows_shrinks_disappears) {
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 2, 1, ed); // anchor (2,1)
    const int anchorLine = ed.active().cursor.line;
    const int anchorCol = ed.active().cursor.col;

    selectPress(ed, EventType::MoveUp); // (1,1): hacia arriba
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().selection->anchor.line, anchorLine); // anchor fijo
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 1);
    CHECK_EQ(sel->end.line, 2);

    selectPress(ed, EventType::MoveUp); // (0,1): crece
    CHECK_EQ(ed.active().selection->anchor.line, anchorLine);
    sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->end.line, 2);

    selectPress(ed, EventType::MoveDown); // (1,1): se reduce
    CHECK_EQ(ed.active().selection->anchor.line, anchorLine);
    sel = ed.selection();
    CHECK_EQ(sel->start.line, 1);
    CHECK_EQ(sel->end.line, 2);

    selectPress(ed, EventType::MoveDown); // (2,1): de vuelta al anchor
    CHECK_EQ(ed.active().cursor.line, anchorLine);
    CHECK_EQ(ed.active().cursor.col, anchorCol);
    CHECK_EQ(ed.active().selection->anchor.line, anchorLine);
    CHECK(!ed.hasSelection());
}

TEST(selection_home_grows_shrinks_disappears) {
    Editor ed;
    setupAbcde(ed);           // anchor (0,2)
    const int anchor = ed.active().cursor.col;

    selectPress(ed, EventType::MoveHome); // (0,0): salta al inicio
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK_EQ(ed.active().selection->anchor.col, anchor); // anchor fijo
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.col, 2);

    selectPress(ed, EventType::MoveRight); // (0,1): se reduce hacia el anchor
    CHECK_EQ(ed.active().selection->anchor.col, anchor);
    sel = ed.selection();
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.col, 2);

    selectPress(ed, EventType::MoveRight); // (0,2): de vuelta al anchor
    CHECK_EQ(ed.active().cursor.col, anchor);
    CHECK_EQ(ed.active().selection->anchor.col, anchor);
    CHECK(!ed.hasSelection());
}

TEST(selection_end_grows_shrinks_disappears) {
    Editor ed;
    setupAbcde(ed);           // anchor (0,2)
    const int anchor = ed.active().cursor.col;

    selectPress(ed, EventType::MoveEnd); // (0,5): salta al final
    CHECK_EQ(ed.active().cursor.col, 5);
    CHECK_EQ(ed.active().selection->anchor.col, anchor); // anchor fijo
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 5);

    selectPress(ed, EventType::MoveLeft); // (0,4): se reduce hacia el anchor
    CHECK_EQ(ed.active().selection->anchor.col, anchor);
    sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 4);

    selectPress(ed, EventType::MoveLeft);
    selectPress(ed, EventType::MoveLeft); // (0,2): de vuelta al anchor
    CHECK_EQ(ed.active().cursor.col, anchor);
    CHECK_EQ(ed.active().selection->anchor.col, anchor);
    CHECK(!ed.hasSelection());
}

// ---------------------------------------------------------------------------
// Paso 13: seleccionar NO modifica el documento (modified_)
// ---------------------------------------------------------------------------
// Seleccion es estado del editor, no una edicion al texto.
// Simulamos un "guardado" fijando savedLines_/modified_ directamente.
static void markSaved(Editor& ed) {
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;
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
    CHECK(!ed.active().modified);
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
    CHECK(!ed.active().modified);
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
    CHECK(!ed.active().modified);
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
    CHECK(!ed.active().modified);
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
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK_EQ(ed.active().document.lineAt(0), "");
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
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 0);
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
    CHECK_EQ(ed.active().document.lineAt(0), "abcde");
}

TEST(selection_other_char_replaces) {
    // P0 interaction: una letra que no es comando de Seleccion REEMPLAZA el
    // rango marcado por esa letra y entra a Interaccion (en una sola edicion,
    // es decir con una sola entrada de undo para todo el grupo de escritura
    // consecutivo). Termina la seleccion.
    Editor ed;
    setupAbcde(ed);
    selectPress(ed, EventType::MoveRight); // [c]
    CHECK(ed.hasSelection());
    ed.handleEvent(insert('Z'));
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    CHECK_EQ(ed.active().document.lineAt(0), "abZde"); // 'c' -> 'Z'
}

// ---------------------------------------------------------------------------
// 'c' desde Seleccion: primera funcionalidad del buffer (copiar)
// ---------------------------------------------------------------------------
// Contrato tras copiar con seleccion: documento identico, cursor en el
// mismo lugar, seleccion eliminada, estado Navegacion y clipboard con
// exactamente el rango seleccionado.
static void assertCopied(Editor& ed,
                         const std::vector<std::string>& docBefore,
                         int lineBefore, int colBefore,
                         const std::vector<std::string>& expectedClipboard) {
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.active().document.snapshot() == docBefore);     // documento identico
    CHECK_EQ(ed.active().cursor.line, lineBefore);            // cursor en el mismo lugar
    CHECK_EQ(ed.active().cursor.col, colBefore);
    CHECK(ed.clipboard_ == expectedClipboard);        // buffer exacto
}

TEST(selection_c_without_selection_noop) {
    // s -> c sin texto marcado (anchor == cursor): no copia nada y solo
    // informa; no toca documento, historial, modified_ ni el buffer.
    Editor ed;
    type(ed, "hola");
    markSaved(ed);
    const std::vector<std::string> docBefore = ed.active().document.snapshot();
    const size_t undoBefore = ed.active().undoStack.size();
    const size_t redoBefore = ed.active().redoStack.size();
    ed.clipboard_ = std::vector<std::string>{"previo"}; // contenido previo
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);                 // s
    CHECK(!ed.hasSelection());

    ed.handleEvent(insert('c'));

    CHECK_EQ(ed.statusMessage_, "Nada seleccionado.");
    CHECK(ed.active().document.snapshot() == docBefore);       // no modifica documento
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);        // no modifica undo
    CHECK_EQ(ed.active().redoStack.size(), redoBefore);        // no modifica redo
    CHECK(!ed.active().modified);                              // no modifica modified_
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.clipboard_ == std::vector<std::string>{"previo"}); // buffer intacto
}

TEST(selection_c_copies_single_char) {
    Editor ed;
    setupAbcde(ed);                    // "abcde", cursor (0,2)
    const auto docBefore = ed.active().document.snapshot();
    selectPress(ed, EventType::MoveRight); // [c] -> cursor (0,3)
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('c'));

    assertCopied(ed, docBefore, 0, 3, {"c"});
}

TEST(selection_c_copies_multiple_chars) {
    Editor ed;
    setupAbcde(ed);                    // "abcde", cursor (0,2)
    const auto docBefore = ed.active().document.snapshot();
    selectPress(ed, EventType::MoveRight); // (0,3)
    selectPress(ed, EventType::MoveRight); // (0,4): [cd]
    selectPress(ed, EventType::MoveRight); // (0,5): [cde]
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('c'));

    assertCopied(ed, docBefore, 0, 5, {"cde"});
}

TEST(selection_c_copies_reverse_selection) {
    // Seleccion inversa (anchor en el final, cursor al inicio): el buffer
    // se llena con el rango normalizado, no con "lo que quedo a la derecha".
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::MoveEnd);         // (0,6) -> anchor (0,6)
    selectPress(ed, EventType::MoveLeft);  // (0,5)
    selectPress(ed, EventType::MoveLeft);  // (0,4): [4..6)
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 4);
    CHECK_EQ(sel->end.col, 6);
    const auto docBefore = ed.active().document.snapshot();

    ed.handleEvent(insert('c'));

    assertCopied(ed, docBefore, 0, 4, {"ef"});
}

TEST(selection_c_copies_multiline) {
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 0, 1, ed); // cursor (0,1) -> anchor
    const auto docBefore = ed.active().document.snapshot();
    selectPress(ed, EventType::MoveDown); // (1,1)
    selectPress(ed, EventType::MoveDown); // (2,1)
    selectPress(ed, EventType::MoveEnd);  // (2,3)
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('c'));

    assertCopied(ed, docBefore, 2, 3, {"aa", "bbb", "ccc"});
}

TEST(selection_c_copies_entire_document) {
    Editor ed;
    editorOfLines({"linea1", "linea2", "linea3"}, 0, 0, ed); // cursor (0,0)
    const auto docBefore = ed.active().document.snapshot();
    selectPress(ed, EventType::MoveDown); // (1,0)
    selectPress(ed, EventType::MoveDown); // (2,0)
    selectPress(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveEnd);   // (2,6): todo el documento
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('c'));

    assertCopied(ed, docBefore, 2, 6, {"linea1", "linea2", "linea3"});
}

// ---------------------------------------------------------------------------
// Seleccion en ambas direcciones: normalizacion de selection()
// ---------------------------------------------------------------------------
// Para cada movimiento se prueba el caso "hacia adelante" (anchor < cursor)
// y el "hacia atras" (cursor < anchor), y en ambos el contrato:
//   selection().start <= selection().end   (siempre normalizado)
// Ademas se verifica que el rango real cubre exactamente los extremos.
static void checkNormalized(const Editor& ed) {
    auto sel = ed.selection();
    if (!sel.has_value()) return; // sin seleccion no hay nada que normalizar
    const Position& s = sel->start;
    const Position& e = sel->end;
    CHECK(!(e < s)); // end nunca antes que start
    // Coherencia con el estado interno: los extremos son los del rango.
    CHECK(s == ed.active().selection->anchor || s == ed.active().selection->position);
    CHECK(e == ed.active().selection->anchor || e == ed.active().selection->position);
}

TEST(normalize_right_same_line_forward) {
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);       // cursor (0,2) -> anchor sera (0,2)

    selectPress(ed, EventType::MoveRight); // (0,3): anchor(2) < cursor(3)
    selectPress(ed, EventType::MoveRight); // (0,4)
    CHECK(ed.hasSelection());
    checkNormalized(ed);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 4);
}

TEST(normalize_left_same_line_backward) {
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::MoveEnd);         // cursor (0,6) -> anchor sera (0,6)

    selectPress(ed, EventType::MoveLeft); // (0,5): cursor(5) < anchor(6)
    selectPress(ed, EventType::MoveLeft); // (0,4)
    CHECK(ed.hasSelection());
    checkNormalized(ed);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 4);
    CHECK_EQ(sel->end.col, 6);
}

TEST(normalize_same_line_both_directions) {
    // Misma linea: crecer hacia adelante, reducir y luego ir hacia atras
    // cruzando el anchor. selection() debe quedar normalizado en todo
    // momento (start <= end).
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);       // cursor (0,2) -> anchor (0,2)

    selectPress(ed, EventType::MoveRight); // (0,3): adelante [2..3)
    selectPress(ed, EventType::MoveRight); // (0,4): adelante [2..4)
    checkNormalized(ed);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 4);

    selectPress(ed, EventType::MoveLeft);  // (0,3): reduce [2..3)
    selectPress(ed, EventType::MoveLeft);  // (0,2): de vuelta al anchor
    CHECK(!ed.hasSelection());

    selectPress(ed, EventType::MoveLeft);  // (0,1): atras [1..2)
    selectPress(ed, EventType::MoveLeft);  // (0,0): atras [0..2)
    CHECK(ed.hasSelection());
    checkNormalized(ed);
    sel = ed.selection();
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.col, 2);
}

TEST(normalize_down_different_lines_forward) {
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 0, 1, ed); // cursor (0,1) -> anchor

    selectPress(ed, EventType::MoveDown); // (1,1): anchor(0,1) < cursor(1,1)
    selectPress(ed, EventType::MoveDown); // (2,1)
    CHECK(ed.hasSelection());
    checkNormalized(ed);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.line, 2);
    CHECK_EQ(sel->end.col, 1);
}

TEST(normalize_up_different_lines_backward) {
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 2, 1, ed); // cursor (2,1) -> anchor

    selectPress(ed, EventType::MoveUp); // (1,1): cursor(1,1) < anchor(2,1)
    selectPress(ed, EventType::MoveUp); // (0,1)
    CHECK(ed.hasSelection());
    checkNormalized(ed);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.line, 2);
    CHECK_EQ(sel->end.col, 1);
}

TEST(normalize_multiline_both_directions) {
    // Distintas lineas: crecer hacia adelante (abajo) y luego hacia atras
    // (arriba) cruzando el anchor.
    Editor ed;
    editorOfLines({"abc", "def", "ghi"}, 1, 0, ed); // cursor (1,0) -> anchor

    selectPress(ed, EventType::MoveDown); // (2,0): adelante [1..2)
    selectPress(ed, EventType::MoveEnd);  // (2,3): adelante [1,0)..(2,3)
    CHECK(ed.hasSelection());
    checkNormalized(ed);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 1);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.line, 2);
    CHECK_EQ(sel->end.col, 3);

    // Resetea la columna preferida a 0 y vuelve al anchor (1,0).
    selectPress(ed, EventType::MoveHome); // (2,0)
    selectPress(ed, EventType::MoveUp);   // (1,0): de vuelta al anchor
    CHECK(!ed.hasSelection());

    selectPress(ed, EventType::MoveUp);   // (0,0): atras [0..1)
    selectPress(ed, EventType::MoveRight);// (0,1): atras (0,1)..(1,0)
    CHECK(ed.hasSelection());
    checkNormalized(ed);
    sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 1);
    CHECK_EQ(sel->end.line, 1);
    CHECK_EQ(sel->end.col, 0);
}

TEST(normalize_home_both_directions) {
    // Home salta al inicio (atras respecto al anchor) y Right vuelve.
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);       // cursor (0,2) -> anchor (0,2)

    selectPress(ed, EventType::MoveHome); // (0,0): cursor(0,0) < anchor(0,2)
    CHECK(ed.hasSelection());
    checkNormalized(ed);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.col, 2);

    selectPress(ed, EventType::MoveRight); // (0,1): reduce [1..2)
    selectPress(ed, EventType::MoveRight); // (0,2): de vuelta al anchor
    CHECK(!ed.hasSelection());
}

TEST(normalize_end_both_directions) {
    // End salta al final (adelante respecto al anchor) y Left vuelve.
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);       // cursor (0,2) -> anchor (0,2)

    selectPress(ed, EventType::MoveEnd); // (0,6): anchor(0,2) < cursor(0,6)
    CHECK(ed.hasSelection());
    checkNormalized(ed);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 6);

    selectPress(ed, EventType::MoveLeft); // (0,5): reduce [2..5)
    selectPress(ed, EventType::MoveLeft); // (0,4)
    selectPress(ed, EventType::MoveLeft); // (0,3)
    selectPress(ed, EventType::MoveLeft); // (0,2): de vuelta al anchor
    CHECK(!ed.hasSelection());
}

// ---------------------------------------------------------------------------
// Prefijo 'a' dentro de Seleccion: seleccion total
// ---------------------------------------------------------------------------
// Contrato:
//   s -> a   : selection_ cubre [BOF, EOF], el CURSOR no se mueve.
//   a -> a   : toggle, vuelve a la seleccion previa (o "sin seleccion" si
//              la previa era empty) y desactiva el prefijo.
//   a -> flecha (Right/Down): cursor == anchor == EOF, sin seleccion.
//   a -> flecha (Left/Up):   cursor == anchor == BOF, sin seleccion.
//   a -> c   : copia el archivo entero.
//   a -> x   : corta el archivo entero.
//   a -> ESC : cancela la seleccion total y vuelve a Navegacion.
//   a -> tecla desconocida: no pasa nada (el prefijo sigue activo).
static void enterSelectAll(Editor& ed) {
    enterSeleccion(ed);
    ed.handleEvent(insert('a'));
    CHECK(ed.active().selectAllActive);
}

TEST(select_all_covers_whole_file_cursor_kept) {
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 1, 1, ed); // cursor (1,1)

    enterSeleccion(ed);
    ed.handleEvent(insert('a'));

    // El prefijo quedo activo y cubre el archivo entero [0,0]..[2,3).
    CHECK(ed.active().selectAllActive);
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.line, 2);
    CHECK_EQ(sel->end.col, 3);
    // El cursor NO se mueve al hacer 'a'.
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK((ed.active().selection->anchor == Position{0, 0}));
    CHECK((ed.active().selection->position == Position{2, 3}));
}

TEST(select_all_toggle_returns_to_previous_selection) {
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::MoveHome);
    selectPress(ed, EventType::MoveRight); // [a]
    selectPress(ed, EventType::MoveRight); // [ab]
    CHECK(ed.hasSelection());
    const auto before = ed.selection();

    ed.handleEvent(insert('a')); // seleccion total
    CHECK(ed.active().selectAllActive);

    ed.handleEvent(insert('a')); // toggle: vuelve a la seleccion previa
    CHECK(!ed.active().selectAllActive);
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.col, before->start.col);
    CHECK_EQ(sel->end.col, before->end.col);
}

TEST(select_all_toggle_from_empty_returns_empty) {
    // s -> a -> a partiendo de "sin seleccion" (anchor == cursor).
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);           // anchor == cursor, sin seleccion
    CHECK(!ed.hasSelection());
    const int l = ed.active().cursor.line, c = ed.active().cursor.col;

    ed.handleEvent(insert('a')); // seleccion total
    CHECK(ed.hasSelection());
    ed.handleEvent(insert('a')); // toggle -> vuelve a sin seleccion

    CHECK(!ed.active().selectAllActive);
    CHECK(!ed.hasSelection());
    CHECK(!ed.selection().has_value());
    // El cursor queda donde estaba antes de 'a'.
    CHECK_EQ(ed.active().cursor.line, l);
    CHECK_EQ(ed.active().cursor.col, c);
}

TEST(select_all_toggle_back_then_move_right) {
    // s -> a -> a -> MoveRight: el toggle vuelve a la seleccion previa SIN
    // mover el cursor; el MoveRight posterior extiende desde esa posicion,
    // no desde el EOF del 'a'.
    Editor ed;
    setupAbcde(ed);           // "abcde", cursor (0,2) -> anchor sera (0,2)
    enterSeleccion(ed);
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(!ed.hasSelection());

    ed.handleEvent(insert('a')); // seleccion total, cursor intacto
    CHECK(ed.active().selectAllActive);
    CHECK_EQ(ed.active().cursor.col, 2);

    ed.handleEvent(insert('a')); // toggle de vuelta, cursor intacto
    CHECK(!ed.active().selectAllActive);
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().cursor.col, 2); // NO se movio tras el toggle

    press(ed, EventType::MoveRight);      // extiende desde (0,2) -> (0,3)
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK_EQ(ed.active().selection->anchor.col, 2); // anchor = posicion original
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 3);
}

TEST(select_all_right_moves_to_eof) {
    Editor ed;
    editorOfLines({"aaa", "bbb"}, 0, 1, ed);
    enterSelectAll(ed);

    press(ed, EventType::MoveRight);
    CHECK(!ed.active().selectAllActive);
    // cursor == anchor == EOF, sin seleccion activa.
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(!ed.hasSelection());
    // La seleccion total NO quedo colgando: se colapso a {EOF, EOF} (sin
    // seleccion) en vez de conservar [BOF, EOF].
    CHECK(ed.active().selection.has_value());
    CHECK(ed.active().selection->anchor == ed.active().selection->position);
    CHECK((ed.active().selection->anchor == Position{1, 3}));
    CHECK(!ed.active().selectAllPrevious.has_value()); // sin estado previo residual
}

TEST(select_all_down_moves_to_eof) {
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 0, 1, ed);
    enterSelectAll(ed);

    press(ed, EventType::MoveDown);
    CHECK(!ed.active().selectAllActive);
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(!ed.hasSelection());
    CHECK(ed.active().selection.has_value());
    CHECK(ed.active().selection->anchor == ed.active().selection->position);
    CHECK(!ed.active().selectAllPrevious.has_value());
}

TEST(select_all_left_moves_to_bof) {
    Editor ed;
    editorOfLines({"aaa", "bbb"}, 1, 2, ed);
    enterSelectAll(ed);

    press(ed, EventType::MoveLeft);
    CHECK(!ed.active().selectAllActive);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.hasSelection());
    CHECK(ed.active().selection.has_value());
    CHECK(ed.active().selection->anchor == ed.active().selection->position);
    CHECK((ed.active().selection->anchor == Position{0, 0}));
    CHECK(!ed.active().selectAllPrevious.has_value());
}

TEST(select_all_up_moves_to_bof) {
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 2, 1, ed);
    enterSelectAll(ed);

    press(ed, EventType::MoveUp);
    CHECK(!ed.active().selectAllActive);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.hasSelection());
    CHECK(ed.active().selection.has_value());
    CHECK(ed.active().selection->anchor == ed.active().selection->position);
    CHECK(!ed.active().selectAllPrevious.has_value());
}

TEST(select_all_escape_cancels) {
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 0, 0, ed);
    enterSelectAll(ed);
    CHECK(ed.hasSelection());
    const int l = ed.active().cursor.line, c = ed.active().cursor.col;

    press(ed, EventType::Escape);
    CHECK(!ed.active().selectAllActive);
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, l);
    CHECK_EQ(ed.active().cursor.col, c);
}

TEST(select_all_unknown_key_is_noop) {
    Editor ed;
    editorOfLines({"aaa", "bbb"}, 0, 0, ed);
    enterSelectAll(ed);

    ed.handleEvent(insert('Z')); // tecla desconocida: no pasa nada
    CHECK(ed.active().selectAllActive);
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.line, 1);
    CHECK_EQ(sel->end.col, 3);
}

TEST(select_all_c_copies_whole_file) {
    Editor ed;
    editorOfLines({"linea1", "linea2"}, 1, 0, ed);
    enterSelectAll(ed);

    ed.handleEvent(insert('c'));
    CHECK(!ed.active().selectAllActive);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"linea1", "linea2"}));
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    // El documento no cambia al copiar.
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"linea1", "linea2"}));
}

TEST(select_all_x_cuts_whole_file) {
    Editor ed;
    editorOfLines({"linea1", "linea2"}, 1, 0, ed);
    const size_t undoBefore = ed.active().undoStack.size();
    enterSelectAll(ed);

    ed.handleEvent(insert('x'));
    CHECK(!ed.active().selectAllActive);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"linea1", "linea2"}));
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    // El archivo entero se borro -> queda una unica linea vacia.
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK(ed.active().modified);
    // Cortar es una edicion: agrega historial.
    CHECK_EQ(ed.active().undoStack.size(), undoBefore + 1);
}

TEST(select_all_not_active_cursor_and_selection_agree) {
    // 'a' no deja el estado inconsistente: al salir del prefijo, el cursor
    // vuelve a ser el extremo movil de la seleccion (el prefijo es solo un
    // resaltado temporal, no una seleccion "colgante").
    Editor ed;
    editorOfLines({"abc", "def", "ghi"}, 1, 1, ed);
    ed.handleEvent(insert('s'));
    ed.handleEvent(insert('a'));
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 1); // cursor intacto durante el prefijo

    press(ed, EventType::Escape); // cancelar
    CHECK(!ed.active().selectAllActive);
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

// ---------------------------------------------------------------------------
// Extensiones del prefijo 'a' (seleccion total)
// ---------------------------------------------------------------------------
TEST(select_all_empty_file) {
    // Archivo vacio (una sola linea sin caracteres): seleccionar todo
    // cubre un rango degenerado [0,0]..[0,0]. El prefijo queda activo y el
    // cursor no se mueve.
    Editor ed;
    ed.active().document.restore({""});
    enterSeleccion(ed);
    ed.handleEvent(insert('a'));

    CHECK(ed.active().selectAllActive);
    CHECK(ed.active().selection.has_value());
    CHECK((ed.active().selection->anchor == Position{0, 0}));
    CHECK((ed.active().selection->position == Position{0, 0}));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(select_all_single_line) {
    // Archivo de una sola linea: el rango va de [0,0] hasta el final de la
    // unica linea, sin mover el cursor.
    Editor ed;
    ed.active().document.restore({"hola"});
    enterSeleccion(ed);
    ed.handleEvent(insert('a'));

    CHECK(ed.active().selectAllActive);
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.line, 0);
    CHECK_EQ(sel->end.col, 4);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(select_all_toggle_then_copy_copies_previous) {
    // s -> a -> a -> c: el segundo 'a' devuelve la seleccion PREVIA (no la
    // total), y 'c' copia ESE rango, no todo el archivo.
    Editor ed;
    setupAbcde(ed);              // "abcde", cursor (0,2) -> anchor (0,2)
    selectPress(ed, EventType::MoveRight); // [c] -> (0,3)
    const auto before = ed.selection();
    CHECK(before.has_value());
    enterSelectAll(ed);          // 'a': seleccion total
    CHECK(ed.active().selectAllActive);

    ed.handleEvent(insert('a')); // toggle -> vuelve al rango previo [2,3)
    CHECK(!ed.active().selectAllActive);
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.col, before->start.col);
    CHECK_EQ(sel->end.col, before->end.col);

    ed.handleEvent(insert('c')); // copia SOLO [c]
    CHECK(ed.clipboard_ == (std::vector<std::string>{"c"}));
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(select_all_toggle_then_cut_cuts_previous) {
    // s -> a -> a -> x: tras el toggle la seleccion es la previa; 'x' corta
    // SOLO ese rango, dejando el resto del documento.
    Editor ed;
    setupAbcde(ed);              // "abcde", cursor (0,2) -> anchor (0,2)
    selectPress(ed, EventType::MoveRight); // [c] -> (0,3)
    enterSelectAll(ed);          // 'a': seleccion total
    CHECK(ed.active().selectAllActive);

    ed.handleEvent(insert('a')); // toggle -> vuelve a [2,3)
    CHECK(!ed.active().selectAllActive);

    ed.handleEvent(insert('x')); // corta SOLO la 'c'
    CHECK(ed.clipboard_ == (std::vector<std::string>{"c"}));
    CHECK_EQ(ed.active().document.lineAt(0), "abde");
    // Cursor al inicio de lo cortado (0,2).
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.active().modified);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

// ---------------------------------------------------------------------------
// RePag / AvPag: desplazamiento del viewport + cursor
// ---------------------------------------------------------------------------
// El viewport (viewport_.top) y el cursor se desplazan la misma cantidad
// (viewport_.height), conservando la posicion relativa del cursor. Antes de
// los bordes el viewport se clampa: nunca muestra mas alla del documento y
// el cursor nunca queda fuera de [0, lineCount-1]. En Seleccion extienden la
// seleccion como una flecha; durante el prefijo 'a' se ignoran.
static std::vector<std::string> linesOf(int n) {
    std::vector<std::string> v;
    for (int i = 0; i < n; ++i) v.push_back("linea" + std::to_string(i));
    return v;
}

TEST(pageup_moves_cursor_and_viewport_keeping_relative) {
    Editor ed;
    editorOfLines(linesOf(100), 67, 2, ed);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 60;          // visible 60..79, cursor relativo 7

    press(ed, EventType::PageUp);

    CHECK_EQ(ed.active().viewport.top, 40); // 60 - 20
    CHECK_EQ(ed.active().cursor.line, 47);  // 67 - 20; relativo 7 conservado
    CHECK_EQ(ed.active().cursor.col, 2);
}

TEST(pagedown_moves_cursor_and_viewport) {
    Editor ed;
    editorOfLines(linesOf(100), 47, 0, ed);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 40;

    press(ed, EventType::PageDown);

    CHECK_EQ(ed.active().viewport.top, 60); // 40 + 20
    CHECK_EQ(ed.active().cursor.line, 67);  // 47 + 20
}

TEST(pageup_top_clamp_keeps_relative) {
    // viewport/archivo al inicio: el layout no puede retroceder una pagina
    // completa; top se clampa a 0 y el cursor conserva su posicion relativa.
    Editor ed;
    editorOfLines(linesOf(100), 15, 1, ed);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 10;          // relativo 15 - 10 = 5

    press(ed, EventType::PageUp);

    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().cursor.line, 5);   // posicion relativa equivalente (5)
}

TEST(pagedown_bottom_clamp_never_exceeds_eof) {
    // Abajo: el viewport queda pegado al EOF; la ultima fila visible es la
    // ultima linea del archivo y el cursor nunca la supera.
    Editor ed;
    editorOfLines(linesOf(50), 25, 0, ed); // lineas 0..49, EOF = 49
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 10;          // relativo 15

    press(ed, EventType::PageDown);

    CHECK_EQ(ed.active().viewport.top, 30);      // maxTop = 50 - 20
    CHECK_EQ(ed.active().cursor.line, 45);       // 30 + 15
    CHECK(ed.active().viewport.top + ed.active().viewport.height - 1 <= ed.active().document.lineCount() - 1);
    CHECK(ed.active().cursor.line <= ed.active().document.lineCount() - 1);
}

TEST(page_small_file_fits_in_viewport) {
    // Archivo pequeno que cabe entero: RePag -> inicio, AvPag -> final.
    Editor ed;
    editorOfLines(linesOf(10), 5, 0, ed); // 10 lineas < viewport 20
    ed.active().viewport.height = 20;

    press(ed, EventType::PageDown);
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().cursor.line, 9); // final del archivo

    press(ed, EventType::PageUp);
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().cursor.line, 0); // inicio del archivo
}

// ---------------------------------------------------------------------------
// applyPage() + scrollToCursor(): el scroll posterior no debe destruir la
// semantica de pagina. run() llama a viewport_.scrollToCursor() despues de
// cada evento; como applyPage() deja el cursor dentro de [top, top+height),
// scrollToCursor() debe ser un no-op que conserve el viewport y el cursor.
// Simulamos ese paso del run loop llamando a scrollToCursor() tras el evento.
static void runLoopScroll(Editor& ed) {
    ed.active().viewport.scrollToCursor(ed.active().cursor);
}

TEST(page_repag_json_example_with_scroll) {
    // Ejemplo del enunciado: viewport 60..79, cursor 67 (relativo 7). RePag
    // -> viewport 40..59, cursor 47. scrollToCursor() no debe mover nada.
    Editor ed;
    editorOfLines(linesOf(100), 67, 2, ed);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 60;

    press(ed, EventType::PageUp);
    runLoopScroll(ed);

    CHECK_EQ(ed.active().viewport.top, 40);
    CHECK_EQ(ed.active().cursor.line, 47);
    CHECK(ed.active().cursor.line >= ed.active().viewport.top);
    CHECK(ed.active().cursor.line < ed.active().viewport.top + ed.active().viewport.height);
}

TEST(page_avpag_bottom_scroll_glued_to_eof) {
    // AvPag abajo: viewport pegado a EOF (top 30) y cursor en su borde. El
    // scroll posterior no puede empujar el top ni el cursor mas alla de la
    // ultima linea.
    Editor ed;
    editorOfLines(linesOf(50), 45, 0, ed); // EOF = 49
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 50 - 20;             // ya abajo, relativo 15
    // cursor 45 ya esta en el viewport 30..49; scrollToCursor no debe cambiar nada
    runLoopScroll(ed);
    CHECK_EQ(ed.active().viewport.top, 30);

    press(ed, EventType::PageDown);         // no hay pagina que bajar
    runLoopScroll(ed);

    CHECK_EQ(ed.active().viewport.top, 30);         // sigue pegado a EOF
    CHECK_EQ(ed.active().cursor.line, 45);
    CHECK(ed.active().viewport.top + ed.active().viewport.height - 1 <= ed.active().document.lineCount() - 1);
    CHECK(ed.active().cursor.line <= ed.active().document.lineCount() - 1);
    // El cursor queda visible dentro del viewport.
    CHECK(ed.active().cursor.line >= ed.active().viewport.top);
    CHECK(ed.active().cursor.line < ed.active().viewport.top + ed.active().viewport.height);
}

TEST(page_repag_top_scroll_keeps_top_zero) {
    // RePag al tope: top se clampa a 0 y el cursor conserva la posicion
    // relativa (5). scrollToCursor() no puede retroceder mas (top >= 0).
    Editor ed;
    editorOfLines(linesOf(100), 15, 1, ed);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 10;                  // relativo 5

    press(ed, EventType::PageUp);
    runLoopScroll(ed);

    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().cursor.line, 5);
    CHECK(ed.active().cursor.line >= ed.active().viewport.top);
}

TEST(page_multiple_avpag_at_bottom_stays_glued) {
    // Varias AvPag seguidas una vez abajo: no se desplaza (idempotente) y
    // tras cada scrollToCursor el viewport sigue pegado a EOF.
    Editor ed;
    editorOfLines(linesOf(200), 0, 0, ed);
    ed.active().viewport.height = 30;
    ed.active().viewport.top = 0;

    for (int i = 0; i < 50; ++i) {
        press(ed, EventType::PageDown);
        runLoopScroll(ed);
        CHECK(ed.active().viewport.top >= 0);
        CHECK(ed.active().viewport.top + ed.active().viewport.height - 1 <= ed.active().document.lineCount() - 1);
        CHECK(ed.active().cursor.line <= ed.active().document.lineCount() - 1);
        // El cursor siempre dentro del viewport tras el scroll.
        CHECK(ed.active().cursor.line >= ed.active().viewport.top);
        CHECK(ed.active().cursor.line < ed.active().viewport.top + ed.active().viewport.height);
    }
    // Llego al fondo y se quedo pegado a EOF.
    CHECK_EQ(ed.active().viewport.top, 200 - 30);
}

TEST(page_multiple_repag_at_top_stays_zero) {
    // Varias RePag seguidas una vez arriba: top se queda en 0, cursor en la
    // posicion relativa, y el scroll no retrocede.
    Editor ed;
    editorOfLines(linesOf(200), 199, 0, ed);
    ed.active().viewport.height = 30;
    ed.active().viewport.top = 200 - 30;            // abajo del todo

    for (int i = 0; i < 50; ++i) {
        press(ed, EventType::PageUp);
        runLoopScroll(ed);
        CHECK(ed.active().viewport.top >= 0);
        CHECK(ed.active().cursor.line >= 0);
        CHECK(ed.active().cursor.line < ed.active().document.lineCount());
    }
    // Arriba del todo, pegado al inicio.
    CHECK_EQ(ed.active().viewport.top, 0);
}

TEST(page_avpag_at_absolute_bottom_does_not_overshoot) {
    // Caso limite del enunciado: archivo de 100 lineas (0..99), viewport de
    // 20 ya abajo del todo (80..99) y cursor en 87. AvPag no puede crear una
    // pagina que no exista: el viewport QUEDA pegado al EOF (80..99) y el
    // cursor se mantiene en <= 99. Nunca cursor=107 ni viewport 81..100.
    Editor ed;
    editorOfLines(linesOf(100), 87, 0, ed); // 100 lineas, EOF (ultima) = 99
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 80;                   // ultima pagina valida: 80..99

    press(ed, EventType::PageDown);
    runLoopScroll(ed);

    // El viewport sigue siendo la ultima pagina valida, sin linea fantasma.
    CHECK_EQ(ed.active().viewport.top, 80);
    CHECK_EQ(ed.active().viewport.top + ed.active().viewport.height - 1, 99); // fondo pegado a EOF
    // El cursor no supero el archivo y sigue dentro del viewport.
    CHECK_EQ(ed.active().cursor.line, 87);
    CHECK(ed.active().cursor.line <= 99);
    CHECK(ed.active().cursor.line >= ed.active().viewport.top);
    CHECK(ed.active().cursor.line < ed.active().viewport.top + ed.active().viewport.height);
}

TEST(page_in_seleccion_extends_selection) {
    // s + AvPag: el anchor permanece y el cursor salta una pagina, como
    // una flecha hacia abajo.
    Editor ed;
    editorOfLines(linesOf(50), 5, 0, ed);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 0;
    enterSeleccion(ed);
    CHECK(!ed.hasSelection());

    press(ed, EventType::PageDown);

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.line, 5);   // anchor fijo
    CHECK_EQ(ed.active().cursor.line, 25);             // 5 + 20
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 5);
    CHECK_EQ(sel->end.line, 25);
}

TEST(page_blocked_during_select_all) {
    // s -> a -> PageUp/PageDown: se ignoran (regla de 'a': solo a/flechas/
    // ESC tienen efecto mientras el prefijo esta activo).
    Editor ed;
    editorOfLines(linesOf(50), 5, 0, ed);
    ed.active().viewport.height = 20;
    enterSelectAll(ed);

    press(ed, EventType::PageUp);
    CHECK(ed.active().selectAllActive);
    CHECK_EQ(ed.active().cursor.line, 5);   // el cursor no se movio
    CHECK(ed.hasSelection());

    press(ed, EventType::PageDown);
    CHECK(ed.active().selectAllActive);
    CHECK_EQ(ed.active().cursor.line, 5);
}

// ---------------------------------------------------------------------------
// Extensiones de RePag/AvPag: casos de vista respecto al archivo
// ---------------------------------------------------------------------------
TEST(page_file_equal_to_viewport) {
    // Archivo con EXACTAMENTE el mismo numero de filas que el viewport:
    // cabe entero en una pagina. AvPag -> final (linea count-1), RePag ->
    // inicio, top siempre 0.
    Editor ed;
    editorOfLines(linesOf(20), 5, 0, ed);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 0;

    press(ed, EventType::PageDown);
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().cursor.line, 19); // ultima linea == height-1

    press(ed, EventType::PageUp);
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().cursor.line, 0);
}

TEST(page_file_slightly_larger_than_viewport) {
    // Archivo con UNA fila mas que el viewport (count == height+1): la
    // segunda pagina tiene solo una fila util. AvPag desde el inicio baja
    // una fila (top=1), no una pagina completa de 20.
    Editor ed;
    editorOfLines(linesOf(21), 0, 0, ed); // lineas 0..20, count 21
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 0;

    press(ed, EventType::PageDown);

    CHECK_EQ(ed.active().viewport.top, 1);         // 21 - 20 = 1
    CHECK_EQ(ed.active().cursor.line, 1);          // rel 0 conservado
    CHECK(ed.active().viewport.top + ed.active().viewport.height - 1 <= ed.active().document.lineCount() - 1);
}

TEST(page_exact_multiple_of_viewport) {
    // Archivo cuyas lineas son un MULTIPLO EXACTO del viewport
    // (count = 3 * height): las paginas terminan de golpe en el EOF, sin
    // filas fantasma al final.
    Editor ed;
    editorOfLines(linesOf(60), 0, 0, ed); // count 60, height 20
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 0;

    // Pagina 1 -> top 20, cursor 20.
    press(ed, EventType::PageDown);
    CHECK_EQ(ed.active().viewport.top, 20);
    CHECK_EQ(ed.active().cursor.line, 20);

    // Pagina 2 -> top 40, cursor 40.
    press(ed, EventType::PageDown);
    CHECK_EQ(ed.active().viewport.top, 40);
    CHECK_EQ(ed.active().cursor.line, 40);

    // Pagina 3 -> top 60 (== count), ya no hay; se pega al EOF (maxTop 40).
    press(ed, EventType::PageDown);
    CHECK_EQ(ed.active().viewport.top, 40); // maxTop = 60 - 20
    CHECK_EQ(ed.active().cursor.line, 40);
    CHECK(ed.active().viewport.top + ed.active().viewport.height - 1 <= ed.active().document.lineCount() - 1);
}

TEST(page_cursor_on_first_visible_line) {
    // Cursor en la PRIMERA fila visible del viewport (rel == 0): la pagina
    // desplaza viewport y cursor la misma cantidad, manteniendo rel en 0.
    Editor ed;
    editorOfLines(linesOf(100), 60, 2, ed); // cursor 60 == top
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 60;

    press(ed, EventType::PageUp);

    CHECK_EQ(ed.active().viewport.top, 40);
    CHECK_EQ(ed.active().cursor.line, 40); // rel 0
}

TEST(page_cursor_on_last_visible_line) {
    // Cursor en la ULTIMA fila visible del viewport (rel == height-1): la
    // pagina conserva esa posicion relativa extrema.
    Editor ed;
    editorOfLines(linesOf(100), 79, 0, ed); // rel = 79 - 60 = 19
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 60;

    press(ed, EventType::PageUp);

    CHECK_EQ(ed.active().viewport.top, 40);
    CHECK_EQ(ed.active().cursor.line, 59); // rel 19 conservado
}

TEST(page_pageup_then_pagedown_roundtrip) {
    // RePag y AvPag encadenados devuelven viewport y cursor a su estado
    // original (movimiento reversible, sin clamps en el medio).
    Editor ed;
    editorOfLines(linesOf(100), 47, 0, ed);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 40; // rel 7

    press(ed, EventType::PageUp);
    CHECK_EQ(ed.active().viewport.top, 20);
    CHECK_EQ(ed.active().cursor.line, 27);

    press(ed, EventType::PageDown);
    CHECK_EQ(ed.active().viewport.top, 40); // vuelve al top original
    CHECK_EQ(ed.active().cursor.line, 47);
}

TEST(page_pagedown_then_pageup_roundtrip) {
    // AvPag y luego RePag: simetrico al roundtrip anterior.
    Editor ed;
    editorOfLines(linesOf(100), 47, 0, ed);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 40; // rel 7

    press(ed, EventType::PageDown);
    CHECK_EQ(ed.active().viewport.top, 60);
    CHECK_EQ(ed.active().cursor.line, 67);

    press(ed, EventType::PageUp);
    CHECK_EQ(ed.active().viewport.top, 40);
    CHECK_EQ(ed.active().cursor.line, 47);
}

TEST(page_pageup_in_seleccion_extends_upwards) {
    // s + RePag: el anchor permanece y el cursor salta una pagina hacia
    // arriba, extendiendo la seleccion (espejo de page_in_seleccion_extends).
    Editor ed;
    editorOfLines(linesOf(50), 30, 0, ed);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 20;          // rel 10
    enterSeleccion(ed);
    CHECK(!ed.hasSelection());

    press(ed, EventType::PageUp);

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.line, 30); // anchor fijo
    CHECK_EQ(ed.active().cursor.line, 10);             // 30 - 20
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 10);
    CHECK_EQ(sel->end.line, 30);
}

// ---------------------------------------------------------------------------
// j/k: movimiento por bloques dentro del modo Seleccion
// ---------------------------------------------------------------------------
// j/k se comportan EXACTAMENTE como una flecha en Seleccion: el anchor
// permanece fijo y el cursor se mueve, extendiendo la seleccion. En
// Navegacion solo mueven el cursor. Nota del usuario: 'k' va hacia la
// DERECHA (siguiente bloque) y 'j' hacia la IZQUIERDA (bloque anterior).
TEST(navegacion_j_moves_cursor) {
    Editor ed;
    editorOfLines({"uno dos tres"}, 0, 0, ed);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    ed.handleEvent(insert('k'));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3); // fin de "uno"
    CHECK(!ed.hasSelection());

    ed.handleEvent(insert('j'));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0); // inicio de "uno"
    CHECK(!ed.hasSelection());
}

TEST(selection_j_extends_keeping_anchor) {
    Editor ed;
    editorOfLines({"uno dos tres"}, 0, 0, ed); // cursor (0,0) -> anchor
    enterSeleccion(ed);
    CHECK(!ed.hasSelection());

    ed.handleEvent(insert('k')); // fin de "uno" -> (0,3)
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 0);        // anchor fijo
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.col, 3);

    ed.handleEvent(insert('k')); // fin de "dos" -> (0,7)
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 0);        // anchor sigue fijo
    CHECK_EQ(ed.active().cursor.col, 7);
    sel = ed.selection();
    CHECK_EQ(sel->end.col, 7);
}

TEST(selection_k_extends_backwards) {
    Editor ed;
    editorOfLines({"uno dos tres"}, 0, 0, ed);
    const int undoBefore = static_cast<int>(ed.active().undoStack.size());
    enterSeleccion(ed);

    ed.handleEvent(insert('k')); // (0,3)
    ed.handleEvent(insert('j')); // vuelve al inicio "uno" -> (0,0): se encoge
    CHECK(!ed.hasSelection());   // anchor == cursor de vuelta en (0,0)
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    // j/k en seleccion NO son ediciones: no tocan el historial.
    CHECK_EQ(ed.active().undoStack.size(), static_cast<size_t>(undoBefore));
}

TEST(selection_j_multiline_utf8) {
    // k cruza lineas sin partir caracteres multibyte y extiende la seleccion.
    Editor ed;
    editorOfLines({"hola café", "mundo adiós"}, 0, 0, ed);
    enterSeleccion(ed);

    ed.handleEvent(insert('k')); // fin de "hola" -> (0,4)
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 4);

    ed.handleEvent(insert('k')); // fin de "café" -> (0,10)
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 10);

    ed.handleEvent(insert('k')); // fin de "mundo" -> (1,5)
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 5);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 0);
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.line, 1);
    CHECK_EQ(sel->end.col, 5);
}

TEST(navegacion_jk_skips_empty_lines) {
    // j/k en Navegacion atraviesan varias lineas vacias: del cursor en
    // "uno" cruzan el hueco hasta la siguiente palabra y viceversa.
    Editor ed;
    editorOfLines({"uno", "", "", "dos"}, 0, 0, ed);

    ed.handleEvent(insert('k')); // fin de "uno" -> (0,3)
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);

    ed.handleEvent(insert('k')); // cruza 2 vacias -> fin de "dos" (3,3)
    CHECK_EQ(ed.active().cursor.line, 3);
    CHECK_EQ(ed.active().cursor.col, 3);

    ed.handleEvent(insert('j')); // -> inicio de "dos" (3,0)
    CHECK_EQ(ed.active().cursor.line, 3);
    CHECK_EQ(ed.active().cursor.col, 0);

    ed.handleEvent(insert('j')); // cruza 2 vacias -> inicio de "uno" (0,0)
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(navegacion_jk_at_bof_eof) {
    // j/k en Navegacion en los extremos del archivo: sin bloque en la
    // direccion pedida, el cursor se queda donde esta.
    Editor ed;
    editorOfLines({"sola"}, 0, 0, ed);

    ed.handleEvent(insert('j')); // sin bloque anterior
    CHECK_EQ(ed.active().cursor.col, 0);

    ed.handleEvent(insert('k')); // fin de "sola" -> (0,4)
    CHECK_EQ(ed.active().cursor.col, 4);

    ed.handleEvent(insert('k')); // sin bloque posterior (EOF)
    CHECK_EQ(ed.active().cursor.col, 4);
}

// ---------------------------------------------------------------------------
// Invariante central de Seleccion: el anchor JAMAS cambia
// ---------------------------------------------------------------------------
// En modo Seleccion TODOS los comandos que extienden (Left/Right/Up/Down/
// PageUp/PageDown/j/k) deben: mover el cursor (el extremo movil) y dejar el
// anchor exactamente donde se fijo al entrar. Un comando solo llama a
// beginSelection() cuando no hay seleccion (anchor == cursor al empezar);
// despues, updateSelectionPosition() toca unicamente la position. Si un
// nuevo comando re-estableciera el anchor, se perderia el origen de la
// seleccion. Esto se verifica con matrices de comandos.
static std::vector<std::string> longDoc(int n) {
    std::vector<std::string> v;
    for (int i = 0; i < n; ++i) v.push_back("linea " + std::to_string(i));
    return v;
}

TEST(selection_matrix_arrows_and_pages_never_change_anchor) {
    // Para cada comando de movimiento: tras fijar el anchor con la primera
    // pulsacion, la segunda debe mover solo el cursor y dejar el anchor
    // intacto.
    const std::vector<EventType> cmds = {
        EventType::MoveLeft,  EventType::MoveRight,
        EventType::MoveUp,    EventType::MoveDown,
        EventType::PageUp,    EventType::PageDown,
    };
    for (EventType type : cmds) {
        Editor ed;
        editorOfLines(longDoc(80), 40, 5, ed); // cursor (40,5), medio documento
        ed.active().viewport.height = 10;
        ed.active().viewport.top = 35;                  // visible 35..44, cursor dentro
        enterSeleccion(ed);
        CHECK(!ed.hasSelection());

        press(ed, type);                        // 1a vez: fija anchor = (40,5)
        CHECK(ed.hasSelection());
        const Position anchor = ed.active().selection->anchor;
        const Position posAfterFirst = ed.active().selection->position;
        CHECK(anchor != posAfterFirst);         // hubo movimiento real

        press(ed, type);                        // 2a vez: extiende/reduce
        CHECK(ed.active().selection->anchor == anchor); // el anchor NO cambio
        CHECK(ed.active().selection->position != posAfterFirst); // el cursor si se movio
    }
}

TEST(selection_matrix_jk_never_change_anchor) {
    // j/k en modo Seleccion: el anchor queda fijo y solo se mueve el
    // extremo movil (puede incluso reducirse hasta tocar el anchor).
    Editor ed;
    editorOfLines({"uno dos tres cuatro cinco"}, 0, 4, ed);
    enterSeleccion(ed);
    CHECK(!ed.hasSelection());

    ed.handleEvent(insert('j'));   // fija anchor (0,4); cursor -> (0,0)
    CHECK(ed.hasSelection());
    const Position anchor = ed.active().selection->anchor;
    CHECK_EQ(anchor.line, 0);
    CHECK_EQ(anchor.col, 4);

    ed.handleEvent(insert('k'));   // siguiente bloque -> (0,3)
    CHECK(ed.active().selection->anchor == anchor);
    CHECK(ed.active().selection->position != anchor); // hay seleccion: extremo distinto

    ed.handleEvent(insert('k'));   // -> (0,7), anchor intacto
    CHECK(ed.active().selection->anchor == anchor);

    ed.handleEvent(insert('j'));   // -> (0,4): reduce hasta el anchor
    CHECK(ed.active().selection->anchor == anchor);
    CHECK(ed.active().selection->position == anchor); // sin seleccion al volver al anchor
}

TEST(selection_anchor_reverse_shrinks_keeps_anchor) {
    // Crecer y luego encoger (cruzar el anchor hacia atras) mantiene el
    // anchor original en todo momento.
    Editor ed;
    editorOfLines(longDoc(40), 20, 3, ed);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 15;
    enterSeleccion(ed);

    press(ed, EventType::MoveDown);  // fija anchor (20,3) -> (21,3)
    const Position anchor = ed.active().selection->anchor;
    press(ed, EventType::MoveDown);  // (22,3)
    press(ed, EventType::MoveUp);    // (21,3): encoge
    press(ed, EventType::MoveUp);    // (20,3): de vuelta al anchor, sin seleccion
    CHECK(ed.active().selection->anchor == anchor);
    CHECK(!ed.hasSelection());       // cursor == anchor: se redujo a nada
}

// ---------------------------------------------------------------------------
// Combinaciones de comandos de movimiento (flujos reales del usuario)
// ---------------------------------------------------------------------------
// Verifican la composicion correcta entre modos y comandos. En particular
// que los comandos de pagina y j/k se ensamblan sin perder el estado del
// viewport ni el de la seleccion.

// s -> a -> flecha: el prefijo 'a' redirige la flecha a los extremos.
// (select_all_left/right/up/down ya cubren cada flecha por separado.)

TEST(combo_s_then_pageup_extends_selection) {
    // s -> RePag: extiende la seleccion una pagina hacia arriba.
    Editor ed;
    editorOfLines(linesOf(40), 15, 1, ed);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 10;          // rel 5
    enterSeleccion(ed);
    CHECK(!ed.hasSelection());

    press(ed, EventType::PageUp);

    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.line, 15);
    CHECK_EQ(ed.active().cursor.line, 5);
    auto sel = ed.selection();
    CHECK_EQ(sel->start.line, 5);
    CHECK_EQ(sel->end.line, 15);
}

TEST(combo_s_then_a_then_j_is_blocked) {
    // s -> a -> j: durante el prefijo 'a' solo tienen efecto a/flechas/ESC;
    // j/k se IGNORAN y no mueven el cursor.
    Editor ed;
    editorOfLines(linesOf(40), 5, 0, ed);
    ed.active().viewport.height = 10;
    enterSelectAll(ed);
    CHECK_EQ(ed.active().cursor.line, 5);

    ed.handleEvent(insert('j')); // deberia ignorarse

    CHECK(ed.active().selectAllActive);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 5); // sin moverse
}

TEST(combo_s_then_a_then_k_is_blocked) {
    Editor ed;
    editorOfLines(linesOf(40), 5, 0, ed);
    ed.active().viewport.height = 10;
    enterSelectAll(ed);

    ed.handleEvent(insert('k'));

    CHECK(ed.active().selectAllActive);
    CHECK_EQ(ed.active().cursor.line, 5);
}

// j/k (palabras) + paginas en Navegacion: encadenan sin corromper el
// viewport. En Navegacion j/k ya no extienden seleccion; solo mueven.
TEST(combo_j_then_pagedown) {
    // Navegacion 'j' (bloque anterior) y luego AvPag: el cursor primero
    // salta a un limite de palabra y despues baja una pagina.
    Editor ed;
    editorOfLines({"primera linea", "segunda linea"}, 1, 7, ed); // parte de "linea"
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 0;

    ed.handleEvent(insert('j')); // bloque anterior desde el espacio -> inicio "segunda"

    // j/k en navegacion caen en limites de palabra; el cursor salta al
    // inicio del bloque actual y el viewport no se toca.
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 0); // inicio de "segunda"
    CHECK_EQ(ed.active().viewport.top, 0);

    press(ed, EventType::PageDown);
    // count(2) <= height(10): archivo cabe entero -> AvPag salta al final.
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().viewport.top, 0);
}

TEST(combo_pagedown_then_j) {
    // AvPag y luego 'j' (bloque anterior): la pagina deja el cursor en la
    // ultima linea y j se mueve a un limite de palabra desde alli.
    Editor ed;
    editorOfLines({"primera linea", "segunda linea"}, 0, 0, ed);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 0;

    press(ed, EventType::PageDown);  // -> (1,0): final de la ultima linea

    ed.handleEvent(insert('j'));     // bloque anterior

    // j desde el inicio de "segunda" cruza al ultimo bloque de la linea
    // anterior: "linea" de la linea 0, que empieza en el byte 8.
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 8);
}

TEST(combo_k_then_pageup) {
    // Navegacion 'k' (siguiente bloque) y luego RePag.
    Editor ed;
    editorOfLines({"primera linea primera", "segunda"}, 1, 0, ed);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 0;

    ed.handleEvent(insert('k')); // siguiente bloque desde (1,0)
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 7); // fin de "segunda"

    press(ed, EventType::PageUp);   // archivo cabe entero -> RePag al inicio
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().viewport.top, 0);
}

TEST(combo_pageup_then_k) {
    // RePag y luego 'k' (siguiente bloque).
    Editor ed;
    editorOfLines({"primera", "segunda linea"}, 1, 0, ed);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 0;

    press(ed, EventType::PageUp);   // -> (0,0)
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);

    ed.handleEvent(insert('k'));    // siguiente bloque -> fin de "primera"
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 7);
}

// ---------------------------------------------------------------------------
// Tabulacion de la seleccion ('}' indenta, '{' desindenta) -- solo en el
// modo Seleccion y solo si hay un rango NO vacio.
// ---------------------------------------------------------------------------
TEST(selection_indent_adds_one_level_to_touched_lines) {
    Editor ed;
    editorOfLines({"foo", "bar", "baz"}, 0, 0, ed);
    enterSeleccion(ed);                // anchor (0,0)
    press(ed, EventType::MoveDown);    // (1,0)
    press(ed, EventType::MoveRight);   // (1,1): seleccion cubre lineas 0..1
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('}'));
    CHECK_EQ(ed.active().document.lineAt(0), "    foo");
    CHECK_EQ(ed.active().document.lineAt(1), "    bar");
    CHECK_EQ(ed.active().document.lineAt(2), "baz");  // fuera del rango intacta
    CHECK(ed.active().modified);
    // Sigue en Seleccion con la seleccion viva (el '}' no sale del modo).
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
}

TEST(selection_dedent_removes_one_level) {
    Editor ed;
    editorOfLines({"    foo", "    bar", "    baz"}, 0, 4, ed);
    enterSeleccion(ed);                // anchor (0,4)
    press(ed, EventType::MoveDown);    // (1,4)
    press(ed, EventType::MoveRight);   // (1,5)
    ed.handleEvent(insert('{'));
    CHECK_EQ(ed.active().document.lineAt(0), "foo");
    CHECK_EQ(ed.active().document.lineAt(1), "bar");
    CHECK_EQ(ed.active().document.lineAt(2), "    baz"); // no la toca
}

TEST(selection_indent_repeated_presses_add_more) {
    Editor ed;
    editorOfLines({"foo"}, 0, 0, ed);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);   // (0,1): rango no vacio linea 0
    ed.handleEvent(insert('}'));
    ed.handleEvent(insert('}'));
    CHECK_EQ(ed.active().document.lineAt(0), "        foo");
    press(ed, EventType::Undo);        // undo global, disponible en Seleccion
    CHECK_EQ(ed.active().document.lineAt(0), "    foo");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "foo");
}

TEST(selection_indent_requires_actual_selection) {
    Editor ed;
    editorOfLines({"foo", "bar"}, 0, 0, ed);
    enterSeleccion(ed);                // anchor == position, sin rango aun
    CHECK(!ed.hasSelection());
    const size_t undoBefore = ed.active().undoStack.size();
    const bool modified = ed.active().modified;

    ed.handleEvent(insert('}'));
    ed.handleEvent(insert('{'));
    CHECK_EQ(ed.active().document.lineAt(0), "foo");
    CHECK_EQ(ed.active().document.lineAt(1), "bar");
    CHECK_EQ(ed.active().undoStack.size(), undoBefore); // sin entradas vacias
    CHECK_EQ(ed.active().modified, modified);           // no marca como modificado
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    // El gate es hasSelection() (anchor != position): con una seleccion
    // degenerada (recien entrado al modo, sin mover el cursor) se debe
    // avisar y no tocar nada.
    CHECK_EQ(ed.statusMessage_, "Nada seleccionado.");
}

TEST(selection_indent_single_line_keeps_selection_and_cursor_coherent) {
    // Seleccion REAL de una sola linea y parcial (dentro de "bar"): al
    // indentar, la linea se tabula pero el cursor y la seleccion se
    // desplazan con el +4, conservando el mismo texto seleccionado.
    Editor ed;
    editorOfLines({"bar"}, 0, 1, ed);  // cursor (0,1)
    enterSeleccion(ed);                // anchor (0,1)
    press(ed, EventType::MoveRight);   // (0,2): seleccion [1,2) sobre "bar"
    const auto before = ed.selection();
    CHECK(before.has_value());
    CHECK_EQ(before->start.col, 1);
    CHECK_EQ(before->end.col, 2);
    const int cursorBefore = ed.active().cursor.col;

    ed.handleEvent(insert('}'));
    CHECK_EQ(ed.active().document.lineAt(0), "    bar");
    auto sel = ed.selection();
    CHECK(sel.has_value());
    // Cursor y seleccion se desplazaron +4, apuntando a la misma zona.
    CHECK_EQ(sel->start.col, before->start.col + 4);
    CHECK_EQ(sel->end.col, before->end.col + 4);
    CHECK_EQ(ed.active().cursor.col, cursorBefore + 4);
    // La seleccion sigue cubriendo el mismo texto ("a" de "bar").
    CHECK_EQ(ed.active().document.lineAt(0).substr(sel->start.col,
                                                   sel->end.col - sel->start.col),
             "a");
}

TEST(selection_indent_single_line_partial_selection_indents_whole_line) {
    // Seleccion que solo toca parte de una linea (comienzo de la linea
    // dentro del rango): la linea entera se tabula.
    Editor ed;
    editorOfLines({"if (a) {", "    bar", "}"}, 1, 2, ed);
    enterSeleccion(ed);                // anchor (1,2)
    press(ed, EventType::MoveLeft);    // (1,1): rango dentro de la linea 1
    ed.handleEvent(insert('}'));
    CHECK_EQ(ed.active().document.lineAt(1), "        bar");
    CHECK_EQ(ed.active().document.lineAt(0), "if (a) {");
    CHECK_EQ(ed.active().document.lineAt(2), "}");
}

TEST(selection_indent_excludes_line_when_selection_ends_at_col_0) {
    // La seleccion termina justo en el COMIENZO de la ultima linea
    // (sel->end.col == 0): esa linea NO quedo incluida y no se tabula,
    // igual que el Renderer no la marca como seleccionada (endsAtStart).
    Editor ed;
    editorOfLines({"a", "b", "c"}, 0, 0, ed);
    enterSeleccion(ed);               // anchor (0,0)
    press(ed, EventType::MoveDown);   // (1,0): seleccion [0,0)..(1,0)
    press(ed, EventType::MoveDown);   // (2,0): [0,0)..(2,0), end.col == 0
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('}'));
    CHECK_EQ(ed.active().document.lineAt(0), "    a");
    CHECK_EQ(ed.active().document.lineAt(1), "    b");
    CHECK_EQ(ed.active().document.lineAt(2), "c");   // NO se tabula
}

TEST(selection_dedent_excludes_line_when_selection_ends_at_col_0) {
    Editor ed;
    editorOfLines({"    a", "    b", "    c"}, 0, 0, ed);
    enterSeleccion(ed);               // anchor (0,0)
    press(ed, EventType::MoveDown);   // (1,0)
    press(ed, EventType::MoveDown);   // (2,0): end.col == 0 en linea 2
    ed.handleEvent(insert('{'));
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    CHECK_EQ(ed.active().document.lineAt(1), "b");
    CHECK_EQ(ed.active().document.lineAt(2), "    c");  // intacta
}

TEST(select_all_tabular_indents_whole_file_and_keeps_mode_on) {
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 1, 1, ed);
    enterSeleccion(ed);                 // 's' -> modo seleccion
    ed.handleEvent(insert('a'));        // seleccion total [0,0]..[2,3)
    CHECK(ed.active().selectAllActive);
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('}'));
    CHECK(ed.active().selectAllActive); // el modo 'a' sigue activo
    CHECK_EQ(ed.active().document.lineAt(0), "    aaa");
    CHECK_EQ(ed.active().document.lineAt(1), "    bbb");
    CHECK_EQ(ed.active().document.lineAt(2), "    ccc");
    CHECK(ed.active().modified);

    // Un '{' mas desindenta todo el archivo de nuevo.
    ed.handleEvent(insert('{'));
    CHECK_EQ(ed.active().document.lineAt(0), "aaa");
    CHECK_EQ(ed.active().document.lineAt(1), "bbb");
    CHECK_EQ(ed.active().document.lineAt(2), "ccc");
}

// ---------------------------------------------------------------------------
// Tabulacion de la seleccion: casos 5-7 (historial, mezcla y push unico)
// ---------------------------------------------------------------------------
TEST(selection_dedent_no_indentation_no_history_noop) {
    // Desindentar un rango donde NINGUNA linea tiene indentacion: no se
    // pushea historial, no se marca modified y se avisa.
    Editor ed;
    editorOfLines({"foo", "bar", "baz"}, 0, 0, ed);
    enterSeleccion(ed);                // anchor (0,0)
    press(ed, EventType::MoveDown);    // (1,0)
    press(ed, EventType::MoveRight);   // (1,1): seleccion lineas 0..1
    const size_t undoBefore = ed.active().undoStack.size();
    const bool modified = ed.active().modified;

    ed.handleEvent(insert('{'));
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);   // sin push
    CHECK_EQ(ed.active().modified, modified);
    CHECK_EQ(ed.active().document.lineAt(0), "foo");
    CHECK_EQ(ed.active().document.lineAt(1), "bar");
    CHECK_EQ(ed.statusMessage_, "Nada que tabular.");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

TEST(selection_dedent_partial_indentation_only_touches_indented) {
    // Algunas lineas indentadas y otras no: solo las que tienen indentacion
    // cambian; willChange detecta el caso general (no "todas o ninguna").
    Editor ed;
    editorOfLines({"    foo", "bar", "    baz"}, 0, 4, ed);
    enterSeleccion(ed);                // anchor (0,4)
    press(ed, EventType::MoveDown);    // (1,4)
    press(ed, EventType::MoveDown);    // (2,4): seleccion lineas 0..2

    ed.handleEvent(insert('{'));
    CHECK_EQ(ed.active().document.lineAt(0), "foo");       // tenia -> cambia
    CHECK_EQ(ed.active().document.lineAt(1), "bar");       // no tenia -> intacta
    CHECK_EQ(ed.active().document.lineAt(2), "baz");       // tenia -> cambia
    CHECK(ed.active().modified);

    // Un solo undo revierte las 3 lineas a la vez.
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "    foo");
    CHECK_EQ(ed.active().document.lineAt(1), "bar");
    CHECK_EQ(ed.active().document.lineAt(2), "    baz");
}

TEST(selection_indent_undo_redo_single_operation) {
    // Un solo pushHistory cubre el rango entero: un undo() revierte las N
    // lineas en una operacion y un redo() reaplica el cambio completo.
    Editor ed;
    editorOfLines({"aaa", "bbb", "ccc"}, 0, 0, ed);
    enterSeleccion(ed);                // anchor (0,0)
    press(ed, EventType::MoveDown);    // (1,0)
    press(ed, EventType::MoveDown);    // (2,0)
    press(ed, EventType::MoveRight);   // (2,1): seleccion lineas 0..2
    CHECK(!ed.active().modified);

    ed.handleEvent(insert('}'));
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "    aaa");
    CHECK_EQ(ed.active().document.lineAt(1), "    bbb");
    CHECK_EQ(ed.active().document.lineAt(2), "    ccc");

    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "aaa");
    CHECK_EQ(ed.active().document.lineAt(1), "bbb");
    CHECK_EQ(ed.active().document.lineAt(2), "ccc");

    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "    aaa");
    CHECK_EQ(ed.active().document.lineAt(1), "    bbb");
    CHECK_EQ(ed.active().document.lineAt(2), "    ccc");
}

// ---------------------------------------------------------------------------
// Tabulacion de la seleccion: modo, contexto y limites del documento
// ---------------------------------------------------------------------------
TEST(selection_indent_does_not_change_mode) {
    // A diferencia de 'c'/'x' (que salen a Navegacion), '}'/'{' se quedan
    // en Seleccion y mantienen la seleccion vigente (desplazada, no borrada).
    Editor ed;
    editorOfLines({"aaa", "bbb"}, 0, 0, ed);
    enterSeleccion(ed);                // anchor (0,0)
    press(ed, EventType::MoveDown);    // (1,0)
    press(ed, EventType::MoveRight);   // (1,1)
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('}'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('{'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());          // sigue con rango tras '}'-'{'
    CHECK_EQ(ed.active().document.lineAt(0), "aaa");
}

TEST(selection_braces_are_noop_in_navegacion) {
    // En Navegacion no hay edicion: '}'-'{' no hacen nada (no comando).
    Editor ed;
    editorOfLines({"abc"}, 0, 1, ed);
    const bool modified = ed.active().modified;
    const size_t undo = ed.active().undoStack.size();
    ed.handleEvent(insert('}'));
    ed.handleEvent(insert('{'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
    CHECK_EQ(ed.active().modified, modified);
    CHECK_EQ(ed.active().undoStack.size(), undo);
}

TEST(selection_braces_are_text_in_interaccion) {
    // En Interaccion '}'/'{' son texto normal y se insertan, como cualquier
    // otra letra (no son comandos de tabulacion ahi).
    Editor ed;
    editorOfLines({""}, 0, 0, ed);
    type(ed, "{}");
    CHECK_EQ(ed.active().document.lineAt(0), "{}");
}

TEST(selection_indent_document_boundaries) {
    // Indentar en la primera linea y en la ultima no rompe ni desborda.
    // Primera linea:
    Editor ed1;
    editorOfLines({"first", "mid", "last"}, 0, 0, ed1);
    enterSeleccion(ed1);
    press(ed1, EventType::MoveRight);   // (0,1)
    ed1.handleEvent(insert('}'));
    CHECK_EQ(ed1.active().document.lineAt(0), "    first");
    CHECK_EQ(ed1.active().document.lineCount(), 3);

    // Ultima linea (contenido dentro de ella):
    Editor ed2;
    editorOfLines({"first", "mid", "last"}, 2, 2, ed2);   // cursor (2,2)
    enterSeleccion(ed2);
    press(ed2, EventType::MoveRight);   // (2,3)
    ed2.handleEvent(insert('}'));
    CHECK_EQ(ed2.active().document.lineAt(2), "    last");
    CHECK_EQ(ed2.active().document.lineCount(), 3);

    // Seleccion que "roza" el inicio de la ultima linea: esa linea NO se
    // tabula (mismo criterio de lineBreakSelected), sin desbordar.
    Editor ed3;
    editorOfLines({"a", "b", "c", "d"}, 0, 0, ed3);
    enterSeleccion(ed3);
    press(ed3, EventType::MoveDown);    // (1,0)
    press(ed3, EventType::MoveDown);    // (2,0)
    press(ed3, EventType::MoveDown);    // (3,0): end.col == 0 en la ultima
    ed3.handleEvent(insert('}'));
    CHECK_EQ(ed3.active().document.lineAt(0), "    a");
    CHECK_EQ(ed3.active().document.lineAt(1), "    b");
    CHECK_EQ(ed3.active().document.lineAt(2), "    c");
    CHECK_EQ(ed3.active().document.lineAt(3), "d");   // intacta
    CHECK_EQ(ed3.active().document.lineCount(), 4);
}
