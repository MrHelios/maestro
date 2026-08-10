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
#include "utf8.h"

using testfw::TempFile;

static Event insert(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

static Event insertBytes(const std::string& text) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = text;
    return e;
}

// Cuantos bytes UTF-8 ocupa el caracter cuyo byte de inicio es `b`.
static int utf8Len(unsigned char b) {
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

static void type(Editor& ed, const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        int len = utf8Len(static_cast<unsigned char>(s[i]));
        ed.handleEvent(insertBytes(s.substr(i, static_cast<size_t>(len))));
        i += static_cast<size_t>(len);
    }
}

static void press(Editor& ed, EventType type) {
    Event e;
    e.type = type;
    ed.handleEvent(e);
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

TEST(editor_open_relative_resolves_absolute) {
    // v0.4: la barra de estado muestra siempre ruta absoluta. Abrir con
    // ruta relativa la resuelve contra cwd(). El archivo no existe, asi
    // que no se toca disco y el nombre queda resuelto.
    Editor ed;
    CHECK(!ed.openFile("archivo_rel_zz_no_existe.txt"));

    char cwd[4096];
    CHECK(getcwd(cwd, sizeof cwd) != nullptr);
    CHECK_EQ(ed.filename_, std::string(cwd) + "/archivo_rel_zz_no_existe.txt");
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
TEST(editor_quit_illegals_without_prefix) {
    // Contrato v0.3 (seguridad): un Quit suelto (sin Ctrl+K antes)
    // NO sale del editor. Debe pasarse por el prefijo (Ctrl+K -> Ctrl+Q).
    Editor ed;
    CHECK(ed.running_);
    press(ed, EventType::Quit);
    CHECK(ed.running_);
}

TEST(editor_quit_after_save_via_prefix) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "a");
    press(ed, EventType::Save);
    // Quit suelto no sale...
    press(ed, EventType::Quit);
    CHECK(ed.running_);
    // ...pero con el prefijo (Ctrl+K -> Ctrl+Q) si.
    press(ed, EventType::Prefix);
    press(ed, EventType::Quit);
    CHECK(!ed.running_);
}

TEST(editor_quit_with_unsaved_changes_via_prefix) {
    // Contrato v0.3: Quit no sale suelto; sale solo via prefijo. Sigue sin
    // preguntar por cambios sin guardar. Si algun dia se agrega un aviso,
    // este test debe cambiar.
    Editor ed;
    type(ed, "a");
    CHECK(ed.modified_);
    press(ed, EventType::Quit);
    CHECK(ed.running_);
    press(ed, EventType::Prefix);
    press(ed, EventType::Quit);
    CHECK(!ed.running_);
}

// ---------------------------------------------------------------------------
// 14. Cursor despues de una seleccion UTF-8 (Editor + Renderer de la mano).
// Verifica que Editor y Renderer coinciden en la posicion del cursor: el
// Editor deja cursor_.col como offset de bytes y el Renderer lo dibuja como
// COLUMNA VISUAL (columnOf). Para "abcédef" borrar/reemplazar "[éde]".
// ---------------------------------------------------------------------------
namespace {

// Columna VISUAL (1-based, la de la secuencia "\x1b[1;<col>H") a la que el
// Renderer moveria el cursor para `line` con cursor en el byte `byteCol`.
int cursorScreenCol(const std::string& line, int byteCol) {
    Document doc;
    doc.restore({line});
    Viewport vp;
    vp.top = 0; vp.height = 1; vp.width = 200;
    Cursor c;
    c.line = 0; c.col = byteCol;
    Renderer r;
    std::string f = r.buildScreen(doc, c, vp, "t", false, "", State::Normal, std::nullopt);
    size_t pos = f.rfind("\x1b[1;");
    if (pos == std::string::npos) return -1;
    size_t end = f.find('H', pos);
    return std::stoi(f.substr(pos + 4, end - pos - 4));
}

// Prepara un Editor con "abcédef" y la seleccion "[éde]" (bytes 3..7).
void setupWithEde(Editor& ed) {
    ed.document_.restore({"abc\xc3\xa9" "def"});
    ed.selection_ = Selection{};
    ed.selection_->anchor   = {0, 3};
    ed.selection_->position = {0, 7};
    ed.state_ = State::Select;
    ed.cursor_.line = 0;
    ed.cursor_.col = 7;
}

} // namespace

TEST(editor_delete_selection_utf8_cursor_position) {
    // "[éde]" -> Delete. Resultado "abcf"; el cursor queda como offset de
    // bytes 3 (tras 'c') que el Renderer dibuja en la columna visual 3.
    Editor ed;
    setupWithEde(ed);
    press(ed, EventType::Delete);

    CHECK_EQ(ed.document_.lineAt(0), "abcf");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 3);          // contrato del Editor
    CHECK(!ed.hasSelection());

    // El Renderer lo pinta en la columna visual correcta: tras 'c', antes
    // de 'f' -> columna 3 (0-indexada) -> secuencia 1;4H. Con un offset de
    // bytes mal usado, "abcf" tiene 4 bytes pero aqui NO da 4+1.
    CHECK_EQ(ed.cursor_.col, static_cast<int>(utf8::columnOf(ed.document_.lineAt(0), 3)));
}

