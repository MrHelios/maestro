#include <cstdlib>
#include <filesystem>
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
#include "ui/Editor.h"
#undef private
#include "core/utf8.h"

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

// Abre el modo Interaccion (presiona 'i') si no estamos ya en el: es lo
// que en v0.5 permite escribir libremente.
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
    enterInteraccion(ed);
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

// v0.5: un Save suelto (Ctrl+S) es no-op fuera del prefijo. Guardar pasa
// obligatoriamente por Ctrl+K -> Ctrl+S (Prefix -> Save).
static void save(Editor& ed) {
    press(ed, EventType::Prefix);
    Event e; e.type = EventType::Save; ed.handleEvent(e);
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
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.active().modified);
}

TEST(editor_open_nonexistent) {
    // TempFile por defecto NO crea el archivo (write() es lo que lo crea),
    // asi que esta ruta no existe en disco. openFile() devuelve false.
    TempFile f;
    Editor ed;
    CHECK(!ed.openFile(f.path));
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK_EQ(ed.active().document.lineCount(), 1);
}

TEST(editor_open_existing_empty_file) {
    // El archivo existe pero esta vacio: openFile() devuelve true
    // (el false solo significa "no existia", no "vacio").
    TempFile f;
    f.write("");
    Editor ed;
    CHECK(ed.openFile(f.path));
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
}

TEST(editor_open_existing) {
    TempFile f;
    f.write("one\ntwo\n");
    Editor ed;
    CHECK(ed.openFile(f.path));
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(1), "two");
}

TEST(editor_open_relative_resolves_absolute) {
    // v0.4: la barra de estado muestra siempre ruta absoluta. Abrir con
    // ruta relativa la resuelve contra cwd(). El archivo no existe, asi
    // que no se toca disco y el nombre queda resuelto.
    Editor ed;
    CHECK(!ed.openFile("archivo_rel_zz_no_existe.txt"));

    char cwd[4096];
    CHECK(getcwd(cwd, sizeof cwd) != nullptr);
    CHECK_EQ(ed.active().filename, std::string(cwd) + "/archivo_rel_zz_no_existe.txt");
}

// Directorio temporal (mkdtemp) que se borra al salir, aunque un CHECK falle.
struct TempDir {
    std::string path;

    TempDir() {
        char tmpl[] = "/tmp/edit_test_dir_XXXXXX";
        char* p = mkdtemp(tmpl);
        path = p ? std::string(p) : std::string();
    }

    ~TempDir() {
        if (!path.empty())
            rmdir(path.c_str());
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

TEST(editor_open_normalizes_dotdot) {
    // Abrir "dir/sub/../a.txt" (con "..") debe guardar el filename
    // NORMALIZADO "dir/a.txt", no la cadena cruda con "..". Asi dos rutas
    // que escriben el mismo archivo de forma distinta quedan iguales para
    // el chequeo de duplicados de buffers.
    TempDir dir;
    CHECK(!dir.path.empty());
    std::filesystem::create_directory(dir.path + "/sub");
    {
        std::ofstream(dir.path + "/a.txt", std::ios::binary) << "x";
    }

    Editor ed;
    // El OS resuelve "sub/.." para abrir el archivo real dir/a.txt.
    CHECK(ed.openFile(dir.path + "/sub/../a.txt"));
    CHECK_EQ(ed.active().filename, dir.path + "/a.txt");
}

TEST(editor_open_normalizes_dot) {
    // Un "." dentro de la ruta tambien se reduce.
    TempFile f;
    f.write("x");
    std::filesystem::path fp(f.path);
    const std::string parent = fp.parent_path().string(); // /tmp
    const std::string name = fp.filename().string();
    const std::string withDot = parent + "/./" + name;

    Editor ed;
    CHECK(ed.openFile(withDot));
    CHECK_EQ(ed.active().filename, parent + "/" + name);
}

// ---------------------------------------------------------------------------
// v0.6.2: abrir por ruta absoluta; las carpetas se rechazan
// ---------------------------------------------------------------------------
TEST(is_directory_true_for_folder) {
    TempDir dir;
    CHECK(!dir.path.empty());
    CHECK(Editor::isDirectory(dir.path));
    CHECK(Editor::isDirectory("."));
}

TEST(is_directory_false_for_file_and_missing) {
    TempFile f;
    f.write("x");
    CHECK(!Editor::isDirectory(f.path));
    CHECK(!Editor::isDirectory("/no/such/dir_or_file_xyz_edit"));
}

TEST(editor_open_absolute_existing_file) {
    // ./edit /tmp/.../nota.txt : carga el contenido y deja filename_
    // igual a la ruta absoluta (sin prefijar cwd).
    TempFile f;
    f.write("desde absoluta");
    CHECK(f.path.front() == '/');

    Editor ed;
    CHECK(ed.openFile(f.path));
    CHECK_EQ(ed.active().filename, f.path);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "desde absoluta");
    CHECK(!ed.active().modified);
}

TEST(editor_open_absolute_file_in_other_directory) {
    // Un archivo que no esta en cwd se abre por su ruta absoluta.
    TempDir dir;
    CHECK(!dir.path.empty());
    const std::string file = dir.path + "/nota.txt";
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        CHECK(out.good());
        out << "hola desde otra carpeta";
        CHECK(out.good());
    }

