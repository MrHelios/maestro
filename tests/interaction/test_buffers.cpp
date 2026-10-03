#include "test_support.h"
#include "rendering/Sink.h"
#include "rendering/tty/TtyRenderer.h"
#include <fstream>
#include <iterator>

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
    press(ed, InputEventType::Escape);
    newBuffer(ed);             // B1 activo
    type(ed, "mundo");         // B1: 5 entradas
    press(ed, InputEventType::Escape);
    CHECK_EQ(ed.buffers.buffers_[0].undoStack.size(), size_t(4));
    CHECK_EQ(ed.buffers.buffers_[1].undoStack.size(), size_t(5));

    // Undo en B1 no toca el historial de B0.
    ed.activateBuffer(1);
    press(ed, InputEventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "mund");
    CHECK_EQ(ed.buffers.buffers_[1].undoStack.size(), size_t(4));

    ed.activateBuffer(0);
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
    CHECK_EQ(ed.buffers.buffers_[0].undoStack.size(), size_t(4));
    press(ed, InputEventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "hol");

    // Volver a B1: sigue en "mund" con su propia pila.
    ed.activateBuffer(1);
    CHECK_EQ(ed.active().document.lineAt(0), "mund");
    CHECK_EQ(ed.buffers.buffers_[1].undoStack.size(), size_t(4));
}

TEST(buffers_isolate_redo_history) {
    Editor ed;
    type(ed, "hola");
    press(ed, InputEventType::Escape);
    newBuffer(ed);
    type(ed, "mundo");
    press(ed, InputEventType::Escape);

    ed.activateBuffer(1);
    press(ed, InputEventType::Undo);
    press(ed, InputEventType::Undo);
    CHECK(!ed.buffers.buffers_[1].redoStack.empty());

    ed.activateBuffer(0);
    CHECK(ed.buffers.buffers_[0].redoStack.empty());  // B0 no tiene redo propio
    press(ed, InputEventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "hola"); // no-op en B0

    ed.activateBuffer(1);
    press(ed, InputEventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "mund");
    CHECK_EQ(ed.buffers.buffers_[0].document.lineAt(0), "hola");
}

TEST(buffers_isolate_cursor_and_preferred_col) {
    Editor ed;
    type(ed, "hola mundo");
    press(ed, InputEventType::MoveHome);
    press(ed, InputEventType::MoveRight);
    press(ed, InputEventType::MoveRight);   // B0 cursor col 2
    const int col0 = ed.active().cursor.col;
    CHECK_EQ(col0, 2);

    newBuffer(ed);
    type(ed, "xyz");
    press(ed, InputEventType::MoveEnd);     // B1 cursor col 3
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
    press(ed, InputEventType::Escape);
    press(ed, InputEventType::MoveHome);
    pressEvent(ed, insert('s'));            // modo seleccion
    press(ed, InputEventType::MoveRight);   // selecciona "a"
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
    press(ed, InputEventType::Escape);
    press(ed, InputEventType::MoveHome);
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
    CHECK(ed.loadIntoActiveBuffer(f.path));        // B0 -> filename f.path
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
    CHECK(ed.loadIntoActiveBuffer(f.path));
    const std::string base = f.path.substr(f.path.find_last_of('/') + 1);
    CHECK_EQ(ed.active().displayName(), base);
    CHECK(ed.active().unnamedName == "SinNombre");
}

