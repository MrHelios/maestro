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

// ---------------------------------------------------------------------------
// 19. Estado del editor: invariantes de estado tras cada evento
// ---------------------------------------------------------------------------
// Comprueba que el documento, el cursor, la bandera modified_, el nombre de
// archivo y los stacks de undo/redo quedan coherentes.
static void assertStateConsistent(Editor& ed) {
    const Document& d = ed.document_;

    CHECK(d.lineCount() >= 1);
    for (int i = 0; i < d.lineCount(); ++i)
        CHECK_EQ(d.lineAt(i).size(), static_cast<size_t>(d.lineLength(i)));

    CHECK(ed.cursor_.line >= 0);
    CHECK(ed.cursor_.col >= 0);
    CHECK(ed.cursor_.line < d.lineCount());
    CHECK(ed.cursor_.col <= d.lineLength(ed.cursor_.line));

    CHECK(ed.undoStack_.size() <= 1000);
}

TEST(state_consistent_after_random_events) {
    Editor ed;
    assertStateConsistent(ed);

    // Secuencia determinista de eventos que cubre todas las mutaciones.
    const std::vector<Event> seq = {
        insert('a'), insert('b'), ev(EventType::InsertNewline),
        insert('c'), ev(EventType::MoveLeft), ev(EventType::Backspace),
        ev(EventType::Delete), ev(EventType::MoveDown), insert('z'),
        ev(EventType::MoveUp), ev(EventType::Undo), ev(EventType::Redo),
        ev(EventType::MoveHome), ev(EventType::MoveEnd), insert('!'),
        ev(EventType::Undo), ev(EventType::Undo), ev(EventType::Redo),
    };
    for (const Event& e : seq) {
        ed.handleEvent(e);
        assertStateConsistent(ed);
    }
}

TEST(state_modified_flag_tracks_changes) {
    Editor ed;
    CHECK(!ed.modified_);
    type(ed, "x");
    CHECK(ed.modified_);
    // Undo no restaura modified_: el historial no guarda la bandera.
    ed.handleEvent(ev(EventType::Undo));
    CHECK(ed.modified_);
    ed.handleEvent(ev(EventType::Redo));
    CHECK(ed.modified_);
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
    CHECK(!ed.undoStack_.empty());
    CHECK(!ed.redoStack_.empty());
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
    ed.handleEvent(ev(EventType::MoveRight)); // col 4
    ed.handleEvent(ev(EventType::Delete));    // no-op (fin de doc)
    CHECK_EQ(ed.document_.lineAt(0), "abXc");
    CHECK_EQ(ed.cursor_.col, 4);
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
    std::string content;
    type(ed, "hola");
    content += "hola";
    ed.handleEvent(ev(EventType::InsertNewline));
    content += "\n";
    type(ed, "mundo");
    content += "mundo";

    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.document_.lineAt(0), "hola");
    CHECK_EQ(ed.document_.lineAt(1), "mundo");

    // Reconstruir el contenido y comparar con lo tecleado.
    std::string rebuilt;
    for (int i = 0; i < ed.document_.lineCount(); ++i)
        rebuilt += ed.document_.lineAt(i) + "\n";
    CHECK_EQ(rebuilt, content + "\n");
}

TEST(invariant_no_crash_on_valid_inputs) {
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
