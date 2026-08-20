#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_framework.h"

#include <cstdlib>
#include <string>
#include <vector>
#define private public
#include "ui/Editor.h"
#undef private

using testfw::TempFile;

static Event insert(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

static void press(Editor& ed, EventType type) {
    Event e;
    e.type = type;
    ed.handleEvent(e);
}

static void pressEvent(Editor& ed, const Event& ev) {
    ed.handleEvent(ev);
}

static void type(Editor& ed, const std::string& s) {
    if (s.empty()) return;
    if (ed.state_ != State::Interaccion) {
        if (ed.state_ == State::Seleccion) {
            Event esc; esc.type = EventType::Escape; ed.handleEvent(esc);
        }
        ed.handleEvent(insert('i'));
    }
    for (char c : s)
        ed.handleEvent(insert(c));
}

// v0.6.3: comandos de buffer via el prefijo Ctrl+K.
static void newBuffer(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('n'));
}

// v0.7: guardar como. Ctrl+K Ctrl+S sobre un buffer sin nombre abre el
// prompt "Guardar archivo:" en la fila de mensajes.
static void openSaveAs(Editor& ed) {
    press(ed, EventType::Prefix);
    press(ed, EventType::Save);
}

// Escribe texto dentro del prompt SaveAs (modal: no pasa por Interaccion).
static void typePrompt(Editor& ed, const std::string& s) {
    for (char c : s)
        pressEvent(ed, insert(c));
}

static void openSelector(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('t'));
}

static void closeBuffer(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('w'));
}

// ---------------------------------------------------------------------------
// Modelo de buffers (v0.6.3)
// ---------------------------------------------------------------------------
TEST(buffers_start_with_one_unnamed_buffer) {
    Editor ed;
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK_EQ(ed.active().unnamedName, "SinNombre");
    CHECK(ed.active().filename.empty());
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK(ed.active().undoStack.empty());
    CHECK(ed.active().redoStack.empty());
}

TEST(buffers_isolate_documents) {
    Editor ed;
    type(ed, "hola");
    newBuffer(ed);             // B1 activo
    type(ed, "mundo");
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
    ed.activateBuffer(1);
    CHECK_EQ(ed.active().document.lineAt(0), "mundo");
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
}

TEST(buffers_isolate_undo_history) {
    Editor ed;
    type(ed, "hola");          // B0: h,ho,hol,hola -> 4 entradas
    press(ed, EventType::Escape);
    newBuffer(ed);             // B1 activo
    type(ed, "mundo");         // B1: 5 entradas
    press(ed, EventType::Escape);
    CHECK_EQ(ed.buffers.buffers_[0].undoStack.size(), size_t(4));
    CHECK_EQ(ed.buffers.buffers_[1].undoStack.size(), size_t(5));

    // Undo en B1 no toca el historial de B0.
    ed.activateBuffer(1);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "mund");
    CHECK_EQ(ed.buffers.buffers_[1].undoStack.size(), size_t(4));

    ed.activateBuffer(0);
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
    CHECK_EQ(ed.buffers.buffers_[0].undoStack.size(), size_t(4));
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "hol");

    // Volver a B1: sigue en "mund" con su propia pila.
    ed.activateBuffer(1);
    CHECK_EQ(ed.active().document.lineAt(0), "mund");
    CHECK_EQ(ed.buffers.buffers_[1].undoStack.size(), size_t(4));
}

TEST(buffers_isolate_redo_history) {
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    newBuffer(ed);
    type(ed, "mundo");
    press(ed, EventType::Escape);

    ed.activateBuffer(1);
    press(ed, EventType::Undo);
    press(ed, EventType::Undo);
    CHECK(!ed.buffers.buffers_[1].redoStack.empty());

    ed.activateBuffer(0);
    CHECK(ed.buffers.buffers_[0].redoStack.empty());  // B0 no tiene redo propio
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "hola"); // no-op en B0

    ed.activateBuffer(1);
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "mund");
    CHECK_EQ(ed.buffers.buffers_[0].document.lineAt(0), "hola");
}

TEST(buffers_isolate_cursor_and_preferred_col) {
    Editor ed;
    type(ed, "hola mundo");
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);   // B0 cursor col 2
    const int col0 = ed.active().cursor.col;
    CHECK_EQ(col0, 2);

    newBuffer(ed);
    type(ed, "xyz");
    press(ed, EventType::MoveEnd);     // B1 cursor col 3
    const int col1 = ed.active().cursor.col;
    CHECK_EQ(col1, 3);

    ed.activateBuffer(0);
    CHECK_EQ(ed.active().cursor.col, col0);
    ed.activateBuffer(1);
    CHECK_EQ(ed.active().cursor.col, col1);
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().cursor.col, col0);

    // preferredCol_ tambien viaja con el buffer.
    const int pref1 = ed.buffers.buffers_[1].cursor.preferredCol_;  // valor real de B1
    ed.buffers.buffers_[0].cursor.preferredCol_ = 7;
    ed.activateBuffer(1);
    CHECK_EQ(ed.buffers.buffers_[1].cursor.preferredCol_, pref1);
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().cursor.preferredCol_, 7);
}

TEST(buffers_isolate_selection) {
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    pressEvent(ed, insert('s'));            // modo seleccion
    press(ed, EventType::MoveRight);   // selecciona "a"
    CHECK(ed.hasSelection());
    CHECK(ed.buffers.buffers_[0].selection.has_value());

    newBuffer(ed);                       // B1 sin seleccion
    ed.activateBuffer(1);
    CHECK(!ed.hasSelection());
    CHECK(!ed.active().selection.has_value());

    ed.activateBuffer(0);              // B0 recupera su seleccion
    CHECK(ed.hasSelection());
    CHECK(ed.state_ == State::Seleccion);
}