TEST(clipboard_global_across_buffers) {
    Editor ed;
    type(ed, "abc");
    press(ed, InputEventType::Escape);
    press(ed, InputEventType::MoveHome);
    pressEvent(ed, insert('s'));
    press(ed, InputEventType::MoveRight);   // [a]
    pressEvent(ed, insert('c'));            // copia "a" -> Navegacion
    CHECK(ed.getClipboardBlock() == (std::vector<std::string>{"a"}));

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
// (vía resize(), dueño: composition root/loop) y no quedarse con el
// Viewport por defecto 24x80.
TEST(ctrl_k_n_new_buffer_viewport_matches_terminal) {
    Editor ed;
    ed.resize(30, 100);
    const int vpHeight = 28;  // 30 - 2 (status bar)
    const int vpWidth = 100;

    newBuffer(ed);
    CHECK_EQ(ed.active().viewport.height, vpHeight);
    CHECK_EQ(ed.active().viewport.width, vpWidth);
}

// El mismo bug aplica al reinicio del ultimo buffer (Ctrl+K w): al
// resetear debe conservar las dimensiones del último resize().
TEST(ctrl_k_w_last_buffer_reset_keeps_terminal_viewport) {
    Editor ed;
    ed.resize(30, 100);
    const int vpHeight = 28;
    const int vpWidth = 100;

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
    press(ed, InputEventType::MoveUp);
    press(ed, InputEventType::MoveUp);          // index 0
    press(ed, InputEventType::InsertNewline);   // activar SinNombre
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    closeBuffer(ed);                       // cerrar SinNombre (sin modificar)
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    press(ed, InputEventType::Escape);          // salir del selector

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

    press(ed, InputEventType::MoveUp);
    CHECK_EQ(ed.bufferSelectorIndex_, 1);
    press(ed, InputEventType::MoveDown);
    CHECK_EQ(ed.bufferSelectorIndex_, 2);
    press(ed, InputEventType::MoveDown);        // clamp abajo
    CHECK_EQ(ed.bufferSelectorIndex_, 2);
    press(ed, InputEventType::MoveUp);
    press(ed, InputEventType::MoveUp);
    press(ed, InputEventType::MoveUp);          // clamp arriba
    CHECK_EQ(ed.bufferSelectorIndex_, 0);
}

TEST(ctrl_k_t_enter_switches_buffer) {
    Editor ed;
    type(ed, "hola");                      // B0
    newBuffer(ed);
    type(ed, "mundo");                     // B1 activo
    openSelector(ed);
    press(ed, InputEventType::MoveUp);          // seleccionar B0
    press(ed, InputEventType::InsertNewline);   // Enter
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
}

TEST(ctrl_k_t_escape_returns_to_previous_buffer_and_mode) {
    Editor ed;
    type(ed, "hola");
    press(ed, InputEventType::Escape);
    newBuffer(ed);
    type(ed, "mundo");                     // B1 activo en Interaccion
    openSelector(ed);
    CHECK(ed.state_ == State::BufferSelector);
    press(ed, InputEventType::Escape);
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
    press(ed, InputEventType::MoveRight);
    press(ed, InputEventType::Undo);
    press(ed, InputEventType::Redo);
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
    press(ed, InputEventType::Prefix);          // Ctrl+K dentro del selector
    CHECK(ed.state_ == State::BufferSelector); // no hay segunda capa
    press(ed, InputEventType::Escape);
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
    press(ed, InputEventType::Escape);
    newBuffer(ed);                       // B1 vacio activo
    ed.activateBuffer(0);                // seleccion sobre B0
    press(ed, InputEventType::MoveHome);
    pressEvent(ed, insert('s'));         // modo seleccion
    press(ed, InputEventType::MoveRight);     // rango [0,0)-(0,1) sobre "abcdef"
    CHECK(ed.hasSelection());

    const auto anchor = ed.active().selection->anchor;
    const auto pos = ed.active().selection->position;
    CHECK(ed.state_ == State::Seleccion);

    openSelector(ed);                    // Ctrl+K t
    CHECK(ed.state_ == State::BufferSelector);
    CHECK_EQ(ed.bufferSelectorIndex_, 0);  // activo = B0
    press(ed, InputEventType::InsertNewline);   // Enter: mismo buffer

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
    press(ed, InputEventType::Escape);
    newBuffer(ed);                       // B1 vacio activo
    ed.activateBuffer(0);                // sobre B0 fijamos la seleccion
    press(ed, InputEventType::MoveHome);
    pressEvent(ed, insert('s'));         // seleccion en B0
    press(ed, InputEventType::MoveRight);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK(ed.hasSelection());

    const auto anchor = ed.active().selection->anchor;
    const auto pos = ed.active().selection->position;

    openSelector(ed);                    // Ctrl+K t (activo = B0)
    press(ed, InputEventType::MoveDown);      // -> B1
    press(ed, InputEventType::InsertNewline);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK(ed.state_ == State::Navegacion);   // B1 sin seleccion

    openSelector(ed);                    // Ctrl+K t (activo = B1)
    press(ed, InputEventType::MoveUp);        // -> B0
    press(ed, InputEventType::InsertNewline);
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
        press(ed, InputEventType::InsertNewline);   // mismo buffer (B1)
        CHECK(ed.state_ == State::Navegacion);
        CHECK_EQ(ed.buffers.activeBuffer_, 1);
    }
    // Interaccion (sin rango) -> Enter -> Navegacion.
    {
        Editor ed;
        newBuffer(ed);                   // 2 buffers para poder abrir selector
        type(ed, "hola");
        openSelector(ed);                // priorState_ = Interaccion
        press(ed, InputEventType::InsertNewline);   // mismo buffer (B1)
        CHECK(ed.state_ == State::Navegacion); // reconcile, no priorState_
        CHECK_EQ(ed.active().document.lineAt(0), "hola");  // contenido intacto
    }
    // Seleccion (con rango) -> Enter -> Seleccion, rango intacto.
    {
        Editor ed;
        type(ed, "abc");
        press(ed, InputEventType::Escape);
        press(ed, InputEventType::MoveHome);
        pressEvent(ed, insert('s'));
        press(ed, InputEventType::MoveRight);
        openSelector(ed);
        press(ed, InputEventType::InsertNewline);
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
    press(ed, InputEventType::Escape);
    newBuffer(ed);                       // B1 vacio activo
    ed.activateBuffer(0);                // seleccion sobre B0
    press(ed, InputEventType::MoveHome);
    pressEvent(ed, insert('s'));
    press(ed, InputEventType::MoveRight);
    CHECK(ed.state_ == State::Seleccion);

    openSelector(ed);
    CHECK(ed.state_ == State::BufferSelector);
    press(ed, InputEventType::Escape);          // cancelar

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
    press(ed, InputEventType::Escape);
    press(ed, InputEventType::MoveHome);      // col 0
    for (int i = 0; i < 6; ++i) press(ed, InputEventType::MoveRight); // col 6
    pressEvent(ed, insert('s'));         // seleccion: anchor (0,6)
    for (int i = 0; i < 5; ++i) press(ed, InputEventType::MoveRight); // -> (0,11)
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
    press(ed, InputEventType::MoveUp);        // -> A (B0)
    press(ed, InputEventType::InsertNewline);
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
    press(ed, InputEventType::Escape);
    newBuffer(ed);
    type(ed, "BBB");
    press(ed, InputEventType::Escape);
    newBuffer(ed);
    type(ed, "CCC");
    press(ed, InputEventType::Escape);

    // activar el buffer del medio (B1)
    ed.activateBuffer(1);
    ed.buffers.buffers_[1].modified = false;                       // limpio para cerrar
    ed.buffers.buffers_[1].originalSnapshot_ = ed.buffers.buffers_[1].document.snapshot();
    closeBuffer(ed);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    CHECK_EQ(ed.active().document.lineAt(0), "CCC"); // hereda la ranura 1
    CHECK_EQ(ed.buffers.buffers_[0].document.lineAt(0), "AAA");
    CHECK(ed.state_ == State::Navegacion);
}

TEST(ctrl_k_w_last_buffer_resets_not_removes) {
    Editor ed;
    type(ed, "contenido");
    press(ed, InputEventType::Escape);
    ed.active().modified = false;                          // limpio para cerrar
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
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
    ed.loadIntoActiveBuffer(f.path);
    type(ed, "x");
    press(ed, InputEventType::Escape);
    closeBuffer(ed);                       // bloqueado
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK(ed.active().modified);

    save(ed);
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

TEST(save_unnamed_buffer_opens_save_as_filebrowser) {
    Editor ed;
    type(ed, "hola");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    // saveAsFileName_ se prefill con "" para buffer sin nombre
    CHECK_EQ(ed.saveAsFileName_, "");
    CHECK(ed.active().modified);
    CHECK(ed.active().filename.empty());
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
}

TEST(save_as_filebrowser_collects_typed_filename) {
    Editor ed;
    type(ed, "hola");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, "nuevo.txt");
    CHECK_EQ(ed.saveAsFileName_, "nuevo.txt");
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK(ed.active().filename.empty());
    CHECK(ed.active().modified);
}

TEST(save_as_filebrowser_backspace_removes_characters) {
    Editor ed;
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, "abc");
    press(ed, InputEventType::Backspace);
    CHECK_EQ(ed.saveAsFileName_, "ab");
    press(ed, InputEventType::Backspace);
    press(ed, InputEventType::Backspace);
    CHECK_EQ(ed.saveAsFileName_, "");
}

