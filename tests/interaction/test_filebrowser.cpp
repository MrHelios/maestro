#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "test_framework.h"

#include <cstdlib>
#include <string>
#include <vector>
#define private public
#include "ui/Editor.h"
#undef private

using testfw::TempFile;

static Event insert(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

static void press(Editor& ed, EventType type) {
    Event e;
    e.type = type;
    ed.handleEvent(e);
}

static void pressEvent(Editor& ed, const Event& ev) {
    ed.handleEvent(ev);
}

static void type(Editor& ed, const std::string& s) {
    ed.handleEvent(insert('i'));   // Interaccion
    for (char c : s) ed.handleEvent(insert(c));
}

// v0.6.4: abre el explorador de archivos (Ctrl+K o).
static void openFileBrowser(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('o'));
}

// Ctrl+K n: buffer nuevo sin nombre (reutiliza la convencion de la suite).
static void newBuffer(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('n'));
}

// Ctrl+K t: selector de buffers.
static void openSelector(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('t'));
}

// Ctrl+K w: cerrar el buffer activo.
static void closeBuffer(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('w'));
}

// Directorio temporal bajo /tmp con estructura configurable, que se
// elimina (borrado recursivo) al salir del scope aunque falle un CHECK.
struct TempDir {
    std::string path;

    TempDir() : path("/tmp/edit_fb_" + std::to_string(::getpid()) + "_" +
                     std::to_string(reinterpret_cast<unsigned long>(this))) {
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    // Crea una carpeta hija. Devuelve su ruta absoluta.
    std::string dir(const std::string& name) const {
        std::filesystem::create_directory(path + "/" + name);
        return path + "/" + name;
    }

    // Crea un archivo con su nombre como contenido. Devuelve la ruta.
    std::string file(const std::string& name) const {
        std::ofstream f(path + "/" + name, std::ios::binary | std::ios::trunc);
        f << name;
        return path + "/" + name;
    }
};

// Cambia de cwd dentro de un bloque y lo restaura al salir. Es un scope
// guard: lo que importa es el destructor, no el constructor.
struct CwdGuard {
    std::string old;

    CwdGuard() {
        char buf[4096];
        getcwd(buf, sizeof buf);
        old = buf;
    }

    ~CwdGuard() { chdir(old.c_str()); }

    void enter(const std::string& p) { chdir(p.c_str()); }
};

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Apertura y cancelacion
// ---------------------------------------------------------------------------
TEST(ctrl_k_o_opens_browser_at_cwd) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.fileBrowser.path_, t.path);
    CHECK(ed.fileBrowser.entries_.size() >= 1);
    CHECK(ed.fileBrowser.entries_[0].name == ".."); // la entrada ".." va primera
    CHECK_EQ(ed.fileBrowser.index_, 0);
    CHECK_EQ(ed.fileBrowser.scroll_, 0);
}

TEST(browser_escape_cancels_to_navegacion) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::Escape);
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1)); // nada se modifico
    CHECK(ed.active().filename.empty());
    CHECK(ed.active().document.lineAt(0).empty());
}

TEST(browser_escape_returns_to_prior_mode) {
    // Abierto desde Interaccion, ESC devuelve a Interaccion (priorState_).
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    type(ed, "hola");                           // Interaccion
    openFileBrowser(ed);
    CHECK(ed.state_ == State::FileBrowser);
    press(ed, EventType::Escape);
    CHECK(ed.state_ == State::Interaccion);
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
}

// ---------------------------------------------------------------------------
// 1. Entrada y salida del modo (lista exhaustiva)
// ---------------------------------------------------------------------------
TEST(ctrl_k_o_from_navegacion_saves_prior_state) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    CHECK(ed.state_ == State::Navegacion);
    openFileBrowser(ed);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK(ed.priorState_ == State::Navegacion);
}

TEST(ctrl_k_o_from_interaccion_saves_prior_state) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    type(ed, "hola");                           // Interaccion
    CHECK(ed.state_ == State::Interaccion);
    openFileBrowser(ed);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK(ed.priorState_ == State::Interaccion);
}

TEST(ctrl_k_o_from_seleccion_saves_prior_state) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    type(ed, "abcdef");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    pressEvent(ed, insert('s'));                // modo seleccion
    press(ed, EventType::MoveRight);            // extiende: rango NO vacio
    CHECK(ed.state_ == State::Seleccion);
    CHECK(ed.hasSelection());
    openFileBrowser(ed);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK(ed.priorState_ == State::Seleccion);
    press(ed, EventType::Escape);               // ESC: vuelve exactamente a Seleccion
    CHECK(ed.state_ == State::Seleccion);
    CHECK(ed.hasSelection());
}

TEST(browser_escape_restores_exact_prior_state) {
    // El modo restaurado es EXACTAMENTE el previo, en los tres casos.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);

    {   // desde Navegacion
        Editor ed;
        openFileBrowser(ed);
        press(ed, EventType::Escape);
        CHECK(ed.state_ == State::Navegacion);
    }
    {   // desde Interaccion
        Editor ed;
        type(ed, "hola");
        openFileBrowser(ed);
        press(ed, EventType::Escape);
        CHECK(ed.state_ == State::Interaccion);
    }
    {   // desde Seleccion
        Editor ed;
        type(ed, "abcdef");
        press(ed, EventType::Escape);
        press(ed, EventType::MoveHome);
        pressEvent(ed, insert('s'));
        press(ed, EventType::MoveRight);        // se extiende la seleccion
        CHECK(ed.hasSelection());
        openFileBrowser(ed);
        press(ed, EventType::Escape);
        CHECK(ed.state_ == State::Seleccion);
        CHECK(ed.hasSelection());
    }
}

TEST(browser_escape_touches_nothing) {
    // ESC no modifica buffers, documento, seleccion ni portapapeles.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    type(ed, "hola");
    press(ed, EventType::Escape);
    ed.setClipboardBlock({"cosa"});                   // hay algo en el portapapeles

    const auto docBefore = ed.active().document.snapshot();
    const auto clipBefore = ed.getClipboardBlock();
    const int undoSize = static_cast<int>(ed.active().undoStack.size());
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);             // mover dentro del explorador
    press(ed, EventType::MoveDown);
    press(ed, EventType::Escape);

    CHECK(ed.active().document.snapshot() == docBefore);
    CHECK(ed.getClipboardBlock() == clipBefore);
    CHECK_EQ(ed.active().undoStack.size(), size_t(undoSize));
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));    // no se abrio ni cerro nada
    CHECK(ed.active().filename.empty());
    CHECK(!ed.hasSelection());
}

TEST(browser_reopen_resets_index_and_path) {
    // Salir con ESC no contamina la proxima apertura: se reabre en la raiz
    // de cwd con indice 0, aunque la vez anterior se hubiera navegado lejos.
    TempDir t;
    t.dir("sub");
    t.file("sub/inner.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);             // -> sub
    press(ed, EventType::InsertNewline);        // entrar en sub
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/sub");
    press(ed, EventType::MoveDown);             // -> inner.txt
    CHECK_EQ(ed.fileBrowser.index_, 1);
    press(ed, EventType::Escape);

    // Reapertura: cwd, indice 0, scroll 0, sin residuos.
    openFileBrowser(ed);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.fileBrowser.path_, t.path);
    CHECK_EQ(ed.fileBrowser.index_, 0);
    CHECK_EQ(ed.fileBrowser.scroll_, 0);
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(2)); // .. , sub/
}

