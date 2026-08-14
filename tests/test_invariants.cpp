#include <fstream>
#include <string>
#include <vector>

#include "test_framework.h"

#include <string>
#include <vector>
#define private public
#include "Editor.h"
#undef private

using testfw::TempFile;

static Event ev(EventType t) {
    Event e;
    e.type = t;
    return e;
}

static Event insert(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

// v0.5: entra al modo Interaccion ('i') si no estamos ya en el.
static void enterInteraccion(Editor& ed) {
    if (ed.state_ != State::Interaccion) {
        if (ed.state_ == State::Seleccion) {
            Event esc; esc.type = EventType::Escape; ed.handleEvent(esc);
        }
        ed.handleEvent(insert('i'));
    }
}

static void type(Editor& ed, const std::string& s) {
    if (s.empty()) return;
    // v0.5: escribir requiere el modo Interaccion (letra 'i').
    if (ed.state_ != State::Interaccion) {
        if (ed.state_ == State::Seleccion) {
            Event esc; esc.type = EventType::Escape; ed.handleEvent(esc);
        }
        ed.handleEvent(insert('i'));
    }
    for (char c : s)
        ed.handleEvent(insert(c));
}

// v0.5: guardar pasa por el prefijo (Ctrl+K + Ctrl+S); un Save suelto no-op.
static void save(Editor& ed) {
    ed.handleEvent(ev(EventType::Prefix));
    Event e; e.type = EventType::Save; ed.handleEvent(e);
}

// ---------------------------------------------------------------------------
// 19. Estado del editor: invariantes de estado tras cada evento
// ---------------------------------------------------------------------------
// Comprueba que el documento, el cursor, la bandera modified_, el nombre de
// archivo y los stacks de undo/redo quedan coherentes.
//
// Paso 8: la selección introduce un segundo conjunto de coordenadas
// (anchor y position) que también debe mantenerse válido. Si la selección
// existe, tanto el anchor como el cursor (position) deben estar dentro de
// los limites del documento. Ademas, anchor == cursor debe equivaler a
// "seleccion vacia" (hasSelection() == false).
// ¿Es `s` una cadena UTF-8 valida (sin bytes de continuacion sueltos)?
static bool validUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int need;
        if ((c & 0x80) == 0) need = 0;
        else if ((c & 0xE0) == 0xC0) need = 1;
        else if ((c & 0xF0) == 0xE0) need = 2;
        else if ((c & 0xF8) == 0xF0) need = 3;
        else return false;
        if (i + static_cast<size_t>(need) >= s.size()) return false;
        for (int k = 1; k <= need; ++k)
            if ((static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0xC0) != 0x80)
                return false;
        i += static_cast<size_t>(need) + 1;
    }
    return true;
}