    Editor ed;
    CHECK(ed.openFile(file));
    CHECK_EQ(ed.active().filename, file);
    CHECK_EQ(ed.active().document.lineAt(0), "hola desde otra carpeta");
    CHECK(!ed.active().modified);

    std::remove(file.c_str());
}

TEST(editor_open_absolute_new_file) {
    // Ruta absoluta que no existe: se trata como archivo nuevo.
    TempFile f;
    CHECK(f.path.front() == '/');

    Editor ed;
    CHECK(!ed.openFile(f.path));
    CHECK_EQ(ed.active().filename, f.path);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK(!ed.active().modified);
}

TEST(editor_open_directory_rejected) {
    // Una carpeta no se abre ni se toma como archivo nuevo: el editor
    // queda como estaba (filename_ vacio, documento de una linea).
    TempDir dir;
    CHECK(!dir.path.empty());

    Editor ed;
    const std::string before = ed.active().filename;
    CHECK(!ed.openFile(dir.path));
    CHECK_EQ(ed.active().filename, before);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.statusMessage_, std::string("No se pueden abrir carpetas."));
}

TEST(editor_open_relative_directory_rejected) {
    Editor ed;
    const std::string before = ed.active().filename;
    CHECK(Editor::isDirectory("."));
    CHECK(!ed.openFile("."));
    CHECK_EQ(ed.active().filename, before);
    CHECK_EQ(ed.active().document.lineCount(), 1);
}

// ---------------------------------------------------------------------------
// 2. Navegacion del cursor a nivel Editor (via eventos)
// ---------------------------------------------------------------------------
TEST(editor_move_left) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveLeft);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 2);
}

TEST(editor_move_right) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveLeft);
    press(ed, EventType::MoveRight);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
}

TEST(editor_move_left_at_start_noop) {
    Editor ed;
    press(ed, EventType::MoveLeft);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(editor_move_right_at_end_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveRight);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
}

TEST(editor_move_left_wraps_to_previous_line) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline); // cursor en (1,0)
    press(ed, EventType::MoveLeft);      // salta al final de la linea anterior
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
}

TEST(editor_move_right_wraps_to_next_line) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline); // cursor en (1,0)
    press(ed, EventType::MoveLeft);      // -> (0,3)
    press(ed, EventType::MoveRight);     // -> (1,0)
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(editor_move_up_at_top_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 0);
}

TEST(editor_move_down_at_bottom_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline); // dos lineas, cursor en (1,0)
    press(ed, EventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 1);
}

TEST(editor_move_up_changes_line) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline); // cursor en (1,0)
    press(ed, EventType::MoveUp);        // -> (0,0)
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(editor_move_down_changes_line) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline); // cursor en (1,0)
    press(ed, EventType::MoveUp);        // -> (0,0)
    press(ed, EventType::MoveDown);      // -> (1,0)
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(editor_move_home) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveEnd);
    press(ed, EventType::MoveHome);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(editor_move_end) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveEnd);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
}