TEST(browser_after_escape_editor_responds_normally) {
    // Tras cancelar, el editor reacciona a las teclas del modo restaurado.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);

    {   // desde Navegacion: 'i' vuelve a entrar a edicion y se escribe.
        Editor ed;
        openFileBrowser(ed);
        press(ed, EventType::Escape);
        CHECK(ed.state_ == State::Navegacion);
        type(ed, "nuevo texto");
        CHECK(ed.state_ == State::Interaccion);
        CHECK_EQ(ed.active().document.lineAt(0), "nuevo texto");
    }
    {   // desde Interaccion: se sigue escribiendo donde se estaba.
        Editor ed;
        type(ed, "hola");
        openFileBrowser(ed);
        press(ed, EventType::Escape);
        CHECK(ed.state_ == State::Interaccion);
        pressEvent(ed, insert('!'));
        CHECK_EQ(ed.active().document.lineAt(0), "hola!");
    }
    {   // desde Seleccion: las flechas siguen extendiendo la seleccion.
        Editor ed;
        type(ed, "abcdef");
        press(ed, EventType::Escape);
        press(ed, EventType::MoveHome);
        pressEvent(ed, insert('s'));
        openFileBrowser(ed);
        press(ed, EventType::Escape);
        CHECK(ed.state_ == State::Seleccion);
        press(ed, EventType::MoveRight);        // selecciona "a"
        CHECK(ed.hasSelection());
    }
}

TEST(browser_ctrl_k_inside_cancels) {
    // Modal puro: Ctrl+K dentro no abre un segundo prefijo, cancela.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::Prefix);
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
}

TEST(browser_other_keys_are_noop) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    pressEvent(ed, insert('i'));
    pressEvent(ed, insert('s'));
    pressEvent(ed, insert('a'));
    pressEvent(ed, insert('j'));
    pressEvent(ed, insert('k'));
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveLeft);
    press(ed, EventType::Undo);
    press(ed, EventType::Redo);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.fileBrowser.index_, 0);
}

// ---------------------------------------------------------------------------
// 3. Navegacion dentro de la lista (lista exhaustiva)
// ---------------------------------------------------------------------------
TEST(browser_down_up_inc_dec_index) {
    // (18)(19) Flecha abajo incrementa, flecha arriba decrementa.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    for (int i = 0; i < 5; ++i) t.file("f" + std::to_string(i) + ".txt");
    Editor ed;
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.index_, 0);
    press(ed, EventType::MoveDown);
    CHECK_EQ(ed.fileBrowser.index_, 1);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveDown);
    CHECK_EQ(ed.fileBrowser.index_, 3);
    press(ed, EventType::MoveUp);
    CHECK_EQ(ed.fileBrowser.index_, 2);
}

TEST(browser_index_never_out_of_range) {
    // (20) Nunca baja de 0. (21) Nunca supera entries_.size() - 1.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    for (int i = 0; i < 5; ++i) t.file("f" + std::to_string(i) + ".txt");
    Editor ed;
    openFileBrowser(ed);
    const int n = static_cast<int>(ed.fileBrowser.entries_.size());
    CHECK_EQ(n, 6); // .. + 5 archivos

    for (int i = 0; i < 50; ++i) press(ed, EventType::MoveDown);
    CHECK_EQ(ed.fileBrowser.index_, n - 1);

    for (int i = 0; i < 50; ++i) press(ed, EventType::MoveUp);
    CHECK_EQ(ed.fileBrowser.index_, 0);
}

TEST(browser_jk_are_noop) {
    // (22)(23) Decision de diseno: solo flechas ↑/↓. j/k NO mueven el
    // indice (a diferencia del modo Normal); se ignoran sin cambiar nada.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    for (int i = 0; i < 3; ++i) t.file("f" + std::to_string(i) + ".txt");
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);              // index 1
    CHECK_EQ(ed.fileBrowser.index_, 1);
    pressEvent(ed, insert('j'));                 // no-op
    pressEvent(ed, insert('k'));                 // no-op
    CHECK_EQ(ed.fileBrowser.index_, 1);
    pressEvent(ed, insert('J'));
    pressEvent(ed, insert('K'));
    CHECK_EQ(ed.fileBrowser.index_, 1);
    CHECK(ed.state_ == State::FileBrowser);
}

TEST(browser_page_keys_are_noop) {
    // (24) RePag/AvPag no se soportan: se ignoran de forma documentada.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    for (int i = 0; i < 5; ++i) t.file("f" + std::to_string(i) + ".txt");
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);              // index 1
    press(ed, EventType::PageUp);
    press(ed, EventType::PageDown);
    CHECK_EQ(ed.fileBrowser.index_, 1);           // sin cambio
    CHECK(ed.state_ == State::FileBrowser);
}

TEST(browser_unrecognized_keys_ignored) {
    // (25) Cualquier tecla no reconocida es no-op: letras, Ctrl+K fuera
    // (ya cubierto), Save/Quit, Home/End, InsertNewline con entrada...
    // Nada cambia de estado ni mueve el indice.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    for (int i = 0; i < 3; ++i) t.file("f" + std::to_string(i) + ".txt");
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);              // index 1 como punto de partida
    CHECK_EQ(ed.fileBrowser.index_, 1);

    press(ed, EventType::Save);
    press(ed, EventType::Quit);
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveEnd);
    pressEvent(ed, insert('x'));
    pressEvent(ed, insert('p'));
    press(ed, EventType::Backspace);
    press(ed, EventType::Delete);
    CHECK(ed.state_ == State::FileBrowser);      // el estado NO cambio
    CHECK_EQ(ed.fileBrowser.index_, 1);           // el indice NO cambio
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(4)); // nada se abrio
    CHECK(ed.running_);                          // no salio con Quit
}

// ---------------------------------------------------------------------------
// Navegacion por indice
// ---------------------------------------------------------------------------
TEST(browser_navigation_clamps) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    for (int i = 0; i < 3; ++i) t.file("f" + std::to_string(i) + ".txt");
    Editor ed;
    openFileBrowser(ed);
    const int n = static_cast<int>(ed.fileBrowser.entries_.size());
    CHECK_EQ(n, 4); // .. + 3 archivos

    CHECK_EQ(ed.fileBrowser.index_, 0);
    press(ed, EventType::MoveUp);   // clamp arriba
    CHECK_EQ(ed.fileBrowser.index_, 0);

    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveDown);
    CHECK_EQ(ed.fileBrowser.index_, 2);
    press(ed, EventType::MoveDown);
    CHECK_EQ(ed.fileBrowser.index_, 3);
    press(ed, EventType::MoveDown); // clamp abajo
    CHECK_EQ(ed.fileBrowser.index_, 3);

    press(ed, EventType::MoveUp);
    CHECK_EQ(ed.fileBrowser.index_, 2);
}

