#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_framework.h"

#include <string>
#include <vector>
#define private public
#include "Editor.h"
#undef private

using testfw::TempFile;

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

static void type(Editor& ed, const std::string& s) {
    if (s.empty()) return;
    // v0.5: escribir requiere el modo Interaccion (letra 'i').
    if (ed.state_ != State::Interaccion) {
        if (ed.state_ == State::Seleccion) {
            ed.handleEvent(escapeEvent());
        }
        ed.handleEvent(insert('i'));
    }
    for (char c : s)
        ed.handleEvent(insert(c));
}

static void press(Editor& ed, EventType type) {
    Event e;
    e.type = type;
    ed.handleEvent(e);
}

// Entra al modo seleccion con la letra 's' (desde Navegacion).
static void enterSeleccion(Editor& ed) {
    if (ed.state_ != State::Seleccion) {
        if (ed.state_ == State::Interaccion) {
            ed.handleEvent(escapeEvent());
        }
        ed.handleEvent(insert('s'));
    }
}

static void prefix(Editor& ed, EventType first, EventType second) {
    press(ed, first);
    press(ed, second);
}

static std::string fileContent(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// v0.5: modo Navegacion (estado por defecto)
// ---------------------------------------------------------------------------
TEST(navigation_starts_in_navegacion) {
    Editor ed;
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(!ed.hasSelection());
}

TEST(navigation_typing_does_not_insert) {
    // En navegacion "no se puede escribir": las letras se ignoran salvo
    // los comandos de modo ('i'/'s').
    Editor ed;
    ed.handleEvent(insert('a'));
    ed.handleEvent(insert('b'));
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK(!ed.active().modified);
}

TEST(navigation_i_enters_interaction) {
    Editor ed;
    ed.handleEvent(insert('i'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(navigation_s_enters_selection) {
    Editor ed;
    ed.handleEvent(insert('s'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    // Sin haber movido el cursor: modo activo pero sin texto marcado.
    CHECK(!ed.hasSelection());
}

// ---------------------------------------------------------------------------
// Transicion Navegacion -> Seleccion (s)
// ---------------------------------------------------------------------------
TEST(navigation_s_does_not_modify_document) {
    // 's' solo cambia el modo: no inserta, no borra, no crea undo.
    Editor ed;
    type(ed, "abc");              // doc "abc", con historial
    press(ed, EventType::Escape); // -> Navegacion
    size_t undoBefore = ed.active().undoStack.size();
    CHECK(ed.active().modified);
    ed.handleEvent(insert('s'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK_EQ(ed.active().document.lineAt(0), "abc");  // el doc queda tal cual
    CHECK_EQ(ed.active().undoStack.size(), undoBefore); // 's' NO crea undo
    CHECK(ed.active().modified);                         // ni toca la bandera
}

TEST(navigation_s_anchor_equals_cursor_initial) {
    // Al entrar con 's' (sin mover el cursor), anchor == cursor.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape);
    ed.handleEvent(insert('s'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.active().selection.has_value());
    CHECK_EQ(ed.active().selection->anchor.line, ed.active().cursor.line);
    CHECK_EQ(ed.active().selection->anchor.col, ed.active().cursor.col);
    CHECK_EQ(ed.active().selection->position.line, ed.active().cursor.line);
    CHECK_EQ(ed.active().selection->position.col, ed.active().cursor.col);
    // anchor == position => no hay texto seleccionado todavia.
    CHECK(!ed.hasSelection());
}

TEST(navigation_s_then_right_produces_selection) {
    // s -> Right: la seleccion extiende y ya hay texto marcado.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveLeft); // cursor (0,2), no en el fin de linea
    ed.handleEvent(insert('s'));
    CHECK(!ed.hasSelection());
    press(ed, EventType::MoveRight); // extiende a (0,3)
    CHECK(ed.hasSelection());        // ya hay rango no vacio
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

TEST(navigation_s_then_right_selects_from_anchor) {
    // Con cursor en medio del texto, s -> Right marca exactamente 1 char.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveLeft);  // cursor (0,2)
    ed.handleEvent(insert('s'));     // anchor = (0,2)
    press(ed, EventType::MoveRight); // position = (0,3)
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 3);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

TEST(navigation_pcx_noop) {
    // En navegacion 'c'/'x' no hacen nada. 'p' sin buffer es no-op sobre
    // el documento (solo informa "Nada para pegar."); con buffer pega
    // (se cubre en los tests v0.55 de buffer).
    Editor ed;
    ed.handleEvent(insert('c'));
    ed.handleEvent(insert('x'));
    ed.handleEvent(insert('p'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "");
}

TEST(navigation_movement_free_no_selection) {
    // Las flechas/Home/End se mueven libremente sin iniciar seleccion.
    Editor ed;
    type(ed, "abc");              // Interaccion
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(navigation_escape_noop) {
    // En navegacion el ESC no tiene a donde volver: no-op.
    Editor ed;
    press(ed, EventType::Escape);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(navigation_backspace_delete_enter_noop) {
    Editor ed;
    press(ed, EventType::Backspace);
    press(ed, EventType::Delete);
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK(!ed.active().modified);
}

// ---------------------------------------------------------------------------
// v0.5: modo Interaccion (edicion libre)
// ---------------------------------------------------------------------------
TEST(interaction_types_every_letter_literal) {
    // En Interaccion toda letra se inserta como texto, incluida i/s/c/x/p.
    Editor ed;
    type(ed, "iscxp");
    CHECK_EQ(ed.active().document.lineAt(0), "iscxp");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(interaction_escape_returns_navegacion) {
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
}

// ---------------------------------------------------------------------------
// Transicion Interaccion -> Navegacion (ESC)
// ---------------------------------------------------------------------------
TEST(interaction_escape_does_not_modify_document) {
    Editor ed;
    type(ed, "abc");
    CHECK(ed.active().modified);
    size_t before = ed.active().undoStack.size();
    press(ed, EventType::Escape);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "abc"); // el doc queda tal cual
    CHECK(ed.active().modified);                     // el estado guardado sigue pendiente
    CHECK_EQ(ed.active().undoStack.size(), before);  // ESC NO crea entrada de undo
}

TEST(interaction_escape_does_not_clear_redo) {
    // Tras un undo queda redo pendiente; ESC desde Interaccion no lo toca.
    // El undo en si deja Navegacion (el historial no distingue Navegacion de
    // Interaccion sin seleccion), asi que volvemos a Interaccion con 'i'
    // antes del ESC para ejercitar el camino Interaccion -> ESC de verdad.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Undo);   // -> "ab", redo pendiente
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    CHECK(!ed.active().redoStack.empty());
    size_t redoBefore = ed.active().redoStack.size();
    size_t undoBefore = ed.active().undoStack.size();
    ed.handleEvent(insert('i'));  // Interaccion explicito
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    press(ed, EventType::Escape);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().redoStack.size(), redoBefore); // el redo sigue vivo
    CHECK_EQ(ed.active().undoStack.size(), undoBefore); // y nada nuevo en undo
}

TEST(interaction_escape_then_char_is_noop) {
    // Tras ESC, un caracter no se escribe: estamos en Navegacion.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    ed.handleEvent(insert('z'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "abc"); // 'z' no se inserto
}

TEST(interaction_escape_then_i_returns_to_interaccion) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    ed.handleEvent(insert('i'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(interaction_cycle_i_type_esc_i_type) {
    // i -> escribir -> ESC -> i -> escribir debe acumular exactamente
    // el texto esperado, sin perdidas ni repeticiones.
    Editor ed;
    type(ed, "ho");
    press(ed, EventType::Escape);
    type(ed, "la");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
    CHECK_EQ(ed.active().cursor.col, 4);
}

TEST(interaction_newline_and_backspace_work) {
    Editor ed;
    type(ed, "ab");
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.active().document.lineCount(), 2);
    press(ed, EventType::Backspace); // une de nuevo
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
}

TEST(interaction_does_not_interpret_i_as_command) {
    // La 'i' dentro de Interaccion es texto real, no un comando.
    Editor ed;
    type(ed, "ai");
    CHECK_EQ(ed.active().document.lineAt(0), "ai");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

// ---------------------------------------------------------------------------
// Transicion Navegacion -> Interaccion
// ---------------------------------------------------------------------------
TEST(navigation_i_changes_state_to_interaccion) {
    Editor ed;
    ed.handleEvent(insert('i'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(navigation_i_alone_does_not_modify_document) {
    // Pulsar 'i' solo cambia el modo: no inserta texto ni toca el doc.
    Editor ed;
    CHECK(!ed.active().modified);
    ed.handleEvent(insert('i'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(navigation_i_then_char_inserts_single) {
    // Tras 'i', el siguiente caracter se inserta de verdad.
    Editor ed;
    ed.handleEvent(insert('i'));
    ed.handleEvent(insert('a'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK(ed.active().modified);
}

TEST(navigation_i_then_many_chars_insert) {
    // Varios caracteres seguidos tras 'i' se acumulan en orden.
    Editor ed;
    ed.handleEvent(insert('i'));
    for (char c : std::string("hola mundo"))
        ed.handleEvent(insert(c));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    CHECK_EQ(ed.active().document.lineAt(0), "hola mundo");
    CHECK_EQ(ed.active().cursor.col, 10);
}

TEST(navigation_re_i_in_interaccion_inserts_text) {
    // Entrar otra vez con 'i' estando ya en Interaccion NO es una
    // transicion: se inserta como texto y el modo no cambia.
    Editor ed;
    ed.handleEvent(insert('i'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    ed.handleEvent(insert('i')); // segunda 'i' = texto
    ed.handleEvent(insert('b'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    CHECK_EQ(ed.active().document.lineAt(0), "ib"); // no se queda en "b"
}

// ---------------------------------------------------------------------------
// Permanencia en Interaccion: a/i/s/c/x/p son texto, no comandos
// ---------------------------------------------------------------------------
TEST(interaction_a_inserts_literal) {
    Editor ed;
    type(ed, "a");
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(interaction_i_inserts_literal) {
    Editor ed;
    type(ed, "i");
    CHECK_EQ(ed.active().document.lineAt(0), "i");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(interaction_s_inserts_literal) {
    // 's' en Interaccion es texto: NO entra al modo Seleccion.
    Editor ed;
    type(ed, "s");
    CHECK_EQ(ed.active().document.lineAt(0), "s");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(interaction_c_inserts_literal) {
    // 'c' en Interaccion es texto: NO copia ni sale al modo Navegacion.
    Editor ed;
    type(ed, "c");
    CHECK_EQ(ed.active().document.lineAt(0), "c");
    CHECK(ed.clipboard_.empty());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(interaction_x_inserts_literal) {
    // 'x' en Interaccion es texto: NO corta ni entra a Navegacion.
    Editor ed;
    type(ed, "x");
    CHECK_EQ(ed.active().document.lineAt(0), "x");
    CHECK(ed.clipboard_.empty());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(interaction_p_inserts_literal) {
    // 'p' en Interaccion es texto: NO pega nada del buffer.
    Editor ed;
    ed.clipboard_ = std::vector<std::string>{"sei"};
    type(ed, "p");
    CHECK_EQ(ed.active().document.lineAt(0), "p"); // no se pego "sei"
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

// ---------------------------------------------------------------------------
// v0.5: modo Seleccion
// ---------------------------------------------------------------------------
TEST(selection_s_enters_and_arrows_extend) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight); // [a]
    press(ed, EventType::MoveRight); // [ab]
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.col, 2);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

TEST(selection_c_exits_to_navegacion) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    ed.handleEvent(insert('c'));
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "abc"); // copiar no modifica el documento
}

TEST(selection_x_exits_to_navegacion) {
    // v0.55: 'x' copia el rango al buffer y lo borra del documento.
    // La "a" queda cortada: doc "bc", buffer ["a"].
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight); // [a]
    ed.handleEvent(insert('x'));
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "bc");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"a"}));
    CHECK(ed.active().modified);
}

TEST(selection_escape_cancels) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    press(ed, EventType::Escape);
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(selection_char_does_not_replace) {
    // v0.5: escribir una letra (que no sea c/x) ya NO reemplaza la
    // seleccion: se ignora y el modo sigue activo.
    Editor ed;
    type(ed, "hello");
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight); // [h]
    ed.handleEvent(insert('H'));
    CHECK_EQ(ed.active().document.lineAt(0), "hello"); // sin reemplazo
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

TEST(selection_newline_backspace_delete_noop) {
    // Ninguna de estas teclas borra/afecta la seleccion en v0.5.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);
    press(ed, EventType::InsertNewline);
    press(ed, EventType::Backspace);
    press(ed, EventType::Delete);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

// ---------------------------------------------------------------------------
// v0.5: prefijo Ctrl+K (guardar / salir)
// ---------------------------------------------------------------------------
TEST(prefix_save_saves_file) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hola");
    CHECK(ed.active().modified);
    prefix(ed, EventType::Prefix, EventType::Save); // Ctrl+K, Ctrl+S
    CHECK(!ed.active().modified);
    CHECK_EQ(fileContent(f.path), "hola");
}

TEST(prefix_save_returns_to_navegacion) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "abc");
    press(ed, EventType::Escape); // -> Navegacion
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(prefix_save_keeps_interaction_mode) {
    // Si el prefijo se abrio estando en Interaccion, al guardar se vuelve
    // a Interaccion (no a Navegacion).
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "abc");              // Interaccion
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(prefix_quit_sets_running_false) {
    Editor ed;
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);
}

TEST(prefix_other_key_cancels_and_discards) {
    // Ctrl+K + una flecha: se descarta todo y se vuelve al estado previo
    // sin mover el cursor (el evento de la flecha no se propaga).
    Editor ed;
    type(ed, "abc");              // Interaccion, cursor (0,3)
    press(ed, EventType::Escape); // -> Navegacion
    press(ed, EventType::Prefix);
    press(ed, EventType::MoveRight);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(ed.hasSelection() == false);
    CHECK_EQ(ed.statusMessage_, "Comando cancelado.");
}

TEST(prefix_cancel_from_interaction_returns_interaction) {
    // El prefijo recuerda el estado previo: cancelar desde Interaccion
    // vuelve a Interaccion.
    Editor ed;
    type(ed, "abc");              // Interaccion
    press(ed, EventType::Prefix);
    press(ed, EventType::MoveRight); // cancela (no guarda/sale)
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    CHECK_EQ(ed.active().cursor.col, 3);      // la flecha NO se propago
}

TEST(prefix_cancel_keeps_selection) {
    // Ctrl+K + tecla que no sea guardar/salir: se cancela el prefijo
    // pero la seleccion (si habia) NO se toca.
    Editor ed;
    type(ed, "hello");
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight); // [h]
    CHECK(ed.hasSelection());
    prefix(ed, EventType::Prefix, EventType::MoveRight);
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

TEST(prefix_save_from_selection_keeps_mode) {
    // Ctrl+K + Ctrl+S dentro del modo seleccion guarda y mantiene la
    // seleccion activa.
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hello");
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    CHECK(ed.active().modified);

    prefix(ed, EventType::Prefix, EventType::Save); // Ctrl+K, Ctrl+S
    CHECK(!ed.active().modified);
    CHECK_EQ(fileContent(f.path), "hello");
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

TEST(prefix_quit_in_selection_quits) {
    Editor ed;
    enterSeleccion(ed);
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);
}

// ---------------------------------------------------------------------------
// v0.5: Undo/Redo disponibles en los tres modos
// ---------------------------------------------------------------------------
TEST(undo_after_interaction_returns_navegacion) {
    // El historial no sabe distinguir Navegacion de Interaccion: un undo
    // desde Interaccion (sin seleccion) vuelve a Navegacion.
    Editor ed;
    type(ed, "abc");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(undo_redo_work_in_selection_mode) {
    // Undo/Redo se evaluan antes del despacho por modo, asi que funcionan
    // incluso dentro del modo seleccion.
    Editor ed;
    type(ed, "x");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    enterSeleccion(ed);           // entramos a seleccion (sin seleccion)
    press(ed, EventType::Redo);   // el redo se aplica igual
    CHECK_EQ(ed.active().document.lineAt(0), "x");
}

TEST(redo_restores_content_in_navegacion) {
    Editor ed;
    type(ed, "x");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "x");
}

TEST(selection_does_not_clear_redo) {
    // Entrar en seleccion es estado, no edicion: no consume el redo.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Undo);   // -> "ab", redo pendiente "abc"
    CHECK(!ed.active().redoStack.empty());
    press(ed, EventType::MoveLeft);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight); // selecciona "b"
    CHECK(ed.hasSelection());
    press(ed, EventType::Redo);   // el redo sigue vivo
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
}

// ---------------------------------------------------------------------------
// v0.5: edicion sin pasar por 'i' no modifica el archivo al abrir
// ---------------------------------------------------------------------------
TEST(open_file_starts_in_navegacion) {
    TempFile f;
    f.write("contenido");
    Editor ed;
    CHECK(ed.openFile(f.path));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    ed.handleEvent(insert('x'));  // no se inserta: navegacion
    CHECK_EQ(ed.active().document.lineAt(0), "contenido");
}

// ---------------------------------------------------------------------------
// Estado inicial de la maquina de estados
//   El editor debe comenzar SIEMPRE en Navegacion, con un cursor valido,
//   sin seleccion, sin portapapeles y sin cambios pendientes.
// ---------------------------------------------------------------------------
// Invariantes del estado inicial: cursor valido (dentro del documento),
// ninguna seleccion, clipboard_ vacio y modified_ == false.
static void assertInitialState(const Editor& ed) {
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    // Cursor valido: apunta a una posicion existente del documento.
    CHECK(ed.active().cursor.line >= 0);
    CHECK(ed.active().cursor.col >= 0);
    CHECK(ed.active().cursor.line < ed.active().document.lineCount());
    CHECK(ed.active().cursor.col <= ed.active().document.lineLength(ed.active().cursor.line));

    // Ninguna seleccion activa.
    CHECK(!ed.hasSelection());

    // Portapapeles vacio.
    CHECK(ed.clipboard_.empty());

    // Sin cambios sin guardar.
    CHECK(!ed.active().modified);
}

TEST(initial_state_fresh_editor_is_navegacion) {
    // Editor recien creado (aun sin abrir archivo alguno).
    Editor ed;
    assertInitialState(ed);
    CHECK_EQ(ed.active().document.lineCount(), 1); // documento vacio con una linea
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(initial_state_empty_document_is_navegacion) {
    // Documento vacio (editor sin archivo abierto): estado por defecto.
    Editor ed;
    CHECK_EQ(ed.active().document.lineCount(), 1);
    assertInitialState(ed);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(initial_state_open_new_file_is_navegacion) {
    // Abrir un archivo que no existe: se crea vacio y hay que empezar
    // igual en Navegacion.
    TempFile f;
    Editor ed;
    ed.openFile(f.path); // no existe: devuelve false, lo crea
    assertInitialState(ed);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(initial_state_open_file_with_content_is_navegacion) {
    TempFile f;
    f.write("primera linea\nsegunda linea");
    Editor ed;
    CHECK(ed.openFile(f.path));
    assertInitialState(ed);
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "primera linea");
    CHECK_EQ(ed.active().document.lineAt(1), "segunda linea");
}

TEST(initial_state_open_utf8_file_is_navegacion) {
    // Abrir un archivo con contenido UTF-8 multibyte: igual en Navegacion.
    TempFile f;
    f.write("cafe con \xC3\xB1\xC3\xB1\xC3\xA9 y emoji \xF0\x9F\x98\x80");
    Editor ed;
    CHECK(ed.openFile(f.path));
    assertInitialState(ed);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0),
             "cafe con \xC3\xB1\xC3\xB1\xC3\xA9 y emoji \xF0\x9F\x98\x80");
}

TEST(initial_state_after_save_is_navegacion) {
    // Tras un ciclo editar + guardar (Ctrl+K Ctrl+S) el editor regresa a
    // Navegacion con modified_ == false y sin seleccion.
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hola");                    // Interaccion
    press(ed, EventType::Escape);        // -> Navegacion
    prefix(ed, EventType::Prefix, EventType::Save); // guarda -> vuelve
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(!ed.active().modified);
    CHECK(ed.clipboard_.empty()); // sin portapapeles
    CHECK(!ed.hasSelection());
    CHECK(ed.active().cursor.line >= 0 && ed.active().cursor.col >= 0);
}

TEST(initial_state_after_quit_ends_running) {
    // Quit (Ctrl+K Ctrl+Q) detiene el bucle: running_ == false.
    Editor ed;
    assertInitialState(ed);
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);
    CHECK(!ed.active().modified);
    CHECK(ed.clipboard_.empty());
}

TEST(initial_state_after_cancel_prefix_returns_navegacion) {
    // Ctrl+K + tecla cualquiera cancela el prefijo y vuelve al estado
    // previo sin introducir cambios: Navegacion limpia.
    Editor ed;
    prefix(ed, EventType::Prefix, EventType::MoveRight);
    assertInitialState(ed);
}

// ---------------------------------------------------------------------------
// v0.55: buffer copiar/cortar/pegar
// ---------------------------------------------------------------------------
// Helper: deja el editor con el cursor en Home y baja a una seleccion
// del rango [0, n) de la linea actual (input y seleccion comparten linea).
// Devuelve el rango seleccionado via ed.hasSelection() si n > 0.
static void selectChars(Editor& ed, int n) {
    press(ed, EventType::MoveHome); // -> cursor col 0
    if (ed.state_ != State::Seleccion) {
        enterSeleccion(ed);
    }
    for (int i = 0; i < n; ++i) {
        press(ed, EventType::MoveRight);
    }
}

TEST(clipboard_c_copies_without_removing) {
    Editor ed;
    type(ed, "abc");               // Interaccion, cursor (0,3); modifica + historial
    size_t undoBefore = ed.active().undoStack.size();
    selectChars(ed, 2);            // selecciona "ab"
    CHECK(ed.hasSelection());
    ed.handleEvent(insert('c'));
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "abc");     // copiar no borra
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ab"}));
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);  // copiar no muta: sin pushHistory
}

TEST(clipboard_c_with_empty_selection_copies_nothing) {
    // 'c' sobre una seleccion vacia (anchor == position) NO debe tocar el
    // buffer: si el usuario habia copiado algo antes, se preserva. El gate
    // correcto es hasSelection(), no selection().has_value() (que es true
    // incluso para un objeto Selection vacio; ver bug corregido en v0.55).
    Editor ed;
    type(ed, "abc");
    ed.clipboard_ = std::vector<std::string>{"precioso"}; // contenido previo
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);            // modo seleccion, pero sin texto marcado
    CHECK(!ed.hasSelection());
    ed.handleEvent(insert('c'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.clipboard_ == std::vector<std::string>{"precioso"}); // intacto
    CHECK_EQ(ed.statusMessage_, "Nada seleccionado.");
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
}

TEST(clipboard_x_cuts_and_pushes_history) {
    Editor ed;
    type(ed, "abc");
    selectChars(ed, 2);            // selecciona "ab"
    ed.handleEvent(insert('x'));
    CHECK_EQ(ed.active().document.lineAt(0), "c");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ab"}));
    CHECK_EQ(ed.active().cursor.col, 0);   // cursor reposicionado al inicio del rango
    CHECK(ed.active().modified);
    CHECK(!ed.active().undoStack.empty()); // cortar SI entra al historial
}

TEST(clipboard_x_with_empty_selection_cuts_nothing) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Undo);    // deja algo pendiente en redoStack_
    CHECK(!ed.active().redoStack.empty());
    size_t undoBefore = ed.active().undoStack.size();
    size_t redoBefore = ed.active().redoStack.size();
    ed.clipboard_ = std::vector<std::string>{"precioso"}; // contenido previo
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    CHECK(!ed.hasSelection());
    ed.handleEvent(insert('x'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.clipboard_ == std::vector<std::string>{"precioso"}); // intacto
    CHECK_EQ(ed.statusMessage_, "Nada seleccionado.");
    CHECK_EQ(ed.active().document.lineAt(0), "ab");      // el undo la dejo en "ab"
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);  // sin mutation: no pushHistory
    CHECK_EQ(ed.active().redoStack.size(), redoBefore);  // el redo NO se pierde
}

TEST(clipboard_p_with_empty_buffer_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape);   // -> Navegacion
    // 'p' con buffer vacio es no-op sobre el documento y NO entra al
    // historial (cuenta de undo antes/despues identica).
    size_t undoBefore = ed.active().undoStack.size();
    ed.handleEvent(insert('p'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "abc"); // sin cambios
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK_EQ(ed.statusMessage_, "Nada para pegar.");
}

TEST(clipboard_p_pastes_and_repositions_cursor) {
    Editor ed;
    type(ed, "abc");
    selectChars(ed, 2);             // selecciona "ab"
    ed.handleEvent(insert('c'));    // copia "ab" -> Navegacion, cursor (0,0)
    ed.handleEvent(insert('p'));    // pega en (0,0)... cursor real (0,2)
    CHECK_EQ(ed.active().document.lineAt(0), "ababc");
    CHECK_EQ(ed.active().cursor.col, 4);    // cursor al final del bloque pegado (2+2)
    CHECK(ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Pegado.");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(clipboard_p_pastes_multiline_block) {
    // Pegar un bloque multilinea en (line,col) parte la linea actual en
    // col, inserta el bloque y deja el cursor al final de la ultima linea
    // insertada del bloque.
    Editor ed;
    ed.active().document.restore({"Z", "Z"}); // documento de 2 lineas
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.clipboard_ = std::vector<std::string>{"X", "Y"};
    ed.handleEvent(insert('p'));
    CHECK_EQ(ed.active().document.lineCount(), 3);
    CHECK_EQ(ed.active().document.lineAt(0), "X");
    CHECK_EQ(ed.active().document.lineAt(1), "YZ"); // ultima del bloque + cola derecha
    CHECK_EQ(ed.active().document.lineAt(2), "Z");
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 1); // final de la ultima linea insertada
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(clipboard_p_in_interaction_is_literal_p) {
    // La letra 'p' dentro de Interaccion es texto real, no pegar.
    Editor ed;
    type(ed, "ap");
    CHECK_EQ(ed.active().document.lineAt(0), "ap");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

// ---------------------------------------------------------------------------
// 'p' en Interaccion es texto literal (no pega)
// ---------------------------------------------------------------------------
// Los tres significados contextuales de 'p':
//   Navegacion -> pegar
//   Interaccion -> insertar 'p' como texto
//   Seleccion  -> no-op
// ---------------------------------------------------------------------------
TEST(interaction_p_single_char) {
    // i -> p produce el texto "p" (unica linea).
    Editor ed;
    ed.handleEvent(insert('i'));     // -> Interaccion
    ed.handleEvent(insert('p'));
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "p");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(interaction_p_between_text_does_not_paste) {
    // i -> "abc" -> p -> "def" debe dar "abcpdef": la 'p' es un caracter
    // mas, no un pegado del clipboard.
    Editor ed;
    ed.clipboard_ = std::vector<std::string>{"PEGAR"}; // buffer con contenido
    type(ed, "abc");
    ed.handleEvent(insert('p'));     // 'p' literal
    type(ed, "def");
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "abcpdef");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 7);     // 6 letras + 'p'
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    // El clipboard NO se pego ni se toco.
    CHECK(ed.clipboard_ == (std::vector<std::string>{"PEGAR"}));
}

TEST(clipboard_p_in_selection_is_noop) {
    // 'p' en el modo Seleccion cae en el mismo default que cualquier letra
    // que no sea c/x: se ignora, sin pegar y sin salir del modo.
    Editor ed;
    type(ed, "abc");
    selectChars(ed, 1);             // [a]
    CHECK(ed.hasSelection());
    ed.handleEvent(insert('p'));
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK_EQ(ed.active().document.lineAt(0), "abc"); // nada se posiciono
    CHECK(ed.clipboard_.empty());
}

// ---------------------------------------------------------------------------
// 'p' es no-op en Seleccion (aunque haya clipboard con contenido)
// ---------------------------------------------------------------------------
// 'p' tiene tres significados contextuales:
//   Navegacion -> pegar
//   Interaccion -> insertar 'p'
//   Seleccion  -> no-op
// Aqui se verifica el caso Seleccion con clipboard NO vacio: no pega nada,
// no toca documento/clipboard/seleccion/undo/redo y no sale del modo.
// ---------------------------------------------------------------------------
TEST(clipboard_p_in_selection_noop_with_content) {
    Editor ed;
    type(ed, "abcdef");
    ed.clipboard_ = std::vector<std::string>{"PEGAR"}; // buffer con contenido
    press(ed, EventType::Escape);       // -> Navegacion
    press(ed, EventType::MoveHome);     // (0,0)
    enterSeleccion(ed);                 // 's': anchor (0,0)
    press(ed, EventType::MoveRight);    // (0,1)
    press(ed, EventType::MoveRight);    // (0,2): seleccion [0,0)-(0,2)
    press(ed, EventType::MoveRight);    // (0,3): seleccion [0,0)-(0,3)
    CHECK(ed.hasSelection());
    auto selBefore = ed.selection();
    CHECK(selBefore.has_value());
    const auto docBefore = ed.active().document.snapshot();
    const auto clipBefore = ed.clipboard_;
    const size_t undoBefore = ed.active().undoStack.size();
    const size_t redoBefore = ed.active().redoStack.size();

    ed.handleEvent(insert('p'));

    // Documento identico.
    CHECK(ed.active().document.snapshot() == docBefore);
    // Clipboard identico (no se pego ni se sobreescribio).
    CHECK(ed.clipboard_ == clipBefore);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"PEGAR"}));
    // Seleccion identica (el rango sigue vigente).
    CHECK(ed.hasSelection());
    CHECK(ed.selection().has_value());
    CHECK_EQ(ed.selection()->start.line, selBefore->start.line);
    CHECK_EQ(ed.selection()->start.col, selBefore->start.col);
    CHECK_EQ(ed.selection()->end.line, selBefore->end.line);
    CHECK_EQ(ed.selection()->end.col, selBefore->end.col);
    // Undo/Redo intactos (no es una edicion).
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK_EQ(ed.active().redoStack.size(), redoBefore);
    // Sigue en Seleccion.
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

// ---------------------------------------------------------------------------
// v0.55: 'p' desde Navegacion — clipboard vacio
// ---------------------------------------------------------------------------
TEST(clipboard_p_from_navegacion_empty_buffer_noop) {
    // 'p' con clipboard vacio es un no-op total: informa "Nada para pegar.",
    // no toca el documento, no crea undo, no cambia modified_ y deja el
    // cursor exactamente donde estaba.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape);   // -> Navegacion
    ed.active().modified = false;           // simula estado guardado
    ed.active().savedLines = ed.active().document.snapshot();
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight); // cursor (0,1)
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 1);
    const size_t undoBefore = ed.active().undoStack.size();
    const size_t redoBefore = ed.active().redoStack.size();

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.statusMessage_, "Nada para pegar.");
    CHECK_EQ(ed.active().document.lineAt(0), "abc");  // no modifica documento
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().undoStack.size(), undoBefore); // no crea undo
    CHECK_EQ(ed.active().redoStack.size(), redoBefore); // tampoco crea redo
    CHECK(!ed.active().modified);                      // no cambia modified_
    CHECK_EQ(ed.active().cursor.line, 0);              // cursor permanece igual
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

// ---------------------------------------------------------------------------
// v0.55: 'p' desde Navegacion — clipboard con contenido
// ---------------------------------------------------------------------------
TEST(clipboard_p_from_navegacion_single_char) {
    // Pegar un solo caracter: se inserta en el cursor y este avanza 1.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 1;
    ed.clipboard_ = {"X"};
    const size_t undoBefore = ed.active().undoStack.size();

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineAt(0), "aXbc");
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 2);              // 1 + 1
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), undoBefore + 1); // UNA entrada de undo
    CHECK_EQ(ed.statusMessage_, "Pegado.");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(clipboard_p_from_navegacion_multiple_chars) {
    // Pegar varios caracteres: todos se insertan en una sola operacion y
    // el cursor avanza el largo total del bloque.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.clipboard_ = {"hola"};
    const size_t undoBefore = ed.active().undoStack.size();

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineAt(0), "holaabc");
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 4);              // 0 + 4
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), undoBefore + 1); // UNA entrada, no 4
    CHECK_EQ(ed.statusMessage_, "Pegado.");
}

TEST(clipboard_p_from_navegacion_single_line) {
    // Pegar una linea completa dentro de una linea existente no parte nada:
    // se inserta inline y el cursor queda tras el bloque.
    Editor ed;
    ed.active().document.restore({"abc", "def"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 3;
    ed.clipboard_ = {"XYZ"};
    const size_t undoBefore = ed.active().undoStack.size();

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineCount(), 2);    // no se partio la linea
    CHECK_EQ(ed.active().document.lineAt(0), "abcXYZ");
    CHECK_EQ(ed.active().document.lineAt(1), "def");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 6);              // 3 + 3
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), undoBefore + 1);
}

TEST(clipboard_p_from_navegacion_multiple_lines) {
    // Pegar un bloque de varias lineas parte la linea actual en el cursor:
    // la cola derecha se une a la ultima linea del bloque (insertBlock).
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 1;
    ed.clipboard_ = {"X", "Y"};
    const size_t undoBefore = ed.active().undoStack.size();

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "aX");
    CHECK_EQ(ed.active().document.lineAt(1), "Ybc");  // cola derecha pegada al final
    CHECK_EQ(ed.active().cursor.line, 1);             // fin de la ultima linea del bloque
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), undoBefore + 1); // UNA entrada, no 2 lineas
    CHECK_EQ(ed.statusMessage_, "Pegado.");
}