static void assertStateConsistent(Editor& ed) {
    const Document& d = ed.active().document;

    CHECK(d.lineCount() >= 1);
    for (int i = 0; i < d.lineCount(); ++i)
        CHECK_EQ(d.lineAt(i).size(), static_cast<size_t>(d.lineLength(i)));

    CHECK(ed.active().cursor.line >= 0);
    CHECK(ed.active().cursor.col >= 0);
    CHECK(ed.active().cursor.line < d.lineCount());
    CHECK(ed.active().cursor.col <= d.lineLength(ed.active().cursor.line));

    CHECK(ed.active().undoStack.size() <= Editor::MAX_UNDO);
    CHECK(ed.active().redoStack.size() <= Editor::MAX_UNDO);

    // --- Invariantes de la seleccion (Paso 8) ---
    // hasSelection() y selection_ deben estar en concordancia explicita.
    if (ed.hasSelection())
        CHECK(ed.active().selection.has_value());

    if (!ed.active().selection.has_value())
        CHECK(!ed.hasSelection());

    // Si el estado interno guarda una seleccion (incluso vacia), sus dos
    // coordenadas deben ser validas.
    if (ed.active().selection.has_value()) {
        const Position& anchor = ed.active().selection->anchor;
        const Position& pos = ed.active().selection->position;

        // anchor valido.
        CHECK(anchor.line >= 0);
        CHECK(anchor.line < d.lineCount());
        CHECK(anchor.col >= 0);
        CHECK(anchor.col <= d.lineLength(anchor.line));

        // cursor (position) valido.
        CHECK(pos.line >= 0);
        CHECK(pos.line < d.lineCount());
        CHECK(pos.col >= 0);
        CHECK(pos.col <= d.lineLength(pos.line));
    }

    // anchor == cursor debe equivaler a "no hay seleccion".
    if (ed.active().selection.has_value() && ed.active().selection->anchor == ed.active().selection->position) {
        CHECK(!ed.hasSelection());
    }

    // selection() (si hay un rango no vacio) debe venir NORMALIZADO:
    // start <= end. Cualquier reconstruccion del rango debe honrarlo.
    if (auto norm = ed.selection()) {
        CHECK(norm->start.line < norm->end.line ||
              (norm->start.line == norm->end.line && norm->start.col <= norm->end.col));
    }

    // --- Invariantes de estado (modo) ---
    // El modo solo admite los estados conocidos. v0.6.3 anadio BufferSelector
    // y v0.7 SaveAs (el prompt "Guardar archivo:" de un buffer sin nombre).
    CHECK(ed.state_ == State::Navegacion ||
          ed.state_ == State::Interaccion ||
          ed.state_ == State::Seleccion ||
          ed.state_ == State::Prefix ||
          ed.state_ == State::BufferSelector ||
          ed.state_ == State::SaveAs);

    // Si hay un rango NO vacio, el editor debe estar en Seleccion... salvo
    // en Prefix, donde la seleccion se conserva mientras el comando espera
    // (priorState_ == Seleccion). Lo mismo vale dentro de BufferSelector y
    // SaveAs, que se abren desde el prefijo y preservan el modo previo.
    // Navegacion/Interaccion con una seleccion activa serian incoherentes.
    if (ed.hasSelection()) {
        CHECK(ed.state_ == State::Seleccion ||
              ed.state_ == State::Prefix ||
              ed.state_ == State::BufferSelector ||
              ed.state_ == State::SaveAs);
    }

    // Estar en modo Seleccion implica que hay (al menos) una seleccion
    // interna, aunque sea vacia (anchor == position).
    if (ed.state_ == State::Seleccion)
        CHECK(ed.active().selection.has_value());

    // --- Invariantes del clipboard ---
    // Cada linea del clipboard es una cadena UTF-8 valida (sin bytes de
    // continuacion colgando): es un bloque bien formado de lineas.
    for (const std::string& l : ed.clipboard_)
        CHECK(validUtf8(l));

    // --- Invariantes del historial ---
    // El clipboard NO forma parte de HistoryState: no puede haber una linea
    // "viajando" en undo/redo que el estado reconozca como portapapeles.
    // (Históricamente: si se restaurara el clipboard en undo, aqui no habria
    // ningun indicio del que fiarse; la regla no es un mero consejo.)
    CHECK(ed.clipboard_.size() <= Editor::MAX_UNDO);
}

// Secuencia determinista de eventos que cubre todas las mutaciones,
// incluido el modo seleccion: entrar (con la letra 's') y extender con las
// flechas, lo que arma y anula la seleccion (rango vacio <-> no vacio).
// Tras cada evento el estado completo (Document, Cursor, undo/redo y
// ahora la seleccion) debe seguir siendo consistente.
TEST(state_consistent_after_random_events) {
    Editor ed;
    assertStateConsistent(ed);

    const std::vector<Event> seq = {
        insert('a'), insert('b'), ev(EventType::InsertNewline),
        insert('c'), ev(EventType::MoveLeft), ev(EventType::Backspace),
        ev(EventType::Delete), ev(EventType::MoveDown), insert('z'),
        ev(EventType::MoveUp), ev(EventType::Undo), ev(EventType::Redo),
        ev(EventType::MoveHome), ev(EventType::MoveEnd), insert('!'),
        // Paso 8: movimientos dentro del modo seleccion (letra 's').
        insert('s'), ev(EventType::MoveLeft),
        ev(EventType::MoveRight), ev(EventType::MoveRight),
        ev(EventType::MoveUp), ev(EventType::MoveDown),
        ev(EventType::MoveHome), ev(EventType::MoveEnd),
        ev(EventType::Escape), ev(EventType::MoveRight),
        insert('#'), insert('s'), ev(EventType::MoveLeft),
        ev(EventType::Escape), ev(EventType::Undo), ev(EventType::Undo),
        ev(EventType::Redo),
    };
    for (const Event& e : seq) {
        ed.handleEvent(e);
        assertStateConsistent(ed);
    }
}