TEST(browser_scroll_follows_selection) {
    // Con una ventana chica (viewport.height), bajar el indice desplaza el
    // scroll para que la seleccion quede siempre visible.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    for (int i = 0; i < 10; ++i) t.file("f" + std::to_string(i) + ".txt");
    Editor ed;
    ed.buffers.buffers_[0].viewport.height = 2; // ventana de solo 2 filas
    openFileBrowser(ed);                // entries: .. + 10 archivos = 11
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(11));

    for (int i = 0; i < 6; ++i) press(ed, EventType::MoveDown);
    CHECK_EQ(ed.fileBrowser.index_, 6);
    CHECK_EQ(ed.fileBrowser.scroll_, 5); // 6 - 2 + 1 -> seleccion en la ultima fila

    press(ed, EventType::MoveUp);
    CHECK_EQ(ed.fileBrowser.index_, 5);
    CHECK_EQ(ed.fileBrowser.scroll_, 5); // aun visible en la fila superior

    press(ed, EventType::MoveUp);
    CHECK_EQ(ed.fileBrowser.index_, 4);
    CHECK_EQ(ed.fileBrowser.scroll_, 4); // la ventana retrocede
}

// ---------------------------------------------------------------------------
// 2. Inicializacion del explorador (lista exhaustiva)
// ---------------------------------------------------------------------------
TEST(browser_init_path_equals_getcwd) {
    // (8) currentPath_ es el resultado de getcwd(). (9)(11) la lista no esta
    // vacia y empieza con "..".
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    CHECK_EQ(FileBrowser::getCwd(), t.path);
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.path_, FileBrowser::getCwd());
    CHECK(!ed.fileBrowser.entries_.empty());
    CHECK_EQ(ed.fileBrowser.entries_[0].name, "..");
}

TEST(browser_init_index_starts_at_zero) {
    // (10) El indice inicial es siempre 0, aunque el directorio tenga de todo.
    TempDir t;
    t.dir("beta");
    t.dir("alfa");
    t.file("x.txt");
    t.file("z.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.index_, 0);
    CHECK_EQ(ed.fileBrowser.scroll_, 0);
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(5)); // .. 2 carpetas 2 archivos
}

TEST(browser_parent_entry_is_directory) {
    // (17) La entrada ".." se marca como isDirectory = true.
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.entries_[0].name, "..");
    CHECK(ed.fileBrowser.entries_[0].isDirectory);
}

TEST(browser_entries_sorted_case_insensitive) {
    // (14) Carpetas primero (13), luego archivos; ambos alfabeticos
    // case-insensitive: "alpha" antes que "Zeta" y "A.txt" antes que "b.txt".
    TempDir t;
    t.file("b.txt");
    t.file("A.txt");
    t.file("c.txt");
    t.dir("Zeta");
    t.dir("alpha");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(6));
    CHECK_EQ(ed.fileBrowser.entries_[0].name, "..");
    CHECK(ed.fileBrowser.entries_[0].isDirectory);
    CHECK_EQ(ed.fileBrowser.entries_[1].name, "alpha"); // a < z
    CHECK(ed.fileBrowser.entries_[1].isDirectory);
    CHECK_EQ(ed.fileBrowser.entries_[2].name, "Zeta");  // a < z
    CHECK(ed.fileBrowser.entries_[2].isDirectory);
    CHECK_EQ(ed.fileBrowser.entries_[3].name, "A.txt"); // a < b < c
    CHECK(!ed.fileBrowser.entries_[3].isDirectory);
    CHECK_EQ(ed.fileBrowser.entries_[4].name, "b.txt");
    CHECK(!ed.fileBrowser.entries_[4].isDirectory);
    CHECK_EQ(ed.fileBrowser.entries_[5].name, "c.txt");
    CHECK(!ed.fileBrowser.entries_[5].isDirectory);
}

TEST(browser_folders_listed_before_files) {
    TempDir t;
    t.file("a.txt");
    t.file("b.txt");
    t.dir("zeta");
    t.dir("alfa");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    // Orden: "..", carpetas alfabeticas, luego archivos alfabeticos.
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(5));
    CHECK_EQ(ed.fileBrowser.entries_[0].name, "..");
    CHECK_EQ(ed.fileBrowser.entries_[1].name, "alfa");
    CHECK(ed.fileBrowser.entries_[1].isDirectory);
    CHECK_EQ(ed.fileBrowser.entries_[2].name, "zeta");
    CHECK(ed.fileBrowser.entries_[2].isDirectory);
    CHECK_EQ(ed.fileBrowser.entries_[3].name, "a.txt");
    CHECK(!ed.fileBrowser.entries_[3].isDirectory);
    CHECK_EQ(ed.fileBrowser.entries_[4].name, "b.txt");
    // Los nombres para dibujar marcan las carpetas con "/".
    CHECK_EQ(ed.fileBrowser.displayNames_[1], "alfa/");
    CHECK_EQ(ed.fileBrowser.displayNames_[3], "a.txt");
}

// ---------------------------------------------------------------------------
// 4. Navegacion por el sistema de archivos (lista exhaustiva)
// ---------------------------------------------------------------------------
TEST(browser_multiple_levels_up) {
    // (33) Se sube varios niveles consecutivos con "..".
    TempDir t;
    t.dir("a");
    std::filesystem::create_directories(t.path + "/a/b");
    t.file("a/b/deep.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.path_, t.path);

    press(ed, EventType::MoveDown);   // -> a
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/a");
    press(ed, EventType::MoveDown);   // -> b
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/a/b");

    press(ed, EventType::InsertNewline);   // Enter sobre ".." -> sube a
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/a");
    press(ed, EventType::InsertNewline);   // -> cwd
    CHECK_EQ(ed.fileBrowser.path_, t.path);
    press(ed, EventType::InsertNewline);   // -> /tmp (padre de cwd)
    CHECK_EQ(ed.fileBrowser.path_, "/tmp");
    press(ed, EventType::InsertNewline);   // -> /
    CHECK_EQ(ed.fileBrowser.path_, "/");
    CHECK(ed.state_ == State::FileBrowser); // sin crashear
}

TEST(browser_reaching_root_hides_parent) {
    // (35)(36) Subir desde un subdirectorio hasta la raiz: en "/" ya no
    // hay entrada "..", el editor no crashea y el indice queda en 0.
    TempDir t;
    t.dir("sub");
    std::filesystem::create_directories(t.path + "/sub/inner");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> sub
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/sub");
    press(ed, EventType::MoveDown);   // -> inner
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/sub/inner");

    // Subir con ".." cuanto haga falta hasta quedar en la raiz. Dividido
    // en trozos para no asumir el prefijo exacto de los directorios tmp.
    press(ed, EventType::InsertNewline);   // cwd/sub/inner -> cwd/sub
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/sub");
    press(ed, EventType::InsertNewline);   // -> cwd
    CHECK_EQ(ed.fileBrowser.path_, t.path);
    press(ed, EventType::InsertNewline);   // -> /tmp (padre de cwd)
    CHECK_EQ(ed.fileBrowser.path_, "/tmp");
    press(ed, EventType::InsertNewline);   // -> /
    CHECK_EQ(ed.fileBrowser.path_, "/");

    // En "/" ya no hay ".." (salir a la raiz no borra las carpetas reales:
    // la entrada 0 sigue siendo una carpeta valida; no la tocamos).
    CHECK(ed.state_ == State::FileBrowser);
    for (const FileBrowserEntry& e : ed.fileBrowser.entries_)
        CHECK(e.name != "..");
    CHECK_EQ(ed.fileBrowser.index_, 0);
    press(ed, EventType::Escape);          // se sale limpio
    CHECK(ed.state_ == State::Navegacion);
}

TEST(browser_down_up_repeatedly_stays_consistent) {
    // (34) Bajar y subir varias veces sin corromper el estado.
    TempDir t;
    t.dir("d1");
    t.file("d1/x.txt");
    t.dir("d2");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(3)); // .., d1/, d2/

    press(ed, EventType::MoveDown);   // d1
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/d1");
    CHECK(ed.state_ == State::FileBrowser);

    press(ed, EventType::InsertNewline);   // subir
    CHECK_EQ(ed.fileBrowser.path_, t.path);
    CHECK_EQ(ed.fileBrowser.index_, 0);
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(3));

    press(ed, EventType::MoveDown);   // d1
    press(ed, EventType::MoveDown);   // d2
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/d2");
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(1)); // solo ".." (d2 vacio)

    press(ed, EventType::InsertNewline);   // subir de nuevo
    CHECK_EQ(ed.fileBrowser.path_, t.path);
    CHECK_EQ(ed.fileBrowser.index_, 0);
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(3));
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1)); // jamas se abrio un buffer
    CHECK(ed.state_ == State::FileBrowser);
}