TEST(clipboard_p_from_navegacion_utf8) {
    // El buffer guarda bytes UTF-8 tal cual; pegar inserta el bloque sin
    // reinterpretar codepoints y el cursor avanza en BYTES (2 por "ñ").
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.clipboard_ = {"\xC3\xB1"};   // "ñ"
    const size_t undoBefore = ed.active().undoStack.size();

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineAt(0), std::string("\xC3\xB1") + "abc");
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 2);              // 2 bytes, no 1 caracter
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), undoBefore + 1);
    CHECK_EQ(ed.statusMessage_, "Pegado.");
}

TEST(clipboard_p_from_navegacion_empty_string_element) {
    // clipboard_ es vector<string> y puede contener una cadena vacia como
    // unico elemento. Pegarla no altera el documento ni el cursor
    // (insertBlock con un bloque de una cadena vacia es no-op), pero SÍ
    // cuenta como operacion de pegar: empuja undo y marca modified_.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 1;
    ed.clipboard_ = {""};
    CHECK_EQ(ed.clipboard_.empty(), false);   // el VECTOR no esta vacio
    const size_t undoBefore = ed.active().undoStack.size();

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineAt(0), "abc");  // el texto no cambia
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().cursor.line, 0);             // cursor intacto
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK(ed.active().modified);                      // pegar siempre marca modified_
    CHECK_EQ(ed.active().undoStack.size(), undoBefore + 1); // pegar siempre es undoable
    CHECK_EQ(ed.statusMessage_, "Pegado.");
}