TEST(editor_move_up_down) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);
    type(ed, "def");
    press(ed, EventType::MoveUp);
    CHECK_EQ(ed.active().cursor.line, 0);
    press(ed, EventType::MoveDown);
    CHECK_EQ(ed.active().cursor.line, 1);
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
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 2);
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
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 5);
}

// ---------------------------------------------------------------------------
// 3. Insercion de caracteres (contrato via handleEvent)
// ---------------------------------------------------------------------------
TEST(editor_insert_character) {
    Editor ed;
    ed.handleEvent(insert('i'));          // entrar a Interaccion
    ed.handleEvent(insert('A'));
    ed.handleEvent(insert('B'));
    CHECK_EQ(ed.active().document.lineAt(0), "AB");
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.active().modified);
}

TEST(editor_insert_inserts_at_cursor_position) {
    Editor ed;
    type(ed, "ac");
    press(ed, EventType::MoveLeft);       // cursor en col 1
    ed.handleEvent(insert('b'));          // inserta en medio
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.active().modified);
}

TEST(editor_insert_marks_modified) {
    Editor ed;
    CHECK(!ed.active().modified);
    type(ed, "x");
    CHECK(ed.active().modified);
}

// ---------------------------------------------------------------------------
// 4. Backspace / Delete
// ---------------------------------------------------------------------------
TEST(editor_backspace) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.active().modified);
}

TEST(editor_backspace_at_line_start_joins) {
    // "abc" / "def" -> backspace al inicio de "def" une las lineas.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);
    type(ed, "def");

    press(ed, EventType::MoveHome);       // cursor al inicio de "def" (1,0)
    press(ed, EventType::Backspace);      // une las dos lineas

    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "abcdef");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);          // justo donde terminaba "abc"
    CHECK(ed.active().modified);
}

TEST(editor_backspace_join_then_undo_restores) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);
    type(ed, "def");

    press(ed, EventType::MoveHome);
    press(ed, EventType::Backspace);      // une: "abcdef"
    press(ed, EventType::Undo);           // restaura la division

    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
    CHECK_EQ(ed.active().document.lineAt(1), "def");
}

TEST(editor_backspace_at_absolute_start_noop) {
    Editor ed;
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.active().modified);
}

TEST(editor_delete) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveLeft);       // cursor en col 2
    press(ed, EventType::Delete);         // borra la "c"
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.active().modified);
}

TEST(editor_delete_at_line_end_joins_next) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);
    type(ed, "def");

    press(ed, EventType::MoveUp);         // -> renglón 0
    press(ed, EventType::MoveEnd);        // cursor al final de "abc" (0,3)
    press(ed, EventType::Delete);         // une la línea siguiente

    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "abcdef");
    // Delete no mueve el cursor: queda en la union (donde empezaba "def").
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(ed.active().modified);
}

TEST(editor_delete_at_end_of_document_noop) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveEnd);
    press(ed, EventType::Delete);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
}

// ---------------------------------------------------------------------------
// 10. Undo
// ---------------------------------------------------------------------------
TEST(editor_undo_insertion) {
    Editor ed;
    type(ed, "x");
    CHECK_EQ(ed.active().document.lineAt(0), "x");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(editor_undo_backspace) {
    Editor ed;
    type(ed, "a");
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    press(ed, EventType::Backspace); // borra "a"
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK(ed.active().modified);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "a");
}

TEST(editor_undo_delete) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::MoveLeft); // cursor en col 2
    press(ed, EventType::Delete);   // borra el 'c'
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
}

TEST(editor_undo_newline) {
    Editor ed;
    type(ed, "a");
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.active().document.lineCount(), 2);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 1);
}