TEST(buffers_isolate_select_all) {
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    pressEvent(ed, insert('s'));
    pressEvent(ed, insert('a'));            // seleccion total en B0
    CHECK(ed.buffers.buffers_[0].selectAllActive);
    CHECK(ed.hasSelection());

    newBuffer(ed);                       // B1 sin seleccion
    ed.activateBuffer(1);
    CHECK(!ed.buffers.buffers_[1].selectAllActive);
    CHECK(!ed.hasSelection());
    CHECK(!ed.buffers.buffers_[1].selection.has_value());

    ed.activateBuffer(0);
    CHECK(ed.buffers.buffers_[0].selectAllActive);
    CHECK(ed.hasSelection());
}

TEST(buffers_isolate_modified) {
    Editor ed;
    type(ed, "a");                     // B0 modificado
    CHECK(ed.buffers.buffers_[0].modified);
    newBuffer(ed);
    CHECK(!ed.buffers.buffers_[1].modified);
    ed.activateBuffer(0);
    CHECK(ed.active().modified);
    ed.activateBuffer(1);
    CHECK(!ed.active().modified);
}

TEST(buffers_isolate_viewport) {
    Editor ed;
    ed.buffers.buffers_[0].viewport.top = 500;
    newBuffer(ed);
    CHECK_EQ(ed.buffers.buffers_[1].viewport.top, 0);
    ed.buffers.buffers_[1].viewport.top = 20;
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().viewport.top, 500);
    ed.activateBuffer(1);
    CHECK_EQ(ed.active().viewport.top, 20);
}

TEST(buffers_isolate_filename) {
    TempFile f;
    f.write("contenido");
    Editor ed;
    CHECK(ed.openFile(f.path));        // B0 -> filename f.path
    newBuffer(ed);                     // B1 sin nombre
    CHECK(ed.active().filename.empty());
    CHECK_EQ(ed.active().unnamedName, "SinNombre1");
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().filename, f.path);
    ed.activateBuffer(1);
    CHECK(ed.active().filename.empty());
}

TEST(buffer_display_name_uses_filename_when_present) {
    TempFile f;
    f.write("x");
    Editor ed;
    CHECK(ed.openFile(f.path));
    const std::string base = f.path.substr(f.path.find_last_of('/') + 1);
    CHECK_EQ(ed.active().displayName(), base);
    CHECK(ed.active().unnamedName == "SinNombre");
}

TEST(clipboard_global_across_buffers) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    pressEvent(ed, insert('s'));
    press(ed, EventType::MoveRight);   // [a]
    pressEvent(ed, insert('c'));            // copia "a" -> Navegacion
    CHECK(ed.clipboard_ == (std::vector<std::string>{"a"}));

    newBuffer(ed);
    pressEvent(ed, insert('p'));            // pega en B1
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    CHECK(ed.active().modified);
}

// ---------------------------------------------------------------------------
// Ctrl+K n : buffer nuevo
// ---------------------------------------------------------------------------
TEST(ctrl_k_n_creates_and_activates_immediately) {
    Editor ed;
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    newBuffer(ed);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().unnamedName, "SinNombre1");
    CHECK(ed.active().document.lineAt(0).empty());
    CHECK(!ed.active().modified);
    CHECK(ed.state_ == State::Navegacion);
    // editable de inmediato, sin otra accion
    type(ed, "zzz");
    CHECK_EQ(ed.active().document.lineAt(0), "zzz");
}

// A -> Ctrl+K n -> editar -> cambiar -> volver. El nuevo buffer B (creado,
// activado, vacio y con nombre SinNombre) conserva el contenido al volver.
TEST(ctrl_k_n_edit_switch_back_preserves_content) {
    Editor ed;
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));   // A = SinNombre

    newBuffer(ed);                                     // Ctrl+K n
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));   // crea B
    CHECK_EQ(ed.buffers.activeBuffer_, 1);             // y lo activa
    CHECK_EQ(ed.active().unnamedName, "SinNombre1");   // nombre auto
    CHECK(ed.active().document.lineAt(0).empty());     // B vacio
    CHECK(!ed.active().modified);

    type(ed, "hello");                                 // editar B
    CHECK_EQ(ed.active().document.lineAt(0), "hello");
    CHECK(ed.active().modified);

    ed.activateBuffer(0);                              // -> A
    CHECK_EQ(ed.active().unnamedName, "SinNombre");
    CHECK(ed.active().document.lineAt(0).empty());     // A sigue vacio
    CHECK_EQ(ed.buffers.activeBuffer_, 0);

    ed.activateBuffer(1);                              // -> B
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().document.lineAt(0), "hello"); // B conserva contenido
    CHECK(ed.active().modified);
}

// Un buffer creado a mitad de sesion debe tomar las dimensiones reales
// de la terminal (no quedarse con el Viewport por defecto 24x80), o si
// no el render solo redibuja esas filas y queda resto del buffer
// anterior en pantalla (bug reportado en Ctrl+K n).
TEST(ctrl_k_n_new_buffer_viewport_matches_terminal) {
    Editor ed;
    int rows, cols;
    ed.terminal_.getWindowSize(rows, cols);
    int vpHeight = rows > 2 ? rows - 2 : 1;
    int vpWidth = cols;

    newBuffer(ed);
    CHECK_EQ(ed.active().viewport.height, vpHeight);
    CHECK_EQ(ed.active().viewport.width, vpWidth);
}

