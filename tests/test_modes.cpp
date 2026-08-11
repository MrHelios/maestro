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
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK(!ed.modified_);
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

TEST(navigation_pcx_noop) {
    // En navegacion 'c'/'x' no hacen nada. 'p' sin buffer es no-op sobre
    // el documento (solo informa "Nada para pegar."); con buffer pega
    // (se cubre en los tests v0.55 de buffer).
    Editor ed;
    ed.handleEvent(insert('c'));
    ed.handleEvent(insert('x'));
    ed.handleEvent(insert('p'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.document_.lineAt(0), "");
}

TEST(navigation_movement_free_no_selection) {
    // Las flechas/Home/End se mueven libremente sin iniciar seleccion.
    Editor ed;
    type(ed, "abc");              // Interaccion
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.cursor_.col, 2);
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
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK(!ed.modified_);
}

// ---------------------------------------------------------------------------
// v0.5: modo Interaccion (edicion libre)
// ---------------------------------------------------------------------------
TEST(interaction_types_every_letter_literal) {
    // En Interaccion toda letra se inserta como texto, incluida i/s/c/x/p.
    Editor ed;
    type(ed, "iscxp");
    CHECK_EQ(ed.document_.lineAt(0), "iscxp");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(interaction_escape_returns_navegacion) {
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.document_.lineAt(0), "hola");
}

TEST(interaction_newline_and_backspace_work) {
    Editor ed;
    type(ed, "ab");
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.document_.lineCount(), 2);
    press(ed, EventType::Backspace); // une de nuevo
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "ab");
}

TEST(interaction_does_not_interpret_i_as_command) {
    // La 'i' dentro de Interaccion es texto real, no un comando.
    Editor ed;
    type(ed, "ai");
    CHECK_EQ(ed.document_.lineAt(0), "ai");
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
    CHECK_EQ(ed.document_.lineAt(0), "abc"); // copiar no modifica el documento
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
    CHECK_EQ(ed.document_.lineAt(0), "bc");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"a"}));
    CHECK(ed.modified_);
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
    CHECK_EQ(ed.document_.lineAt(0), "hello"); // sin reemplazo
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
    CHECK_EQ(ed.document_.lineAt(0), "abc");
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
    CHECK(ed.modified_);
    prefix(ed, EventType::Prefix, EventType::Save); // Ctrl+K, Ctrl+S
    CHECK(!ed.modified_);
    CHECK_EQ(fileContent(f.path), "hola");
}

TEST(prefix_save_returns_to_navegacion) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape); // -> Navegacion
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(prefix_save_keeps_interaction_mode) {
    // Si el prefijo se abrio estando en Interaccion, al guardar se vuelve
    // a Interaccion (no a Navegacion).
    Editor ed;
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
    CHECK_EQ(ed.cursor_.col, 3);
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
    CHECK_EQ(ed.cursor_.col, 3);      // la flecha NO se propago
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
    CHECK(ed.modified_);

    prefix(ed, EventType::Prefix, EventType::Save); // Ctrl+K, Ctrl+S
    CHECK(!ed.modified_);
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
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(undo_redo_work_in_selection_mode) {
    // Undo/Redo se evaluan antes del despacho por modo, asi que funcionan
    // incluso dentro del modo seleccion.
    Editor ed;
    type(ed, "x");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "");
    enterSeleccion(ed);           // entramos a seleccion (sin seleccion)
    press(ed, EventType::Redo);   // el redo se aplica igual
    CHECK_EQ(ed.document_.lineAt(0), "x");
}

TEST(redo_restores_content_in_navegacion) {
    Editor ed;
    type(ed, "x");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "");
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "x");
}

TEST(selection_does_not_clear_redo) {
    // Entrar en seleccion es estado, no edicion: no consume el redo.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Undo);   // -> "ab", redo pendiente "abc"
    CHECK(!ed.redoStack_.empty());
    press(ed, EventType::MoveLeft);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight); // selecciona "b"
    CHECK(ed.hasSelection());
    press(ed, EventType::Redo);   // el redo sigue vivo
    CHECK_EQ(ed.document_.lineAt(0), "abc");
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
    CHECK_EQ(ed.document_.lineAt(0), "contenido");
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
    size_t undoBefore = ed.undoStack_.size();
    selectChars(ed, 2);            // selecciona "ab"
    CHECK(ed.hasSelection());
    ed.handleEvent(insert('c'));
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.document_.lineAt(0), "abc");     // copiar no borra
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ab"}));
    CHECK_EQ(ed.undoStack_.size(), undoBefore);  // copiar no muta: sin pushHistory
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
    CHECK_EQ(ed.document_.lineAt(0), "abc");
}