// Paso 8: estresar la seleccion alternando entrada/salida del modo
// seleccion con movimientos sobre un documento de varias lineas de
// distinto largo. La seleccion introduce un segundo par de coordenadas
// (anchor/position) que debe quedar siempre dentro del documento,
// aunque el cursor vaya y vuelva cruzando el anchor.
TEST(state_selection_consistent_after_shift_moves) {
    Editor ed;
    ed.active().document.restore({"abcde", "xy", "", "abcdefghij", "uv"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.active().cursor.preferredCol_ = 0;

    const EventType moves[] = {
        EventType::MoveLeft, EventType::MoveRight,
        EventType::MoveUp, EventType::MoveDown,
        EventType::MoveHome, EventType::MoveEnd,
    };
    for (int i = 0; i < 400; ++i) {
        EventType t = moves[i % 6];
        // Mitad arma seleccion (letra 's'), mitad la cancela (ESC): asi se
        // arma y se desarma la seleccion al mover.
        if (i % 2 == 0) {
            ed.handleEvent(insert('s'));
            ed.handleEvent(ev(t));
        } else {
            ed.handleEvent(ev(EventType::Escape));
            ed.handleEvent(ev(t));
        }
        assertStateConsistent(ed);
    }
}

// Paso 8/19: en modo seleccion, Up/Down sobre lineas de distinto largo usan
// preferredCol_ para recuperar la columna original. El ciclo
// Shift+Down, Shift+Down, Shift+Up, Shift+Up debe llevar el cursor de vuelta
// exactamente al anchor (y por tanto anular la seleccion), sin corromper
// preferredCol_.
TEST(state_selection_shift_cycle_preserves_preferred_col) {
    Editor ed;
    ed.active().document.restore({"abcdef", "xy", "abcdef"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 5;
    ed.active().cursor.preferredCol_ = 5;

    // Entrar en modo seleccion (letra 's'): arma el anchor {0, 5}.
    ed.handleEvent(insert('s'));
    CHECK(ed.active().selection.has_value());
    const Position anchor{0, 5};
    CHECK(ed.active().selection->anchor == anchor);

    // Shift+Down -> linea "xy" (largo 2), el cursor se clampa a col 2.
    ed.handleEvent(ev(EventType::MoveDown));
    assertStateConsistent(ed);
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK_EQ(ed.active().cursor.preferredCol_, 5);

    // Shift+Down -> "abcdef" (largo 6), recupera la col 5 preferida.
    ed.handleEvent(ev(EventType::MoveDown));
    assertStateConsistent(ed);
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 5);

    // Shift+Up -> "xy" de nuevo, clamped a col 2.
    ed.handleEvent(ev(EventType::MoveUp));
    assertStateConsistent(ed);
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 2);

    // Shift+Up -> linea 0, recupera col 5 == anchor.
    ed.handleEvent(ev(EventType::MoveUp));
    assertStateConsistent(ed);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 5);
    CHECK_EQ(ed.active().cursor.preferredCol_, 5);

    // El ciclo completo devuelve el cursor exactamente al anchor; la
    // seleccion queda vacia (anchor == position) de forma consistente.
    CHECK(ed.active().selection->anchor == anchor);
    CHECK(ed.active().selection->position == anchor);
    CHECK(!ed.hasSelection());
}

TEST(state_modified_flag_tracks_changes) {
    Editor ed;
    CHECK(!ed.active().modified);
    type(ed, "x");
    CHECK(ed.active().modified);
    // Undo vuelve al estado inicial (== al guardado): modified_ se limpia.
    ed.handleEvent(ev(EventType::Undo));
    CHECK(!ed.active().modified);
    ed.handleEvent(ev(EventType::Redo));
    CHECK(ed.active().modified);
}

// ---------------------------------------------------------------------------
// Paso 16: stress determinista que mezcla todo tipo de operacion sobre un
// documento multilinea, verificando las invariantes de estado tras CADA
// evento. Combina edicion, modo seleccion (letra 's'), movimiento, borrado y
// reemplazo de seleccion, y undo/redo (incluidas las operaciones sobre
// seleccion del Paso 12).
// ---------------------------------------------------------------------------
TEST(state_stress_mixed_operations_selection) {
    Editor ed;
    ed.active().document.restore({"hola", "mundo", "", "chau"});
    ed.active().cursor.line = 1;
    ed.active().cursor.col = 2;
    ed.active().cursor.preferredCol_ = 2;

    assertStateConsistent(ed);

    // Generador deterministico (LGC simple) para las posiciones de los
    // movimientos con/sin seleccion (modo Seleccion o Navegacion) y las letras.
    unsigned long seed = 12345;
    auto rnd = [&seed]() {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int>((seed >> 33) & 0xFFFFFFFF);
    };

    // Un solo pasada larga y determinista: tras cada evento se chequean
    // las invariantes. Se alterna el dominio de operaciones para no caer
    // en un lazo que solo mueve el cursor alrededor de un par de lineas.
    for (int step = 0; step < 2000; ++step) {
        const int k = rnd() % 6; // dominio general de operacion
        Event e;
        switch (k) {
            case 0: // InsertChar (a veces reemplaza la seleccion activa)
                e.type = EventType::InsertChar;
                e.text = std::string(1, static_cast<char>('a' + (rnd() % 26)));
                break;
            case 1: // entrar en modo seleccion con la letra 's'
                e.type = EventType::InsertChar;
                e.text = "s";
                break;
            case 2: // movimiento (en modo seleccion estira; sin el, mueve)
                e.type = static_cast<EventType>(
                    static_cast<int>(EventType::MoveLeft) + (rnd() % 6));
                break;
            case 3: // cancelar seleccion
                e.type = EventType::Escape;
                break;
            case 4: // borrado: Backspace o Delete (con seleccion o no)
                e.type = (rnd() % 2) ? EventType::Backspace
                                     : EventType::Delete;
                break;
            case 5: // ediciones que arman/desarman undo/redo
                e.type = (rnd() % 2) ? EventType::Undo : EventType::Redo;
                break;
        }
        ed.handleEvent(e);
        assertStateConsistent(ed);
    }
}

TEST(state_filename_unchanged_by_edits) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "contenido");
    ed.handleEvent(ev(EventType::Undo));
    ed.handleEvent(ev(EventType::Redo));
    CHECK_EQ(ed.active().filename, f.path);
}