// ---------------------------------------------------------------------------
// v0.55: 'p' en distintas posiciones del documento
// ---------------------------------------------------------------------------
// El mismo clipboard ({"XY"}) pegado en inicio / medio / final / linea vacia
// / otras lineas debe insertarse en el punto exacto y dejar el cursor al
// final del bloque pegado (posicion de insercion + largo del bloque).
// ---------------------------------------------------------------------------

TEST(clipboard_p_at_document_start) {
    // |abcdef -> pega en (0,0): "XY" va antes de todo.
    Editor ed;
    ed.active().document.restore({"abcdef"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.clipboard_ = {"XY"};

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "XYabcdef");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 2);              // 0 + largo("XY")
    CHECK(ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Pegado.");
}

TEST(clipboard_p_at_document_middle) {
    // abc|def -> pega en (0,3): "XY" se inserta en la col 3.
    Editor ed;
    ed.active().document.restore({"abcdef"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 3;
    ed.clipboard_ = {"XY"};

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "abcXYdef");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 5);              // 3 + 2
    CHECK(ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Pegado.");
}

TEST(clipboard_p_at_document_end) {
    // abcdef| -> pega en (0,6): "XY" se anexa al final.
    Editor ed;
    ed.active().document.restore({"abcdef"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 6;                       // final de linea
    ed.clipboard_ = {"XY"};

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "abcdefXY");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 8);              // 6 + 2
    CHECK(ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Pegado.");
}

TEST(clipboard_p_on_empty_line) {
    // | -> linea vacia, cursor (0,0): "XY" queda como unica linea.
    Editor ed;
    ed.active().document.restore({""});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.clipboard_ = {"XY"};

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "XY");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Pegado.");
}

TEST(clipboard_p_on_second_line_single_line_block) {
    // Pegar un bloque de una sola linea en la segunda linea del documento:
    // la inserta inline y deja las demas lineas intactas. Cursor al final
    // del bloque en esa linea.
    Editor ed;
    ed.active().document.restore({"aaaa", "bbbb", "cccc"});
    ed.active().cursor.line = 1;              // segunda linea (0-based)
    ed.active().cursor.col = 1;
    ed.clipboard_ = {"XY"};

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineCount(), 3);
    CHECK_EQ(ed.active().document.lineAt(0), "aaaa");
    CHECK_EQ(ed.active().document.lineAt(1), "bXYbbb");
    CHECK_EQ(ed.active().document.lineAt(2), "cccc");
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 3);              // 1 + 2
    CHECK(ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Pegado.");
}

TEST(clipboard_p_on_third_line_multiline_block) {
    // Pegar un bloque MULTILINEA en la tercera linea: la linea actual se
    // parte en el cursor, la cola derecha se une a la ULTIMA linea del
    // bloque y el cursor queda al final de esa ultima linea insertada.
    Editor ed;
    ed.active().document.restore({"aaaa", "bbbb", "cccc", "dddd"});
    ed.active().cursor.line = 2;              // tercera linea (0-based)
    ed.active().cursor.col = 1;
    ed.clipboard_ = {"X", "Y", "Z"};

    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.active().document.lineCount(), 6);
    CHECK_EQ(ed.active().document.lineAt(0), "aaaa");
    CHECK_EQ(ed.active().document.lineAt(1), "bbbb");
    CHECK_EQ(ed.active().document.lineAt(2), "cX");
    CHECK_EQ(ed.active().document.lineAt(3), "Y");
    CHECK_EQ(ed.active().document.lineAt(4), "Zccc"); // ultima del bloque + cola derecha
    CHECK_EQ(ed.active().document.lineAt(5), "dddd");
    CHECK_EQ(ed.active().cursor.line, 4);             // final de la ultima linea insertada
    CHECK_EQ(ed.active().cursor.col, 1);              // largo de "Z" (pos 1 + 1)
    CHECK(ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Pegado.");
}

// ---------------------------------------------------------------------------
// v0.55: Undo/Redo del pegado
// ---------------------------------------------------------------------------
// El clipboard NO participa del historial, pero la operacion de pegar SI
// modifica el documento, asi que empuja una entrada de undo. Tras pegar:
//   Undo  -> se elimina el pegado (doc vuelve al estado previo)
//   Redo  -> se restaura el pegado
// El clipboard permanece intacto durante todo el ciclo.
// ---------------------------------------------------------------------------
TEST(clipboard_paste_undo_removes_paste) {
    Editor ed;
    ed.active().document.restore({"abcdef"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 3;
    ed.clipboard_ = {"XYZ"};
    const size_t undoBefore = ed.active().undoStack.size();

    ed.handleEvent(insert('p'));     // "abcXYZdef", cursor (0,6)
    CHECK_EQ(ed.active().document.lineAt(0), "abcXYZdef");
    CHECK_EQ(ed.active().undoStack.size(), undoBefore + 1); // pegar es una edicion

    press(ed, EventType::Undo);      // elimina el pegado

    CHECK_EQ(ed.active().document.lineAt(0), "abcdef");  // doc como antes de pegar
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().cursor.line, 0);                // cursor como antes de pegar
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"XYZ"})); // buffer intacto
}

