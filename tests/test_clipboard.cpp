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

static void enterSeleccion(Editor& ed) {
    if (ed.state_ != State::Seleccion) {
        if (ed.state_ == State::Interaccion) {
            ed.handleEvent(escapeEvent());
        }
        ed.handleEvent(insert('s'));
    }
}

// Selecciona los primeros n caracteres de la linea actual (0-based),
// partiendo del Home; deja la seleccion activa con el rango [0, n).
static void selectFirstChars(Editor& ed, int n) {
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    for (int i = 0; i < n; ++i)
        press(ed, EventType::MoveRight);
}

static void copySelection(Editor& ed) {
    ed.handleEvent(insert('c'));
}

// ---------------------------------------------------------------------------
// El clipboard NO participa en Undo/Redo
// ---------------------------------------------------------------------------
// Decision de diseno v0.5/v0.55: clipboard_ es estado de la UI ("lo que
// el usuario tiene en la mano"), no del documento. No se guarda en un
// pushHistory ni se restaura con undo/redo.
// ---------------------------------------------------------------------------

TEST(clipboard_copy_does_not_push_undo) {
    // Copiar con seleccion NO entra al historial: undoStack intacto.
    Editor ed;
    type(ed, "abc");
    selectFirstChars(ed, 2);          // [ab]
    CHECK(ed.hasSelection());
    const size_t undoBefore = ed.undoStack_.size();
    const size_t redoBefore = ed.redoStack_.size();

    copySelection(ed);

    CHECK_EQ(ed.undoStack_.size(), undoBefore); // sin pushHistory
    CHECK_EQ(ed.redoStack_.size(), redoBefore); // tampoco crea redo
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ab"}));
}

TEST(clipboard_copy_does_not_create_redo) {
    // Tras copiar no queda nada pendiente de rehacer.
    Editor ed;
    type(ed, "abc");
    selectFirstChars(ed, 2);
    copySelection(ed);

    CHECK(ed.redoStack_.empty());
    CHECK_EQ(ed.document_.lineAt(0), "abc"); // copiar no edita
}

TEST(clipboard_undo_does_not_erase_buffer) {
    // Hacer edicion, copiar, luego Undo de la edicion: el buffer sigue
    // conteniendo el rango copiado (el undo restaura el DOCUMENTO, no el
    // clipboard).
    Editor ed;
    type(ed, "hola");                 // doc "hola", cursor (0,4)
    press(ed, EventType::Escape);     // -> Navegacion
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);  // [h]
    CHECK(ed.hasSelection());
    copySelection(ed);                // buffer ["h"]
    CHECK(ed.clipboard_ == (std::vector<std::string>{"h"}));

    type(ed, "X");                    // edicion: doc "hXola" (el buffer no cambia)

    press(ed, EventType::Escape);     // -> Navegacion (undo tambien funciona en)
    press(ed, EventType::Undo);       // deshace la edicion
    CHECK_EQ(ed.document_.lineAt(0), "hola");        // doc restaurado
    CHECK(ed.clipboard_ == (std::vector<std::string>{"h"})); // buffer intacto
}

TEST(clipboard_redo_does_not_restore_buffer) {
    // Undo deja el buffer tal cual; Redo tampoco lo toca.
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);  // [h]
    copySelection(ed);                // buffer ["h"]
    CHECK(ed.clipboard_ == (std::vector<std::string>{"h"}));

    type(ed, "X");                    // edicion: doc "Xhola"
    press(ed, EventType::Escape);
    press(ed, EventType::Undo);       // doc "hola", buffer ["h"]
    CHECK(ed.clipboard_ == (std::vector<std::string>{"h"}));

    press(ed, EventType::Redo);       // doc "hXola" otra vez
    CHECK_EQ(ed.document_.lineAt(0), "hXola");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"h"})); // sigue intacto
}

TEST(clipboard_survives_undo_of_edit_after_copy) {
    // Caso CENTRAL: copiar A, hacer una edicion, Undo -> el clipboard
    // sigue conteniendo A. Es lo que un usuario espera: copiar, equivocarse
    // y deshacer, para luego pegar lo copiado.
    Editor ed;
    type(ed, "texto");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);  // [te]
    CHECK(ed.hasSelection());
    copySelection(ed);                // A = "te"
    CHECK(ed.clipboard_ == (std::vector<std::string>{"te"}));

    type(ed, "Z");                    // doc "Ztexto" (una sola edicion)
    press(ed, EventType::Escape);
    press(ed, EventType::Undo);       // deshace la edicion

    CHECK_EQ(ed.document_.lineAt(0), "texto");          // doc restaurado
    CHECK(ed.clipboard_ == (std::vector<std::string>{"te"})); // A intacto
}

TEST(clipboard_after_copy_then_edit_then_full_undo_redo_cycle) {
    // Copiar A -> editar -> Undo -> Redo: el buffer nunca cambia durante
    // todo el ciclo, y el documento recorre su historia normal.
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);  // [h]
    copySelection(ed);                // A = "h"
    CHECK(ed.clipboard_ == (std::vector<std::string>{"h"}));

    type(ed, "AB");                   // doc "hABola" (dos ediciones)
    press(ed, EventType::Escape);
    press(ed, EventType::Undo);       // doc "hAola"
    press(ed, EventType::Undo);       // doc "hola"
    press(ed, EventType::Redo);       // doc "hAola"
    press(ed, EventType::Redo);       // doc "hABola"

    CHECK_EQ(ed.document_.lineAt(0), "hABola");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"h"})); // buffer intacto
}

TEST(clipboard_change_then_undo_redo_keeps_latest_buffer) {
    // CASO CLAVE: copiar A -> pegar A -> copiar B -> Undo -> Redo.
    // El historial restaura SOLO el documento/estado; el clipboard queda
    // siendo B (el ultimo copiado), aunque el Undo viaje a una operacion
    // anterior. Valida que clipboard_ NO esta en HistoryState.
    Editor ed;
    type(ed, "hola");                     // doc "hola"
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);      // [h]
    press(ed, EventType::MoveRight);      // [ho] = A
    copySelection(ed);                    // clipboard A = ["ho"]
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ho"}));
    CHECK_EQ(ed.document_.lineAt(0), "hola");

    ed.handleEvent(insert('p'));          // pega A en (0,2): doc "hohola"
    CHECK_EQ(ed.document_.lineAt(0), "hohola");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ho"}));

    enterSeleccion(ed);                   // nueva seleccion para copiar B
    press(ed, EventType::MoveRight);      // (0,5)
    press(ed, EventType::MoveRight);      // (0,6): [la] = B
    CHECK(ed.hasSelection());
    copySelection(ed);                    // clipboard B = ["la"]
    CHECK(ed.clipboard_ == (std::vector<std::string>{"la"}));

    press(ed, EventType::Undo);           // deshace el pegado de A

    // El documento vuelve al estado previo al pegado...
    CHECK_EQ(ed.document_.lineAt(0), "hola");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);          // donde estaba antes de pegar
    // ...pero el clipboard es B, no A (no participa del historial).
    CHECK(ed.clipboard_ == (std::vector<std::string>{"la"}));

    press(ed, EventType::Redo);           // reaplica el pegado de A

    CHECK_EQ(ed.document_.lineAt(0), "hohola");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 6);          // final del bloque pegado
    // El clipboard sigue siendo B durante todo el ciclo.
    CHECK(ed.clipboard_ == (std::vector<std::string>{"la"}));
}