TEST(save_as_filebrowser_backspace_on_empty_is_noop) {
    Editor ed;
    openSaveAs(ed);
    clearPrompt(ed);
    press(ed, InputEventType::Backspace);
    CHECK_EQ(ed.saveAsFileName_, "");
}

TEST(save_as_filebrowser_ignores_other_keys) {
    Editor ed;
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, "abc");
    press(ed, InputEventType::MoveRight);  // no-op en SaveAsFileBrowser
    CHECK_EQ(ed.saveAsFileName_, "abc");
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    // Prefix (Ctrl+K) cancela el modal
    press(ed, InputEventType::Prefix);
    CHECK(static_cast<int>(ed.state_) != static_cast<int>(State::SaveAsFileBrowser));
}

// Contrato basename: '/' y '\' se filtran del input (el directorio solo
// cambia navegando carpetas con Enter, nunca editando el nombre).
TEST(save_as_filebrowser_rejects_slashes_in_filename) {
    Editor ed;
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, "a/b\\c");
    CHECK_EQ(ed.saveAsFileName_, "abc");
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    // Hay pista visible de por que no entro el caracter.
    CHECK(ed.statusMessage_.text.find("carpeta") != std::string::npos);
}

// Contrato basename: "." y ".." no se confirman (escaparian del directorio
// seleccionado). El modal se queda, no se escribe nada y el input se
// conserva para corregir.
TEST(save_as_filebrowser_dot_names_rejected_on_commit) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);

    Editor ed;
    type(ed, "x");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    const int countBefore = ed.buffers.count();
    const std::string pathBefore = ed.fileBrowser.path_;

    clearPrompt(ed);
    typePrompt(ed, "..");
    saveAsConfirm(ed);  // Ctrl+S: debe rechazar
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK_EQ(ed.saveAsFileName_, "..");
    CHECK(ed.active().filename.empty());
    CHECK_EQ(ed.buffers.count(), countBefore);
    CHECK_EQ(ed.fileBrowser.path_, pathBefore);  // no escapo del directorio
    CHECK(ed.statusMessage_.text.find("invalido") != std::string::npos);

    clearPrompt(ed);
    typePrompt(ed, ".");
    saveAsConfirm(ed);  // Ctrl+S: debe rechazar
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK(ed.active().filename.empty());
    CHECK_EQ(ed.buffers.count(), countBefore);
    CHECK_EQ(ed.fileBrowser.path_, pathBefore);
}