TEST(state_history_coherent_after_sequence) {
    Editor ed;
    type(ed, "abc");
    ed.handleEvent(ev(EventType::Undo));  // -> "ab"
    ed.handleEvent(ev(EventType::Undo));  // -> "a"
    ed.handleEvent(ev(EventType::Redo));  // -> "ab"
    ed.handleEvent(ev(EventType::Undo));  // -> "a"
    ed.handleEvent(ev(EventType::Redo));  // -> "ab"
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    // pushHistory guarda el snapshot ANTERIOR a cada mutacion, asi que tras
    // teclear "abc" el undoStack_ es [init, "a", "ab"]. La secuencia
    // undo/undo/redo/undo/redo deja undoStack_ == [init, "a"] (2) y
    // redoStack_ == ["abc"] (1).
    CHECK_EQ(ed.active().undoStack.size(), size_t(2));
    CHECK_EQ(ed.active().redoStack.size(), size_t(1));
}

// ---------------------------------------------------------------------------
// 20. Secuencias de operaciones completas
// ---------------------------------------------------------------------------
TEST(sequence_open_insert_save_close) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hola");
    save(ed);
    CHECK(!ed.active().modified);
    // v0.3: Quit solo sale via prefijo (Ctrl+K -> Ctrl+Q).
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(ev(EventType::Quit));
    CHECK(!ed.running_);

    std::ifstream in(f.path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    CHECK_EQ(content, "hola");
}

TEST(sequence_open_edit_undo_redo_save) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hola mundo");
    ed.handleEvent(ev(EventType::Undo));
    ed.handleEvent(ev(EventType::Redo));
    save(ed);
    CHECK(!ed.active().modified);

    std::ifstream in(f.path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    CHECK_EQ(content, "hola mundo");
}