TEST(browser_enter_folder_and_go_back) {
    TempDir t;
    t.dir("sub");
    t.file("sub/inner.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    // entries: .. , sub/
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(2));
    press(ed, EventType::MoveDown);   // -> sub
    press(ed, EventType::InsertNewline);

    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/sub");
    CHECK_EQ(ed.fileBrowser.index_, 0);
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(2)); // .. , inner.txt
    CHECK_EQ(ed.fileBrowser.entries_[1].name, "inner.txt");

    press(ed, EventType::InsertNewline);  // Enter sobre ".."
    CHECK_EQ(ed.fileBrowser.path_, t.path);
    CHECK_EQ(ed.fileBrowser.index_, 0);
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(2));
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1)); // nunca se abrio nada
}

// ---------------------------------------------------------------------------
// Apertura de archivos
// ---------------------------------------------------------------------------
TEST(browser_open_new_file_adds_buffer) {
    TempDir t;
    t.file("a.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> a.txt
    press(ed, EventType::InsertNewline);
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().filename, t.path + "/a.txt");
    CHECK_EQ(ed.active().document.lineAt(0), "a.txt");
    CHECK(!ed.active().modified);
}

TEST(browser_opened_file_name_is_basename) {
    // (41) El buffer nuevo conserva la ruta absoluta como filename y su
    // nombre visible es el base del archivo (convencion del proyecto).
    TempDir t;
    t.file("notas.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> notas.txt
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.active().filename, t.path + "/notas.txt");
    CHECK_EQ(ed.active().displayName(), "notas.txt");
    CHECK_EQ(ed.buffers.buffers_[0].unnamedName, "SinNombre"); // el previo sigue intacto
}

TEST(browser_open_restores_prior_state) {
    TempDir t;
    t.file("a.txt");
    CwdGuard g;
    g.enter(t.path);

    {   // desde Navegacion
        Editor ed;
        openFileBrowser(ed);
        press(ed, EventType::MoveDown);
        press(ed, EventType::InsertNewline);
        CHECK(ed.state_ == State::Navegacion);
    }
    {   // desde Interaccion -> al enfocar archivo vuelve a Navegacion
        Editor ed;
        type(ed, "hola");
        openFileBrowser(ed);
        press(ed, EventType::MoveDown);
        press(ed, EventType::InsertNewline);
        CHECK(ed.state_ == State::Navegacion);
        CHECK_EQ(ed.active().document.lineAt(0), "a.txt");
    }
    {   // desde Seleccion -> al enfocar archivo nuevo (sin seleccion) vuelve a Navegacion
        Editor ed;
        type(ed, "abcdef");
        press(ed, EventType::Escape);
        press(ed, EventType::MoveHome);
        pressEvent(ed, insert('s'));
        openFileBrowser(ed);
        press(ed, EventType::MoveDown);
        press(ed, EventType::InsertNewline);
        CHECK(ed.state_ == State::Navegacion);
    }
}

TEST(browser_open_focus_resets_mode_to_navegacion) {
    TempDir t;
    t.file("a.txt");
    t.file("b.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    type(ed, "hola");
    CHECK(ed.state_ == State::Interaccion);
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);
    press(ed, EventType::InsertNewline);
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.active().filename, t.path + "/a.txt");
    type(ed, "mundo");
    CHECK(ed.state_ == State::Interaccion);
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveDown);
    press(ed, EventType::InsertNewline);
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.active().filename, t.path + "/b.txt");
    ed.activateBuffer(0);
    CHECK(ed.state_ == State::Navegacion);
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);
    press(ed, EventType::InsertNewline);
    CHECK(ed.state_ == State::Navegacion);
}

TEST(browser_open_does_not_touch_other_buffers) {
    // (44) Abrir un archivo nuevo no modifica los demas buffers.
    TempDir t;
    t.file("nuevo.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    type(ed, "contenido B0");
    press(ed, EventType::Escape);
    newBuffer(ed);
    type(ed, "contenido B1");
    press(ed, EventType::Escape);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));

    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> nuevo.txt
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(3));
    CHECK_EQ(ed.buffers.buffers_[0].document.lineAt(0), "contenido B0");
    CHECK_EQ(ed.buffers.buffers_[1].document.lineAt(0), "contenido B1");
    CHECK_EQ(ed.active().document.lineAt(0), "nuevo.txt");
}

TEST(browser_open_does_not_touch_clipboard) {
    // (45) Abrir un archivo no toca el portapapeles global.
    TempDir t;
    t.file("a.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    ed.setClipboardBlock({"texto copiado"});
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);
    press(ed, EventType::InsertNewline);
    CHECK(ed.getClipboardBlock() == (std::vector<std::string>{"texto copiado"}));
}

TEST(browser_reopen_file_activates_existing) {
    TempDir t;
    t.file("a.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    CHECK_EQ(ed.buffers.activeBuffer_, 1);

    openFileBrowser(ed);
    press(ed, EventType::MoveDown);
    press(ed, EventType::InsertNewline);
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));   // NO se abrio una copia
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().filename, t.path + "/a.txt");
}