TEST(save_as_ctrl_s_saves_file) {
    // Directorio aislado: el browser arranca en cwd y el nombre se resuelve
    // contra el (sin rutas fijas en /tmp que colisionen entre corridas).
    TempDir t;
    CwdGuard g;
    g.enter(t.path);

    Editor ed;
    type(ed, "hola");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, "nuevo.txt");
    saveAsConfirm(ed);  // Ctrl+S
    CHECK(!ed.active().modified);
    const std::string expected = t.path + "/nuevo.txt";
    CHECK_EQ(ed.active().filename, expected);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
    CHECK(ed.statusMessage_.text.find("Guardado") != std::string::npos);

    CHECK_EQ(fileContent(expected), "hola");

    type(ed, "!");
    press(ed, InputEventType::Escape);
    save(ed);
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Guardado.");
}

// Ctrl+K n -> escribir -> Ctrl+K Save As -> guardar. Al confirmar:
// filename actualizado, SinNombre deja de mostrarse, modified == false.
// Luego Ctrl+K Ctrl+S guarda de nuevo en el mismo path (sin prompt).
TEST(save_as_on_new_buffer_updates_name_and_display) {
    TempFile f;
    // El browser arranca en cwd para buffers sin nombre: entrar al
    // directorio padre del destino (RAII restaura cwd aunque falle un CHECK).
    CwdGuard g;
    g.enter(std::filesystem::path(f.path).parent_path().string());

    Editor ed;
    newBuffer(ed);
    CHECK_EQ(ed.active().unnamedName, "SinNombre1");
    type(ed, "hello");
    press(ed, InputEventType::Escape);
    CHECK(ed.active().filename.empty());
    CHECK_EQ(ed.active().displayName(), "SinNombre1");

    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    clearPrompt(ed);
    typePrompt(ed, std::filesystem::path(f.path).filename().string());
    saveAsConfirm(ed);  // Ctrl+S

    CHECK(!ed.active().filename.empty());
    CHECK_EQ(ed.active().filename, f.path);
    const std::string base = f.path.substr(f.path.find_last_of('/') + 1);
    CHECK_EQ(ed.active().displayName(), base);
    CHECK(!ed.active().modified);

    type(ed, "!");
    press(ed, InputEventType::Escape);
    CHECK(ed.active().modified);
    save(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
    CHECK(!ed.active().modified);

    std::ifstream in(f.path);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    CHECK_EQ(content, "hello!");
}

// Ctrl+K n -> escribir -> Ctrl+K Save As -> Esc: se cancela sin perder nada.
// El buffer sigue sin nombre (SinNombre), el contenido esta intacto y el
// estado sigue marcado como modificado.
TEST(save_as_cancel_keeps_new_buffer_untouched) {
    TempFile f;
    Editor ed;
    newBuffer(ed);
    type(ed, "hello");
    press(ed, InputEventType::Escape);
    CHECK_EQ(ed.active().document.lineAt(0), "hello");
    CHECK(ed.active().modified);
    const size_t undoSize = ed.active().undoStack.size();

    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    clearPrompt(ed);
    typePrompt(ed, std::filesystem::path(f.path).filename().string());
    press(ed, InputEventType::Escape);

    CHECK_EQ(ed.active().filename, std::string());
    CHECK_EQ(ed.active().unnamedName, "SinNombre1");
    CHECK_EQ(ed.active().displayName(), "SinNombre1");
    CHECK_EQ(ed.active().document.lineAt(0), "hello");
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), undoSize);
    CHECK(!std::ifstream(f.path).good());
}

TEST(save_as_cancel_with_escape) {
    TempFile f;
    Editor ed;
    type(ed, "hola");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, std::filesystem::path(f.path).filename().string());
    press(ed, InputEventType::Escape);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
    CHECK(ed.active().filename.empty());
    CHECK(ed.active().modified);
    CHECK_EQ(ed.statusMessage_, "Guardado cancelado.");

    std::ifstream in(f.path);
    CHECK(!in.is_open());
}