TEST(clipboard_paste_redo_restores_paste) {
    // Redo tras el undo del pegado vuelve a insertar el bloque.
    Editor ed;
    ed.active().document.restore({"abcdef"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 3;
    ed.clipboard_ = {"XYZ"};

    ed.handleEvent(insert('p'));     // "abcXYZdef"
    press(ed, EventType::Undo);      // -> "abcdef"
    CHECK_EQ(ed.active().document.lineAt(0), "abcdef");

    press(ed, EventType::Redo);      // vuelve a pegar

    CHECK_EQ(ed.active().document.lineAt(0), "abcXYZdef");
    CHECK_EQ(ed.active().cursor.line, 0);    // cursor al final del bloque pegado
    CHECK_EQ(ed.active().cursor.col, 6);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"XYZ"})); // buffer intacto
    CHECK(ed.active().redoStack.empty());    // el redo se consumio
}

TEST(clipboard_paste_undo_redo_clipboard_stays_constant) {
    // El clipboard permanece "abc" durante todo el ciclo pegar->undo->redo.
    Editor ed;
    ed.active().document.restore({"hola"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 2;
    ed.clipboard_ = {"abc"};

    ed.handleEvent(insert('p'));     // "hoabcla"
    CHECK(ed.clipboard_ == (std::vector<std::string>{"abc"}));
    press(ed, EventType::Undo);      // -> "hola"
    CHECK(ed.clipboard_ == (std::vector<std::string>{"abc"}));
    press(ed, EventType::Redo);      // -> "hoabcla"
    CHECK(ed.clipboard_ == (std::vector<std::string>{"abc"}));

    CHECK_EQ(ed.active().document.lineAt(0), "hoabcla");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

// Test CENTRAL de v0.55: cortar y luego deshacer restaura el DOCUMENTO,
// pero el BUFFER no vuelve a su estado anterior (decision de diseno del
// punto 3 de v0.5: el buffer no participa del historial).
TEST(clipboard_cut_then_undo_keeps_buffer) {
    Editor ed;
    type(ed, "hola");
    selectChars(ed, 2);             // [ho]
    ed.handleEvent(insert('x'));    // corta "ho": doc "la", buffer ["ho"]
    CHECK_EQ(ed.active().document.lineAt(0), "la");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ho"}));

    press(ed, EventType::Undo);     // deshace el corte
    CHECK_EQ(ed.active().document.lineAt(0), "hola");        // el documento SI se restaura
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ho"})); // el buffer NO
}

// ---------------------------------------------------------------------------
// Undo/Redo y modo: transiciones modales + historial
// ---------------------------------------------------------------------------
// applyState() decide el modo restaurado con el criterio
//   anchor != position  -> Seleccion (se restaura la seleccion vigente)
//   sin seleccion       -> Navegacion
// Aqui se verifican los flujos completos de modo + historial.
// ---------------------------------------------------------------------------

TEST(undo_after_interaction_esc_restores_navegacion) {
    // Navegacion -> i -> escribir -> ESC -> Undo: el undo no restaura
    // Interaccion (el historial no la distingue); vuelve a Navegacion con
    // el documento deshecho y redo disponible.
    Editor ed;
    press(ed, EventType::MoveEnd);            // (0,0) seguros
    ed.handleEvent(insert('i'));              // -> Interaccion
    ed.handleEvent(insert('a'));
    ed.handleEvent(insert('b'));              // "ab"
    press(ed, EventType::Escape);             // -> Navegacion

    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "ab");

    press(ed, EventType::Undo);               // deshace la 'b'

    CHECK_EQ(ed.active().document.lineAt(0), "a");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK(!ed.hasSelection());
    CHECK(!ed.active().redoStack.empty());

    press(ed, EventType::Redo);               // rehace la 'b'

    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.col, 2);
}

TEST(undo_after_cut_restores_selection_state) {
    // Navegacion -> s -> seleccionar -> x -> Undo: el undo debe volver al
    // ESTADO Seleccion (anchor != position restaurado), con el cursor al
    // final del rango y la seleccion vigente de nuevo.
    Editor ed;
    ed.active().document.restore({"hola", "mundo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    ed.handleEvent(insert('s'));              // -> Seleccion
    press(ed, EventType::MoveEnd);            // selecciona "hola" (0,0)-(0,4)
    CHECK(ed.hasSelection());
    ed.handleEvent(insert('x'));              // corta: doc {"","mundo"}

    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"", "mundo"}));
    CHECK(!ed.hasSelection());

    press(ed, EventType::Undo);               // deshace el corte

    // El estado restaurado es Seleccion (se restauro anchor != position).
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"hola", "mundo"}));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 4);              // final de la seleccion restaurada

    press(ed, EventType::Redo);               // reaplica el corte

    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(!ed.hasSelection());
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"", "mundo"}));
}