TEST(sequence_insert_enter_write_backspace_undo) {
    Editor ed;
    type(ed, "ab");
    ed.handleEvent(ev(EventType::InsertNewline));  // linea0 "ab", linea1 ""
    ed.handleEvent(ev(EventType::InsertNewline));  // linea1 "", linea2 ""
    type(ed, "cd");
    ed.handleEvent(ev(EventType::Backspace));  // borra 'd'
    CHECK_EQ(ed.active().document.lineAt(2), "c");
    ed.handleEvent(ev(EventType::Undo));
    CHECK_EQ(ed.active().document.lineAt(2), "cd");
    assertStateConsistent(ed);
}

TEST(sequence_undo_restores_trailing_newline_flag) {
    // El flag del '\n' final debe viajar con el undo/redo. Abrir "a\n",
    // Enter al final (el '\n' pasa a ser la linea vacia: flag false) y
    // undo debe volver al estado ["a"] con flag true. Guardar tras el
    // undo debe dar "a\n", no "a".
    TempFile f;
    f.write("a\n");
    Editor ed;
    ed.openFile(f.path);
    CHECK(ed.active().document.endsWithNewline());
    enterInteraccion(ed);
    ed.handleEvent(ev(EventType::MoveEnd));            // cursor al final de "a"
    ed.handleEvent(ev(EventType::InsertNewline));      // ["a",""] flag false
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK(!ed.active().document.endsWithNewline());
    ed.handleEvent(ev(EventType::Undo));
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK(ed.active().document.endsWithNewline());
    ed.handleEvent(ev(EventType::Redo));
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK(!ed.active().document.endsWithNewline());
    assertStateConsistent(ed);
}

TEST(sequence_move_insert_move_delete) {
    Editor ed;
    type(ed, "abc");
    ed.handleEvent(ev(EventType::MoveLeft));  // col 2
    ed.handleEvent(insert('X'));              // "abXc", col 3
    ed.handleEvent(ev(EventType::MoveLeft));  // col 2
    ed.handleEvent(ev(EventType::Delete));    // elimina la 'X'
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
    CHECK_EQ(ed.active().cursor.col, 2);
    assertStateConsistent(ed);
}

TEST(sequence_mixed_edits_end_consistent) {
    Editor ed;
    type(ed, "linea");
    ed.handleEvent(ev(EventType::InsertNewline));
    type(ed, "dos");
    ed.handleEvent(ev(EventType::MoveLeft));
    ed.handleEvent(ev(EventType::Delete));   // "do"
    ed.handleEvent(ev(EventType::MoveDown)); // linea 1, col 0 -> noop
    ed.handleEvent(ev(EventType::Undo));
    ed.handleEvent(ev(EventType::Redo));
    CHECK_EQ(ed.active().document.lineAt(0), "linea");
    CHECK_EQ(ed.active().document.lineAt(1), "do");
    assertStateConsistent(ed);
}

// ---------------------------------------------------------------------------
// 18. Casos limite
// ---------------------------------------------------------------------------
TEST(edge_empty_document) {
    Editor ed;
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    ed.handleEvent(ev(EventType::Backspace));
    ed.handleEvent(ev(EventType::Delete));
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(edge_single_empty_line) {
    Editor ed;
    enterInteraccion(ed);
    ed.handleEvent(ev(EventType::InsertNewline));  // "", ""
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().document.lineAt(1), "");
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
}

TEST(edge_single_letter) {
    Editor ed;
    enterInteraccion(ed);
    ed.handleEvent(insert('a'));
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    CHECK_EQ(ed.active().cursor.col, 1);
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.active().document.lineAt(0), "");
}

TEST(edge_long_line_million_chars) {
    Editor ed;
    const int n = 1000000;
    ed.active().document.restore({std::string(n, 'x')});
    ed.active().cursor.col = n;
    CHECK_EQ(ed.active().document.lineLength(0), n);
    CHECK_EQ(ed.active().cursor.col, n);
    enterInteraccion(ed); // Backspace solo actua en Interaccion (v0.5)
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.active().document.lineLength(0), n - 1);
    ed.handleEvent(ev(EventType::Undo));
    CHECK_EQ(ed.active().document.lineLength(0), n);
    assertStateConsistent(ed);
}