// ---------------------------------------------------------------------------
// 'x' desde Seleccion: cortar
// ---------------------------------------------------------------------------
// Tras 'x' con seleccion:
//   seleccion -> clipboard; seleccion -> deleteRange(); cursor -> inicio
//   del rango; estado -> Navegacion; modified_ = true; undo empujado.
static void assertCut(Editor& ed,
                      const std::vector<std::string>& expectedDoc,
                      int cursorLine, int cursorCol,
                      const std::vector<std::string>& expectedClipboard) {
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.document_.snapshot() == expectedDoc);      // deleteRange aplicado
    CHECK_EQ(ed.cursor_.line, cursorLine);               // cursor -> inicio
    CHECK_EQ(ed.cursor_.col, cursorCol);
    CHECK(ed.clipboard_ == expectedClipboard);           // seleccion -> clipboard
    CHECK(ed.modified_);
}

// Documento directo (sin pasar por el teclado) para casos multilinea.
static void setupLines(Editor& ed, const std::vector<std::string>& lines) {
    ed.document_.restore(lines);
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;
}

TEST(selection_x_without_selection_noop) {
    // s -> x sin texto marcado: no corta nada, informa, y no toca doc,
    // undo, redo, clipboard ni modified_.
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    ed.modified_ = false;               // simula estado guardado
    ed.savedLines_ = ed.document_.snapshot();
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);                 // s
    CHECK(!ed.hasSelection());
    const auto docBefore = ed.document_.snapshot();
    const size_t undoBefore = ed.undoStack_.size();
    const size_t redoBefore = ed.redoStack_.size();
    ed.clipboard_ = std::vector<std::string>{"previo"};

    ed.handleEvent(insert('x'));

    CHECK_EQ(ed.statusMessage_, "Nada seleccionado.");
    CHECK(ed.document_.snapshot() == docBefore);   // no modifica documento
    CHECK_EQ(ed.undoStack_.size(), undoBefore);    // no crea undo
    CHECK_EQ(ed.redoStack_.size(), redoBefore);    // tampoco toca redo
    CHECK(ed.clipboard_ == std::vector<std::string>{"previo"}); // no altera clipboard
    CHECK(!ed.modified_);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(selection_c_without_selection_noop) {
    // Hermano de selection_x_without_selection_noop: 'c' sin texto marcado
    // tampoco copia nada; no toca doc, undo, redo, clipboard ni modified_.
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    ed.modified_ = false;               // simula estado guardado
    ed.savedLines_ = ed.document_.snapshot();
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);                 // s
    CHECK(!ed.hasSelection());
    const auto docBefore = ed.document_.snapshot();
    const size_t undoBefore = ed.undoStack_.size();
    const size_t redoBefore = ed.redoStack_.size();
    ed.clipboard_ = std::vector<std::string>{"previo"};

    ed.handleEvent(insert('c'));

    CHECK_EQ(ed.statusMessage_, "Nada seleccionado.");
    CHECK(ed.document_.snapshot() == docBefore);   // no modifica documento
    CHECK_EQ(ed.undoStack_.size(), undoBefore);    // no crea undo
    CHECK_EQ(ed.redoStack_.size(), redoBefore);    // tampoco toca redo
    CHECK(ed.clipboard_ == std::vector<std::string>{"previo"}); // no altera clipboard
    CHECK(!ed.modified_);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(selection_p_in_seleccion_is_noop) {
    // Diseno explicito: 'p' NO tiene funcionamiento dentro del modo de
    // seleccion. Con y sin rango marcado, presionar 'p' deja intacta la
    // seleccion, el documento, el buffer y el estado (sigue en Seleccion).
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    ed.modified_ = false;               // simula estado guardado
    ed.savedLines_ = ed.document_.snapshot();
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);                            // s, sin rango
    ed.clipboard_ = std::vector<std::string>{"previo"};
    const auto docBefore = ed.document_.snapshot();
    const size_t undoBefore = ed.undoStack_.size();
    const size_t redoBefore = ed.redoStack_.size();

    ed.handleEvent(insert('p'));                   // p sin rango: no-op

    CHECK(!ed.hasSelection());                     // sin rango sigue sin rango
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.document_.snapshot() == docBefore);
    CHECK(ed.clipboard_ == std::vector<std::string>{"previo"});
    CHECK_EQ(ed.undoStack_.size(), undoBefore);
    CHECK_EQ(ed.redoStack_.size(), redoBefore);
    CHECK(!ed.modified_);
}

TEST(selection_p_on_range_in_seleccion_is_noop) {
    // Hermano con rango real marcado: 'p' tampoco pega ni altera nada.
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    ed.modified_ = false;               // simula estado guardado
    ed.savedLines_ = ed.document_.snapshot();
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);               // [h]
    CHECK(ed.hasSelection());
    ed.clipboard_ = std::vector<std::string>{"previo"};
    const auto docBefore = ed.document_.snapshot();
    const size_t undoBefore = ed.undoStack_.size();
    const size_t redoBefore = ed.redoStack_.size();

    ed.handleEvent(insert('p'));                   // p con rango: no-op

    CHECK(ed.hasSelection());                      // la seleccion se mantiene intacta
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.document_.snapshot() == docBefore);
    CHECK(ed.clipboard_ == std::vector<std::string>{"previo"});
    CHECK_EQ(ed.undoStack_.size(), undoBefore);
    CHECK_EQ(ed.redoStack_.size(), redoBefore);
    CHECK(!ed.modified_);
}

TEST(selection_x_cuts_single_char) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);  // [a]
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('x'));

    assertCut(ed, {"bc"}, 0, 0, {"a"});
}

TEST(selection_x_cuts_word) {
    Editor ed;
    type(ed, "hola mundo");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);   // cursor (0,0)
    enterSeleccion(ed);
    for (int i = 0; i < 4; ++i)
        press(ed, EventType::MoveRight); // [hola]
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('x'));

    assertCut(ed, {" mundo"}, 0, 0, {"hola"});
}