TEST(browser_reopen_matches_absolute_path) {
    // (48) La deduplicacion compara rutas ABSOLUTAS: si el buffer se abrio
    // con una ruta relativa, al reabrirlo desde el explorador no se duplica.
    TempDir t;
    t.file("a.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    CHECK(ed.openFile("a.txt"));               // ruta RELATIVA al abrir
    CHECK_EQ(ed.active().filename, t.path + "/a.txt"); // pero queda absoluta
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));

    openFileBrowser(ed);
    press(ed, EventType::MoveDown);            // -> a.txt (ruta absoluta)
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));   // NO se duplico
    CHECK_EQ(ed.buffers.activeBuffer_, 0);             // se activo el existente
    CHECK_EQ(ed.active().filename, t.path + "/a.txt");
    CHECK(ed.state_ == State::Navegacion);     // y se salio del explorador
}

TEST(browser_folder_not_opened_as_buffer) {
    TempDir t;
    t.dir("sub");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> sub
    press(ed, EventType::InsertNewline);
    // Sigue en el explorador (ahora dentro de sub), no se agrego buffer.
    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK(ed.active().filename.empty());
}

TEST(browser_folder_enter_never_creates_buffer) {
    // (50)(51) Enter sobre una carpeta nunca crea un buffer: solo cambia
    // el directorio actual del explorador.
    TempDir t;
    t.dir("a");
    t.file("a/f.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> a (carpeta)
    press(ed, EventType::InsertNewline);
    CHECK(ed.state_ == State::FileBrowser);        // sigue en el explorador
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));       // no se creo buffer
    CHECK(ed.active().filename.empty());           // tampoco se cargo nada
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/a");  // solo cambio el directorio
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(2)); // .., f.txt
    CHECK(ed.active().document.lineAt(0).empty()); // y el documento sigue igual
}

// ---------------------------------------------------------------------------
// 7. Rechazo de carpetas como buffers (lista exhaustiva)
// ---------------------------------------------------------------------------
TEST(open_file_rejects_directory) {
    // (52)(53) Editor::openFile rechaza rutas que son directorios, sean
    // relativas o absolutas: no abre nada, no cambia el estado y avisa.
    TempDir t;
    t.dir("carpeta");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    // ruta RELATIVA
    CHECK(!ed.openFile("carpeta"));
    CHECK(ed.active().filename.empty());
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK_EQ(ed.statusMessage_, "No se pueden abrir carpetas.");
    // ruta ABSOLUTA
    CHECK(!ed.openFile(t.path + "/carpeta"));
    CHECK(ed.active().filename.empty());
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK_EQ(ed.statusMessage_, "No se pueden abrir carpetas.");
}

TEST(browser_open_empty_list_enters_and_returns) {
    TempDir t;
    t.dir("empty");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> empty
    press(ed, EventType::InsertNewline);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/empty");
    // Solo la entrada ".." (el directorio esta vacio).
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(1));
    CHECK_EQ(ed.fileBrowser.entries_[0].name, "..");
    CHECK_EQ(ed.fileBrowser.index_, 0);
    press(ed, EventType::InsertNewline); // volver
    CHECK_EQ(ed.fileBrowser.path_, t.path);
}

// ---------------------------------------------------------------------------
// Raiz del sistema
// ---------------------------------------------------------------------------
TEST(browser_at_root_has_no_parent) {
    CwdGuard g;
    g.enter("/");
    Editor ed;
    openFileBrowser(ed);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.fileBrowser.path_, "/");
    for (const FileBrowserEntry& e : ed.fileBrowser.entries_) {
        CHECK(e.name != ".."); // no hay a donde subir
    }
    press(ed, EventType::Escape);
    CHECK(ed.state_ == State::Navegacion);
}

// ---------------------------------------------------------------------------
// Helpers de ruta
// ---------------------------------------------------------------------------
TEST(browser_parent_path_computation) {
    CHECK_EQ(FileBrowser::parentPath("/a/b/c"), "/a/b"); // sube un solo nivel
    CHECK_EQ(FileBrowser::parentPath("/a/b"), "/a");
    CHECK_EQ(FileBrowser::parentPath("/a"), "/");
    CHECK_EQ(FileBrowser::parentPath("/"), "/");
    CHECK_EQ(FileBrowser::parentPath(""), "/");
    // Relativa sin directorio: sin a donde subir, se ancla a la raiz.
    CHECK_EQ(FileBrowser::parentPath("algo"), "/");
}

TEST(browser_list_directory_unreadable_reports_error) {
    std::string err;
    std::vector<FileBrowserEntry> entries =
        FileBrowser::listDirectory("/nonexistent_xyz_123", err);
    CHECK(!err.empty());
    // Aunque no se pueda leer, siempre queda ".." para poder subir.
    CHECK_EQ(entries.size(), size_t(1));
    CHECK_EQ(entries[0].name, "..");
}

// ---------------------------------------------------------------------------
// 8. Casos de error y permisos (lista exhaustiva)
// ---------------------------------------------------------------------------
TEST(browser_unreadable_directory_shows_error) {
    // (54) Un directorio sin permisos de lectura no se lista y muestra un
    // mensaje de error (cuando la maquina respeta permisos: bajo root no se
    // puede forzar el fallo, asi que esa parte se omite).
    TempDir t;
    t.dir("locked");
    std::filesystem::permissions(t.path + "/locked",
                                 std::filesystem::perms::none);
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> locked
    press(ed, EventType::InsertNewline);
    CHECK(ed.state_ == State::FileBrowser);              // sigue en el explorador
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/locked");   // el directorio cambio
    if (::geteuid() != 0) {
        // Sin permiso: no se lista (queda "..") y hay mensaje de error.
        CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(1));
        CHECK_EQ(ed.fileBrowser.entries_[0].name, "..");
        CHECK(!ed.statusMessage_.empty());
        CHECK(ed.statusMessage_.find("No se pudo leer") != std::string::npos);
    }
    press(ed, EventType::Escape);
    CHECK(ed.state_ == State::Navegacion);
}

TEST(browser_empty_directory_lists_only_parent) {
    // (56) Un directorio vacio se lista correctamente: solo aparece "..".
    TempDir t;
    t.dir("vacio");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> vacio
    press(ed, EventType::InsertNewline);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/vacio");
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(1));
    CHECK_EQ(ed.fileBrowser.entries_[0].name, "..");
    CHECK(ed.fileBrowser.entries_[0].isDirectory);
    // Las carpetas vacias NO se muestran como archivo (nada que abrir).
    press(ed, EventType::InsertNewline);   // volver al padre
    CHECK_EQ(ed.fileBrowser.path_, t.path);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
}