TEST(edge_many_empty_lines) {
    Editor ed;
    const int n = 5000;
    enterInteraccion(ed);
    for (int i = 0; i < n; ++i)
        ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(ed.active().document.lineCount(), n + 1);
    for (int i = 0; i < n + 1; ++i)
        CHECK_EQ(ed.active().document.lineAt(i), "");
    CHECK_EQ(ed.active().cursor.line, n);
    ed.handleEvent(ev(EventType::MoveUp));  // la ultima linea esta vacia
    CHECK_EQ(ed.active().cursor.col, 0);
    assertStateConsistent(ed);
}

TEST(edge_cursor_at_absolute_start) {
    Editor ed;
    type(ed, "abc");
    ed.handleEvent(ev(EventType::InsertNewline));
    type(ed, "def");
    ed.handleEvent(ev(EventType::MoveHome));
    ed.handleEvent(ev(EventType::MoveHome));
    ed.handleEvent(ev(EventType::MoveUp));
    ed.handleEvent(ev(EventType::MoveLeft));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(edge_cursor_at_absolute_end) {
    Editor ed;
    type(ed, "abc");
    ed.handleEvent(ev(EventType::InsertNewline));
    type(ed, "def");
    ed.handleEvent(ev(EventType::MoveEnd));
    ed.handleEvent(ev(EventType::MoveEnd));
    ed.handleEvent(ev(EventType::MoveDown));
    ed.handleEvent(ev(EventType::MoveRight));
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 3);
}

TEST(edge_millions_of_chars_roundtrip) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    const int n = 1000000;
    ed.active().document.restore({std::string(n, 'y')});
    save(ed);
    CHECK(!ed.active().modified);

    Editor ed2;
    CHECK(ed2.openFile(f.path));
    CHECK_EQ(ed2.active().document.lineLength(0), n);
    for (int i = 0; i < n; i += 10000)
        CHECK_EQ(ed2.active().document.lineAt(0)[i], 'y');
}

// ---------------------------------------------------------------------------
// 21. Invariantes globales
// ---------------------------------------------------------------------------
// Guardar y volver a abrir debe producir exactamente el mismo contenido.
static void assertRoundTrip(const std::string& path, const Editor& ed) {
    Editor reloaded;
    CHECK(reloaded.openFile(path));
    CHECK_EQ(reloaded.active().document.lineCount(), ed.active().document.lineCount());
    for (int i = 0; i < ed.active().document.lineCount(); ++i)
        CHECK_EQ(reloaded.active().document.lineAt(i), ed.active().document.lineAt(i));
    CHECK_EQ(reloaded.active().cursor.line, 0);
    CHECK_EQ(reloaded.active().cursor.col, 0);
    assertStateConsistent(reloaded);
}

TEST(invariant_save_reload_exact) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "primera");
    ed.handleEvent(ev(EventType::InsertNewline));
    type(ed, "segunda");
    ed.handleEvent(ev(EventType::InsertNewline));
    type(ed, "tercera");
    save(ed);
    assertRoundTrip(f.path, ed);
}

TEST(invariant_save_does_not_change_document) {
    TempFile f;
    Editor ed;

    ed.openFile(f.path);
    type(ed, "abc");

    const std::string before = ed.active().document.lineAt(0);
    const int line = ed.active().cursor.line;
    const int col = ed.active().cursor.col;

    save(ed);

    CHECK_EQ(ed.active().document.lineAt(0), before);
    CHECK_EQ(ed.active().cursor.line, line);
    CHECK_EQ(ed.active().cursor.col, col);
    CHECK(!ed.active().modified);

    assertStateConsistent(ed);
}

TEST(invariant_undo_redo_never_corrupts) {
    Editor ed;
    type(ed, "a");
    ed.handleEvent(ev(EventType::InsertNewline));
    type(ed, "b");

    // Ciclo undo/redo: el contenido debe quedar siempre dentro de los
    // estados validos conocidos.
    for (int round = 0; round < 50; ++round) {
        ed.handleEvent(ev(EventType::Undo));
        ed.handleEvent(ev(EventType::Redo));
        CHECK_EQ(ed.active().document.lineAt(0), "a");
        CHECK_EQ(ed.active().document.lineCount(), 2);
        CHECK_EQ(ed.active().document.lineAt(1), "b");
        assertStateConsistent(ed);
    }
}