TEST(selection_x_cuts_part_of_line) {
    Editor ed;
    type(ed, "abcdefgh");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);  // (0,1)
    press(ed, EventType::MoveRight);  // (0,2)
    enterSeleccion(ed);               // anchor (0,2)
    press(ed, EventType::MoveRight);  // (0,3)
    press(ed, EventType::MoveRight);  // (0,4): [cd]
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('x'));

    assertCut(ed, {"abefgh"}, 0, 2, {"cd"});
}

TEST(selection_x_cuts_full_line) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);   // (0,0)
    enterSeleccion(ed);
    press(ed, EventType::MoveEnd);    // [abc] entera
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection()->start.col, 0);
    CHECK_EQ(ed.selection()->end.col, 3);

    ed.handleEvent(insert('x'));

    // Corta toda la linea: queda una linea vacia.
    assertCut(ed, {""}, 0, 0, {"abc"});
}

TEST(selection_x_cuts_multiline) {
    Editor ed;
    setupLines(ed, {"aaa", "bbb", "ccc"});
    enterSeleccion(ed);               // anchor (0,0)
    press(ed, EventType::MoveDown);   // (1,0)
    press(ed, EventType::MoveDown);   // (2,0)
    press(ed, EventType::MoveEnd);    // (2,3)
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('x'));

    assertCut(ed, {""}, 0, 0, {"aaa", "bbb", "ccc"});
}

TEST(selection_x_cuts_entire_document) {
    Editor ed;
    setupLines(ed, {"linea1", "linea2", "linea3"});
    enterSeleccion(ed);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveEnd);    // todo el documento
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('x'));

    assertCut(ed, {""}, 0, 0, {"linea1", "linea2", "linea3"});
}

TEST(selection_x_cuts_forward_selection) {
    // Seleccion forward (anchor antes que cursor): el cursor queda al
    // inicio del rango (sel->start), no donde estaba el cursor.
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);   // (0,0) anchor
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);  // (0,1)
    press(ed, EventType::MoveRight);  // (0,2): [ho]
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('x'));

    assertCut(ed, {"la"}, 0, 0, {"ho"});
}

TEST(selection_x_cuts_reverse_selection) {
    // Seleccion reverse (cursor antes que anchor): el cursor va al inicio
    // del rango normalizado (sel->start), es decir la posicion mas pequeña.
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveEnd);    // (0,4) anchor
    enterSeleccion(ed);
    press(ed, EventType::MoveLeft);   // (0,3)
    press(ed, EventType::MoveLeft);   // (0,2): [la]
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK_EQ(sel->start.col, 2);
    CHECK_EQ(sel->end.col, 4);

    ed.handleEvent(insert('x'));

    assertCut(ed, {"ho"}, 0, 2, {"la"});
}

// ---------------------------------------------------------------------------
// 'x' ES una sola operacion de Undo
// ---------------------------------------------------------------------------
// Cortar un rango de N caracteres debe empujar UNA sola entrada al
// historial (no una por caracter). Undo restaura todo el contenido, la
// posicion del cursor y el estado correspondiente (la seleccion vigente);
// Redo vuelve a aplicar el corte exacto.
// ---------------------------------------------------------------------------

TEST(selection_x_cut_is_one_undo_entry) {
    // Cortar 8 caracteres agrega UNA sola entrada, no ocho. undoStack
    // debe crecer exactamente en 1.
    Editor ed;
    type(ed, "abcdefgh");          // 8 chars -> 8 entradas de undo
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    for (int i = 0; i < 8; ++i)
        press(ed, EventType::MoveRight);  // selecciona toda la linea
    CHECK(ed.hasSelection());
    const size_t undoBefore = ed.undoStack_.size();

    ed.handleEvent(insert('x'));

    CHECK_EQ(ed.undoStack_.size(), undoBefore + 1); // una sola entrada
    assertCut(ed, {""}, 0, 0, {"abcdefgh"});
}

TEST(selection_x_undo_restores_everything) {
    // Undo tras un corte restaura todo el contenido, la posicion del
    // cursor y el estado correspondiente (la seleccion que existia).
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);   // (0,0)
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);  // (0,1)
    press(ed, EventType::MoveRight);  // (0,2): [ho]
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection()->start.col, 0);
    CHECK_EQ(ed.selection()->end.col, 2);

    ed.handleEvent(insert('x'));      // corta [ho]: doc "la", cursor (0,0)
    assertCut(ed, {"la"}, 0, 0, {"ho"});

    press(ed, EventType::Undo);

    // Todo el contenido restaurado.
    CHECK_EQ(ed.document_.lineAt(0), "hola");
    CHECK_EQ(ed.document_.lineCount(), 1);
    // Posicion del cursor restaurada (la que tenia al momento del corte:
    // el final de la seleccion).
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);
    // Estado correspondiente: la seleccion que habia vuelve a estar vigente.
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection()->start.col, 0);
    CHECK_EQ(ed.selection()->end.col, 2);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    // El clipboard NO se deshace (decision de diseno v0.5).
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ho"}));
}

TEST(selection_x_redo_reapplies_cut) {
    // Tras Undo, Redo vuelve a aplicar el corte exacto: contenido,
    // cursor al inicio del rango, Navegacion y clipboard intacto.
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);  // [ho]
    ed.handleEvent(insert('x'));      // corta -> "la"

    press(ed, EventType::Undo);       // -> "hola"
    CHECK_EQ(ed.document_.lineAt(0), "hola");

    press(ed, EventType::Redo);       // vuelve a cortar
    assertCut(ed, {"la"}, 0, 0, {"ho"});
    CHECK(ed.redoStack_.empty()); // el redo se consumio
}

TEST(selection_x_multiline_cut_undo_redo_cycle) {
    // Corte multilinea: undo restaura TODAS las lineas, el cursor y el
    // estado; redo reaplica el corte. Todo con una sola entrada de undo.
    Editor ed;
    setupLines(ed, {"aaa", "bbb", "ccc"});
    enterSeleccion(ed);               // anchor (0,0)
    press(ed, EventType::MoveDown);   // (1,0)
    press(ed, EventType::MoveDown);   // (2,0)
    press(ed, EventType::MoveEnd);    // (2,3): todo el documento
    CHECK(ed.hasSelection());
    const size_t undoBefore = ed.undoStack_.size();

    ed.handleEvent(insert('x'));
    assertCut(ed, {""}, 0, 0, {"aaa", "bbb", "ccc"});
    CHECK_EQ(ed.undoStack_.size(), undoBefore + 1); // una entrada, no 3 lineas

    press(ed, EventType::Undo);
    CHECK(ed.document_.snapshot() == (std::vector<std::string>{"aaa", "bbb", "ccc"}));
    CHECK_EQ(ed.cursor_.line, 2);      // final del rango al momento del corte
    CHECK_EQ(ed.cursor_.col, 3);
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));

    press(ed, EventType::Redo);
    // El corte se reaplica: doc {""}, cursor (0,0), Navegacion, sin seleccion.
    CHECK(ed.document_.snapshot() == (std::vector<std::string>{""}));
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.clipboard_ == (std::vector<std::string>{"aaa", "bbb", "ccc"}));
    // modified_ recoge que el contenido restaurado ({""}) coincide con el
    // ultimo guardado (savedLines_ inicial = doc vacio), asi que es false.
    // Es el comportamiento correcto: modified_ se deriva de la comparacion.
    CHECK(!ed.modified_);
    CHECK(ed.redoStack_.empty());
}