TEST(undo_of_empty_selection_stays_navegacion) {
    // Entrar en Seleccion sin marcar (anchor == position), cancelar con
    // ESC, y deshacer: como el historial no guardo seleccion, el undo no
    // debe "inventar" un modo Seleccion.
    Editor ed;
    ed.active().document.restore({"hola"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    ed.handleEvent(insert('s'));              // -> Seleccion (sin marcar)
    press(ed, EventType::Escape);             // -> Navegacion

    press(ed, EventType::Undo);               // no hay nada que deshacer

    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(!ed.hasSelection());
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"hola"}));
}

TEST(undo_cut_redo_then_undo_cycles_selection) {
    // Ciclo completo Undo/Redo de un corte: cada undo restaura Seleccion
    // con el rango, cada redo vuelve a Navegacion con el doc cortado.
    Editor ed;
    ed.active().document.restore({"abcdef"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    selectChars(ed, 2);                       // [ab] (0,0)-(0,2)
    ed.handleEvent(insert('x'));              // corta "ab"

    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"cdef"}));

    press(ed, EventType::Undo);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().cursor.col, 2);

    press(ed, EventType::Redo);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(!ed.hasSelection());
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"cdef"}));

    press(ed, EventType::Undo);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"abcdef"}));
    CHECK_EQ(ed.active().cursor.col, 2);
}