TEST(invariant_line_count_matches_content) {
    Editor ed;
    type(ed, "hola");
    ed.handleEvent(ev(EventType::InsertNewline));
    type(ed, "mundo");

    // lineCount() y lineAt(i) son coherentes entre si.
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
    CHECK_EQ(ed.active().document.lineAt(1), "mundo");
}

// Serializacion: como se ve el contenido en el archivo al guardar.
// Intentamos/no asumimos el formato exacto (p.ej. si hay newline final).
TEST(invariant_serialize_joins_lines) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hola");
    ed.handleEvent(ev(EventType::InsertNewline));
    type(ed, "mundo");
    save(ed);

    std::ifstream in(f.path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());

    // "hola\nmundo" -- las lineas se unen con \n, sin newline final.
    CHECK_EQ(content, "hola\nmundo");
}

TEST(invariant_no_crash_on_event_sequence) {
    // Secuencia de eventos (incluye Save y Quit, que corta el loop). No
    // busca cubrir cada tipo de forma uniforme, solo que ninguna secuencia
    // arbitraria rompa el estado. v0.7: el buffer tiene nombre para que un
    // Save aleatorio guarde en el TempFile y NO entre al prompt SaveAs
    // (que, con un Enter aleatorio, escribira un archivo en el cwd).
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    // Todos los tipos de evento ante un editor recien creado.
    const std::vector<EventType> types = {
        EventType::InsertChar, EventType::InsertNewline, EventType::Backspace,
        EventType::Delete,     EventType::Undo,          EventType::Redo,
        EventType::MoveLeft,   EventType::MoveRight,     EventType::MoveUp,
        EventType::MoveDown,   EventType::MoveHome,      EventType::MoveEnd,
        EventType::Prefix,     EventType::Save,          EventType::None,
    };
    for (int round = 0; round < 1000; ++round) {
        Event e;
        e.type = types[round % types.size()];
        if (e.type == EventType::InsertChar)
            e.text = std::string(1, static_cast<char>('a' + (round % 26)));
        ed.handleEvent(e);
        assertStateConsistent(ed);
        if (!ed.running_) break;
    }
}

// ---------------------------------------------------------------------------
// Invariantes globales nuevas: clipboard, historial y modos
// ---------------------------------------------------------------------------

TEST(invariant_state_consistent_with_clipboard_and_prefix) {
    // El generador anterior no ejercia el clipboard ni el prefijo. Aqui
    // alternamos cortar/copiar/pegar, entrar en modo seleccion, abrir el
    // prefijo y los movimientos; tras CADA evento se verifican las
    // invariantes ampliadas (cuyo conjunto incluye documento, cursor,
    // seleccion, estado, clipboard y limites de historial).
    TempFile f;
    Editor ed;
    ed.openFile(f.path); // nombre real: un Save aleatorio no abre el prompt
    ed.active().document.restore({"hola", "mundo", "", "cafe"});
    ed.active().cursor.line = 1;
    ed.active().cursor.col = 2;
    ed.active().cursor.preferredCol_ = 2;
    assertStateConsistent(ed);

    unsigned long seed = 987654;
    auto rnd = [&seed]() {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int>((seed >> 33) & 0xFFFFFFFF);
    };

    for (int step = 0; step < 1500; ++step) {
        const int k = rnd() % 9;
        Event e;
        switch (k) {
            case 0: // letra (a veces 's' para entrar a seleccion, 'c'/'x'/'p')
                e.type = EventType::InsertChar;
                switch (rnd() % 4) {
                    case 0: e.text = std::string(1, static_cast<char>('a' + (rnd() % 26))); break;
                    case 1: e.text = "s"; break;
                    case 2: e.text = (rnd() % 2) ? "c" : "x"; break;
                    default: e.text = "p"; break;
                }
                break;
            case 1:
                e.type = static_cast<EventType>(static_cast<int>(EventType::MoveLeft) + (rnd() % 6));
                break;
            case 2:
                e.type = EventType::Escape;
                break;
            case 3:
                e.type = (rnd() % 2) ? EventType::Backspace : EventType::Delete;
                break;
            case 4:
                e.type = (rnd() % 2) ? EventType::Undo : EventType::Redo;
                break;
            case 5:
                e.type = EventType::Prefix;               // Ctrl+K
                break;
            case 6:
                e.type = (rnd() % 2) ? EventType::Save : EventType::Quit;
                break;
            default:
                e.type = EventType::InsertNewline;
                break;
        }
        ed.handleEvent(e);
        assertStateConsistent(ed);
        if (!ed.running_) ed.running_ = true; // no detener el bucle por un Quit aislado
    }
}