// El mismo bug aplica al reinicio del ultimo buffer (Ctrl+K w): al
// resetear se sustituye el viewport por uno por defecto que no cubria
// toda la terminal. Debe conservar las dimensiones reales.
TEST(ctrl_k_w_last_buffer_reset_keeps_terminal_viewport) {
    Editor ed;
    int rows, cols;
    ed.terminal_.getWindowSize(rows, cols);
    int vpHeight = rows > 2 ? rows - 2 : 1;
    int vpWidth = cols;

    closeBuffer(ed);                         // unico buffer: se reinicia
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK_EQ(ed.active().viewport.height, vpHeight);
    CHECK_EQ(ed.active().viewport.width, vpWidth);
}

TEST(ctrl_k_n_names_are_session_global) {
    Editor ed;
    CHECK_EQ(ed.active().unnamedName, "SinNombre");
    newBuffer(ed);
    CHECK_EQ(ed.active().unnamedName, "SinNombre1");
    newBuffer(ed);
    CHECK_EQ(ed.active().unnamedName, "SinNombre2");

    // Cerrar el primero (SinNombre) via selector + w.
    openSelector(ed);
    press(ed, EventType::MoveUp);
    press(ed, EventType::MoveUp);          // index 0
    press(ed, EventType::InsertNewline);   // activar SinNombre
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    closeBuffer(ed);                       // cerrar SinNombre (sin modificar)
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    press(ed, EventType::Escape);          // salir del selector

    // El contador NO reutiliza nombres: el siguiente es SinNombre3.
    newBuffer(ed);
    CHECK_EQ(ed.active().unnamedName, "SinNombre3");
}

// ---------------------------------------------------------------------------
// Ctrl+K t : selector de buffers
// ---------------------------------------------------------------------------
TEST(ctrl_k_t_opens_selector_on_active) {
    Editor ed;
    newBuffer(ed);
    newBuffer(ed);                         // activo = 2
    openSelector(ed);
    CHECK(ed.state_ == State::BufferSelector);
    CHECK_EQ(ed.bufferSelectorIndex_, 2);
    CHECK_EQ(ed.buffers.activeBuffer_, 2);         // el activo no cambia al abrir

    press(ed, EventType::MoveUp);
    CHECK_EQ(ed.bufferSelectorIndex_, 1);
    press(ed, EventType::MoveDown);
    CHECK_EQ(ed.bufferSelectorIndex_, 2);
    press(ed, EventType::MoveDown);        // clamp abajo
    CHECK_EQ(ed.bufferSelectorIndex_, 2);
    press(ed, EventType::MoveUp);
    press(ed, EventType::MoveUp);
    press(ed, EventType::MoveUp);          // clamp arriba
    CHECK_EQ(ed.bufferSelectorIndex_, 0);
}

TEST(ctrl_k_t_enter_switches_buffer) {
    Editor ed;
    type(ed, "hola");                      // B0
    newBuffer(ed);
    type(ed, "mundo");                     // B1 activo
    openSelector(ed);
    press(ed, EventType::MoveUp);          // seleccionar B0
    press(ed, EventType::InsertNewline);   // Enter
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
}

TEST(ctrl_k_t_escape_returns_to_previous_buffer_and_mode) {
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    newBuffer(ed);
    type(ed, "mundo");                     // B1 activo en Interaccion
    openSelector(ed);
    CHECK(ed.state_ == State::BufferSelector);
    press(ed, EventType::Escape);
    CHECK(ed.state_ == State::Interaccion); // vuelve al modo previo
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().document.lineAt(0), "mundo");
}

TEST(ctrl_k_t_other_keys_are_noop) {
    Editor ed;
    newBuffer(ed);
    newBuffer(ed);
    openSelector(ed);
    pressEvent(ed, insert('i'));
    pressEvent(ed, insert('s'));
    pressEvent(ed, insert('a'));
    pressEvent(ed, insert('c'));
    pressEvent(ed, insert('x'));
    pressEvent(ed, insert('p'));
    pressEvent(ed, insert('j'));
    press(ed, EventType::MoveRight);
    press(ed, EventType::Undo);
    press(ed, EventType::Redo);
    CHECK(ed.state_ == State::BufferSelector);
    CHECK_EQ(ed.bufferSelectorIndex_, 2);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(3));
    CHECK_EQ(ed.buffers.activeBuffer_, 2);
}

TEST(ctrl_k_t_already_in_selector_is_noop) {
    Editor ed;
    newBuffer(ed);
    openSelector(ed);
    CHECK(ed.state_ == State::BufferSelector);
    press(ed, EventType::Prefix);          // Ctrl+K dentro del selector
    CHECK(ed.state_ == State::BufferSelector); // no hay segunda capa
    press(ed, EventType::Escape);
    CHECK(ed.state_ == State::Navegacion);
}

TEST(ctrl_k_t_single_buffer_message) {
    Editor ed;
    openSelector(ed);
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.statusMessage_, "Solo hay un buffer.");
}