TEST(editor_undo_mixed_operations) {
    // Contrato: cada InsertChar / InsertNewline genera su propia entrada.
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::InsertNewline);
    type(ed, "def");

    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(1), "def");

    for (int step = 0; step < 3; ++step)   // 'f' -> 'e' -> 'd'
        press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(1), "");

    press(ed, EventType::Undo);            // deshace el Enter
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");

    for (int step = 0; step < 3; ++step)   // 'c' -> 'b' -> 'a'
        press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "");

    press(ed, EventType::Undo);            // ya no hay nada que deshacer
    CHECK_EQ(ed.active().document.lineAt(0), "");
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

    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(editor_undo_multiple) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "");
}

TEST(editor_undo_empty_noop) {
    Editor ed;
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "");
}

TEST(editor_undo_everything) {
    Editor ed;
    type(ed, "hello");
    for (int i = 0; i < 6; ++i)
        press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

// ---------------------------------------------------------------------------
// 11. Redo
// ---------------------------------------------------------------------------
TEST(editor_redo_after_undo) {
    Editor ed;
    type(ed, "x");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "x");
}

TEST(editor_redo_several) {
    Editor ed;
    type(ed, "abc");
    press(ed, EventType::Undo);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
}

TEST(editor_redo_invalidated_by_new_change) {
    Editor ed;
    type(ed, "a");
    press(ed, EventType::Undo);
    type(ed, "Z");
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "Z");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 1);
    // Un Redo adicional no recupera la rama descartada: sigue en "Z".
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "Z");
    CHECK_EQ(ed.active().cursor.col, 1);
}

TEST(editor_redo_empty_noop) {
    Editor ed;
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "");
}

// ---------------------------------------------------------------------------
// 12. Guardado (nivel Editor / modified_)
// ---------------------------------------------------------------------------
TEST(editor_save_new_document) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    type(ed, "Hi");
    CHECK(ed.active().modified);
    save(ed);
    CHECK(!ed.active().modified);
    CHECK_EQ(fileContent(f.path), "Hi");
}

TEST(editor_save_empty_document) {
    TempFile f;
    Editor ed;
    ed.openFile(f.path);
    save(ed);
    CHECK(fileContent(f.path).empty());
}

TEST(editor_save_overwrites_content) {
    // Guardar reemplaza el contenido anterior: no append, no mezcla.
    TempFile f;
    f.write("old");

    Editor ed;
    CHECK(ed.openFile(f.path));

    // El cursor abre en (0,0): muevo al final, entro en Interaccion para
    // poder borrar "old" y luego escribo "new".
    press(ed, EventType::MoveEnd);
    enterInteraccion(ed);
    for (int i = 0; i < 3; ++i)
        press(ed, EventType::Backspace);
    type(ed, "new");
    save(ed);

    CHECK(!ed.active().modified);
    CHECK_EQ(fileContent(f.path), "new");

    Editor reloaded;
    CHECK(reloaded.openFile(f.path));
    CHECK_EQ(reloaded.active().document.lineCount(), 1);
    CHECK_EQ(reloaded.active().document.lineAt(0), "new");
}

TEST(editor_save_error_path) {
    Editor ed;
    ed.openFile("/no/such/dir/file.txt");
    type(ed, "a");
    CHECK(ed.active().modified);
    save(ed);
    CHECK(ed.active().modified);
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
    save(ed);
    CHECK(!ed.active().modified);

    type(ed, "b");
    CHECK(ed.active().modified);

    press(ed, EventType::Undo);
    CHECK(!ed.active().modified);
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
    save(ed);
    CHECK(!ed.active().modified);

    type(ed, "b");
    CHECK(ed.active().modified);

    press(ed, EventType::Undo);
    CHECK(!ed.active().modified);

    press(ed, EventType::Redo);
    CHECK(ed.active().modified);
}

TEST(editor_modified_until_undo_then_redo) {
    Editor ed;
    type(ed, "x");
    CHECK(ed.active().modified);
    // Undo vuelve al estado inicial (igual al guardado): modified_ se limpia.
    press(ed, EventType::Undo);
    CHECK(!ed.active().modified);
    press(ed, EventType::Redo);
    CHECK(ed.active().modified);
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
    save(ed);
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
    CHECK(ed.active().modified);
    press(ed, EventType::Quit);
    CHECK(ed.running_);
    press(ed, EventType::Prefix);
    press(ed, EventType::Quit);
    CHECK(!ed.running_);
}

