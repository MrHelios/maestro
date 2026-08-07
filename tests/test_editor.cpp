#include <fstream>
#include <iterator>
#include <string>

#include "test_framework.h"

// Incluimos los headers estandar ANTES de abrir las visibilidades privadas
// para que <string>/<vector> mantengan su layout. Solo Editor se expone.
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

static std::string fileContent(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
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

TEST(editor_open_nonexistent) {
    // TempFile por defecto NO crea el archivo (write() es lo que lo crea),
    // asi que esta ruta no existe en disco. openFile() devuelve false.
    TempFile f;
    Editor ed;
    CHECK(!ed.openFile(f.path));
    CHECK(!ed.modified_);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK_EQ(ed.document_.lineCount(), 1);
}

TEST(editor_open_existing_empty_file) {
    // El archivo existe pero esta vacio: openFile() devuelve true
    // (el false solo significa "no existia", no "vacio").
    TempFile f;
    f.write("");
    Editor ed;
    CHECK(ed.openFile(f.path));
    CHECK(!ed.modified_);
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "");
}

TEST(editor_open_existing) {
    TempFile f;
    f.write("one\ntwo\n");
    Editor ed;
    CHECK(ed.openFile(f.path));
    CHECK(!ed.modified_);
    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.document_.lineAt(1), "two");
}

// ---------------------------------------------------------------------------
// 2. Navegacion del cursor a nivel Editor (via eventos)
// ---------------------------------------------------------------------------
TEST(editor_move_left) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveLeft);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 2);
}

TEST(editor_move_right) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveLeft);
    press(ed, EventType::MoveRight);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);
}

TEST(editor_move_left_at_start_noop) {
    Editor ed;
    press(ed, EventType::MoveLeft);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_move_right_at_end_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveRight);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);
}

TEST(editor_move_left_wraps_to_previous_line) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline); // cursor en (1,0)
    press(ed, EventType::MoveLeft);      // salta al final de la linea anterior
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);
}

TEST(editor_move_right_wraps_to_next_line) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline); // cursor en (1,0)
    press(ed, EventType::MoveLeft);      // -> (0,3)
    press(ed, EventType::MoveRight);     // -> (1,0)
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_move_up_at_top_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveUp);
    CHECK_EQ(ed.cursor_.line, 0);
}

TEST(editor_move_down_at_bottom_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline); // dos lineas, cursor en (1,0)
    press(ed, EventType::MoveDown);
    CHECK_EQ(ed.cursor_.line, 1);
}

TEST(editor_move_up_changes_line) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline); // cursor en (1,0)
    press(ed, EventType::MoveUp);        // -> (0,0)
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_move_down_changes_line) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline); // cursor en (1,0)
    press(ed, EventType::MoveUp);        // -> (0,0)
    press(ed, EventType::MoveDown);      // -> (1,0)
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_move_home) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveEnd);
    press(ed, EventType::MoveHome);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_move_end) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveEnd);
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);
}

TEST(editor_move_up_down) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);
    type(ed, "def");
    press(ed, EventType::MoveUp);
    CHECK_EQ(ed.cursor_.line, 0);
    press(ed, EventType::MoveDown);
    CHECK_EQ(ed.cursor_.line, 1);
}

TEST(editor_vertical_clamps_to_shorter_line) {
    // "abcdef" / "xy": bajar desde col 5 debe aterrizar en col 2 (fin de "xy").
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::InsertNewline); // (1,0), line0 sigue "abcdef"
    type(ed, "xy");                      // line1="xy", cursor (1,2)
    press(ed, EventType::MoveUp);        // -> (0,0)
    for (int i = 0; i < 5; ++i)
        press(ed, EventType::MoveRight); // -> (0,5), preferredCol=5
    press(ed, EventType::MoveDown);      // -> (1,2): col se clampa a 2
    CHECK_EQ(ed.cursor_.line, 1);
    CHECK_EQ(ed.cursor_.col, 2);
}

TEST(editor_vertical_remembers_preferred_column) {
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::InsertNewline); // line0 sigue "abcdef"
    type(ed, "xy");                      // line1="xy", cursor (1,2)
    press(ed, EventType::MoveUp);        // -> (0,0)
    for (int i = 0; i < 5; ++i)
        press(ed, EventType::MoveRight); // -> (0,5), preferredCol=5
    press(ed, EventType::MoveDown);      // -> (1,2), preferredCol sigue 5
    press(ed, EventType::MoveUp);        // -> (0,5): recupera la columna deseada
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 5);
}

// ---------------------------------------------------------------------------
// 3. Insercion de caracteres (contrato via handleEvent)
// ---------------------------------------------------------------------------
TEST(editor_insert_character) {
    Editor ed;
    ed.handleEvent(insert('A'));
    ed.handleEvent(insert('B'));
    CHECK_EQ(ed.document_.lineAt(0), "AB");
    CHECK_EQ(ed.cursor_.col, 2);
    CHECK(ed.modified_);
}