// ---------------------------------------------------------------------------
// Copiar -> seleccionar otra cosa -> ESC -> pegar
// ---------------------------------------------------------------------------
// Seleccionar texto nuevo NO reemplaza el clipboard: el clipboard solo
// cambia con 'c' (copiar) o 'x' (cortar). Tras copiar AB, seleccionar DE
// y salir con ESC, pegar debe insertar AB (no DE).
// ---------------------------------------------------------------------------
TEST(clipboard_copy_then_select_other_esc_then_paste) {
    Editor ed;
    type(ed, "ABCDEF");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);   // (0,0)
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);  // (0,1)
    press(ed, EventType::MoveRight);  // (0,2): [AB]
    copySelection(ed);                // clipboard = ["AB"]
    CHECK(ed.clipboard_ == (std::vector<std::string>{"AB"}));

    enterSeleccion(ed);               // nueva seleccion
    press(ed, EventType::MoveRight);  // (0,3)
    press(ed, EventType::MoveRight);  // (0,4): [DE]
    CHECK(ed.hasSelection());

    ed.handleEvent(escapeEvent());    // ESC sale de seleccion -> Navegacion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(!ed.hasSelection());

    // Seleccionar DE NO toco el clipboard: sigue siendo AB.
    CHECK(ed.clipboard_ == (std::vector<std::string>{"AB"}));

    ed.handleEvent(insert('p'));      // pega AB en (0,4)

    CHECK_EQ(ed.document_.lineAt(0), "ABCDABEF");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 6);      // final del bloque pegado (4+2)
    // El clipboard se conserva: lo que se pega es AB, no DE.
    CHECK(ed.clipboard_ == (std::vector<std::string>{"AB"}));
}

// ---------------------------------------------------------------------------
// Cortar -> copiar otra cosa
// ---------------------------------------------------------------------------
// 'x' corta y 'c' copia; ambos sobrescriben el clipboard con el rango en
// curso. Tras 'x' sobre AB (doc queda "CDEF") y luego 'c' sobre CD, el
// clipboard termina siendo CD y el documento refleja SOLO el corte de AB
// (copiar no muta el documento ni el historial).
// ---------------------------------------------------------------------------
TEST(clipboard_cut_then_copy_other_replaces_buffer) {
    Editor ed;
    type(ed, "ABCDEF");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);   // (0,0)
    enterSeleccion(ed);
    press(ed, EventType::MoveRight);  // (0,1)
    press(ed, EventType::MoveRight);  // (0,2): [AB]
    ed.handleEvent(insert('x'));      // corta AB

    CHECK_EQ(ed.document_.lineAt(0), "CDEF");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"AB"}));
    const size_t undoAfterCut = ed.undoStack_.size();

    enterSeleccion(ed);               // nueva seleccion
    press(ed, EventType::MoveRight);  // (0,1)
    press(ed, EventType::MoveRight);  // (0,2): [CD]
    CHECK(ed.hasSelection());
    copySelection(ed);                // copia CD -> sobreescribe el buffer

    // El clipboard termina conteniendo CD.
    CHECK(ed.clipboard_ == (std::vector<std::string>{"CD"}));
    // El documento refleja solamente el corte de AB (copiar no edita).
    CHECK_EQ(ed.document_.lineAt(0), "CDEF");
    // Copiar no empuja historial: undo intacto tras el corte.
    CHECK_EQ(ed.undoStack_.size(), undoAfterCut);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

// ---------------------------------------------------------------------------
// Test de INTEGRACION CENTRAL de v0.55: cortar -> pegar -> Undo/Redo
// ---------------------------------------------------------------------------
// Flujo completo:
//   "ABC DEF" -> seleccionar DEF -> x (corta) -> p (pega)
//   Undo deshace SOLO el pegado; otro Undo deshace el corte; Redo+Redo
//   reconstruye todo. El clipboard permanece {"DEF"} durante todo el ciclo.
// Nota: tras el corte queda "ABC " (el espacio sobrevive); el diagrama
// conceptual omitia el espacio.
// ---------------------------------------------------------------------------
TEST(clipboard_cut_paste_full_undo_redo_cycle) {
    Editor ed;
    type(ed, "ABC DEF");                  // doc "ABC DEF"
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);       // (0,0)
    press(ed, EventType::MoveRight);      // (0,1)
    press(ed, EventType::MoveRight);      // (0,2)
    press(ed, EventType::MoveRight);      // (0,3)
    press(ed, EventType::MoveRight);      // (0,4)
    enterSeleccion(ed);                   // anchor (0,4)
    press(ed, EventType::MoveRight);      // (0,5)
    press(ed, EventType::MoveRight);      // (0,6)
    press(ed, EventType::MoveRight);      // (0,7): selecciona "DEF"
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('x'));          // corta "DEF" -> doc "ABC ", buffer ["DEF"]

    CHECK_EQ(ed.document_.lineAt(0), "ABC ");   // el espacio sobrevive
    CHECK(ed.clipboard_ == (std::vector<std::string>{"DEF"}));
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 4);          // inicio del rango
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    ed.handleEvent(insert('p'));          // pega en (0,4): doc "ABC DEF"

    CHECK_EQ(ed.document_.lineAt(0), "ABC DEF");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 7);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"DEF"}));

    press(ed, EventType::Undo);           // deshace SOLO el pegado

    CHECK_EQ(ed.document_.lineAt(0), "ABC ");   // vuelve al estado post-corte
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 4);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"DEF"})); // buffer intacto

    press(ed, EventType::Undo);           // deshace el CORTE

    CHECK_EQ(ed.document_.lineAt(0), "ABC DEF"); // doc original restaurado
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 7);          // donde estaba al cortar (final del rango)
    CHECK(ed.clipboard_ == (std::vector<std::string>{"DEF"})); // sigue intacto

    press(ed, EventType::Redo);           // reaplica el corte

    CHECK_EQ(ed.document_.lineAt(0), "ABC ");   // cortado otra vez
    CHECK(ed.clipboard_ == (std::vector<std::string>{"DEF"}));

    press(ed, EventType::Redo);           // reaplica el pegado

    CHECK_EQ(ed.document_.lineAt(0), "ABC DEF"); // reconstruido completo
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 7);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"DEF"}));
    CHECK(ed.redoStack_.empty());
}

