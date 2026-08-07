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
    e.ch = c;
    return e;
}

static void type(Editor& ed, const std::string& s) {
    for (char c : s)
        ed.handleEvent(insert(c));
}

static Event shift(EventType t) {
    Event e;
    e.type = t;
    e.shift = true;
    return e;
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
static void assertStateConsistent(Editor& ed) {
    const Document& d = ed.document_;

    CHECK(d.lineCount() >= 1);
    for (int i = 0; i < d.lineCount(); ++i)
        CHECK_EQ(d.lineAt(i).size(), static_cast<size_t>(d.lineLength(i)));

    CHECK(ed.cursor_.line >= 0);
    CHECK(ed.cursor_.col >= 0);
    CHECK(ed.cursor_.line < d.lineCount());
    CHECK(ed.cursor_.col <= d.lineLength(ed.cursor_.line));

    CHECK(ed.undoStack_.size() <= Editor::MAX_UNDO);
    CHECK(ed.redoStack_.size() <= Editor::MAX_UNDO);

    // --- Invariantes de la seleccion (Paso 8) ---
    // Si el estado interno guarda una seleccion (incluso vacia), sus dos
    // coordenadas deben ser validas.
    if (ed.selection_.has_value()) {
        const Position& anchor = ed.selection_->anchor;
        const Position& pos = ed.selection_->position;

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
    if (ed.selection_.has_value() && ed.selection_->anchor == ed.selection_->position) {
        CHECK(!ed.hasSelection());
    }
}

// Secuencia determinista de eventos que cubre todas las mutaciones,
// incluidos los movimientos con Shift que arman y anulan seleccion.
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
        // Paso 8: movimientos con Shift.
        shift(EventType::MoveLeft), shift(EventType::MoveRight),
        shift(EventType::MoveRight), shift(EventType::MoveUp),
        shift(EventType::MoveDown), shift(EventType::MoveHome),
        shift(EventType::MoveEnd), ev(EventType::MoveRight),
        insert('#'), shift(EventType::MoveLeft), ev(EventType::MoveRight),
        ev(EventType::Undo), ev(EventType::Undo), ev(EventType::Redo),
    };
    for (const Event& e : seq) {
        ed.handleEvent(e);
        assertStateConsistent(ed);
    }
}

// Paso 8: estresar la seleccion con movimientos Shift aleatorios sobre un
// documento de varias lineas de distinto largo. La seleccion introduce un
// segundo par de coordenadas (anchor/position) que debe quedar siempre
// dentro del documento, aunque el cursor vaya y vuelva cruzando el anchor.
TEST(state_selection_consistent_after_shift_moves) {
    Editor ed;
    ed.document_.restore({"abcde", "xy", "", "abcdefghij", "uv"});
    ed.cursor_.line = 0;
    ed.cursor_.col = 0;
    ed.cursor_.preferredCol_ = 0;

    const EventType moves[] = {
        EventType::MoveLeft, EventType::MoveRight,
        EventType::MoveUp, EventType::MoveDown,
        EventType::MoveHome, EventType::MoveEnd,
    };
    for (int i = 0; i < 400; ++i) {
        EventType t = moves[i % 6];
        // Mitad con Shift, mitad sin: asi se arma y se desarma seleccion.
        if (i % 2 == 0) {
            ed.handleEvent(shift(t));
        } else {
            ed.handleEvent(ev(t));
        }
        assertStateConsistent(ed);
    }
}

TEST(state_modified_flag_tracks_changes) {
    Editor ed;
    CHECK(!ed.modified_);
    type(ed, "x");
    CHECK(ed.modified_);
    // Undo vuelve al estado inicial (== al guardado): modified_ se limpia.
    ed.handleEvent(ev(EventType::Undo));
    CHECK(!ed.modified_);
    ed.handleEvent(ev(EventType::Redo));
    CHECK(ed.modified_);
}

