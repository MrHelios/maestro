// Tests de interaccion con mouse en los modos lista:
//   - BufferSelector      (Ctrl+K t)
//   - FileBrowser         (Ctrl+K o)      -> abrir archivo
//   - SaveAsFileBrowser   (Ctrl+K Ctrl+S) -> guardar archivo
//
// Regla comun: un click sobre una fila distinta a la seleccionada mueve el
// cursor de la lista; un click sobre la fila YA seleccionada la activa
// (misma accion que Enter). En Guardar como un archivo nunca se abre.
// Todos los tests llevan el prefijo `list_mouse_` para filtrarlos.

#include "test_support.h"
#include "platform/MouseEvent.h"

namespace {

// Click completo (press + release) sobre la fila `row` del listado, en la
// columna 0. La release modela el click real; tras una activacion es no-op.
void clickRow(Editor& ed, int row) {
    const CellPos cell{0, row};
    ed.handleEvent(makeMousePressEvent(cell));
    ed.handleEvent(makeMouseReleaseEvent(cell));
}

}  // namespace

// ---------------------------------------------------------------------------
//  Lista de buffers: click mueve el cursor y, sobre la fila seleccionada,
//  abre el buffer (como Enter).
// ---------------------------------------------------------------------------
TEST(list_mouse_buffer_selector_click_moves_then_activates) {
    Editor ed;
    newBuffer(ed);  // B1
    newBuffer(ed);  // B2 (activo)
    CHECK_EQ(ed.buffers.count(), 3);
    CHECK_EQ(ed.buffers.activeBuffer_, 2);

    openSelector(ed);
    CHECK(ed.state_ == State::BufferSelector);
    // MRU = [B2, B1, B0]: el selector arranca en el anterior (posicion 1).
    CHECK_EQ(ed.bufferSelectorIndex_, 1);

    // Click en otra fila: solo mueve el cursor, no cambia el buffer activo.
    clickRow(ed, 2);
    CHECK(ed.state_ == State::BufferSelector);
    CHECK_EQ(ed.bufferSelectorIndex_, 2);
    CHECK_EQ(ed.buffers.activeBuffer_, 2);

    // Click sobre la fila ya seleccionada: abre ese buffer y sale del modal.
    clickRow(ed, 2);
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
}

// ---------------------------------------------------------------------------
// Abrir archivo: click mueve el cursor; sobre la fila seleccionada
// ".." sube, una carpeta entra y un archivo se abre.
// ---------------------------------------------------------------------------
TEST(list_mouse_file_browser_click_moves_then_activates) {
    TempDir t;
    t.dir("carpeta");
    t.file("archivo.txt");
    CwdGuard g;
    g.enter(t.path);

    Editor ed;
    openFileBrowser(ed);
    // entries: "..", "carpeta/", "archivo.txt"
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(3));
    CHECK_EQ(ed.fileBrowser.index_, 0);

    // Click en "carpeta": mueve el cursor sin entrar.
    clickRow(ed, 1);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.fileBrowser.index_, 1);
    CHECK_EQ(ed.fileBrowser.path_, t.path);

    // Segundo click sobre la carpeta seleccionada: entra.
    clickRow(ed, 1);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/carpeta");
    CHECK_EQ(ed.fileBrowser.index_, 0);

    // ".." ya esta seleccionado: el click sube un nivel.
    clickRow(ed, 0);
    CHECK_EQ(ed.fileBrowser.path_, t.path);

    // Click en "archivo.txt": mueve el cursor, no abre.
    clickRow(ed, 2);
    CHECK(ed.state_ == State::FileBrowser);
    CHECK_EQ(ed.fileBrowser.index_, 2);
    CHECK_EQ(ed.buffers.count(), 1);

    // Segundo click sobre el archivo seleccionado: lo abre.
    clickRow(ed, 2);
    CHECK(ed.state_ == State::Navegacion);
    CHECK_EQ(ed.buffers.count(), 2);
    CHECK_EQ(ed.active().filename, t.path + "/archivo.txt");
}

// ---------------------------------------------------------------------------
// Guardar archivo: mismo click que abrir para mover/entrar, pero un
// archivo NUNCA se abre (solo las carpetas son activables).
// ---------------------------------------------------------------------------
TEST(list_mouse_save_as_click_moves_then_activates_directories_only) {
    TempDir t;
    t.dir("carpeta");
    t.file("archivo.txt");
    CwdGuard g;
    g.enter(t.path);

    Editor ed;
    openSaveAs(ed);
    CHECK(ed.state_ == State::SaveAsFileBrowser);
    // entries: "..", "carpeta/", "archivo.txt"
    CHECK_EQ(ed.fileBrowser.entries_.size(), size_t(3));
    CHECK_EQ(ed.fileBrowser.index_, 0);

    // Click en "carpeta": mueve el cursor sin entrar.
    clickRow(ed, 1);
    CHECK(ed.state_ == State::SaveAsFileBrowser);
    CHECK_EQ(ed.fileBrowser.index_, 1);
    CHECK_EQ(ed.fileBrowser.path_, t.path);

    // Segundo click sobre la carpeta seleccionada: entra.
    clickRow(ed, 1);
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/carpeta");

    // ".." ya esta seleccionado: el click sube un nivel.
    clickRow(ed, 0);
    CHECK_EQ(ed.fileBrowser.path_, t.path);

    // Click en "archivo.txt": mueve el cursor, no abre ni guarda.
    clickRow(ed, 2);
    CHECK_EQ(ed.fileBrowser.index_, 2);
    CHECK(ed.state_ == State::SaveAsFileBrowser);

    // Segundo click sobre el archivo seleccionado: no-op (abrir no disponible).
    clickRow(ed, 2);
    CHECK(ed.state_ == State::SaveAsFileBrowser);
    CHECK_EQ(ed.fileBrowser.index_, 2);
    CHECK_EQ(ed.fileBrowser.path_, t.path);
    CHECK_EQ(ed.buffers.count(), 1);
    CHECK(ed.saveAsFileName_.empty());
}