// ---------------------------------------------------------------------------
// Ctrl+K t : vuelta al mismo buffer. define el contrato del modo/selection.
// El modo global se reconcilia con el BUFFER activado (activateBuffer),
// no con priorState_:
//   * fuente Navegacion -> Enter igual buffer -> Navegacion, sin seleccion.
//   * fuente Interaccion -> Enter igual buffer -> Navegacion (sin rango).
//   * fuente Seleccion -> Enter igual buffer -> Seleccion, seleccion intacta.
// (Enter="cambiar a": reconcilia; ESC="cancelar": restaura priorState_.)
// ---------------------------------------------------------------------------

// Seleccion -> Ctrl+K t -> Enter sobre el MISMO buffer: la seleccion se
// conserva exactamente y el editor vuelve a Seleccion (rango intacto).
TEST(ctrl_k_t_return_same_buffer_preserves_selection) {
    Editor ed;
    type(ed, "abcdef");                  // B0 con contenido
    press(ed, EventType::Escape);
    newBuffer(ed);                       // B1 vacio activo
    ed.activateBuffer(0);                // seleccion sobre B0
    press(ed, EventType::MoveHome);
    pressEvent(ed, insert('s'));         // modo seleccion
    press(ed, EventType::MoveRight);     // rango [0,0)-(0,1) sobre "abcdef"
    CHECK(ed.hasSelection());

    const auto anchor = ed.active().selection->anchor;
    const auto pos = ed.active().selection->position;
    CHECK(ed.state_ == State::Seleccion);

    openSelector(ed);                    // Ctrl+K t
    CHECK(ed.state_ == State::BufferSelector);
    CHECK_EQ(ed.bufferSelectorIndex_, 0);  // activo = B0
    press(ed, EventType::InsertNewline);   // Enter: mismo buffer

    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK_EQ(ed.active().document.lineAt(0), "abcdef");          // contenido intacto
    CHECK(ed.state_ == State::Seleccion);                       // vuelve a Seleccion
    CHECK(ed.hasSelection());
    CHECK(ed.active().selection->anchor == anchor);             // seleccion intacta
    CHECK(ed.active().selection->position == pos);
}

// Seleccion -> Ctrl+K t -> ir a otro buffer y volver: la seleccion del
// buffer original sigue intacta (Enter reconcilia con la seleccion de B).
TEST(ctrl_k_t_switch_away_and_back_preserves_selection) {
    Editor ed;
    type(ed, "abcdef");                  // B0
    press(ed, EventType::Escape);
    newBuffer(ed);                       // B1 vacio activo
    ed.activateBuffer(0);                // sobre B0 fijamos la seleccion
    press(ed, EventType::MoveHome);
    pressEvent(ed, insert('s'));         // seleccion en B0
    press(ed, EventType::MoveRight);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK(ed.hasSelection());

    const auto anchor = ed.active().selection->anchor;
    const auto pos = ed.active().selection->position;

    openSelector(ed);                    // Ctrl+K t (activo = B0)
    press(ed, EventType::MoveDown);      // -> B1
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK(ed.state_ == State::Navegacion);   // B1 sin seleccion

    openSelector(ed);                    // Ctrl+K t (activo = B1)
    press(ed, EventType::MoveUp);        // -> B0
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK(ed.state_ == State::Seleccion);    // B0 conserva su seleccion
    CHECK(ed.hasSelection());
    CHECK(ed.active().selection->anchor == anchor);
    CHECK(ed.active().selection->position == pos);
}

// Fuente en cada modo -> Ctrl+K t -> Enter sobre el MISMO buffer: contrato
// de modo reconcilado con el buffer activado (tabla de interaccion).
TEST(ctrl_k_t_mode_table_per_source_mode) {
    // Navegacion (sin rango) -> Enter -> Navegacion.
    {
        Editor ed;
        newBuffer(ed);
        CHECK(ed.state_ == State::Navegacion);
        openSelector(ed);
        press(ed, EventType::InsertNewline);   // mismo buffer (B1)
        CHECK(ed.state_ == State::Navegacion);
        CHECK_EQ(ed.buffers.activeBuffer_, 1);
    }
    // Interaccion (sin rango) -> Enter -> Navegacion.
    {
        Editor ed;
        newBuffer(ed);                   // 2 buffers para poder abrir selector
        type(ed, "hola");
        openSelector(ed);                // priorState_ = Interaccion
        press(ed, EventType::InsertNewline);   // mismo buffer (B1)
        CHECK(ed.state_ == State::Navegacion); // reconcile, no priorState_
        CHECK_EQ(ed.active().document.lineAt(0), "hola");  // contenido intacto
    }
    // Seleccion (con rango) -> Enter -> Seleccion, rango intacto.
    {
        Editor ed;
        type(ed, "abc");
        press(ed, EventType::Escape);
        press(ed, EventType::MoveHome);
        pressEvent(ed, insert('s'));
        press(ed, EventType::MoveRight);
        openSelector(ed);
        press(ed, EventType::InsertNewline);
        CHECK(ed.state_ == State::Seleccion);
        CHECK(ed.hasSelection());
    }
}

// Seleccion -> Ctrl+K t -> ESC cancela el selector SIN tocar la seleccion
// y restaurando el modo previo (Seleccion), a diferencia del Enter que lo
// reconcilia con el buffer.
TEST(ctrl_k_t_escape_from_selection_keeps_selection) {
    Editor ed;
    type(ed, "abcdef");                  // B0
    press(ed, EventType::Escape);
    newBuffer(ed);                       // B1 vacio activo
    ed.activateBuffer(0);                // seleccion sobre B0
    press(ed, EventType::MoveHome);
    pressEvent(ed, insert('s'));
    press(ed, EventType::MoveRight);
    CHECK(ed.state_ == State::Seleccion);

    openSelector(ed);
    CHECK(ed.state_ == State::BufferSelector);
    press(ed, EventType::Escape);          // cancelar

    CHECK(ed.state_ == State::Seleccion);  // restaura priorState_
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK(ed.hasSelection());              // seleccion intacta
    CHECK_EQ(ed.active().document.lineAt(0), "abcdef");
}