// ---------------------------------------------------------------------------
// Acciones globales desde los tres modos
// ---------------------------------------------------------------------------
// Undo/Redo/Prefix (Ctrl+K) se evaluan ANTES del despacho por modo, asi que
// deben funcionar identico desde Navegacion, Interaccion y Seleccion:
//   - Ctrl+U (Undo): deshace, NO se inserta como texto, funciona en seleccion;
//   - Ctrl+Y (Redo): rehace, NO se inserta como texto;
//   - Ctrl+K (Prefix): guarda / sale, devolviendo al estado previo.
// ---------------------------------------------------------------------------

// Construye un editor con undo/redo pendientes: doc "abc" escrito por
// separado (3 entradas) y un Undo que deja "ab" con "c" en el redo.
static void setupUndoRedoPendientes(Editor& ed) {
    ed.active().document.restore({""});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.handleEvent(insert('i'));
    ed.handleEvent(insert('a'));
    ed.handleEvent(insert('b'));
    ed.handleEvent(insert('c'));
    press(ed, EventType::Undo);            // "ab", redo=["c"]
}

TEST(global_undo_from_navegacion) {
    Editor ed;
    setupUndoRedoPendientes(ed);
    press(ed, EventType::Escape);          // -> Navegacion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
}

TEST(global_undo_redo_from_interaccion_not_literal) {
    // Ctrl+U desde Interaccion deshace; NO inserta una 'u' literal.
    Editor ed;
    setupUndoRedoPendientes(ed);
    press(ed, EventType::Escape);
    ed.handleEvent(insert('i'));           // -> Interaccion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));

    press(ed, EventType::Undo);            // deshace la 'b', no escribe "u"
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    press(ed, EventType::Redo);            // rehace la 'b', no escribe "y"
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(global_undo_from_selection_works) {
    // Ctrl+U funciona incluso mientras se esta seleccionando.
    Editor ed;
    setupUndoRedoPendientes(ed);
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);        // cursor a (0,0)
    ed.handleEvent(insert('s'));           // -> Seleccion
    press(ed, EventType::MoveEnd);         // selecciona "ab"
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));

    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "a");
}