TEST(save_as_cancel_returns_to_prior_mode) {
    // Abierto desde Interaccion, ESC devuelve a Interaccion.
    Editor ed;
    type(ed, "hola");                       // Interaccion
    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    press(ed, InputEventType::Escape);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Interaccion));
}

TEST(save_as_empty_filename_stays_in_filebrowser) {
    Editor ed;
    openSaveAs(ed);
    clearPrompt(ed);
    saveAsConfirm(ed);  // Ctrl+S con nombre vacío
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK(ed.active().filename.empty());
    CHECK(ed.statusMessage_.text.find("vacio") != std::string::npos);
}

TEST(save_as_directory_rejected) {
    // Una carpeta existente no puede convertirse en destino de archivo:
    // el nombre se trata como basename y el commit rechaza lo que resuelve
    // a un directorio (rama isDirectory de commitSaveAsFileBrowser).
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    std::filesystem::create_directories(t.path + "/sub");

    Editor ed;
    type(ed, "hola");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, "sub");
    saveAsConfirm(ed);  // Ctrl+S: debe rechazar, no guardar
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK(ed.statusMessage_.text.find("Es una carpeta") != std::string::npos);
    CHECK_EQ(ed.saveAsFileName_, "sub");  // input conservado para corregir
    CHECK(ed.active().filename.empty());
    CHECK(ed.active().modified);
    CHECK_EQ(ed.buffers.count(), 1);
    CHECK(std::filesystem::is_directory(t.path + "/sub"));  // sigue siendo dir
}

TEST(save_as_resolves_relative_path_against_cwd) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);

    Editor ed;
    type(ed, "rel");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, "notas.txt");
    saveAsConfirm(ed);  // Ctrl+S
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().filename, t.path + "/notas.txt");
    CHECK_EQ(fileContent(t.path + "/notas.txt"), "rel");
}

TEST(save_as_unnamed_prefills_empty_filename) {
    Editor ed;
    openSaveAs(ed);
    // saveAsFileName_ está vacío para buffer sin nombre
    CHECK_EQ(ed.saveAsFileName_, "");
}

TEST(save_as_unnamed_editable_filename) {
    TempFile f;
    // El browser arranca en cwd: entrar al directorio del destino.
    CwdGuard g;
    g.enter(std::filesystem::path(f.path).parent_path().string());

    Editor ed;
    type(ed, "hi");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    // saveAsFileName_ está vacío inicialmente
    CHECK_EQ(ed.saveAsFileName_, "");
    clearPrompt(ed);
    typePrompt(ed, std::filesystem::path(f.path).filename().string());
    saveAsConfirm(ed);  // Ctrl+S
    CHECK_EQ(ed.active().filename, f.path);
    CHECK(!ed.active().modified);
}

TEST(save_as_copy_prefills_current_filename) {
    TempFile f;
    f.write("x");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f.path));
    openSaveAs(ed);
    // saveAsFileName_ se prefill con el nombre del archivo actual
    CHECK_EQ(ed.saveAsFileName_, std::filesystem::path(f.path).filename().string());
}

// Núcleo del UX SaveAs, verificado a la vez: buffer con nombre en
// <dir>/foo/bar.txt -> el browser arranca en <dir>/foo (directorio del
// archivo, no cwd) Y el input se prellena con "bar.txt".
TEST(save_as_starts_in_file_directory_with_prefilled_name) {
    TempDir t;
    std::filesystem::create_directories(t.path + "/foo");
    const std::string orig = t.path + "/foo/bar.txt";
    { std::ofstream f(orig, std::ios::binary); f << "x"; }

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/foo");
    CHECK_EQ(ed.saveAsFileName_, "bar.txt");
}

TEST(save_as_copy_allows_editing_filename) {
    // Fuente dentro del dir aislado: el browser arranca en el dir del
    // archivo abierto, asi el nombre editado cae tambien ahi.
    TempDir t;
    const std::string orig = t.path + "/orig.txt";
    { std::ofstream f(orig, std::ios::binary); f << "x"; }
    CwdGuard g;
    g.enter(t.path);

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openSaveAs(ed);
    // saveAsFileName_ tiene el nombre del archivo original
    CHECK_EQ(ed.saveAsFileName_, "orig.txt");
    clearPrompt(ed);
    typePrompt(ed, "copia.txt");
    saveAsConfirm(ed);  // Ctrl+S
    CHECK_EQ(ed.active().filename, t.path + "/copia.txt");
    CHECK(!ed.active().modified);
    CHECK_EQ(fileContent(t.path + "/copia.txt"), "x");
    CHECK_EQ(fileContent(orig), "x");  // el original intacto
}