// ---------------------------------------------------------------------------
// Seleccion -> Ctrl+K t -> B -> volver A. A CONSERVA la seleccion:
// el span seleccionado sigue siendo exactamente el mismo texto ("world").
// ---------------------------------------------------------------------------
TEST(ctrl_k_t_buffer_switch_returns_preserves_named_selection) {
    Editor ed;
    type(ed, "hello world");             // A = B0
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);      // col 0
    for (int i = 0; i < 6; ++i) press(ed, EventType::MoveRight); // col 6
    pressEvent(ed, insert('s'));         // seleccion: anchor (0,6)
    for (int i = 0; i < 5; ++i) press(ed, EventType::MoveRight); // -> (0,11)
    CHECK(ed.state_ == State::Seleccion);

    auto span = [&] {                     // texto seleccionado (una linea)
        auto s = ed.selection();
        CHECK(s.has_value());
        return ed.active().document.lineAt(s->start.line)
                   .substr(s->start.col, s->end.col - s->start.col);
    };
    CHECK_EQ(span(), "world");

    newBuffer(ed);                       // Ctrl+K n -> B1 vacio activo
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK(ed.state_ == State::Navegacion);
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().document.lineAt(0), "");

    openSelector(ed);                    // Ctrl+K t
    press(ed, EventType::MoveUp);        // -> A (B0)
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK(ed.state_ == State::Seleccion);      // vuelve a seleccion...
    CHECK(ed.hasSelection());
    CHECK_EQ(span(), "world");                 // ...con "world" aun seleccionado
}

// ---------------------------------------------------------------------------
// Ctrl+K w : cerrar buffer
// ---------------------------------------------------------------------------
TEST(ctrl_k_w_closes_active_and_activates_neighbor) {
    Editor ed;
    newBuffer(ed);                         // B1 activo
    newBuffer(ed);                         // B2 activo
    closeBuffer(ed);                       // cierra B2 (SinNombre2, el ultimo)
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    CHECK_EQ(ed.buffers.buffers_[0].unnamedName, "SinNombre");
    CHECK_EQ(ed.buffers.buffers_[1].unnamedName, "SinNombre1");
    // No abre el selector: activa el vecino que hereda la ranura (clamp
    // al final, aqui indice 1).
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().unnamedName, "SinNombre1");
}

TEST(ctrl_k_w_close_middle_buffer_preserves_others) {
    Editor ed;
    type(ed, "AAA");
    press(ed, EventType::Escape);
    newBuffer(ed);
    type(ed, "BBB");
    press(ed, EventType::Escape);
    newBuffer(ed);
    type(ed, "CCC");
    press(ed, EventType::Escape);

    // activar el buffer del medio (B1)
    ed.activateBuffer(1);
    ed.buffers.buffers_[1].modified = false;                       // limpio para cerrar
    ed.buffers.buffers_[1].savedLines = ed.buffers.buffers_[1].document.snapshot();
    closeBuffer(ed);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    CHECK_EQ(ed.active().document.lineAt(0), "CCC"); // hereda la ranura 1
    CHECK_EQ(ed.buffers.buffers_[0].document.lineAt(0), "AAA");
    CHECK(ed.state_ == State::Navegacion);
}

TEST(ctrl_k_w_last_buffer_resets_not_removes) {
    Editor ed;
    type(ed, "contenido");
    press(ed, EventType::Escape);
    ed.active().modified = false;                          // limpio para cerrar
    ed.active().savedLines = ed.active().document.snapshot();
    closeBuffer(ed);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK(ed.active().filename.empty());
    CHECK(!ed.active().modified);
    CHECK(ed.state_ == State::Navegacion);
    // El nombre nuevo es generico y distinto del anterior.
    CHECK_EQ(ed.active().unnamedName, "SinNombre1");
}

TEST(ctrl_k_w_modified_buffer_blocked) {
    Editor ed;
    type(ed, "x");                         // modificado
    CHECK(ed.active().modified);
    closeBuffer(ed);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1)); // no se cerro
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "x");
    CHECK(ed.state_ == State::Interaccion);  // vuelve al modo previo
    CHECK_EQ(ed.statusMessage_,
             "Buffer modificado: guarda con Ctrl+K s o restaura.");
}

TEST(ctrl_k_w_modified_blocked_until_save) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "x");
    press(ed, EventType::Escape);
    closeBuffer(ed);                       // bloqueado
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK(ed.active().modified);

    press(ed, EventType::Prefix);          // guardar
    press(ed, EventType::Save);
    CHECK(!ed.active().modified);

    closeBuffer(ed);                       // ahora si (ultimo buffer -> reset)
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK(ed.state_ == State::Navegacion);
}

TEST(ctrl_k_w_modified_multi_buffer_blocked) {
    Editor ed;
    type(ed, "x");                         // B0 modificado
    newBuffer(ed);                         // B1 activo, sin modificar
    closeBuffer(ed);                       // cierra B1 -> B0 hereda la ranura
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK(ed.state_ == State::Navegacion); // sin modal al cerrar
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK(ed.active().modified);

    closeBuffer(ed);                       // B0 modificado -> bloqueado
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "x");
    CHECK(ed.state_ == State::Navegacion);
}