TEST(global_redo_from_selection_works) {
    Editor ed;
    setupUndoRedoPendientes(ed);
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);        // cursor a (0,0)
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);         // selecciona "ab"

    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");   // rehace la 'c' pendiente
}

TEST(prefix_from_navegacion_returns_to_navegacion) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    press(ed, EventType::Prefix);          // Ctrl+K
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Prefix));

    Event e;
    e.type = EventType::Save;
    ed.handleEvent(e);                     // Ctrl+S
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(prefix_from_interaccion_returns_to_interaccion) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.handleEvent(insert('i'));           // -> Interaccion

    press(ed, EventType::Prefix);          // Ctrl+K
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Prefix));

    Event e;
    e.type = EventType::Save;
    ed.handleEvent(e);                     // Ctrl+S
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(prefix_from_seleccion_returns_to_seleccion) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);         // selecciona "abc"

    press(ed, EventType::Prefix);          // Ctrl+K
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Prefix));

    Event e;
    e.type = EventType::Save;
    ed.handleEvent(e);                     // Ctrl+S
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());              // la seleccion se conserva
}

TEST(prefix_quit_from_all_modes) {
    // Ctrl+K + Ctrl+Q sale, desde cualquier modo.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    press(ed, EventType::Prefix);
    Event q;
    q.type = EventType::Quit;
    ed.handleEvent(q);
    CHECK(!ed.running_);

    Editor ed2;
    ed2.active().document.restore({"abc"});
    ed2.active().cursor.line = 0;
    ed2.active().cursor.col = 0;
    ed2.handleEvent(insert('i'));          // Interaccion
    press(ed2, EventType::Prefix);
    ed2.handleEvent(q);
    CHECK(!ed2.running_);

    Editor ed3;
    ed3.active().document.restore({"abc"});
    ed3.active().cursor.line = 0;
    ed3.active().cursor.col = 0;
    ed3.handleEvent(insert('s'));          // Seleccion
    press(ed3, EventType::Prefix);
    ed3.handleEvent(q);
    CHECK(!ed3.running_);
}

// ---------------------------------------------------------------------------
// Regresiones de Ctrl+Q
// ---------------------------------------------------------------------------
// El editor NO debe cerrarse con un Ctrl+Q accidental. Quit SOLO tiene
// efecto como segundo comando del prefijo (Ctrl+K, Ctrl+Q). Solos o en
// cualquier modo, no deben cerrar ni tocar el documento.
// ---------------------------------------------------------------------------