TEST(editor_replace_selection_utf8_cursor_position) {
    // "[éde]" -> 'X'. Resultado "abcXf"; el cursor justo despues de 'X'.
    Editor ed;
    setupWithEde(ed);
    ed.handleEvent(insert('X'));

    CHECK_EQ(ed.document_.lineAt(0), "abcXf");
    CHECK_EQ(ed.cursor_.line, 0);
    CHECK_EQ(ed.cursor_.col, 4);          // byte offset inmediatamente tras la X
    CHECK(!ed.hasSelection());

    // El Renderer dibuja el cursor justo despues de la X (columna visual 4
    // -> secuencia 1;5H), no despues de contar bytes.
    CHECK_EQ(cursorScreenCol(ed.document_.lineAt(0), ed.cursor_.col), 5);
}

// ---------------------------------------------------------------------------
// 15. UTF-8 + Undo/Redo. El historial guarda lineas + cursor + seleccion
// (posiciones en BYTES). Para "abc[é—😀]def" verificar Delete y Replace con
// su ciclo Undo/Redo: contenido, cursor, seleccion y posiciones visuales.
// ---------------------------------------------------------------------------
namespace {

// "abcé—😀def": a b c é — 😀 d e f. El bloque "[é—😀]" son los bytes 3..12.
const char* edemo = "abc\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80" "def";

void setupDemo(Editor& ed) {
    ed.document_.restore({edemo});
    ed.selection_ = Selection{};
    ed.selection_->anchor   = {0, 3};
    ed.selection_->position = {0, 12};
    ed.state_ = State::Select;
    ed.cursor_.line = 0;
    ed.cursor_.col = 12;
}

} // namespace

TEST(editor_undo_redo_delete_selection_utf8) {
    Editor ed;
    setupDemo(ed);

    // Delete: borra "[é—😀]".
    press(ed, EventType::Delete);
    CHECK_EQ(ed.document_.lineAt(0), "abcdef");
    CHECK_EQ(ed.cursor_.col, 3);          // inicio del rango borrado
    CHECK(!ed.hasSelection());
    CHECK_EQ(cursorScreenCol("abcdef", 3), 4);

    // Undo: vuelve "abcé—😀def" restaurando la seleccion completa.
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), edemo);
    CHECK_EQ(ed.cursor_.col, 12);         // se restaura la posicion previa
    CHECK(ed.hasSelection());
    auto sel = ed.selection();            // empieza y termina exactamente en el bloque
    CHECK(sel.has_value());
    CHECK(sel->start.col == 3 && sel->end.col == 12);
    // El cursor se sigue dibujando con la columna VISUAL (tras "😀"),
    // no con el offset de bytes.
    CHECK_EQ(cursorScreenCol(edemo, ed.cursor_.col), 7);

    // Redo: repite el borrado.
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "abcdef");
    CHECK_EQ(ed.cursor_.col, 3);
    CHECK(!ed.hasSelection());
}

TEST(editor_undo_redo_replace_selection_utf8) {
    Editor ed;
    setupDemo(ed);

    // Replace "[é—😀]" -> 'X'.
    ed.handleEvent(insert('X'));
    CHECK_EQ(ed.document_.lineAt(0), "abcXdef");
    CHECK_EQ(ed.cursor_.col, 4);          // justo despues de la X
    CHECK(!ed.hasSelection());
    CHECK_EQ(cursorScreenCol("abcXdef", 4), 5);

    // Undo: restaura el texto UTF-8 y la seleccion de 3 multibyte.
    press(ed, EventType::Undo);
    CHECK_EQ(ed.document_.lineAt(0), edemo);
    CHECK_EQ(ed.cursor_.col, 12);
    CHECK(ed.hasSelection());
    auto sel = ed.selection();
    CHECK(sel.has_value());
    CHECK(sel->start.col == 3 && sel->end.col == 12);
    CHECK_EQ(cursorScreenCol(edemo, ed.cursor_.col), 7);

    // Redo: vuelve a "abcXdef" con el cursor tras la X.
    press(ed, EventType::Redo);
    CHECK_EQ(ed.document_.lineAt(0), "abcXdef");
    CHECK_EQ(ed.cursor_.col, 4);
    CHECK(!ed.hasSelection());
    CHECK_EQ(cursorScreenCol("abcXdef", 4), 5);
}