TEST(save_unnamed_buffer_opens_save_as_prompt) {
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
    CHECK_EQ(ed.statusMessage_, "Save file: ");
    CHECK(ed.active().modified);
    CHECK(ed.active().filename.empty());
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
}

TEST(save_as_prompt_collects_typed_path) {
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    openSaveAs(ed);
    typePrompt(ed, "/tmp/nuevo.txt");
    CHECK_EQ(ed.saveAsPath_, "/tmp/nuevo.txt");
    CHECK_EQ(ed.statusMessage_, "Save file: /tmp/nuevo.txt");
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
    CHECK(ed.active().filename.empty()); // aun no se confirma
    CHECK(ed.active().modified);
}

TEST(save_as_prompt_backspace_removes_characters) {
    Editor ed;
    openSaveAs(ed);
    typePrompt(ed, "abc");
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.saveAsPath_, "ab");
    CHECK_EQ(ed.statusMessage_, "Save file: ab");
    press(ed, EventType::Backspace);
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.saveAsPath_, "");
    CHECK_EQ(ed.statusMessage_, "Save file: ");
}

TEST(save_as_prompt_backspace_on_empty_is_noop) {
    Editor ed;
    openSaveAs(ed);
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.saveAsPath_, "");
    CHECK_EQ(ed.statusMessage_, "Save file: ");
}

TEST(save_as_prompt_ignores_other_keys) {
    // El prompt es modal: una flecha no cancela ni se filtra.
    Editor ed;
    openSaveAs(ed);
    typePrompt(ed, "abc");
    press(ed, EventType::MoveRight);
    press(ed, EventType::Prefix);           // Ctrl+K tampoco filtra
    CHECK_EQ(ed.saveAsPath_, "abc");
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
}

TEST(save_as_enter_saves_file) {
    TempFile f;
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    openSaveAs(ed);
    typePrompt(ed, f.path);
    press(ed, EventType::InsertNewline);    // Enter: confirmar
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().filename, f.path);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.statusMessage_, "Guardado: " + f.path);

    std::ifstream in(f.path);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    CHECK_EQ(content, "hola");

    // A partir de aqui el buffer tiene nombre: Ctrl+K Ctrl+S guarda normal.
    type(ed, "!");
    press(ed, EventType::Escape);
    openSaveAs(ed);
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Guardado.");
}

// Ctrl+K n -> escribir -> Ctrl+K Save As -> guardar. Al confirmar:
// filename actualizado, SinNombre deja de mostrarse, modified == false.
// Luego Ctrl+K Ctrl+S guarda de nuevo en el mismo path (sin prompt).
TEST(save_as_on_new_buffer_updates_name_and_display) {
    TempFile f;
    Editor ed;
    newBuffer(ed);                       // B = SinNombre1
    CHECK_EQ(ed.active().unnamedName, "SinNombre1");
    type(ed, "hello");
    press(ed, EventType::Escape);
    CHECK(ed.active().filename.empty());          // aun sin nombre
    CHECK_EQ(ed.active().displayName(), "SinNombre1");

    openSaveAs(ed);                      // Ctrl+K Ctrl+S -> prompt
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
    typePrompt(ed, f.path);
    press(ed, EventType::InsertNewline);          // Enter: guardar

    CHECK(!ed.active().filename.empty());         // filename actualizado
    CHECK_EQ(ed.active().filename, f.path);
    const std::string base = f.path.substr(f.path.find_last_of('/') + 1);
    CHECK_EQ(ed.active().displayName(), base);    // SinNombre desaparece de la UI
    CHECK(!ed.active().modified);                 // modified == false

    // Ctrl+K Ctrl+S ya no abre el prompt: guarda normal en el mismo path.
    type(ed, "!");
    press(ed, EventType::Escape);
    CHECK(ed.active().modified);
    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
    CHECK(!ed.active().modified);

    std::ifstream in(f.path);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    CHECK_EQ(content, "hello!");                  // guardado en el nuevo path
}

// Ctrl+K n -> escribir -> Ctrl+K Save As -> Esc: se cancela sin perder nada.
// El buffer sigue sin nombre (SinNombre), el contenido esta intacto y el
// estado sigue marcado como modificado.
TEST(save_as_cancel_keeps_new_buffer_untouched) {
    TempFile f;
    Editor ed;
    newBuffer(ed);                       // B = SinNombre1
    type(ed, "hello");
    press(ed, EventType::Escape);
    CHECK_EQ(ed.active().document.lineAt(0), "hello");
    CHECK(ed.active().modified);
    const size_t undoSize = ed.active().undoStack.size();

    openSaveAs(ed);                      // Ctrl+K Ctrl+S -> prompt
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
    typePrompt(ed, f.path);              // se escribe una ruta...
    press(ed, EventType::Escape);        // ...pero se cancela con ESC

    CHECK_EQ(ed.active().filename, std::string());        // sigue sin nombre
    CHECK_EQ(ed.active().unnamedName, "SinNombre1");      // sigue SinNombre
    CHECK_EQ(ed.active().displayName(), "SinNombre1");
    CHECK_EQ(ed.active().document.lineAt(0), "hello");    // contenido intacto
    CHECK(ed.active().modified);                          // modified sigue true
    CHECK_EQ(ed.active().undoStack.size(), undoSize);      // historial intacto
    CHECK(!std::ifstream(f.path).good());                  // no se creo archivo
}