TEST(browser_handles_utf8_and_special_names) {
    // (57) Nombres con UTF-8, espacios y simbolos no corrompen la lista.
    TempDir t;
    t.file("naño.txt");
    t.file("a b.txt");
    t.file("100%_#.c");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(4)); // .. + 3
    auto hasName = [&](const std::string& n) {
        for (const FileBrowserEntry& e : ed.fileBrowser.entries_)
            if (e.name == n) return true;
        return false;
    };
    CHECK(hasName("naño.txt"));
    CHECK(hasName("a b.txt"));
    CHECK(hasName("100%_#.c"));
    // Navegacion completa sobre la lista sin romper nada y se sale limpio.
    for (int i = 0; i < 10; ++i) press(ed, EventType::MoveDown);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.fileBrowser.index_,
             static_cast<int>(ed.fileBrowser.entries_.size()) - 1);
    // Abrir el archivo UTF-8: el buffer conserva el nombre exacto.
    for (int i = 0; i < 10; ++i) press(ed, EventType::MoveUp);
    press(ed, EventType::MoveDown);   // naño.txt (a b y 100% van antes)
    press(ed, EventType::InsertNewline);
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.active().filename, t.path + "/100%_#.c");
}

TEST(browser_cwd_failure_does_not_crash) {
    // (58) Si getcwd() falla (la cwd actual fue borrada), el explorador no
    // crashea: queda en un estado controlado con ".." y mensaje de error.
    TempDir t;
    {
        CwdGuard g;
        g.enter(t.path);
        std::filesystem::remove_all(t.path);   // borramos la cwd actual
        Editor ed;
        CHECK_EQ(FileBrowser::getCwd(), "");        // getcwd falla

        openFileBrowser(ed);
        CHECK(ed.state_ == State::FileBrowser);          // no crashea
        CHECK_EQ(ed.fileBrowser.path_, "");
        CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(1));
        CHECK_EQ(ed.fileBrowser.entries_[0].name, "..");  // sube a raiz
        CHECK_EQ(ed.fileBrowser.entries_[0].fullPath, "/");
        CHECK(!ed.statusMessage_.empty());
        CHECK(ed.statusMessage_.find("No se pudo leer") != std::string::npos);

        press(ed, EventType::Escape);
        CHECK(ed.state_ == State::Navegacion);
        CHECK(ed.running_);
    } // ~CwdGuard restaura la cwd original (que sigue existiendo)
}

// ---------------------------------------------------------------------------
// 9. Integracion con multi-buffer (lista exhaustiva)
// ---------------------------------------------------------------------------
TEST(browser_multiple_opens_increase_buffer_count) {
    // (59) Abrir varios archivos desde el explorador suma buffers y deja
    // activo el ultimo.
    TempDir t;
    t.file("a.txt");
    t.file("b.txt");
    t.file("c.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));

    openFileBrowser(ed);  press(ed, EventType::MoveDown);  press(ed, EventType::InsertNewline); // a.txt
    openFileBrowser(ed);  press(ed, EventType::MoveDown);  press(ed, EventType::MoveDown);
                          press(ed, EventType::InsertNewline); // b.txt
    openFileBrowser(ed);  press(ed, EventType::MoveDown);  press(ed, EventType::MoveDown);
                          press(ed, EventType::MoveDown);  press(ed, EventType::InsertNewline); // c.txt

    CHECK_EQ(ed.buffers.buffers_.size(), size_t(4));      // SinNombre + a + b + c
    CHECK_EQ(ed.buffers.activeBuffer_, 3);
    CHECK_EQ(ed.active().filename, t.path + "/c.txt");
    CHECK_EQ(ed.active().document.lineAt(0), "c.txt");
}

TEST(browser_opened_files_appear_in_selector) {
    // (60) El selector Ctrl+K t lista los archivos abiertos desde el explorador.
    TempDir t;
    t.file("a.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);
    press(ed, EventType::InsertNewline);          // abre a.txt
    openSelector(ed);
    CHECK(ed.state_ == State::BufferSelector);
    const std::vector<std::string> names = ed.bufferNames();
    CHECK_EQ(names.size(), size_t(2));
    bool found = false;
    for (const std::string& n : names) if (n == "a.txt") found = true;
    CHECK(found);
}

TEST(browser_open_switch_reopen_open) {
    // (61) Abrir, cambiar de buffer, reabrir el explorador y abrir otro.
    TempDir t;
    t.file("a.txt");
    t.file("b.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> a.txt
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);

    // cambiar al buffer sin nombre (indice 0) via selector
    openSelector(ed);
    press(ed, EventType::MoveUp);     // indice 1 -> 0
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK(ed.active().filename.empty());

    // reabrir el explorador y abrir b.txt
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // a.txt
    press(ed, EventType::MoveDown);   // b.txt
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.buffers.activeBuffer_, 2);
    CHECK_EQ(ed.active().filename, t.path + "/b.txt");

    // volver a a.txt por selector
    openSelector(ed);
    press(ed, EventType::MoveUp);     // indice 2 -> 1 (a.txt)
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().filename, t.path + "/a.txt");
    CHECK_EQ(ed.active().document.lineAt(0), "a.txt");
}

// ===========================================================================
// Interaction: abrir B desde el explorador deja el buffer A intacto.
// A (indice 0) con contenido editado; Ctrl+K O -> FileBrowser -> seleccionar
// B -> Enter: B queda activo y A conserva contenido y bandera modified.
// ---------------------------------------------------------------------------
TEST(browser_interaction_open_keeps_previous_buffer_intact) {
    TempDir t;
    t.file("A.txt");
    t.file("B.txt");
    CwdGuard g;
    g.enter(t.path);

    Editor ed;
    type(ed, "contenido A");         // buffer A (indice 0), editado
    CHECK_EQ(ed.active().document.lineAt(0), "contenido A");
    CHECK(ed.active().modified);
    press(ed, EventType::Escape);    // -> Navegacion

    openFileBrowser(ed);             // Ctrl+K o
    CHECK(ed.state_ == State::FileBrowser);
    press(ed, EventType::MoveDown);  // -> A.txt
    press(ed, EventType::MoveDown);  // -> B.txt
    press(ed, EventType::InsertNewline);   // Enter: abrir B.txt
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK(ed.active().filename.find("B.txt") != std::string::npos);
    CHECK_EQ(ed.active().document.lineAt(0), "B.txt");   // contenido del archivo
    CHECK(!ed.active().modified);    // recien abierto: sin cambios

    // Volver a A: intacto (contenido + modified).
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().document.lineAt(0), "contenido A");
    CHECK(ed.active().modified);
}

// ===========================================================================
// Interaction: reabrir desde el explorador un archivo YA abierto solo lo
// ACTIVA, no crea un tercer buffer (v0.6.4: no duplicar archivos abiertos).
// A abierto, B abierto; FileBrowser -> seleccionar A -> Enter:
//   -> A activo y bufferCount sigue siendo 2.
// ---------------------------------------------------------------------------
TEST(browser_interaction_reopen_existing_only_activates) {
    TempDir t;
    t.file("A.txt");
    t.file("B.txt");
    CwdGuard g;
    g.enter(t.path);

    Editor ed;
    ed.openFile("A.txt");            // reusa buffer 0 = A
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));

    openFileBrowser(ed);             // Ctrl+K o
    press(ed, EventType::MoveDown);  // -> A.txt
    press(ed, EventType::MoveDown);  // -> B.txt
    press(ed, EventType::InsertNewline);   // abre B.txt (buffer 1)
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().filename, t.path + "/B.txt");

    // Reabrir el explorador y seleccionar A (ya abierto): activar, NO duplicar.
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);  // -> A.txt
    press(ed, EventType::InsertNewline);   // Enter sobre A.txt
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));   // NO tercer buffer
    CHECK_EQ(ed.buffers.activeBuffer_, 0);             // A reactivado
    CHECK_EQ(ed.active().filename, t.path + "/A.txt");
    CHECK_EQ(ed.active().document.lineAt(0), "A.txt");
    CHECK(ed.state_ != State::FileBrowser);   // se salio del explorador
}