// Cursor: columna VISUAL (1-based, la de la secuencia "\x1b[1;<col>H") a la
// que el Renderer moveria el cursor para `line` con cursor en el byte
// `byteCol`. Usado por los tests de cursor tras tipear multibyte.
namespace {
int cursorScreenCol(const std::string& line, int byteCol) {
    Document doc;
    doc.restore({line});
    Viewport vp;
    vp.top = 0; vp.height = 1; vp.width = 200;
    Cursor c;
    c.line = 0; c.col = byteCol;
    Renderer r;
    std::string f = r.buildScreen(doc, c, vp, "t", false, "", State::Navegacion, std::nullopt);
    size_t pos = f.rfind("\x1b[1;");
    if (pos == std::string::npos) return -1;
    size_t end = f.find('H', pos);
    // La columna de terminal emite gutter+visual+1; se resta el gutter (3
    // para un frame de 1 linea) para devolver la columna VISUAL del texto.
    return std::stoi(f.substr(pos + 4, end - pos - 4)) - 3;
}
} // namespace

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
        ed.active().document.restore({cs.line});
        ed.active().cursor.line = 0;
        ed.active().cursor.col = 0;

        const int nchars = static_cast<int>(std::string(cs.line).size()) / cs.nbytes;

        // Avanzar de a un caracter: 0 -> nbytes -> 2*nbytes -> ... -> fin.
        for (int i = 1; i <= nchars; ++i) {
            press(ed, EventType::MoveRight);
            CHECK_EQ(ed.active().cursor.col, cs.nbytes * i);
        }
        // Al final, mover derecha no pasa del largo (no "entra" en nul).
        const int endByte = static_cast<int>(std::string(cs.line).size());
        press(ed, EventType::MoveRight);
        CHECK_EQ(ed.active().cursor.col, endByte);

        // Volver: fin -> ... -> 2*nbytes -> nbytes -> 0.
        for (int i = nchars - 1; i >= 1; --i) {
            press(ed, EventType::MoveLeft);
            CHECK_EQ(ed.active().cursor.col, cs.nbytes * i);
        }
        press(ed, EventType::MoveLeft);
        CHECK_EQ(ed.active().cursor.col, 0);
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
    enterInteraccion(ed);
    ed.handleEvent(insertBytes("\xc3\xb1"));
    CHECK_EQ(ed.active().document.lineAt(0), "\xc3\xb1");
    CHECK_EQ(ed.active().cursor.col, 2); // avanza los 2 bytes del caracter

    ed.handleEvent(insertBytes("\xc3\xa1"));
    CHECK_EQ(ed.active().document.lineAt(0), "\xc3\xb1\xc3\xa1"); // "ñá"
    CHECK_EQ(ed.active().cursor.col, 4);

    // El render de la linea es UTF-8 valido y el cursor se dibuja en la
    // columna visual 2 (los 2 caracteres), no en la 4 (los bytes).
    CHECK_EQ(cursorScreenCol(ed.active().document.lineAt(0), ed.active().cursor.col), 3);
}

TEST(editor_type_helper_groups_multibyte) {
    // El helper type() agrupa los bytes por caracter: escribir "ño" en dos
    // pasos produce dos caracteres completos, no cuatro bytes sueltos.
    Editor ed;
    type(ed, "\xc3\xb1o");
    CHECK_EQ(ed.active().document.lineAt(0), "\xc3\xb1o");
    CHECK_EQ(ed.active().cursor.col, 3); // 2 bytes de "ñ" + 1 de 'o'
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
    ed.active().document.restore({"\xc3\xb1\xc3\xa1"}); // "ñá"
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 4; // tras "á"
    enterInteraccion(ed); // Backspace solo actua en Interaccion (v0.5)
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineAt(0), "\xc3\xb1"); // queda "ñ"
    CHECK_EQ(ed.active().cursor.col, 2); // limite tras "ñ", no un byte suelto

    press(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().cursor.col, 0);
}