TEST(save_as_unnamed_user_can_change_directory) {
    // Flujo real de cambio de directorio por navegacion: crear temp/sub/,
    // abrir Save As en temp, seleccionar sub/, Enter, escribir copia.txt,
    // Ctrl+S => temp/sub/copia.txt.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    std::filesystem::create_directories(t.path + "/sub");

    Editor ed;
    type(ed, "data");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    CHECK_EQ(ed.fileBrowser.path_, t.path);
    int dirIdx = -1;
    for (int i = 0; i < static_cast<int>(ed.fileBrowser.entries_.size()); ++i) {
        const auto& e = ed.fileBrowser.entries_[static_cast<size_t>(i)];
        if (e.isDirectory && e.name == "sub") { dirIdx = i; break; }
    }
    CHECK(dirIdx >= 0);
    while (ed.fileBrowser.index_ < dirIdx) press(ed, InputEventType::MoveDown);
    while (ed.fileBrowser.index_ > dirIdx) press(ed, InputEventType::MoveUp);
    press(ed, InputEventType::InsertNewline);  // Enter: entra a sub
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/sub");
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));

    clearPrompt(ed);
    typePrompt(ed, "copia.txt");
    saveAsConfirm(ed);  // Ctrl+S
    CHECK_EQ(ed.active().filename, t.path + "/sub/copia.txt");
    CHECK(!ed.active().modified);
    CHECK_EQ(fileContent(t.path + "/sub/copia.txt"), "data");
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
}

TEST(invariants_always_at_least_one_buffer) {
    Editor ed;
    newBuffer(ed);
    newBuffer(ed);
    closeBuffer(ed);
    press(ed, InputEventType::Escape);
    closeBuffer(ed);
    press(ed, InputEventType::Escape);
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
    press(ed, InputEventType::Escape);
    press(ed, InputEventType::MoveHome);
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
    press(ed, InputEventType::Escape);
    assertBuffersConsistent(ed);

    unsigned long seed = 4242;
    auto rnd = [&seed]() {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int>((seed >> 33) & 0xFFFFFFFF);
    };

    for (int step = 0; step < 1000; ++step) {
        const int k = rnd() % 10;
        InputEvent e;
        switch (k) {
            case 0:
            case 1:
            case 8:
                e.type = InputEventType::InsertChar;
                e.text = std::string(1, static_cast<char>('a' + (rnd() % 26)));
                break;
            case 2:
                e.type = static_cast<InputEventType>(
                    static_cast<int>(InputEventType::MoveLeft) + (rnd() % 6));
                break;
            case 3:
                e.type = InputEventType::Escape;
                break;
            case 4:
                e.type = (rnd() % 2) ? InputEventType::Undo : InputEventType::Redo;
                break;
            case 5:
                e.type = InputEventType::Prefix;
                break;
            case 6:
                e.type = (rnd() % 2) ? InputEventType::MoveUp : InputEventType::MoveDown;
                break;
            case 7:
                e.type = InputEventType::InsertNewline;
                break;
            default:
                e.type = InputEventType::None;
                break;
        }
        ed.handleEvent(e);
        assertBuffersConsistent(ed);
    }
}

TEST(renderer_buffer_list_marks_selected) {
    TtyRenderer tr;
    std::string out = tr.buildBufferListScreen({"a.txt", "b.txt", "SinNombre"}, 1, 80, 10);
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
    CHECK(out.compare(textEnd, tr.theme().reset.size(), tr.theme().reset) != 0);

    size_t resetPos = out.find(tr.theme().reset, textEnd);
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
    TtyRenderer tr;
    std::string out = tr.buildBufferListScreen({"a.txt", "b.txt"}, 0, 80, 10);
    std::string styledText = std::string(kListSelectedStyle) + "  a.txt";
    size_t stylePos = out.find(styledText);
    CHECK(stylePos != std::string::npos);
    size_t textEnd = stylePos + styledText.size();
    // Mismo criterio: reset no pegado, hay padding antes.
    CHECK(out.compare(textEnd, tr.theme().reset.size(), tr.theme().reset) != 0);
    size_t resetPos = out.find(tr.theme().reset, textEnd);
    CHECK(resetPos != std::string::npos && resetPos > textEnd);

    CHECK(!contains(out, std::string(kListSelectedStyle) + "  b.txt"));
}