// Otra variante con bloque multilinea (2 lineas) cortado y pegado.
TEST(clipboard_cut_paste_multiline_undo_redo_cycle) {
    Editor ed;
    setupLines(ed, {"aaaa", "bbbb", "cccc"});
    enterSeleccion(ed);                   // anchor (0,0)
    press(ed, EventType::MoveDown);       // (1,0)
    press(ed, EventType::MoveEnd);        // (1,4): selecciona lineas 0-1
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('x'));          // corta -> doc {"", "cccc"}

    // deleteRange multilinea deja una linea vacia al inicio (la cola vacia
    // de la ultima linea del rango): {"", "cccc"}.
    CHECK(ed.document_.snapshot() == (std::vector<std::string>{"", "cccc"}));
    CHECK(ed.clipboard_ == (std::vector<std::string>{"aaaa", "bbbb"}));

    press(ed, EventType::MoveHome);       // (0,0)
    ed.handleEvent(insert('p'));          // pega -> {"aaaa","bbbb","cccc"}

    CHECK(ed.document_.snapshot() == (std::vector<std::string>{"aaaa", "bbbb", "cccc"}));
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 4);

    press(ed, EventType::Undo);           // deshace solo el pegado

    CHECK(ed.document_.snapshot() == (std::vector<std::string>{"", "cccc"}));
    CHECK(ed.clipboard_ == (std::vector<std::string>{"aaaa", "bbbb"}));

    press(ed, EventType::Undo);           // deshace el corte

    CHECK(ed.document_.snapshot() == (std::vector<std::string>{"aaaa", "bbbb", "cccc"}));
    CHECK(ed.clipboard_ == (std::vector<std::string>{"aaaa", "bbbb"}));

    press(ed, EventType::Redo);           // reaplica el corte
    press(ed, EventType::Redo);           // reaplica el pegado

    CHECK(ed.document_.snapshot() == (std::vector<std::string>{"aaaa", "bbbb", "cccc"}));
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 4);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"aaaa", "bbbb"}));
    CHECK(ed.redoStack_.empty());
}

// ---------------------------------------------------------------------------
// UTF-8 + clipboard: suite propia
// ---------------------------------------------------------------------------
// Copiar/pegar texto UTF-8 multibyte debe:
//   - no cortar ningun caracter (el rango respeta los limites de byte);
//   - extraer los bytes correctos (extractRange);
//   - reinsertarlos intactos (insertBlock via pegado);
//   - dejar el cursor en la posicion correcta (byte-addressable).
// ---------------------------------------------------------------------------

// Copia la linea completa actual (Home -> s -> End -> c) y deja el editor
// en Navegacion con la seleccion completada.
static void copyWholeLine(Editor& ed) {
    press(ed, EventType::MoveHome);
    enterSeleccion(ed);
    press(ed, EventType::MoveEnd);
    copySelection(ed);
}

TEST(clipboard_utf8_copy_paste_cafe) {
    // "café" = 5 bytes (c,a,f + é de 2 bytes). Copiar la linea completa y
    // pegarla al final debe dar "cafécafé" sin partir el "é".
    Editor ed;
    ed.document_.restore({std::string("caf\xC3\xA9")});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    copyWholeLine(ed);

    CHECK(ed.clipboard_ == (std::vector<std::string>{std::string("caf\xC3\xA9")}));
    CHECK_EQ(ed.document_.lineAt(0), std::string("caf\xC3\xA9"));

    press(ed, EventType::MoveEnd);        // cursor al final (byte 5)
    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.document_.lineAt(0), std::string("caf\xC3\xA9") + "caf\xC3\xA9");
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 10);         // 5 + 5 bytes
    CHECK(ed.clipboard_ == (std::vector<std::string>{std::string("caf\xC3\xA9")}));
}

TEST(clipboard_utf8_copy_paste_em_dash) {
    // "—" (em dash) = 3 bytes (0xE2 0x80 0x94).
    Editor ed;
    ed.document_.restore({std::string("\xE2\x80\x94")});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    copyWholeLine(ed);

    CHECK(ed.clipboard_ == (std::vector<std::string>{std::string("\xE2\x80\x94")}));

    press(ed, EventType::MoveEnd);        // cursor al final (byte 3)
    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.document_.lineAt(0), std::string("\xE2\x80\x94\xE2\x80\x94"));
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 6);          // 3 + 3 bytes
}

TEST(clipboard_utf8_copy_paste_emoji) {
    // "😀" = 4 bytes (0xF0 0x9F 0x98 0x80).
    Editor ed;
    ed.document_.restore({std::string("\xF0\x9F\x98\x80")});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    copyWholeLine(ed);

    CHECK(ed.clipboard_ == (std::vector<std::string>{std::string("\xF0\x9F\x98\x80")}));

    press(ed, EventType::MoveEnd);        // cursor al final (byte 4)
    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.document_.lineAt(0), std::string("\xF0\x9F\x98\x80\xF0\x9F\x98\x80"));
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 8);          // 4 + 4 bytes
}

TEST(clipboard_utf8_copy_paste_mixed) {
    // "café — 😀" = c,a,f(3) + é(2) + " "(1) + —(3) + " "(1) + 😀(4) = 14
    // bytes. Ningun caracter se corta: el clipboard y el pegado son byte a
    // byte identicos y el cursor termina tras el bloque completo.
    const std::string mixto = std::string("caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x98\x80");
    Editor ed;
    ed.document_.restore({mixto});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    copyWholeLine(ed);

    CHECK(ed.clipboard_ == (std::vector<std::string>{mixto}));
    CHECK_EQ(ed.document_.lineAt(0), mixto);

    press(ed, EventType::MoveEnd);        // cursor al final (byte 14)
    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.document_.lineAt(0), mixto + mixto);
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 28);         // 14 + 14 bytes
    CHECK(ed.clipboard_ == (std::vector<std::string>{mixto}));
}