TEST(save_as_cancel_with_escape) {
    TempFile f;
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    openSaveAs(ed);
    typePrompt(ed, f.path);
    press(ed, EventType::Escape);           // cancelar
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
    CHECK(ed.active().filename.empty());
    CHECK(ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Guardado cancelado.");

    // No se creo el archivo.
    std::ifstream in(f.path);
    CHECK(!in.is_open());
}

TEST(save_as_cancel_returns_to_prior_mode) {
    // Abierto desde Interaccion, ESC devuelve a Interaccion.
    Editor ed;
    type(ed, "hola");                       // Interaccion
    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
    press(ed, EventType::Escape);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Interaccion));
}

TEST(save_as_enter_empty_path_stays_in_prompt) {
    Editor ed;
    openSaveAs(ed);
    press(ed, EventType::InsertNewline);    // Enter sin ruta
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
    CHECK(ed.active().filename.empty());
    CHECK_EQ(ed.statusMessage_, "Save file: ");
}

TEST(save_as_directory_rejected) {
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    openSaveAs(ed);
    typePrompt(ed, "/tmp");
    press(ed, EventType::InsertNewline);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
    CHECK(ed.active().filename.empty());
    CHECK(ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Es una carpeta: /tmp");
}

TEST(save_as_resolves_relative_path_against_cwd) {
    // Directorio temporal bajo /tmp para no ensuciar el repo.
    char dirTemplate[] = "/tmp/edit_saveas_XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    CHECK(dir != nullptr);
    char cwdBuf[4096];
    CHECK(getcwd(cwdBuf, sizeof cwdBuf) != nullptr);
    std::string cwdOld = cwdBuf;
    CHECK_EQ(chdir(dir), 0);

    Editor ed;
    type(ed, "rel");
    press(ed, EventType::Escape);
    openSaveAs(ed);
    typePrompt(ed, "notas.txt");
    press(ed, EventType::InsertNewline);
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().filename, std::string(dir) + "/notas.txt");

    std::ifstream in(std::string(dir) + "/notas.txt");
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    CHECK_EQ(content, "rel");

    // Restaurar cwd y limpiar el directorio temporal.
    chdir(cwdOld.c_str());
    std::remove((std::string(dir) + "/notas.txt").c_str());
    rmdir(dir);
}

// ---------------------------------------------------------------------------
// Invariantes globales del modelo de buffers (v0.6.3)
// ---------------------------------------------------------------------------
static void assertBuffersConsistent(Editor& ed) {
    // 1. Siempre existe al menos un buffer.
    CHECK(ed.buffers.buffers_.size() >= 1);
    // 2. Existe exactamente un buffer activo y es valido.
    CHECK(ed.buffers.activeBuffer_ >= 0);
    CHECK(ed.buffers.activeBuffer_ < static_cast<int>(ed.buffers.buffers_.size()));

    // 3-9. Cada buffer mantiene su propio estado coherente.
    for (const Buffer& b : ed.buffers.buffers_) {
        CHECK(b.document.lineCount() >= 1);
        CHECK(b.cursor.line >= 0);
        CHECK(b.cursor.line < b.document.lineCount());
        CHECK(b.cursor.col >= 0);
        CHECK(b.cursor.col <= b.document.lineLength(b.cursor.line));
        CHECK(b.undoStack.size() <= Buffer::MAX_UNDO);
        CHECK(b.redoStack.size() <= Buffer::MAX_UNDO);
        if (b.selection.has_value()) {
            CHECK(b.selection->anchor.line >= 0);
            CHECK(b.selection->anchor.line < b.document.lineCount());
            CHECK(b.selection->position.line >= 0);
            CHECK(b.selection->position.line < b.document.lineCount());
        }
    }

    // 10. El modo global es coherente con el buffer activo.
    if (ed.hasSelection()) {
        CHECK(ed.state_ == State::Seleccion || ed.state_ == State::Prefix);
    }
}

TEST(invariants_always_at_least_one_buffer) {
    Editor ed;
    newBuffer(ed);
    newBuffer(ed);
    closeBuffer(ed);
    press(ed, EventType::Escape);
    closeBuffer(ed);
    press(ed, EventType::Escape);
    closeBuffer(ed);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    assertBuffersConsistent(ed);
    // El editor sigue operable tras "cerrar todo".
    type(ed, "sigo vivo");
    CHECK_EQ(ed.active().document.lineAt(0), "sigo vivo");
}

TEST(invariants_switch_never_mixes_selection) {
    // Escenario del punto 13: seleccionar todo en A, cambiar a B y la
    // seleccion no puede aparecer en B.
    Editor ed;
    type(ed, "aaaaaaaa");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    pressEvent(ed, insert('s'));
    pressEvent(ed, insert('a'));            // seleccion total en B0
    CHECK(ed.hasSelection());
    newBuffer(ed);
    CHECK(!ed.hasSelection());         // B1 sin seleccion
    CHECK(!ed.active().selection.has_value());
    ed.activateBuffer(0);
    CHECK(ed.hasSelection());          // B0 la conserva
}