TEST(regression_ctrl_q_alone_does_not_quit) {
    // Ctrl+Q suelto, sin prefijo: no-op.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    Event q;
    q.type = EventType::Quit;
    ed.handleEvent(q);

    CHECK(ed.running_);
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"abc"}));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(regression_ctrl_q_in_navegacion_does_not_quit) {
    // Con undo pendiente: Ctrl+Q no debe deshacer ni cerrar.
    Editor ed;
    ed.active().document.restore({""});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.handleEvent(insert('i'));
    ed.handleEvent(insert('a'));
    ed.handleEvent(insert('b'));
    press(ed, EventType::Escape);          // -> Navegacion, undo=2
    size_t undoBefore = ed.active().undoStack.size();
    CHECK(undoBefore > size_t{0});

    Event q;
    q.type = EventType::Quit;
    ed.handleEvent(q);

    CHECK(ed.running_);
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
}

TEST(regression_ctrl_q_in_interaccion_does_not_quit_or_insert) {
    // Ctrl+Q en Interaccion: no cierra ni inserta texto.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.handleEvent(insert('i'));           // -> Interaccion
    press(ed, EventType::MoveEnd);

    Event q;
    q.type = EventType::Quit;
    ed.handleEvent(q);

    CHECK(ed.running_);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");   // no inserta 'q'
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(regression_ctrl_q_in_seleccion_does_not_quit_or_clear) {
    // Ctrl+Q en Seleccion: no cierra ni cancela la seleccion.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);         // selecciona "abc"
    CHECK(ed.hasSelection());

    Event q;
    q.type = EventType::Quit;
    ed.handleEvent(q);

    CHECK(ed.running_);
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

TEST(regression_ctrl_q_as_incomplete_prefix_does_not_quit) {
    // Ctrl+K es un prefijo "incompleto": solo. El editor sigue vivo.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    press(ed, EventType::Prefix);          // Ctrl+K
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Prefix));
    CHECK(ed.running_);
}

TEST(regression_ctrl_q_followed_by_invalid_key_cancels) {
    // Ctrl+K, luego una tecla invalida (no Save/Quit): se cancela el
    // prefijo y el editor sigue vivo, sin efectos secundarios.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    press(ed, EventType::Prefix);          // Ctrl+K
    press(ed, EventType::MoveLeft);        // tecla invalida en prefijo
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.running_);
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"abc"}));
}

TEST(regression_ctrl_q_after_prefix_quits) {
    // La unica forma de salir es Ctrl+K -> Ctrl+Q.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    press(ed, EventType::Prefix);
    Event q;
    q.type = EventType::Quit;
    ed.handleEvent(q);

    CHECK(!ed.running_);
}

// ---------------------------------------------------------------------------
// Prefijo Prefix: migracion a tres modos no debe romperlo
// ---------------------------------------------------------------------------
// En modo Prefix todo pasa por handlePrefixKey ANTES que el despacho por
// modo. Solo Save y Quit tienen efecto; cualquier otra tecla cancela y
// vuelve al estado previo SIN filtrar acciones de Navegacion/Interaccion/
// Seleccion (Ctrl+U/Y no deshacen/rehacen, i/s/c/x/p no hacen nada).
// ---------------------------------------------------------------------------

TEST(prefix_enters_prefix_state) {
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    press(ed, EventType::Prefix);

    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Prefix));
    CHECK(ed.running_);
}

TEST(prefix_valid_save_command_returns_to_prior_state) {
    // Desde Navegacion: Save valido devuelve a Navegacion.
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    press(ed, EventType::Prefix);
    Event s;
    s.type = EventType::Save;
    ed.handleEvent(s);

    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.running_);
}

TEST(prefix_invalid_command_cancels_and_returns) {
    // Una tecla invalida cancela el prefijo y vuelve al estado previo.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    press(ed, EventType::Prefix);
    press(ed, EventType::MoveLeft);        // tecla invalida en prefijo

    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"abc"}));
}

TEST(prefix_escape_cancels_and_returns) {
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    press(ed, EventType::Prefix);
    press(ed, EventType::Escape);          // ESC cancela el prefijo

    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"abc"}));
}

TEST(prefix_ctrl_k_inside_prefix_cancels) {
    // Ctrl+K dentro de Prefix no anida: se cancela y vuelve al estado previo.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    press(ed, EventType::Prefix);
    press(ed, EventType::Prefix);

    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(prefix_ctrl_u_inside_prefix_does_not_undo) {
    // Ctrl+U en Prefix NO deshace: se cancela el prefijo y se vuelve al
    // estado previo con el historial intacto.
    Editor ed;
    ed.active().document.restore({""});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.handleEvent(insert('i'));
    ed.handleEvent(insert('a'));
    ed.handleEvent(insert('b'));
    press(ed, EventType::Escape);          // Navegacion, undo=2
    size_t undoBefore = ed.active().undoStack.size();

    press(ed, EventType::Prefix);
    press(ed, EventType::Undo);

    CHECK_EQ(ed.active().undoStack.size(), undoBefore);   // NO deshizo
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(prefix_ctrl_y_inside_prefix_does_not_redo) {
    Editor ed;
    ed.active().document.restore({""});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.handleEvent(insert('i'));
    ed.handleEvent(insert('a'));
    press(ed, EventType::Escape);
    press(ed, EventType::Undo);            // "a" -> "", redo=["a"]
    size_t redoBefore = ed.active().redoStack.size();
    CHECK(redoBefore > size_t{0});

    press(ed, EventType::Prefix);
    press(ed, EventType::Redo);

    CHECK_EQ(ed.active().redoStack.size(), redoBefore);   // NO rehizo
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(prefix_letters_inside_prefix_do_not_leak) {
    // i/s/c/x/p dentro del prefijo NO deben filtrar acciones de los modos:
    // ni entrar a Interaccion/Seleccion, ni copiar/cortar/pegar.
    Editor ed;
    ed.active().document.restore({""});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.handleEvent(insert('i'));
    ed.handleEvent(insert('a'));
    ed.handleEvent(insert('b'));
    ed.handleEvent(insert('c'));
    press(ed, EventType::Escape);          // Navegacion, doc "abc"
    size_t undoBefore = ed.active().undoStack.size();
    CHECK(ed.clipboard_.empty());

    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('s'));           // seleccionar "abc"
    press(ed, EventType::MoveEnd);
    CHECK(ed.hasSelection());
    press(ed, EventType::Escape);          // -> Navegacion

    // 'c' en Prefix: no copia.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('c'));
    CHECK(ed.clipboard_.empty());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    // 'x' en Prefix: no corta.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('x'));
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"abc"}));
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    // 'p' en Prefix: no pega.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('p'));
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"abc"}));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    // 'i' en Prefix: no entra a Interaccion.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('i'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "abc");   // no inserto 'i'

    // 's' en Prefix: no entra a Seleccion.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('s'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(!ed.hasSelection());
}

TEST(prefix_quit_from_prefix_quits) {
    // Quit es un comando valido del prefijo: si se completa, sale.
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    press(ed, EventType::Prefix);
    Event q;
    q.type = EventType::Quit;
    ed.handleEvent(q);

    CHECK(!ed.running_);
}