TEST(clipboard_utf8_cut_paste_partial_range) {
    // Copiar SOLO "é " (bytes 3..6 de "café — 😀"): el rango no debe caer
    // dentro de un caracter multibyte y el bloque debe salir entero.
    // El ancla se coloca en (0,3) moviendo antes de entrar en seleccion.
    const std::string mixto = std::string("caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x98\x80");
    Editor ed;
    ed.document_.restore({mixto});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    press(ed, EventType::MoveHome);
    // Mover el cursor a (0,3) tras "caf" SIN seleccionar todavia.
    press(ed, EventType::MoveRight);      // (0,1)
    press(ed, EventType::MoveRight);      // (0,2)
    press(ed, EventType::MoveRight);      // (0,3)
    enterSeleccion(ed);                   // ancla en (0,3)
    // Un MoveRight cruza "é" (2 bytes) hasta (0,5); otro cruza el espacio.
    press(ed, EventType::MoveRight);      // (0,5) tras "é"
    press(ed, EventType::MoveRight);      // (0,6) tras el espacio
    CHECK(ed.hasSelection());
    copySelection(ed);

    // El bloque es "é " (bytes 3..6).
    CHECK(ed.clipboard_ == (std::vector<std::string>{std::string("\xC3\xA9 ")}));

    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.document_.lineAt(0), std::string("\xC3\xA9 ") + mixto);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);          // 0 + largo("é ") = 3 bytes
}

// ---------------------------------------------------------------------------
// UTF-8 + cortar: regresion de deleteCharBefore/deleteCharAt
// ---------------------------------------------------------------------------
// Cortar individualmente un caracter multibyte y deshacerlo con Undo debe
// restaurar el caracter COMPLETO (todos sus bytes), sin dejar bytes de
// continuacion sueltos. Sirve de regresion del bug de borrado parcial.
// ---------------------------------------------------------------------------

// Corta el primer caracter de la linea (Home -> s -> Right -> x).
static void cutFirstChar(Editor& ed) {
    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveRight);
    ed.handleEvent(insert('x'));
}

TEST(clipboard_utf8_cut_undo_cafe) {
    // "é" = 2 bytes. Cortar deja la linea vacia; Undo la restaura completa.
    Editor ed;
    ed.document_.restore({std::string("\xC3\xA9")});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    cutFirstChar(ed);

    CHECK(ed.clipboard_ == (std::vector<std::string>{std::string("\xC3\xA9")}));
    CHECK(ed.document_.lineAt(0).empty());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK(ed.state_ == State::Navegacion);

    press(ed, EventType::Undo);

    CHECK_EQ(ed.document_.lineAt(0), std::string("\xC3\xA9"));
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);          // final de la seleccion restaurada
    CHECK(ed.state_ == State::Seleccion);
}

TEST(clipboard_utf8_cut_undo_em_dash) {
    // "—" = 3 bytes.
    Editor ed;
    ed.document_.restore({std::string("\xE2\x80\x94")});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    cutFirstChar(ed);

    CHECK(ed.clipboard_ == (std::vector<std::string>{std::string("\xE2\x80\x94")}));
    CHECK(ed.document_.lineAt(0).empty());

    press(ed, EventType::Undo);

    CHECK_EQ(ed.document_.lineAt(0), std::string("\xE2\x80\x94"));
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);          // final de la seleccion restaurada
    CHECK(ed.state_ == State::Seleccion);
}

TEST(clipboard_utf8_cut_undo_emoji) {
    // "😀" = 4 bytes.
    Editor ed;
    ed.document_.restore({std::string("\xF0\x9F\x98\x80")});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    cutFirstChar(ed);

    CHECK(ed.clipboard_ == (std::vector<std::string>{std::string("\xF0\x9F\x98\x80")}));
    CHECK(ed.document_.lineAt(0).empty());

    press(ed, EventType::Undo);

    CHECK_EQ(ed.document_.lineAt(0), std::string("\xF0\x9F\x98\x80"));
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 4);          // final de la seleccion restaurada
    CHECK(ed.state_ == State::Seleccion);
}

TEST(clipboard_utf8_cut_undo_mixed) {
    // "aé—😀" = a(1) + é(2) + —(3) + 😀(4) = 10 bytes. Cortar el primer
    // caracter (a) y Undo: el resto no se toca y el 'a' vuelve entero.
    const std::string mixto = std::string("a\xC3\xA9\xE2\x80\x94\xF0\x9F\x98\x80");
    Editor ed;
    ed.document_.restore({mixto});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    cutFirstChar(ed);

    CHECK(ed.clipboard_ == (std::vector<std::string>{std::string("a")}));
    CHECK_EQ(ed.document_.lineAt(0), std::string("\xC3\xA9\xE2\x80\x94\xF0\x9F\x98\x80"));
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);

    press(ed, EventType::Undo);

    CHECK_EQ(ed.document_.lineAt(0), mixto);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 1);          // final de la seleccion restaurada
    CHECK(ed.state_ == State::Seleccion);
}

// ---------------------------------------------------------------------------
// Multilinea + clipboard: suite propia
// ---------------------------------------------------------------------------
// Copiar un bloque de 3 lineas UTF-8 ("línea 1/2/3", 8 bytes cada una) y
// pegarlo en distintas posiciones: inicio, medio, final y dentro de otra
// linea. insertBlock() multilinea parte la linea objetivo en col, pega la
// primera linea del bloque tras la cola izquierda, y funde la ultima con la
// cola derecha. El cursor termina al final de la ultima linea del bloque.
// ---------------------------------------------------------------------------
static const std::string& utf8Block0() {
    static const std::string s = std::string("l\xC3\xADnea 1");
    return s;
}
static const std::string& utf8Block1() {
    static const std::string s = std::string("l\xC3\xADnea 2");
    return s;
}
static const std::string& utf8Block2() {
    static const std::string s = std::string("l\xC3\xADnea 3");
    return s;
}
static const std::vector<std::string>& utf8Block() {
    static const std::vector<std::string> v = {utf8Block0(), utf8Block1(), utf8Block2()};
    return v;
}

// Copia el bloque de 3 lineas completo (Home -> s -> End -> Down x2 -> c)
// desde un documento que CONTIENE el bloque UTF-8. Deja el clipboard con el
// bloque y el cursor al final de la ultima linea (2, 8) en Navegacion.
static void copyThreeLineUtf8Block(Editor& ed) {
    ed.document_.restore(utf8Block());
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;
    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveDown);
    copySelection(ed);
}