TEST(editor_insert_inserts_at_cursor_position) {
    Editor ed;
    type(ed, "ac");
    press(ed, EventType::MoveLeft);       // cursor en col 1
    ed.handleEvent(insert('b'));          // inserta en medio
    CHECK_EQ(ed.document_.lineAt(0), "abc");
    CHECK_EQ(ed.cursor_.col, 2);
    CHECK(ed.modified_);
}

TEST(editor_insert_marks_modified) {
    Editor ed;
    CHECK(!ed.modified_);
    ed.handleEvent(insert('x'));
    CHECK(ed.modified_);
}

// ---------------------------------------------------------------------------
// 4. Backspace / Delete
// ---------------------------------------------------------------------------
TEST(editor_backspace) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    CHECK_EQ(ed.cursor_.col, 2);
    CHECK(ed.modified_);
}

TEST(editor_backspace_at_line_start_joins) {
    // "abc" / "def" -> backspace al inicio de "def" une las lineas.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);
    type(ed, "def");

    press(ed, EventType::MoveHome);       // cursor al inicio de "def" (1,0)
    press(ed, EventType::Backspace);      // une las dos lineas

    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "abcdef");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);          // justo donde terminaba "abc"
    CHECK(ed.modified_);
}

TEST(editor_backspace_join_then_undo_restores) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);
    type(ed, "def");

    press(ed, EventType::MoveHome);
    press(ed, EventType::Backspace);      // une: "abcdef"
    press(ed, EventType::Undo);           // restaura la division

    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.document_.lineAt(0), "abc");
    CHECK_EQ(ed.document_.lineAt(1), "def");
}

TEST(editor_backspace_at_absolute_start_noop) {
    Editor ed;
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
    CHECK(!ed.modified_);
}

TEST(editor_delete) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveLeft);       // cursor en col 2
    press(ed, EventType::Delete);         // borra la "c"
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    CHECK_EQ(ed.cursor_.col, 2);
    CHECK(ed.modified_);
}

TEST(editor_delete_at_line_end_joins_next) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);
    type(ed, "def");

    press(ed, EventType::MoveUp);         // -> renglón 0
    press(ed, EventType::MoveEnd);        // cursor al final de "abc" (0,3)
    press(ed, EventType::Delete);         // une la línea siguiente

    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "abcdef");
    // Delete no mueve el cursor: queda en la union (donde empezaba "def").
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);
    CHECK(ed.modified_);
}

TEST(editor_delete_at_end_of_document_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveEnd);
    press(ed, EventType::Delete);
    CHECK_EQ(ed.document_.lineAt(0), "abc");
}

// ---------------------------------------------------------------------------
// 10. Undo
// ---------------------------------------------------------------------------
TEST(editor_undo_insertion) {
    Editor ed;
    type(ed, "x");
    CHECK_EQ(ed.document_.lineAt(0), "x");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_undo_backspace) {
    Editor ed;
    type(ed, "a");
    CHECK_EQ(ed.document_.lineAt(0), "a");
    press(ed, EventType::Backspace); // borra "a"
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK(ed.modified_);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "a");
}

TEST(editor_undo_delete) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveLeft); // cursor en col 2
    press(ed, EventType::Delete);   // borra el 'c'
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "abc");
}

TEST(editor_undo_newline) {
    Editor ed;
    type(ed, "a");
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.document_.lineCount(), 2);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "a");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 1);
}

TEST(editor_undo_mixed_operations) {
    // Contrato: cada InsertChar / InsertNewline genera su propia entrada.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);
    type(ed, "def");

    CHECK_EQ(ed.document_.lineCount(), 2);
    CHECK_EQ(ed.document_.lineAt(1), "def");

    for (int step = 0; step < 3; ++step)   // 'f' -> 'e' -> 'd'
        press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(1), "");

    press(ed, EventType::Undo);            // deshace el Enter
    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "abc");

    for (int step = 0; step < 3; ++step)   // 'c' -> 'b' -> 'a'
        press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "");

    press(ed, EventType::Undo);            // ya no hay nada que deshacer
    CHECK_EQ(ed.document_.lineAt(0), "");
}

TEST(editor_undo_back_to_start_of_mixed) {
    // Deshacer todo el tecleo mixto debe dejar el documento exactamente como
    // estaba al principio (una línea vacía, cursor 0,0).
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);
    type(ed, "def");
    press(ed, EventType::MoveHome);
    press(ed, EventType::Backspace);       // une: "abcdef"

    for (int i = 0; i < 20; ++i)
        press(ed, EventType::Undo);

    CHECK_EQ(ed.document_.lineCount(), 1);
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 0);
}

TEST(editor_undo_multiple) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "a");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "");
}

TEST(editor_undo_empty_noop) {
    Editor ed;
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "");
}

