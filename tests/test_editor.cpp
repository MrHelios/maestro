#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

#include "test_framework.h"

// Incluimos los headers estandar ANTES de abrir las visibilidades privadas
// para que <string>/<vector> mantengan su layout. Solo Editor se expone.
#include <string>
#include <vector>
#define private public
#include "Editor.h"
#undef private

static std::string tmpPath() {
    static int n = 0;
    return "/tmp/edit_ed_" + std::to_string(static_cast<long>(::getpid())) + "_" +
           std::to_string(n++) + ".txt";
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

static std::string fileContent(const std::string& p) {
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// 1. Inicio del programa / Abrir archivo
// ---------------------------------------------------------------------------
TEST(editor_start_empty) {
    Editor ed;
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK(!ed.modified_);
}

TEST(editor_open_new_file_empty) {
    std::string p = tmpPath();
    std::remove(p.c_str());
    Editor ed;
    CHECK(!ed.openFile(p));
    CHECK(!ed.modified_);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.document_.lineCount(), 1);
}

TEST(editor_open_existing) {
    std::string p = tmpPath();
    { std::ofstream f(p, std::ios::trunc); f << "one\ntwo\n"; }
    Editor ed;
    CHECK(ed.openFile(p));
    CHECK(!ed.modified_);
    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.document_.lineAt(1), "two");
    std::remove(p.c_str());
}

// ---------------------------------------------------------------------------
// 10. Undo
// ---------------------------------------------------------------------------
TEST(editor_undo_insertion) {
    Editor ed;
    type(ed, "x");
    CHECK_EQ(ed.document_.lineAt(0), "x");
    ed.handleEvent(Event{EventType::Undo});
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_undo_backspace) {
    Editor ed;
    type(ed, "a");
    CHECK_EQ(ed.document_.lineAt(0), "a");
    ed.handleEvent(Event{EventType::Backspace}); // borra "a"
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK(ed.modified_);
    ed.handleEvent(Event{EventType::Undo});
    CHECK_EQ(ed.document_.lineAt(0), "a");
}

TEST(editor_undo_delete) {
    Editor ed;
    type(ed, "abc");
    ed.handleEvent(Event{EventType::MoveLeft}); // cursor en col 2
    ed.handleEvent(Event{EventType::Delete});   // borra el 'c'
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    ed.handleEvent(Event{EventType::Undo});
    CHECK_EQ(ed.document_.lineAt(0), "abc");
}

TEST(editor_undo_newline) {
    Editor ed;
    type(ed, "a");
    ed.handleEvent(Event{EventType::InsertNewline});
    CHECK_EQ(ed.document_.lineCount(), 2);
    ed.handleEvent(Event{EventType::Undo});
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "a");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 1);
}

TEST(editor_undo_multiple) {
    Editor ed;
    type(ed, "abc");
    ed.handleEvent(Event{EventType::Undo});
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    ed.handleEvent(Event{EventType::Undo});
    CHECK_EQ(ed.document_.lineAt(0), "a");
    ed.handleEvent(Event{EventType::Undo});
    CHECK_EQ(ed.document_.lineAt(0), "");
}

TEST(editor_undo_empty_noop) {
    Editor ed;
    ed.handleEvent(Event{EventType::Undo});
    CHECK_EQ(ed.document_.lineAt(0), "");
}

TEST(editor_undo_everything) {
    Editor ed;
    type(ed, "hello");
    for (int i = 0; i < 6; ++i)
        ed.handleEvent(Event{EventType::Undo});
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
}

// ---------------------------------------------------------------------------
// 11. Redo
// ---------------------------------------------------------------------------
TEST(editor_redo_after_undo) {
    Editor ed;
    type(ed, "x");
    ed.handleEvent(Event{EventType::Undo});
    CHECK_EQ(ed.document_.lineAt(0), "");
    ed.handleEvent(Event{EventType::Redo});
    CHECK_EQ(ed.document_.lineAt(0), "x");
}

TEST(editor_redo_several) {
    Editor ed;
    type(ed, "abc");
    ed.handleEvent(Event{EventType::Undo});
    ed.handleEvent(Event{EventType::Undo});
    CHECK_EQ(ed.document_.lineAt(0), "a");
    ed.handleEvent(Event{EventType::Redo});
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    ed.handleEvent(Event{EventType::Redo});
    CHECK_EQ(ed.document_.lineAt(0), "abc");
}

TEST(editor_redo_invalidated_by_new_change) {
    Editor ed;
    type(ed, "a");
    ed.handleEvent(Event{EventType::Undo});
    ed.handleEvent(insert('Z'));
    ed.handleEvent(Event{EventType::Redo});
    CHECK_EQ(ed.document_.lineAt(0), "Z");
}

TEST(editor_redo_empty_noop) {
    Editor ed;
    ed.handleEvent(Event{EventType::Redo});
    CHECK_EQ(ed.document_.lineAt(0), "");
}

// ---------------------------------------------------------------------------
// 12. Guardado (nivel Editor / modified_)
// ---------------------------------------------------------------------------
TEST(editor_save_new_document) {
    std::string p = tmpPath();
    std::remove(p.c_str());
    Editor ed;
    ed.openFile(p);
    type(ed, "Hi");
    CHECK(ed.modified_);
    ed.handleEvent(Event{EventType::Save});
    CHECK(!ed.modified_);
    CHECK_EQ(fileContent(p), "Hi");
    std::remove(p.c_str());
}

TEST(editor_save_empty_document) {
    std::string p = tmpPath();
    Editor ed;
    ed.openFile(p);
    ed.handleEvent(Event{EventType::Save});
    CHECK(fileContent(p).empty());
    std::remove(p.c_str());
}

TEST(editor_save_error_path) {
    Editor ed;
    ed.openFile("/no/such/dir/file.txt");
    type(ed, "a");
    CHECK(ed.modified_);
    ed.handleEvent(Event{EventType::Save});
    CHECK(ed.modified_);
}

TEST(editor_modified_until_undo_then_redo) {
    Editor ed;
    type(ed, "x");
    CHECK(ed.modified_);
    ed.handleEvent(Event{EventType::Undo});
    CHECK(ed.modified_);
}

// ---------------------------------------------------------------------------
// 13. Quit
// ---------------------------------------------------------------------------
TEST(editor_quit) {
    Editor ed;
    CHECK(ed.running_);
    ed.handleEvent(Event{EventType::Quit});
    CHECK(!ed.running_);
}

TEST(editor_quit_after_save) {
    std::string p = tmpPath();
    Editor ed;
    ed.openFile(p);
    type(ed, "a");
    ed.handleEvent(Event{EventType::Save});
    ed.handleEvent(Event{EventType::Quit});
    CHECK(!ed.running_);
    std::remove(p.c_str());
}