TEST(buffer_stress_mixed_operations) {
    Editor ed;
    type(ed, "hola");
    newBuffer(ed);
    type(ed, "mundo");
    newBuffer(ed);
    type(ed, "x");
    press(ed, EventType::Escape);
    assertBuffersConsistent(ed);

    unsigned long seed = 4242;
    auto rnd = [&seed]() {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int>((seed >> 33) & 0xFFFFFFFF);
    };

    for (int step = 0; step < 1000; ++step) {
        const int k = rnd() % 10;
        Event e;
        switch (k) {
            case 0:
            case 1:
            case 8:
                e.type = EventType::InsertChar;
                e.text = std::string(1, static_cast<char>('a' + (rnd() % 26)));
                break;
            case 2:
                e.type = static_cast<EventType>(
                    static_cast<int>(EventType::MoveLeft) + (rnd() % 6));
                break;
            case 3:
                e.type = EventType::Escape;
                break;
            case 4:
                e.type = (rnd() % 2) ? EventType::Undo : EventType::Redo;
                break;
            case 5:
                e.type = EventType::Prefix;
                break;
            case 6:
                e.type = (rnd() % 2) ? EventType::MoveUp : EventType::MoveDown;
                break;
            case 7:
                e.type = EventType::InsertNewline;
                break;
            default:
                e.type = EventType::None;
                break;
        }
        ed.handleEvent(e);
        assertBuffersConsistent(ed);
    }
}

// ---------------------------------------------------------------------------
// Renderer del selector (v0.6.3)
// ---------------------------------------------------------------------------
static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

TEST(renderer_buffer_list_marks_selected) {
    Renderer r;
    std::string out = r.buildBufferListScreen({"a.txt", "b.txt", "SinNombre"}, 1, 80, 10);
    // El item activo de la lista lleva el mismo gris que la fila del cursor
    // (listSelected == currentLine: lenguaje ACTIVO unificado), no video
    // inverso. El fondo debe cubrir TODO el ancho de la fila, no solo el
    // texto: en vez de asumir el ancho exacto del area de contenido (que
    // depende de computeLayout), se verifica la FORMA: estilo, texto,
    // padding de espacios, y reciEN despues el reset.
    std::string styledText = std::string(kListSelectedStyle) + "  b.txt";
    size_t stylePos = out.find(styledText);
    CHECK(stylePos != std::string::npos);
    size_t textEnd = stylePos + styledText.size();

    // El reset no debe estar pegado inmediatamente al texto: tiene que
    // haber padding (espacios) entre medio, prueba de que el fondo cubre
    // el resto de la fila.
    CHECK(out.compare(textEnd, 4, "\x1b[0m") != 0);

    size_t resetPos = out.find("\x1b[0m", textEnd);
    CHECK(resetPos != std::string::npos);
    CHECK(resetPos > textEnd); // hay espacios de relleno entre medio

    // Todo lo que hay entre el texto y el reset debe ser padding (espacios).
    std::string between = out.substr(textEnd, resetPos - textEnd);
    CHECK(between.find_first_not_of(' ') == std::string::npos);

    CHECK(contains(out, "  a.txt"));
    CHECK(contains(out, "  SinNombre"));
    CHECK(contains(out, "Buffers"));
    CHECK(contains(out, "SELECCIONAR"));
    CHECK(contains(out, "2/3"));
}

TEST(renderer_buffer_list_first_selected) {
    Renderer r;
    std::string out = r.buildBufferListScreen({"a.txt", "b.txt"}, 0, 80, 10);
    std::string styledText = std::string(kListSelectedStyle) + "  a.txt";
    size_t stylePos = out.find(styledText);
    CHECK(stylePos != std::string::npos);
    size_t textEnd = stylePos + styledText.size();
    // Mismo criterio: reset no pegado, hay padding antes.
    CHECK(out.compare(textEnd, 4, "\x1b[0m") != 0);
    size_t resetPos = out.find("\x1b[0m", textEnd);
    CHECK(resetPos != std::string::npos && resetPos > textEnd);

    CHECK(!contains(out, std::string(kListSelectedStyle) + "  b.txt"));
}

// El selector mantiene el aspecto del editor: filas vacias con "~". Ya no
// dibuja su propia barra en video inverso (MULTIBUFFER): produce datos
// (Buffers | SELECCIONAR | n/total) y se los entrega al StatusBar comun.
TEST(renderer_buffer_list_only_unified_bar) {
    Renderer r;
    std::string out = r.buildBufferListScreen({"a.txt", "b.txt"}, 1, 80, 10);
    // Filas vacias con el marcador del editor, alineado con las entradas
    // (misma indentacion de 2 espacios) y sin el texto "BUFFERS".
    CHECK(contains(out, "\x1b[K  " + std::string(kMarkerStyle) + "~\x1b[0m\r\n"));
    CHECK(!contains(out, "~ BUFFERS"));
    // Ya no hay barra en video inverso MULTIBUFFER: la barra es la del
    // StatusBar comun (fondo gris 60%) con Buffers/SELECCIONAR y el
    // contador estilo editor, sin Linea/Col ni la ruta del buffer.
    CHECK(!contains(out, "\x1b[7mMULTIBUFFER"));
    CHECK(contains(out, kStatusBarStyle));
    CHECK(contains(out, "Buffers"));
    CHECK(contains(out, "SELECCIONAR"));
    CHECK(contains(out, "2/2"));
    CHECK(!contains(out, "Linea:"));
    CHECK(!contains(out, "Col:"));
    CHECK(!contains(out, "a.txt - "));
}

TEST(buffer_names_include_modified_marker) {
    Editor ed;
    type(ed, "x");                       // B0 modificado
    newBuffer(ed);                       // B1 limpio
    std::vector<std::string> names = ed.bufferNames();
    CHECK_EQ(names.size(), size_t(2));
    CHECK_EQ(names[0], "SinNombre *");
    CHECK_EQ(names[1], "SinNombre1");
}