TEST(editor_undo_everything) {
    Editor ed;
    type(ed, "hello");
    for (int i = 0; i < 6; ++i)
        press(ed, EventType::Undo);
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
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "");
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "x");
}

TEST(editor_redo_several) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Undo);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), "a");
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "ab");
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "abc");
}

TEST(editor_redo_invalidated_by_new_change) {
    Editor ed;
    type(ed, "a");
    press(ed, EventType::Undo);
    ed.handleEvent(insert('Z'));
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "Z");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 1);
    // Un Redo adicional no recupera la rama descartada: sigue en "Z".
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "Z");
    CHECK_EQ(ed.cursor_.col, 1);
}

TEST(editor_redo_empty_noop) {
    Editor ed;
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "");
}

// ---------------------------------------------------------------------------
// 12. Guardado (nivel Editor / modified_)
// ---------------------------------------------------------------------------
TEST(editor_save_new_document) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "Hi");
    CHECK(ed.modified_);
    press(ed, EventType::Save);
    CHECK(!ed.modified_);
    CHECK_EQ(fileContent(f.path), "Hi");
}

TEST(editor_save_empty_document) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    press(ed, EventType::Save);
    CHECK(fileContent(f.path).empty());
}

TEST(editor_save_overwrites_content) {
    // Guardar reemplaza el contenido anterior: no append, no mezcla.
    TempFile f;
    f.write("old");

    Editor ed;
    CHECK(ed.openFile(f.path));

    // El cursor abre en (0,0): muevo al final, borro "old" y escribo "new".
    press(ed, EventType::MoveEnd);
    for (int i = 0; i < 3; ++i)
        press(ed, EventType::Backspace);
    type(ed, "new");
    press(ed, EventType::Save);

    CHECK(!ed.modified_);
    CHECK_EQ(fileContent(f.path), "new");

    Editor reloaded;
    CHECK(reloaded.openFile(f.path));
    CHECK_EQ(reloaded.document_.lineCount(), 1);
    CHECK_EQ(reloaded.document_.lineAt(0), "new");
}

TEST(editor_save_error_path) {
    Editor ed;
    ed.openFile("/no/such/dir/file.txt");
    type(ed, "a");
    CHECK(ed.modified_);
    press(ed, EventType::Save);
    CHECK(ed.modified_);
}

TEST(editor_modified_after_change_after_save) {
    // modified_ debe reflejar "difiere del ultimo guardado", no "se toco alguna vez".
    //   abrir:   ""   modified=false
    //   editar  "a"   modified=true  -> guardar -> modified=false
    //   editar  "ab"  modified=true
    //   undo    "a"   modified=false (vuelve al contenido guardado)
    TempFile f;
    Editor ed;
    ed.openFile(f.path);

    type(ed, "a");
    press(ed, EventType::Save);
    CHECK(!ed.modified_);

    type(ed, "b");
    CHECK(ed.modified_);

    press(ed, EventType::Undo);
    CHECK(!ed.modified_);
}

TEST(editor_modified_undo_redo) {
    // Redo también afecta modified_: rehacer un cambio que difiere del
    // último guardado vuelve a marcarlo.
    //   insert "a"; save -> ""            modified=false
    //   insert "b"         -> "ab"        modified=true
    //   undo               -> "a"         modified=false
    //   redo               -> "ab"        modified=true
    TempFile f;
    Editor ed;
    ed.openFile(f.path);

    type(ed, "a");
    press(ed, EventType::Save);
    CHECK(!ed.modified_);

    type(ed, "b");
    CHECK(ed.modified_);

    press(ed, EventType::Undo);
    CHECK(!ed.modified_);

    press(ed, EventType::Redo);
    CHECK(ed.modified_);
}

TEST(editor_modified_until_undo_then_redo) {
    Editor ed;
    type(ed, "x");
    CHECK(ed.modified_);
    // Undo vuelve al estado inicial (igual al guardado): modified_ se limpia.
    press(ed, EventType::Undo);
    CHECK(!ed.modified_);
    press(ed, EventType::Redo);
    CHECK(ed.modified_);
}

// ---------------------------------------------------------------------------
// 13. Quit
// ---------------------------------------------------------------------------
TEST(editor_quit) {
    Editor ed;
    CHECK(ed.running_);
    press(ed, EventType::Quit);
    CHECK(!ed.running_);
}

TEST(editor_quit_after_save) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "a");
    press(ed, EventType::Save);
    press(ed, EventType::Quit);
    CHECK(!ed.running_);
}

TEST(editor_quit_with_unsaved_changes) {
    // Contrato actual: Quit sale SIEMPRE, sin preguntar por cambios sin
    // guardar. Si algun dia se agrega un aviso, este test debe cambiar.
    Editor ed;
    type(ed, "a");
    CHECK(ed.modified_);
    press(ed, EventType::Quit);
    CHECK(!ed.running_);
}