TEST(clipboard_x_cuts_and_pushes_history) {
    Editor ed;
    type(ed, "abc");
    selectChars(ed, 2);            // selecciona "ab"
    ed.handleEvent(insert('x'));
    CHECK_EQ(ed.document_.lineAt(0), "c");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ab"}));
    CHECK_EQ(ed.cursor_.col, 0);   // cursor reposicionado al inicio del rango
    CHECK(ed.modified_);
    CHECK(!ed.undoStack_.empty()); // cortar SI entra al historial
}

TEST(clipboard_x_with_empty_selection_cuts_nothing) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Undo);    // deja algo pendiente en redoStack_
    CHECK(!ed.redoStack_.empty());
    size_t undoBefore = ed.undoStack_.size();
    size_t redoBefore = ed.redoStack_.size();
    ed.clipboard_ = std::vector<std::string>{"precioso"}; // contenido previo
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    CHECK(!ed.hasSelection());
    ed.handleEvent(insert('x'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.clipboard_ == std::vector<std::string>{"precioso"}); // intacto
    CHECK_EQ(ed.statusMessage_, "Nada seleccionado.");
    CHECK_EQ(ed.document_.lineAt(0), "ab");      // el undo la dejo en "ab"
    CHECK_EQ(ed.undoStack_.size(), undoBefore);  // sin mutation: no pushHistory
    CHECK_EQ(ed.redoStack_.size(), redoBefore);  // el redo NO se pierde
}

TEST(clipboard_p_with_empty_buffer_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape);   // -> Navegacion
    // 'p' con buffer vacio es no-op sobre el documento y NO entra al
    // historial (cuenta de undo antes/despues identica).
    size_t undoBefore = ed.undoStack_.size();
    ed.handleEvent(insert('p'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.document_.lineAt(0), "abc"); // sin cambios
    CHECK_EQ(ed.undoStack_.size(), undoBefore);
    CHECK_EQ(ed.statusMessage_, "Nada para pegar.");
}

TEST(clipboard_p_pastes_and_repositions_cursor) {
    Editor ed;
    type(ed, "abc");
    selectChars(ed, 2);             // selecciona "ab"
    ed.handleEvent(insert('c'));    // copia "ab" -> Navegacion, cursor (0,0)
    ed.handleEvent(insert('p'));    // pega en (0,0)... cursor real (0,2)
    CHECK_EQ(ed.document_.lineAt(0), "ababc");
    CHECK_EQ(ed.cursor_.col, 4);    // cursor al final del bloque pegado (2+2)
    CHECK(ed.modified_);
    CHECK_EQ(ed.statusMessage_, "Pegado.");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(clipboard_p_pastes_multiline_block) {
    // Pegar un bloque multilinea en (line,col) parte la linea actual en
    // col, inserta el bloque y deja el cursor al final de la ultima linea
    // insertada del bloque.
    Editor ed;
    ed.document_.restore({"Z", "Z"}); // documento de 2 lineas
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;
    ed.clipboard_ = std::vector<std::string>{"X", "Y"};
    ed.handleEvent(insert('p'));
    CHECK_EQ(ed.document_.lineCount(), 3);
    CHECK_EQ(ed.document_.lineAt(0), "X");
    CHECK_EQ(ed.document_.lineAt(1), "YZ"); // ultima del bloque + cola derecha
    CHECK_EQ(ed.document_.lineAt(2), "Z");
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 1); // final de la ultima linea insertada
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(clipboard_p_in_interaction_is_literal_p) {
    // La letra 'p' dentro de Interaccion es texto real, no pegar.
    Editor ed;
    type(ed, "ap");
    CHECK_EQ(ed.document_.lineAt(0), "ap");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
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
    CHECK_EQ(ed.document_.lineAt(0), "abc"); // nada se posiciono
    CHECK(ed.clipboard_.empty());
}

// Test CENTRAL de v0.55: cortar y luego deshacer restaura el DOCUMENTO,
// pero el BUFFER no vuelve a su estado anterior (decision de diseno del
// punto 3 de v0.5: el buffer no participa del historial).
TEST(clipboard_cut_then_undo_keeps_buffer) {
    Editor ed;
    type(ed, "hola");
    selectChars(ed, 2);             // [ho]
    ed.handleEvent(insert('x'));    // corta "ho": doc "la", buffer ["ho"]
    CHECK_EQ(ed.document_.lineAt(0), "la");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ho"}));

    press(ed, EventType::Undo);     // deshace el corte
    CHECK_EQ(ed.document_.lineAt(0), "hola");        // el documento SI se restaura
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ho"})); // el buffer NO
}