// El selector mantiene el aspecto del editor: filas vacias con "~". Ya no
// dibuja su propia barra en video inverso (MULTIBUFFER): produce datos
// (Buffers | SELECCIONAR | n/total) y se los entrega al chrome comun.
TEST(renderer_buffer_list_only_unified_bar) {
    TtyRenderer tr;
    std::string out = tr.buildBufferListScreen({"a.txt", "b.txt"}, 1, 80, 10);
    // Filas vacias con el marcador del editor, alineado con las entradas
    // (misma indentacion de 2 espacios) y sin el texto "BUFFERS".
    CHECK(contains(out, "\x1b[K  " + std::string(kMarkerStyle) + "~" + std::string(tr.theme().reset) + "\r\n"));
    CHECK(!contains(out, "~ BUFFERS"));
    // Ya no hay barra en video inverso MULTIBUFFER: la barra es la del
    // chrome comun (fondo gris 60%) con Buffers/SELECCIONAR y el
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

TEST(ctrl_k_b_single_buffer_no_change) {
    Editor ed;
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK(ed.state_ == State::Navegacion);
    // Mensaje de advertencia
    CHECK(ed.statusMessage_ == "No hay buffer anterior.");
}

TEST(ctrl_k_b_two_buffers_toggle) {
    Editor ed;
    type(ed, "A");                       // B0
    press(ed, InputEventType::Escape);
    newBuffer(ed);                       // B1 activo
    type(ed, "B");
    press(ed, InputEventType::Escape);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().document.lineAt(0), "B");

    previousBuffer(ed);                  // Ctrl+K b -> B0
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK_EQ(ed.active().document.lineAt(0), "A");

    previousBuffer(ed);                  // Ctrl+K b -> B1 (toggle)
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().document.lineAt(0), "B");
}

TEST(ctrl_k_b_three_buffers_last_activated) {
    Editor ed;
    type(ed, "A");                       // B0
    press(ed, InputEventType::Escape);
    newBuffer(ed);                       // B1
    type(ed, "B");
    press(ed, InputEventType::Escape);
    newBuffer(ed);                       // B2 activo
    type(ed, "C");
    press(ed, InputEventType::Escape);

    previousBuffer(ed);                  // Ctrl+K b -> B1 (ultimo activado)
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().document.lineAt(0), "B");
}

TEST(ctrl_k_b_via_selector_updates_history) {
    Editor ed;
    type(ed, "A");                       // B0
    press(ed, InputEventType::Escape);
    newBuffer(ed);                       // B1
    type(ed, "B");
    press(ed, InputEventType::Escape);
    newBuffer(ed);                       // B2 activo
    type(ed, "C");
    press(ed, InputEventType::Escape);

    // Via selector: ir a B1
    openSelector(ed);
    press(ed, InputEventType::MoveUp);        // B1
    press(ed, InputEventType::InsertNewline);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);

    // Ctrl+K b -> B2 (el buffer anterior era B2)
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 2);
    CHECK_EQ(ed.active().document.lineAt(0), "C");

    // Toggle -> B1
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().document.lineAt(0), "B");
}

TEST(ctrl_k_b_previous_buffer_closed) {
    Editor ed;
    type(ed, "A");                       // B0
    press(ed, InputEventType::Escape);
    newBuffer(ed);                       // B1 activo
    type(ed, "B");
    press(ed, InputEventType::Escape);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);

    // Cerrar B0 (el anterior) - necesita estar sin modificar
    ed.activateBuffer(0);
    ed.active().modified = false;
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    closeBuffer(ed);                     // B0 cerrado
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK_EQ(ed.buffers.activeBuffer_, 0); // B1 hereda ranura 0

    // Ctrl+K b: B0 ya no existe
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 0); // se queda en B1
    CHECK(ed.statusMessage_.find("ya no existe") != std::string::npos);
    CHECK(ed.statusMessage_.find("fue cerrado") != std::string::npos);
}

TEST(ctrl_k_b_previous_buffer_closed_via_selector) {
    Editor ed;
    type(ed, "A");                       // B0
    press(ed, InputEventType::Escape);
    newBuffer(ed);                       // B1
    type(ed, "B");
    press(ed, InputEventType::Escape);
    newBuffer(ed);                       // B2 activo
    type(ed, "C");
    press(ed, InputEventType::Escape);

    // Ir a B1 via selector
    openSelector(ed);
    press(ed, InputEventType::MoveUp);        // B1
    press(ed, InputEventType::InsertNewline);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);

    // Cerrar B2 (el "anterior") - necesita estar sin modificar
    ed.activateBuffer(2);
    ed.active().modified = false;
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    closeBuffer(ed);                     // cierra B2
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    // Activo ahora es B1 (indice 1, que era B1 antes)
    CHECK_EQ(ed.buffers.activeBuffer_, 1);

    // Ctrl+K b: el anterior era B2, que fue cerrado
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 1); // se queda en B1
    CHECK(ed.statusMessage_.find("ya no existe") != std::string::npos);
    CHECK(ed.statusMessage_.find("fue cerrado") != std::string::npos);
}

TEST(ctrl_k_b_three_new_buffers_toggle_via_b) {
    Editor ed;
    type(ed, "A");
    press(ed, InputEventType::Escape);
    newBuffer(ed);
    type(ed, "B");
    press(ed, InputEventType::Escape);
    newBuffer(ed);
    type(ed, "C");
    press(ed, InputEventType::Escape);
    CHECK_EQ(ed.buffers.activeBuffer_, 2);
    CHECK_EQ(ed.active().document.lineAt(0), "C");
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().document.lineAt(0), "B");
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 2);
    CHECK_EQ(ed.active().document.lineAt(0), "C");
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().document.lineAt(0), "B");
}