// ---------------------------------------------------------------------------
// Paso 16: stress determinista que mezcla todo tipo de operacion sobre un
// documento multilinea, verificando las invariantes de estado tras CADA
// evento. Combina edicion, movimiento con/sin Shift, borrado de seleccion,
// reemplazo de seleccion, y undo/redo (incluidas las operaciones sobre
// seleccion del Paso 12).
// ---------------------------------------------------------------------------
TEST(state_stress_mixed_operations_selection) {
    Editor ed;
    ed.document_.restore({"hola", "mundo", "", "chau"});
    ed.cursor_.line = 1;
    ed.cursor_.col = 2;
    ed.cursor_.preferredCol_ = 2;

    assertStateConsistent(ed);

    // Generador deterministico (LGC simple) para las posiciones Shift /
    // no-Shift y las letras.
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
                e.ch = static_cast<char>('a' + (rnd() % 26));
                break;
            case 1: // movimiento con Shift (arma/ajusta seleccion)
                e.type = static_cast<EventType>(
                    static_cast<int>(EventType::MoveLeft) + (rnd() % 6));
                e.shift = true;
                break;
            case 2: // movimiento sin Shift (cancela la seleccion)
                e.type = static_cast<EventType>(
                    static_cast<int>(EventType::MoveLeft) + (rnd() % 6));
                e.shift = false;
                break;
            case 3: // borrado: Backspace o Delete (con seleccion o no)
                e.type = (rnd() % 2) ? EventType::Backspace
                                     : EventType::Delete;
                break;
            case 4: // ediciones que arman/desarman undo/redo
                e.type = (rnd() % 2) ? EventType::Undo : EventType::Redo;
                break;
            default: // InsertNewline: cambia la estructura de lineas
                e.type = EventType::InsertNewline;
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
    CHECK_EQ(ed.filename_, f.path);
}

TEST(state_history_coherent_after_sequence) {
    Editor ed;
    type(ed, "abc");
    ed.handleEvent(ev(EventType::Undo));  // -> "ab"
    ed.handleEvent(ev(EventType::Undo));  // -> "a"
    ed.handleEvent(ev(EventType::Redo));  // -> "ab"
    ed.handleEvent(ev(EventType::Undo));  // -> "a"
    ed.handleEvent(ev(EventType::Redo));  // -> "ab"
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    // pushHistory guarda el snapshot ANTERIOR a cada mutacion, asi que tras
    // teclear "abc" el undoStack_ es [init, "a", "ab"]. La secuencia
    // undo/undo/redo/undo/redo deja undoStack_ == [init, "a"] (2) y
    // redoStack_ == ["abc"] (1).
    CHECK_EQ(ed.undoStack_.size(), size_t(2));
    CHECK_EQ(ed.redoStack_.size(), size_t(1));
}

// ---------------------------------------------------------------------------
// 20. Secuencias de operaciones completas
// ---------------------------------------------------------------------------
TEST(sequence_open_insert_save_close) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hola");
    ed.handleEvent(ev(EventType::Save));
    CHECK(!ed.modified_);
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
    ed.handleEvent(ev(EventType::Save));
    CHECK(!ed.modified_);

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
    CHECK_EQ(ed.document_.lineAt(2), "c");
    ed.handleEvent(ev(EventType::Undo));
    CHECK_EQ(ed.document_.lineAt(2), "cd");
    assertStateConsistent(ed);
}

TEST(sequence_move_insert_move_delete) {
    Editor ed;
    type(ed, "abc");
    ed.handleEvent(ev(EventType::MoveLeft));  // col 2
    ed.handleEvent(insert('X'));              // "abXc", col 3
    ed.handleEvent(ev(EventType::MoveLeft));  // col 2
    ed.handleEvent(ev(EventType::Delete));    // elimina la 'X'
    CHECK_EQ(ed.document_.lineAt(0), "abc");
    CHECK_EQ(ed.cursor_.col, 2);
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
    CHECK_EQ(ed.document_.lineAt(0), "linea");
    CHECK_EQ(ed.document_.lineAt(1), "do");
    assertStateConsistent(ed);
}

// ---------------------------------------------------------------------------
// 18. Casos limite
// ---------------------------------------------------------------------------
TEST(edge_empty_document) {
    Editor ed;
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "");
    ed.handleEvent(ev(EventType::Backspace));
    ed.handleEvent(ev(EventType::Delete));
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(edge_single_empty_line) {
    Editor ed;
    ed.handleEvent(ev(EventType::InsertNewline));  // "", ""
    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK_EQ(ed.document_.lineAt(1), "");
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "");
}

TEST(edge_single_letter) {
    Editor ed;
    ed.handleEvent(insert('a'));
    CHECK_EQ(ed.document_.lineAt(0), "a");
    CHECK_EQ(ed.cursor_.col, 1);
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.document_.lineAt(0), "");
}

TEST(edge_long_line_million_chars) {
    Editor ed;
    const int n = 1000000;
    ed.document_.restore({std::string(n, 'x')});
    ed.cursor_.col = n;
    CHECK_EQ(ed.document_.lineLength(0), n);
    CHECK_EQ(ed.cursor_.col, n);
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.document_.lineLength(0), n - 1);
    ed.handleEvent(ev(EventType::Undo));
    CHECK_EQ(ed.document_.lineLength(0), n);
    assertStateConsistent(ed);
}