// ===========================================================================
// Interaction: Abrir B desde el explorador, EDITARLO, volver a A y regresar a B.
// B debe conservar contenido, cursor, modified y su historial de undo.
// ---------------------------------------------------------------------------
TEST(browser_interaction_edit_back_and_return_preserves_b_state) {
    TempDir t;
    t.file("B.txt");
    CwdGuard g;
    g.enter(t.path);

    Editor ed;
    type(ed, "texto A");             // buffer 0 = A
    press(ed, EventType::Escape);    // -> Navegacion

    // Abrir B desde FileBrowser -> buffer 1, cursor (0,0), modified false.
    openFileBrowser(ed);             // Ctrl+K o
    press(ed, EventType::MoveDown);  // -> B.txt
    press(ed, EventType::InsertNewline);   // abre B.txt (buffer 1)
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    CHECK_EQ(ed.active().document.lineAt(0), "B.txt");
    CHECK(!ed.active().modified);

    // Editar B: teclear al inicio + mover el cursor en medio.
    type(ed, "NUEVO");               // "NUEVOB.txt", cursor (0,5), modified true
    press(ed, EventType::Escape);
    CHECK_EQ(ed.active().document.lineAt(0), "NUEVOB.txt");
    CHECK(ed.active().modified);
    press(ed, EventType::MoveHome);   // Navegacion: cursor a col 0
    press(ed, EventType::MoveRight);  // -> col 1
    press(ed, EventType::MoveRight);  // -> col 2
    press(ed, EventType::MoveRight);  // -> col 3
    CHECK_EQ(ed.active().cursor.col, 3);

    // Volver a A.
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().document.lineAt(0), "texto A");

    // Volver a B: conserva contenido, cursor y modified.
    ed.activateBuffer(1);
    CHECK_EQ(ed.buffers.activeBuffer_, 1);
    CHECK_EQ(ed.active().document.lineAt(0), "NUEVOB.txt");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(ed.active().modified);     // la edicion de B sigue marcada

    // El historial de undo de B sobrevive al cambio de buffer: undo revierte
    // (la escritura normal se deshace por caracter) y redo restaura.
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "NUEVB.txt");   // perdio la 'O'
    CHECK(ed.active().modified);                             // sigue editado
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "NUEVOB.txt");  // restaurado
    CHECK(ed.active().modified);
}

TEST(browser_close_opened_buffer) {
    // (62) Cerrar con Ctrl+K w un buffer abierto desde el explorador.
    TempDir t;
    t.file("a.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);
    press(ed, EventType::InsertNewline);          // abre a.txt (indice 1)
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));

    closeBuffer(ed);                              // cierra a.txt (sin modificar)
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK(ed.active().filename.empty());          // queda el buffer sin nombre
    CHECK(ed.state_ == State::Navegacion);        // cerrar no abre el selector
    CHECK_EQ(ed.buffers.activeBuffer_, 0);        // el unnamed hereda la ranura
    CHECK(ed.active().document.lineAt(0).empty());
}

TEST(browser_open_does_not_consume_unnamed_count) {
    // (63) Abrir archivos desde el explorador NO consume nombres "SinNombre":
    // el buffer inicial ya uso uno y el siguiente Ctrl+K n sigue siendo
    // "SinNombre1" (los archivos no son buffers anonimos).
    TempDir t;
    t.file("a.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    CHECK_EQ(ed.buffers.unnamedCounter_, 1);

    openFileBrowser(ed);
    press(ed, EventType::MoveDown);
    press(ed, EventType::InsertNewline);          // abre a.txt
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    CHECK_EQ(ed.buffers.unnamedCounter_, 1);              // NO crecio

    newBuffer(ed);                                // Ctrl+K n
    CHECK_EQ(ed.active().unnamedName, "SinNombre1");
    CHECK_EQ(ed.buffers.activeBuffer_, 2);
    CHECK(ed.active().filename.empty());
}

// ---------------------------------------------------------------------------
// 10. Estado y limpieza (lista exhaustiva)
// ---------------------------------------------------------------------------
TEST(browser_reopen_reads_directory_freshly) {
    // (65) Cada apertura relista el directorio: no hay lista cacheada.
    TempDir t;
    t.file("a.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(2)); // .., a.txt

    press(ed, EventType::Escape);
    t.file("later.txt");                    // se crea MIENTRAS el editor vive

    openFileBrowser(ed);                    // se vuelve a leer el directorio
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(3)); // .., a.txt, later.txt
    bool found = false;
    for (const FileBrowserEntry& e : ed.fileBrowser.entries_)
        if (e.name == "later.txt") found = true;
    CHECK(found);
}

TEST(browser_escape_after_navigation_no_side_effects) {
    // (66) Cancelar con ESC tras navegar por varios directorios no deja
    // efectos en el editor: sin buffers nuevos, sin archivos cargados y
    // con el mensaje de estado limpio.
    TempDir t;
    t.dir("x");
    t.file("x/y.txt");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> x
    press(ed, EventType::InsertNewline);   // entrar en x
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/x");
    press(ed, EventType::MoveDown);   // -> y.txt
    press(ed, EventType::MoveDown);   // clamp al final de la lista
    CHECK_EQ(ed.fileBrowser.index_,
             static_cast<int>(ed.fileBrowser.entries_.size()) - 1);
    press(ed, EventType::Escape);     // cancelar SIN abrir nada

    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));       // no se creo buffer
    CHECK(ed.active().filename.empty());           // no se cargo archivo
    CHECK(ed.active().document.lineAt(0).empty()); // documento intacto
    CHECK(ed.statusMessage_.empty());              // mensaje limpio
}

TEST(browser_prior_state_after_deep_navigation) {
    // (67) El modo previo se restaura fielmente aunque se haya navegado por
    // varios directorios antes de cancelar.
    TempDir t;
    t.dir("x");
    std::filesystem::create_directories(t.path + "/x/inner");
    CwdGuard g;
    g.enter(t.path);

    {   // desde Interaccion: bajar 2 niveles y volver
        Editor ed;
        type(ed, "hola");
        openFileBrowser(ed);
        press(ed, EventType::MoveDown);   // x
        press(ed, EventType::InsertNewline);
        press(ed, EventType::MoveDown);   // inner
        press(ed, EventType::InsertNewline);
        CHECK_EQ(ed.fileBrowser.path_, t.path + "/x/inner");
        press(ed, EventType::Escape);
        CHECK(ed.state_ == State::Interaccion);
        CHECK_EQ(ed.active().document.lineAt(0), "hola");
    }
    {   // desde Seleccion: la seleccion sobrevive a la navegacion
        Editor ed;
        type(ed, "abcdef");
        press(ed, EventType::Escape);
        press(ed, EventType::MoveHome);
        pressEvent(ed, insert('s'));
        press(ed, EventType::MoveRight);
        openFileBrowser(ed);
        press(ed, EventType::MoveDown);   // x
        press(ed, EventType::InsertNewline);
        press(ed, EventType::Escape);
        CHECK(ed.state_ == State::Seleccion);
        CHECK(ed.hasSelection());
    }
}

