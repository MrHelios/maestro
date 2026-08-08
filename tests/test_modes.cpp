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
    e.ch = c;
    return e;
}

static void type(Editor& ed, const std::string& s) {
    for (char c : s)
        ed.handleEvent(insert(c));
}

static void press(Editor& ed, EventType type) {
    ed.handleEvent(Event{type});
}

static void prefix(Editor& ed, EventType first, EventType second) {
    ed.handleEvent(Event{first});
    ed.handleEvent(Event{second});
}

static std::string fileContent(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// v0.3: prefijo Ctrl+K (guardar / salir)
// ---------------------------------------------------------------------------
TEST(prefix_save_saves_file) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hola");
    CHECK(ed.modified_);
    prefix(ed, EventType::Prefix, EventType::Select); // Ctrl+K, Ctrl+S
    CHECK(!ed.modified_);
    CHECK_EQ(fileContent(f.path), "hola");
}

TEST(prefix_save_keeps_normal_state) {
    Editor ed;
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Normal));
}

TEST(prefix_quit_sets_running_false) {
    Editor ed;
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);
}

TEST(prefix_other_key_cancels_and_discards) {
    // Ctrl+K + una flecha: se descarta todo y se vuelve a Normal sin
    // mover el cursor (el evento de la flecha no se propaga).
    Editor ed;
    type(ed, "abc");
    ed.handleEvent(Event{EventType::Prefix});
    ed.handleEvent(Event{EventType::MoveRight});
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Normal));
    CHECK_EQ(ed.cursor_.col, 3);
    CHECK(ed.hasSelection() == false);
}

TEST(prefix_other_key_keeps_selection) {
    // Ctrl+K + tecla que no sea guardar/salir: se cancela el prefijo
    // pero la seleccion (si habia) NO se toca.
    Editor ed;
    type(ed, "hello");
    press(ed, EventType::MoveHome);
    press(ed, EventType::Select);             // entrar a seleccion
    press(ed, EventType::MoveRight);          // no seleccion [h]
    CHECK(ed.hasSelection());
    prefix(ed, EventType::Prefix, EventType::MoveRight);
    CHECK(ed.hasSelection());
}

TEST(prefix_quit_in_selection_quits) {
    Editor ed;
    press(ed, EventType::Select);
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);
}

// ---------------------------------------------------------------------------
// v0.3: modo seleccion via Ctrl+S (Select)
// ---------------------------------------------------------------------------
TEST(select_enters_through_ctrl_s) {
    Editor ed;
    press(ed, EventType::Select);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Select));
}

TEST(select_arrows_extend_without_shift) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    press(ed, EventType::Select);             // inicia seleccion en (0,0)
    press(ed, EventType::MoveRight);          // extiende
    press(ed, EventType::MoveRight);          // [ab]
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK_EQ(sel->start.col, 0);
    CHECK_EQ(sel->end.col, 2);
}

TEST(select_home_end_extend) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveEnd);
    press(ed, EventType::Select);
    press(ed, EventType::MoveHome);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.selection()->start.col, 0);
    CHECK_EQ(ed.selection()->end.col, 3);
}

TEST(select_ctrl_s_while_in_selection_ignored) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    press(ed, EventType::Select);
    press(ed, EventType::MoveRight);          // [a]
    CHECK(ed.hasSelection());
    press(ed, EventType::Select);             // Ctrl+S se ignora
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Select));
}

TEST(select_escape_exits_mode) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    press(ed, EventType::Select);
    press(ed, EventType::MoveRight);          // [a]
    CHECK(ed.hasSelection());
    press(ed, EventType::Escape);
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Normal));
}

TEST(select_escape_preserves_document) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Select);
    press(ed, EventType::MoveRight);
    press(ed, EventType::Escape);
    CHECK_EQ(ed.document_.lineAt(0), "abc");
}

TEST(select_type_replaces_and_exits) {
    Editor ed;
    type(ed, "hello");
    press(ed, EventType::MoveHome);
    press(ed, EventType::Select);
    press(ed, EventType::MoveRight);          // [h]
    ed.handleEvent(insert('H'));
    CHECK_EQ(ed.document_.lineAt(0), "Hello");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Normal));
    CHECK(!ed.hasSelection());
}

TEST(select_space_replaces_and_exits) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    press(ed, EventType::Select);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);          // [abc]
    ed.handleEvent(insert(' '));
    CHECK_EQ(ed.document_.lineAt(0), " ");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Normal));
}

TEST(select_enter_replaces_with_newline_and_exits) {
    Editor ed;
    type(ed, "ab");
    press(ed, EventType::MoveHome);
    press(ed, EventType::Select);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveEnd);            // [ab]
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK_EQ(ed.document_.lineAt(1), "");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Normal));
}

TEST(select_delete_selection_and_exits) {
    Editor ed;
    type(ed, "hello");
    press(ed, EventType::MoveHome);
    press(ed, EventType::Select);
    press(ed, EventType::MoveRight);          // [h]
    press(ed, EventType::Delete);
    CHECK_EQ(ed.document_.lineAt(0), "ello");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Normal));
    CHECK(!ed.hasSelection());
}

TEST(select_save_without_exiting) {
    // Ctrl+K, Ctrl+S dentro del modo seleccion guarda y mantiene la
    // seleccion activa.
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "hello");
    press(ed, EventType::MoveHome);
    press(ed, EventType::Select);
    press(ed, EventType::MoveRight);
    CHECK(ed.hasSelection());
    CHECK(ed.modified_);

    prefix(ed, EventType::Prefix, EventType::Select); // Ctrl+K, Ctrl+S
    CHECK(!ed.modified_);
    CHECK_EQ(fileContent(f.path), "hello");
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Select));
}