TEST(clipboard_multiline_utf8_paste_at_start) {
    // Bloque de 3 lineas en un doc de 3 lineas "AAAA/BBBB/CCCC"; pegar en
    // (0,0) debe partir la primera linea en col 0: izquierda vacia, la
    // primera linea del bloque la reemplaza y la ultima se funde con "AAAA".
    Editor ed;
    copyThreeLineUtf8Block(ed);
    CHECK(ed.clipboard_ == utf8Block());

    ed.document_.restore({"AAAA", "BBBB", "CCCC"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    press(ed, EventType::MoveHome);         // (0,0) real (col solo)
    press(ed, EventType::MoveUp);           // no debe salir de linea 0
    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.document_.lineCount(), 5);
    CHECK(ed.document_.snapshot() == (std::vector<std::string>{
        utf8Block0(), utf8Block1(), utf8Block2() + "AAAA", "BBBB", "CCCC"}));
    CHECK_EQ(ed.cursor_.line, 2);
    CHECK_EQ(ed.cursor_.col, 8);            // final de "línea 3"
}

TEST(clipboard_multiline_utf8_paste_in_middle) {
    // Doc de 4 lineas "aaaa/bbbb/cccc/dddd"; pegar al inicio de la linea 2
    // parte esa linea en col 0: "línea 3" se funde con "cccc" y "dddd" queda
    // despues del bloque.
    Editor ed;
    copyThreeLineUtf8Block(ed);
    ed.document_.restore({"aaaa", "bbbb", "cccc", "dddd"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveDown);         // (2,0)
    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.document_.lineCount(), 6);
    CHECK(ed.document_.snapshot() == (std::vector<std::string>{
        "aaaa", "bbbb", utf8Block0(), utf8Block1(), utf8Block2() + "cccc", "dddd"}));
    CHECK_EQ(ed.cursor_.line, 4);
    CHECK_EQ(ed.cursor_.col, 8);
}

TEST(clipboard_multiline_utf8_paste_at_end) {
    // Doc de 3 lineas "aaaa/bbbb/cccc"; pegar al final de la ultima linea
    // (col 4): la cola derecha esta vacia, asi que "línea 3" no se fusiona
    // con nada y el bloque simplemente se cierra.
    Editor ed;
    copyThreeLineUtf8Block(ed);
    ed.document_.restore({"aaaa", "bbbb", "cccc"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveEnd);          // (2,4)
    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.document_.lineCount(), 5);
    CHECK(ed.document_.snapshot() == (std::vector<std::string>{
        "aaaa", "bbbb", "cccc" + utf8Block0(), utf8Block1(), utf8Block2()}));
    CHECK_EQ(ed.cursor_.line, 4);
    CHECK_EQ(ed.cursor_.col, 8);
}

TEST(clipboard_multiline_utf8_paste_inside_line) {
    // Pegar dentro de una linea ("wxyz" en col 2): parte "wxyz" en "wx"|"yz",
    // inserta el bloque y funde la ultima linea con "yz".
    Editor ed;
    copyThreeLineUtf8Block(ed);
    ed.document_.restore({"wxyz"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);        // (0,2)
    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.document_.lineCount(), 3);
    CHECK(ed.document_.snapshot() == (std::vector<std::string>{
        "wx" + utf8Block0(), utf8Block1(), utf8Block2() + "yz"}));
    CHECK_EQ(ed.cursor_.line, 2);
    CHECK_EQ(ed.cursor_.col, 8);
}

// ---------------------------------------------------------------------------
// Seleccion completa + cortar
// ---------------------------------------------------------------------------
// Seleccionar TODO el documento (Home -> s -> End -> Down x2 -> End) y
// cortar con x debe dejar un documento vacio VALIDO: 1 linea vacia, cursor
// en (0,0) y el clipboard con el contenido original. Un p posterior debe
// reconstruir el contenido completo.
// ---------------------------------------------------------------------------

TEST(clipboard_select_all_cut_paste_restores_document) {
    Editor ed;
    ed.document_.restore({"hola", "mundo", "cafe"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    // Seleccionar todo el documento: Home, s, End, Down x2, End.
    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveEnd);
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('x'));

    // Documento vacio valido: 1 linea vacia.
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK(ed.document_.lineAt(0).empty());
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"hola", "mundo", "cafe"}));
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.undoStack_.size(), size_t{1});

    // Pegar reconstruye el contenido.
    ed.handleEvent(insert('p'));

    CHECK(ed.document_.snapshot() == (std::vector<std::string>{"hola", "mundo", "cafe"}));
    CHECK_EQ(ed.cursor_.line, 2);
    CHECK_EQ(ed.cursor_.col, 4);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"hola", "mundo", "cafe"}));
}

// ---------------------------------------------------------------------------
// Seleccion completa + copiar + pegar
// ---------------------------------------------------------------------------
// Copiar TODO el documento (extractRange sobre el rango completo) NO debe
// modificar nada: ni el documento, ni la modificacion, ni el historial.
// Pegar luego en otra posicion debe reconstruir el bloque completo
// (regresion de extractRange sin efectos secundarios).
// ---------------------------------------------------------------------------