// ---------------------------------------------------------------------------
// 11. Barra de estado y mensajes (lista exhaustiva, via estado interno)
// ---------------------------------------------------------------------------
TEST(browser_status_label_is_abrir_archivo) {
    // (68) Mientras se está en FileBrowser el estado es coherente y la barra
    // lleva la etiqueta "ABRIR ARCHIVO". (La etiqueta la pinta buildFileListScreen;
    // aqui se alimenta con el estado real del editor.)
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK(!ed.statusMessage_.empty());           // ayuda al entrar

    Renderer r;
    const std::string out = r.buildFileListScreen(
        ed.fileBrowser.displayNames_, ed.fileBrowser.index_, ed.fileBrowser.scroll_,
        ed.fileBrowser.path_, ed.statusMessage_, 80, 10);
    CHECK(contains(out, "ABRIR ARCHIVO"));
}

TEST(browser_status_path_matches_current_dir) {
    // (69) La ruta dibujada en la barra es exactamente currentPath_.
    TempDir t;
    t.dir("x");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    press(ed, EventType::MoveDown);   // -> x
    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/x");

    Renderer r;
    const std::string out = r.buildFileListScreen(
        ed.fileBrowser.displayNames_, ed.fileBrowser.index_, ed.fileBrowser.scroll_,
        ed.fileBrowser.path_, ed.statusMessage_, 80, 10);
    CHECK(contains(out, t.path + "/x"));
}

TEST(browser_exit_clears_status_message) {
    // (71) Al salir del explorador el mensaje de status se limpia.
    TempDir t;
    t.dir("x");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    openFileBrowser(ed);
    CHECK(!ed.statusMessage_.empty());
    press(ed, EventType::MoveDown);   // -> x
    press(ed, EventType::InsertNewline);
    press(ed, EventType::Escape);     // cancelar
    CHECK(ed.state_ == State::Navegacion);
    CHECK(ed.statusMessage_.empty());
}

// ---------------------------------------------------------------------------
// Renderer del explorador
// ---------------------------------------------------------------------------
TEST(renderer_file_list_layout) {
    Renderer r;
    std::string out = r.buildFileListScreen(
        {"..", "sub/", "a.txt", "b.txt"}, 1, 0, "/tmp/sandbox",
        "ABRIR: arriba/abajo mover | Enter abrir/entrar | ESC cancelar", 80, 10);
    // Entradas: sin estilo la no seleccionada; la activa lleva el gris del
    // item marcado (listSelected: mismo lenguaje ACTIVO que el editor).
    // El fondo cubre TODO el ancho de la fila, no solo el texto: estilo,
    // texto, padding de espacios, y reciEN despues el reset.
    CHECK(contains(out, "  .."));
    std::string styledText = std::string(kListSelectedStyle) + "  sub/";
    size_t stylePos = out.find(styledText);
    CHECK(stylePos != std::string::npos);
    size_t textEnd = stylePos + styledText.size();
    CHECK(out.compare(textEnd, 4, "\x1b[0m") != 0);
    size_t resetPos = out.find("\x1b[0m", textEnd);
    CHECK(resetPos != std::string::npos);
    CHECK(resetPos > textEnd);
    std::string between = out.substr(textEnd, resetPos - textEnd);
    CHECK(between.find_first_not_of(' ') == std::string::npos);
    CHECK(contains(out, "  a.txt"));
    CHECK(contains(out, "  b.txt"));
    // Barra de estado con la ruta y la etiqueta de modo.
    CHECK(contains(out, "/tmp/sandbox"));
    CHECK(contains(out, "ABRIR ARCHIVO"));
    // Fila de mensajes con la ayuda.
    CHECK(contains(out, "Enter abrir/entrar"));
    // Filas vacias bajo la lista con el marcador del editor, alineado.
    CHECK(contains(out, "\x1b[K  " + std::string(kMarkerStyle) + "~\x1b[0m\r\n"));
}

TEST(renderer_file_list_scroll_hides_off_window) {
    Renderer r;
    // scroll=2 sobre 4 entradas con altura 2: se ven b.txt y c.txt, no a.txt.
    std::string out = r.buildFileListScreen(
        {"..", "a.txt", "b.txt", "c.txt"}, 3, 2, "/", "x", 80, 2);
    CHECK(contains(out, "  b.txt"));
    std::string styledText = std::string(kListSelectedStyle) + "  c.txt";
    size_t stylePos = out.find(styledText);
    CHECK(stylePos != std::string::npos);
    size_t textEnd = stylePos + styledText.size();
    // Mismo criterio: reset no pegado, hay padding antes.
    CHECK(out.compare(textEnd, 4, "\x1b[0m") != 0);
    size_t resetPos = out.find("\x1b[0m", textEnd);
    CHECK(resetPos != std::string::npos && resetPos > textEnd);
    CHECK(!contains(out, "  a.txt"));
    CHECK(!contains(out, "  .."));
}

TEST(browser_starts_at_active_file_directory_absolute) {
    TempDir t;
    t.dir("docs");
    t.file("docs/file.cpp");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    ed.active().filename = t.path + "/docs/file.cpp";
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/docs");
}

TEST(browser_starts_at_active_file_directory_other_dir) {
    TempDir t;
    t.dir("docs");
    t.dir("work");
    t.file("work/main.cpp");
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    ed.active().filename = t.path + "/work/main.cpp";
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/work");
}

TEST(browser_starts_at_active_buffer_directory_after_switch) {
    TempDir t;
    std::string docs = t.dir("docs");
    std::string work = t.dir("work");
    std::ofstream(docs + "/a.cpp") << "a";
    std::ofstream(work + "/b.cpp") << "b";
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    ed.active().filename = docs + "/a.cpp";
    newBuffer(ed);
    ed.active().filename = work + "/b.cpp";
    ed.activateBuffer(0);
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.path_, docs);
    press(ed, EventType::Escape);
    ed.activateBuffer(1);
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.path_, work);
}

TEST(browser_starts_at_cwd_when_unnamed_buffer) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    CHECK(ed.active().filename.empty());
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.path_, t.path);
}

TEST(browser_starts_at_cwd_when_relative_filename) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    ed.active().filename = "foo.cpp";
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.path_, t.path);
}

TEST(browser_falls_back_to_cwd_when_parent_missing) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    Editor ed;
    ed.active().filename = "/tmp/no_such_dir_xyz_maestro_test_123/file.cpp";
    openFileBrowser(ed);
    CHECK_EQ(ed.fileBrowser.path_, t.path);
}