TEST(edge_many_empty_lines) {
    Editor ed;
    const int n = 5000;
    for (int i = 0; i < n; ++i)
        ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(ed.document_.lineCount(), n + 1);
    for (int i = 0; i < n + 1; ++i)
        CHECK_EQ(ed.document_.lineAt(i), "");
    CHECK_EQ(ed.cursor_.line, n);
    ed.handleEvent(ev(EventType::MoveUp));  // la ultima linea esta vacia
    CHECK_EQ(ed.cursor_.col, 0);
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
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
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
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 3);
}

TEST(edge_millions_of_chars_roundtrip) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    const int n = 1000000;
    ed.document_.restore({std::string(n, 'y')});
    ed.handleEvent(ev(EventType::Save));
    CHECK(!ed.modified_);

    Editor ed2;
    CHECK(ed2.openFile(f.path));
    CHECK_EQ(ed2.document_.lineLength(0), n);
    for (int i = 0; i < n; i += 10000)
        CHECK_EQ(ed2.document_.lineAt(0)[i], 'y');
}

// ---------------------------------------------------------------------------
// 21. Invariantes globales
// ---------------------------------------------------------------------------
// Guardar y volver a abrir debe producir exactamente el mismo contenido.
static void assertRoundTrip(const std::string& path, const Editor& ed) {
    Editor reloaded;
    CHECK(reloaded.openFile(path));
    CHECK_EQ(reloaded.document_.lineCount(), ed.document_.lineCount());
    for (int i = 0; i < ed.document_.lineCount(); ++i)
        CHECK_EQ(reloaded.document_.lineAt(i), ed.document_.lineAt(i));
    CHECK_EQ(reloaded.cursor_.line, 0);
    CHECK_EQ(reloaded.cursor_.col, 0);
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
    ed.handleEvent(ev(EventType::Save));
    assertRoundTrip(f.path, ed);
}

TEST(invariant_save_does_not_change_document) {
    TempFile f;
    Editor ed;

    ed.openFile(f.path);
    type(ed, "abc");

    const std::string before = ed.document_.lineAt(0);
    const int line = ed.cursor_.line;
    const int col = ed.cursor_.col;

    ed.handleEvent(ev(EventType::Save));

    CHECK_EQ(ed.document_.lineAt(0), before);
    CHECK_EQ(ed.cursor_.line, line);
    CHECK_EQ(ed.cursor_.col, col);
    CHECK(!ed.modified_);

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
        CHECK_EQ(ed.document_.lineAt(0), "a");
        CHECK_EQ(ed.document_.lineCount(), 2);
        CHECK_EQ(ed.document_.lineAt(1), "b");
        assertStateConsistent(ed);
    }
}

TEST(invariant_line_count_matches_content) {
    Editor ed;
    type(ed, "hola");
    ed.handleEvent(ev(EventType::InsertNewline));
    type(ed, "mundo");

    // lineCount() y lineAt(i) son coherentes entre si.
    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.document_.lineAt(0), "hola");
    CHECK_EQ(ed.document_.lineAt(1), "mundo");
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
    ed.handleEvent(ev(EventType::Save));

    std::ifstream in(f.path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());

    // "hola\nmundo" -- las lineas se unen con \n, sin newline final.
    CHECK_EQ(content, "hola\nmundo");
}

TEST(invariant_no_crash_on_event_sequence) {
    // Secuencia de eventos (incluye Save sin archivo abierto y Quit, que
    // corta el loop). No busca cubrir cada tipo de forma uniforme, solo
    // que ninguna secuencia arbitraria rompa el estado.
    Editor ed;
    // Todos los tipos de evento ante un editor recien creado.
    const std::vector<EventType> types = {
        EventType::InsertChar, EventType::InsertNewline, EventType::Backspace,
        EventType::Delete,     EventType::Undo,          EventType::Redo,
        EventType::MoveLeft,   EventType::MoveRight,     EventType::MoveUp,
        EventType::MoveDown,   EventType::MoveHome,      EventType::MoveEnd,
        EventType::Save,       EventType::Quit,          EventType::None,
    };
    for (int round = 0; round < 1000; ++round) {
        Event e;
        e.type = types[round % types.size()];
        if (e.type == EventType::InsertChar)
            e.ch = static_cast<char>('a' + (round % 26));
        ed.handleEvent(e);
        assertStateConsistent(ed);
        if (!ed.running_) break;
    }
}