// ---------------------------------------------------------------------------
// 16. Cursor desplazandose por caracteres UTF-8 CONSECUTIVOS ("éééé",
// "😀😀😀", "———"). moveLeft/moveRight debe saltar los bytes de continuacion
// y caer SIEMPRE en el lead byte del caracter siguiente/anterior (nunca
// "dentro" de uno). En diesen casos no hay ASCII que enmascare un byte-step.
// ---------------------------------------------------------------------------
TEST(editor_cursor_moves_char_by_char_consecutive_utf8) {
    // (line, nbytes): cada caracter pesa nbytes y son todos adyacentes.
    struct Case { const char* line; int nbytes; } cases[] = {
        {"\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", 2},          // éééé
        {"\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80", 4}, // 😀😀😀
        {"\xe2\x80\x94\xe2\x80\x94\xe2\x80\x94", 3},        // ———
    };
    for (const Case& cs : cases) {
        Editor ed;
        ed.document_.restore({cs.line});
        ed.cursor_.line = 0;
        ed.cursor_.col = 0;

        const int nchars = static_cast<int>(std::string(cs.line).size()) / cs.nbytes;

        // Avanzar de a un caracter: 0 -> nbytes -> 2*nbytes -> ... -> fin.
        for (int i = 1; i <= nchars; ++i) {
            press(ed, EventType::MoveRight);
            CHECK_EQ(ed.cursor_.col, cs.nbytes * i);
        }
        // Al final, mover derecha no pasa del largo (no "entra" en nul).
        const int endByte = static_cast<int>(std::string(cs.line).size());
        press(ed, EventType::MoveRight);
        CHECK_EQ(ed.cursor_.col, endByte);

        // Volver: fin -> ... -> 2*nbytes -> nbytes -> 0.
        for (int i = nchars - 1; i >= 1; --i) {
            press(ed, EventType::MoveLeft);
            CHECK_EQ(ed.cursor_.col, cs.nbytes * i);
        }
        press(ed, EventType::MoveLeft);
        CHECK_EQ(ed.cursor_.col, 0);
    }
}

// ---------------------------------------------------------------------------
// 17. Escribir caracteres acentuados/multibyte ("á", "ñ") via el teclado.
// Histórico: Terminal descartaba todo byte >= 0x80 como None, asi que no se
// podia tipear acentos. Ahora cada caracter UTF-8 entra como UN InsertChar.
// ---------------------------------------------------------------------------
TEST(editor_type_accented_multibyte) {
    Editor ed;
    // Tipear "ñ" (0xC3 0xB1) y "á" (0xC3 0xA1) como un solo evento cada uno.
    ed.handleEvent(insertBytes("\xc3\xb1"));
    CHECK_EQ(ed.document_.lineAt(0), "\xc3\xb1");
    CHECK_EQ(ed.cursor_.col, 2); // avanza los 2 bytes del caracter

    ed.handleEvent(insertBytes("\xc3\xa1"));
    CHECK_EQ(ed.document_.lineAt(0), "\xc3\xb1\xc3\xa1"); // "ñá"
    CHECK_EQ(ed.cursor_.col, 4);

    // El render de la linea es UTF-8 valido y el cursor se dibuja en la
    // columna visual 2 (los 2 caracteres), no en la 4 (los bytes).
    CHECK_EQ(cursorScreenCol(ed.document_.lineAt(0), ed.cursor_.col), 3);
}

TEST(editor_type_helper_groups_multibyte) {
    // El helper type() agrupa los bytes por caracter: escribir "ño" en dos
    // pasos produce dos caracteres completos, no cuatro bytes sueltos.
    Editor ed;
    type(ed, "\xc3\xb1o");
    CHECK_EQ(ed.document_.lineAt(0), "\xc3\xb1o");
    CHECK_EQ(ed.cursor_.col, 3); // 2 bytes de "ñ" + 1 de 'o'
}

TEST(document_insert_text_keeps_whole_char) {
    Document doc;
    doc.restore({""});
    doc.insertText(0, 0, "\xc3\xa1"); // "á" de 2 bytes, una sola operacion
    CHECK_EQ(doc.lineAt(0), "\xc3\xa1");
    doc.insertText(0, 2, "\xc3\xa9"); // "é" despues de "á" (limite de caracter)
    CHECK_EQ(doc.lineAt(0), "\xc3\xa1\xc3\xa9"); // "áé"
    doc.insertChar(0, 0, 'X');        // un ASCII normal igual funciona
    CHECK_EQ(doc.lineAt(0), "X\xc3\xa1\xc3\xa9");
}

TEST(editor_backspace_removes_whole_multibyte) {
    // Backspace sobre un caracter multibyte debe borrarlo COMPLETO y dejar
    // el cursor en un limite de caracter (nunca un byte de continuacion).
    Editor ed;
    ed.document_.restore({"\xc3\xb1\xc3\xa1"}); // "ñá"
    ed.cursor_.line = 0;
    ed.cursor_.col = 4; // tras "á"
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.document_.lineAt(0), "\xc3\xb1"); // queda "ñ"
    CHECK_EQ(ed.cursor_.col, 2); // limite tras "ñ", no un byte suelto

    press(ed, EventType::Backspace);
    CHECK_EQ(ed.document_.lineAt(0), "");
    CHECK_EQ(ed.cursor_.col, 0);
}