TEST(clipboard_select_all_copy_is_side_effect_free) {
    Editor ed;
    ed.document_.restore({"hola", "mundo", "cafe"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    // Seleccionar todo el documento: Home, s, End, Down x2, End.
    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveEnd);
    CHECK(ed.hasSelection());

    ed.handleEvent(insert('c'));

    // Copiar no debe tocar nada del documento ni del historial.
    CHECK(ed.clipboard_ == (std::vector<std::string>{"hola", "mundo", "cafe"}));
    CHECK(ed.document_.snapshot() == (std::vector<std::string>{"hola", "mundo", "cafe"}));
    CHECK_EQ(ed.document_.lineCount(), 3);
    CHECK(ed.undoStack_.empty());
    CHECK(ed.redoStack_.empty());
    CHECK_EQ(ed.modified_, false);

    // Pegar en otra posicion: inicio de la linea 2 (2,0). insertBlock
    // multilinea parte "cafe" en col 0 y funde la ultima linea del bloque
    // con la cola derecha -> "cafecafe".
    press(ed, EventType::MoveEnd);        // (2,4)
    press(ed, EventType::MoveHome);       // (2,0)
    ed.handleEvent(insert('p'));

    CHECK_EQ(ed.document_.lineCount(), 5);
    CHECK(ed.document_.snapshot() == (std::vector<std::string>{
        "hola", "mundo", "hola", "mundo", "cafecafe"}));
    CHECK_EQ(ed.cursor_.line, 4);
    CHECK_EQ(ed.cursor_.col, 4);          // final de "cafecafe"
    CHECK(ed.clipboard_ == (std::vector<std::string>{"hola", "mundo", "cafe"}));
    CHECK_EQ(ed.undoStack_.size(), size_t{1});
}

// ---------------------------------------------------------------------------
// modified_ en todas las operaciones
// ---------------------------------------------------------------------------
// Matriz explicita de como cada operacion afecta a modified_:
//   s / movimiento / ESC / c        -> NO cambia
//   x / p con contenido / p en Inter -> SI (true)
//   p vacio / c sin sel / x sin sel -> NO
//   Undo despues de x o p           -> depende del estado guardado (se
//                                      re-deriva comparando con savedLines_)
// El caso central: guardar -> copiar -> pegar -> undo debe volver a
// modified_ == false.
// ---------------------------------------------------------------------------

// Simula un guardado: marca el contenido actual como el estado persistido.
static void markSaved(Editor& ed) {
    ed.savedLines_ = ed.document_.snapshot();
    ed.modified_ = false;
}

// Guarda en disco de verdad (Ctrl+K, Ctrl+S), como en test_editor.cpp.
static void saveForReal(Editor& ed) {
    press(ed, EventType::Prefix);
    Event e;
    e.type = EventType::Save;
    ed.handleEvent(e);
}

TEST(modified_matrix_non_mutating_ops) {
    // s / movimiento / ESC / c / p vacio / c sin sel / x sin sel: no tocan.
    Editor ed;
    ed.document_.restore({"hola"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;
    markSaved(ed);
    CHECK(!ed.modified_);

    ed.handleEvent(insert('s'));            // s: entra a seleccion
    CHECK(!ed.modified_);

    press(ed, EventType::MoveRight);        // movimiento
    CHECK(!ed.modified_);

    press(ed, EventType::Escape);           // ESC cancela
    CHECK(!ed.modified_);

    ed.handleEvent(insert('c'));            // c fuera de modo Seleccion: no-op
    CHECK(!ed.modified_);

    ed.handleEvent(insert('x'));            // x fuera de modo Seleccion: no-op
    CHECK(!ed.modified_);

    ed.handleEvent(insert('p'));            // p con clipboard vacio: no-op
    CHECK(!ed.modified_);
}

TEST(modified_matrix_copy_keeps_clean) {
    // Copiar una seleccion real NO modifica (extractRange es read-only).
    Editor ed;
    ed.document_.restore({"hola"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;
    markSaved(ed);

    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insert('c'));

    CHECK(ed.clipboard_ == (std::vector<std::string>{"hola"}));
    CHECK(!ed.modified_);
    CHECK(ed.undoStack_.empty());
}

TEST(modified_matrix_cut_marks_modified) {
    Editor ed;
    ed.document_.restore({"hola"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;
    markSaved(ed);

    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insert('x'));

    CHECK(ed.modified_);
    CHECK(ed.document_.lineAt(0).empty());
}

TEST(modified_matrix_paste_content_marks_modified) {
    Editor ed;
    ed.document_.restore({"hola"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;
    markSaved(ed);

    ed.handleEvent(insert('c'));            // no-op
    ed.handleEvent(insert('p'));            // vacio: no-op

    CHECK(!ed.modified_);

    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insert('c'));            // copia "hola"
    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('p'));            // pega con contenido

    CHECK(ed.modified_);
}

TEST(modified_matrix_paste_in_interaccion_marks_modified) {
    // 'p' en Interaccion es texto literal, no comando de pegado.
    Editor ed;
    ed.document_.restore({"hola"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;
    markSaved(ed);

    ed.handleEvent(insert('i'));
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insert('p'));

    CHECK(ed.modified_);
    CHECK_EQ(ed.document_.lineAt(0), "holap");
}

TEST(modified_undo_after_cut_returns_to_saved) {
    // Undo de un corte restaura el documento guardado -> modified_ false.
    Editor ed;
    ed.document_.restore({"hola"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;
    markSaved(ed);

    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insert('x'));

    CHECK(ed.modified_);

    press(ed, EventType::Undo);

    CHECK(!ed.modified_);
    CHECK(ed.document_.snapshot() == (std::vector<std::string>{"hola"}));
}

TEST(modified_undo_after_paste_returns_to_saved) {
    // Undo de un pegado restaura el documento guardado -> modified_ false.
    Editor ed;
    ed.document_.restore({"hola"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;
    markSaved(ed);

    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insert('c'));
    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('p'));

    CHECK(ed.modified_);

    press(ed, EventType::Undo);

    CHECK(!ed.modified_);
    CHECK(ed.document_.snapshot() == (std::vector<std::string>{"hola"}));
}

TEST(modified_save_copy_paste_undo_returns_clean) {
    // guardar -> copiar -> pegar -> undo: debe volver a modified_ == false.
    TempFile f;
    f.write("hola");
    Editor ed;
    CHECK(ed.openFile(f.path));
    CHECK(!ed.modified_);

    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insert('c'));
    CHECK(ed.clipboard_ == (std::vector<std::string>{"hola"}));
    CHECK(!ed.modified_);

    saveForReal(ed);                        // guardar -> modified_ false
    CHECK(!ed.modified_);

    press(ed, EventType::MoveHome);
    ed.handleEvent(insert('p'));            // pegar "hola" -> modified_ true

    CHECK(ed.modified_);
    CHECK_EQ(ed.document_.lineAt(0), "holahola");

    press(ed, EventType::Undo);             // deshacer pegado -> vuelve al guardado

    CHECK(!ed.modified_);
    CHECK_EQ(ed.document_.lineAt(0), "hola");
}

// ---------------------------------------------------------------------------
// Undo/Redo NO debe restaurar el clipboard
// ---------------------------------------------------------------------------
// Prueba obligatoria: copiar A, hacer una edicion B, copiar C, Undo y Redo.
// El clipboard debe permanecer C en todos los casos (es estado de la UI,
// no parte del historial: HistoryState no lo incluye).
// ---------------------------------------------------------------------------

TEST(undo_redo_never_restores_clipboard) {
    Editor ed;
    ed.document_.restore({"abcd"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;

    // Copiar A = "ab" (seleccion de las 2 primeras letras).
    selectFirstChars(ed, 2);
    copySelection(ed);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ab"}));

    // Edicion B: escribir "XYZ" al final (Interaccion, 3 entradas undo).
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insert('i'));
    ed.handleEvent(insert('X'));
    ed.handleEvent(insert('Y'));
    ed.handleEvent(insert('Z'));
    press(ed, EventType::Escape);
    CHECK_EQ(ed.document_.lineAt(0), "abcdXYZ");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ab"}));   // sigue A

    // Copiar C = "cd" (seleccion de las ultimas 2 letras, posicion 2..4).
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);        // (0,2)
    ed.handleEvent(insert('s'));
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);        // (0,4): rango [2,4) = "cd"
    copySelection(ed);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"cd"}));   // ahora C

    // Undo: deshace la 'Z'. El clipboard NO debe volver a A.
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "abcdXY");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"cd"}));

    // Redo: reaplica la 'Z'. El clipboard sigue en C.
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "abcdXYZ");
    CHECK(ed.clipboard_ == (std::vector<std::string>{"cd"}));

    // Un ciclo mas de Undo/Redo sobre otra entrada: sigue C.
    press(ed, EventType::Undo);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"cd"}));
    press(ed, EventType::Redo);
    CHECK(ed.clipboard_ == (std::vector<std::string>{"cd"}));
}