TEST(ctrl_k_b_selector_open_without_selection_preserves_history) {
    Editor ed;
    type(ed, "A");
    press(ed, InputEventType::Escape);
    newBuffer(ed);
    type(ed, "B");
    press(ed, InputEventType::Escape);
    newBuffer(ed);
    type(ed, "C");
    press(ed, InputEventType::Escape);
    CHECK_EQ(ed.buffers.activeBuffer_, 2);
    int prevIdBefore = ed.previousBuffer_.id;
    CHECK(prevIdBefore != -1);
    openSelector(ed);
    CHECK(ed.state_ == State::BufferSelector);
    press(ed, InputEventType::Escape);
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.buffers.activeBuffer_, 2);
    CHECK_EQ(ed.previousBuffer_.id, prevIdBefore);
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().document.lineAt(0), "B");
}

TEST(ctrl_k_b_close_active_with_previous_to_third_shows_closed) {
    Editor ed;
    type(ed, "A");
    press(ed, InputEventType::Escape);
    newBuffer(ed);
    type(ed, "B");
    press(ed, InputEventType::Escape);
    newBuffer(ed);
    type(ed, "C");
    press(ed, InputEventType::Escape);
    CHECK_EQ(ed.buffers.activeBuffer_, 2);
    ed.active().modified = false;
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    closeBuffer(ed);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().document.lineAt(0), "B");
    CHECK(ed.previousBuffer_.valid);
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK(ed.statusMessage_.find("ya no existe") != std::string::npos);
    CHECK(ed.statusMessage_.find("fue cerrado") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Ctrl+K b : mode reconciliation with buffer state
// ---------------------------------------------------------------------------

TEST(ctrl_k_b_reconciles_mode_selection) {
    Editor ed;
    type(ed, "hello world");             // B0
    press(ed, InputEventType::Escape);
    press(ed, InputEventType::MoveHome);
    for (int i = 0; i < 6; ++i) press(ed, InputEventType::MoveRight); // col 6
    pressEvent(ed, insert('s'));         // modo seleccion en B0
    for (int i = 0; i < 5; ++i) press(ed, InputEventType::MoveRight); // seleccion "world"
    CHECK(ed.state_ == State::Seleccion);
    CHECK(ed.hasSelection());

    newBuffer(ed);                       // B1 activo, sin seleccion
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK(ed.state_ == State::Navegacion);
    CHECK(!ed.hasSelection());

    // Ctrl+K b -> B0, modo se reconcilia a Seleccion
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK(ed.state_ == State::Seleccion);
    CHECK(ed.hasSelection());
    // seleccion intacta
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(ed.active().document.lineAt(sel->start.line)
                 .substr(sel->start.col, sel->end.col - sel->start.col),
             "world");
}

TEST(ctrl_k_b_reconciles_mode_interaction) {
    Editor ed;
    type(ed, "hello");                   // B0 en Interaccion
    CHECK(ed.state_ == State::Interaccion);
    CHECK_EQ(ed.active().document.lineAt(0), "hello");

    newBuffer(ed);                       // B1
    type(ed, "xyz");                     // B1 con contenido
    press(ed, InputEventType::Escape);        // -> Navegacion, cursor al final
    press(ed, InputEventType::MoveLeft);      // cursor a 'z'
    pressEvent(ed, insert('s'));         // B1 en Seleccion, anchor en 'z'
    press(ed, InputEventType::MoveRight);     // extiende seleccion
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK(ed.state_ == State::Seleccion);
    CHECK(ed.hasSelection());

    // Ctrl+K b -> B0, modo se reconcilia a Navegacion (sin seleccion)
    previousBuffer(ed);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK(ed.state_ == State::Navegacion);  // sin seleccion = Navegacion
    CHECK_EQ(ed.active().document.lineAt(0), "hello");
    CHECK(!ed.hasSelection());
}

// ---------------------------------------------------------------------------
// Fase E paso 5: la vuelta del modal al editor es rebuild total, por la
// invalidación en la transición (no por invalidar en cada frame modal).
// ---------------------------------------------------------------------------
TEST(editor_modal_exit_rebuilds_full_frame) {
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    newBuffer(ed);  // 2 buffers: el selector abre (con 1 es no-op)
    StringSink sink;
    ed.setSink(sink);
    ed.renderFrame();  // editor: prima el diff
    openSelector(ed);  // Ctrl+K t -> BufferSelector
    CHECK(ed.state_ == State::BufferSelector);
    ed.renderFrame();  // modal (transición de entrada: invalida)
    ed.renderFrame();  // modal (sin invalidación extra)
    press(ed, InputEventType::Escape);  // sale del modal
    sink.buf.clear();
    ed.renderFrame();  // editor: rebuild total por transición de salida
    CHECK(sink.buf.find("\x1b[2J") != std::string::npos);
}