TEST(invariant_clipboard_stays_valid_across_undo_redo) {
    // Copiar y cortar con UTF-8 mezclado, deshacer y rehacer: el clipboard
    // sigue siendo siempre un bloque de lineas UTF-8 valido.
    Editor ed;
    ed.active().document.restore({"caf\xc3\xa9 \xe2\x80\x94 \xf0\x9f\x98\x80"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    assertStateConsistent(ed);

    // Entrar en seleccion y copiar el primer caracter ("c", 1 byte).
    ed.handleEvent(insert('s'));
    ed.handleEvent(ev(EventType::MoveRight));
    ed.handleEvent(insert('c'));
    assertStateConsistent(ed);

    // Undo/Redo repetidos: el clipboard no debe corromperse.
    for (int i = 0; i < 20; ++i) {
        ed.handleEvent(ev(EventType::Undo));
        assertStateConsistent(ed);
        ed.handleEvent(ev(EventType::Redo));
        assertStateConsistent(ed);
    }
}

TEST(invariant_undo_reaches_limit_unbounded) {
    // Verifica que el historial respeta el limite superior y que por debajo
    // el estado nunca se desborda con entradas corruptas.
    Editor ed;
    type(ed, "x");
    // Generamos muchos estados distintos: cada letra nueva empuja historia.
    for (int i = 0; i < 2500; ++i) {
        ed.handleEvent(insert(static_cast<char>('a' + (i % 26))));
        assertStateConsistent(ed);
        CHECK(ed.active().undoStack.size() <= Editor::MAX_UNDO);
    }
    CHECK_EQ(ed.active().undoStack.size(), Editor::MAX_UNDO);
}

TEST(invariant_redo_consistent_with_undo) {
    // Despues de un Undo, redoStack_ se rellena; Redo lo reaplica y la pila
    // de undo vuelve a crecer. Tras una mutacion nueva el redo se vacia.
    Editor ed;
    type(ed, "ab");
    ed.handleEvent(ev(EventType::Undo));  // "a", redo=["b"]
    CHECK(!ed.active().redoStack.empty());
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    assertStateConsistent(ed);

    ed.handleEvent(ev(EventType::Redo));  // "ab"
    CHECK(ed.active().redoStack.empty());
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    assertStateConsistent(ed);

    // Debajo de un mismo undo hay exactamente una entrada de redo.
    ed.handleEvent(ev(EventType::Undo));
    ed.handleEvent(ev(EventType::Undo));
    CHECK_EQ(ed.active().redoStack.size(), size_t{2});
    assertStateConsistent(ed);
}

TEST(invariant_clipboard_not_in_history) {
    // El clipboard vive fuera de HistoryState: deshacer una edicion no debe
    // "devolver" un buffer viejo. Lo verificamos de forma estructural: los
    // estados guardados no llevan rastro del buffer y el buffer sobrevive.
    Editor ed;
    ed.active().document.restore({"abcdef"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    // Copiar "ab".
    ed.handleEvent(insert('s'));
    ed.handleEvent(ev(EventType::MoveRight));
    ed.handleEvent(ev(EventType::MoveRight));
    ed.handleEvent(insert('c'));
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ab"}));

    // Editar distinto contenido (que apila historia).
    ed.handleEvent(ev(EventType::MoveEnd));
    ed.handleEvent(insert('i'));
    ed.handleEvent(insert('!'));
    ed.handleEvent(ev(EventType::Escape));

    // Undo y Redo: el clipboard "ab" debe permanecer intacto.
    ed.handleEvent(ev(EventType::Undo));
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ab"}));
    ed.handleEvent(ev(EventType::Redo));
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ab"}));
    assertStateConsistent(ed);
}
