#include "test_framework.h"
#include "test_support.h"
#include "helpers/spy_screen_renderer.h"
#include "helpers/FakeClipboard.h"
#include "filesystem/NullFileWatcher.h"

// Convención de esta suite: todos los contadores del spy son acumulativos.
// Cada test captura la base de CADA contador que afirma, actúa, y compara
// deltas. Nunca se resetea nada a mitad de secuencia.
//
// Cada test posee su NullSink local (nada de estáticos compartidos): el
// sink vive lo que el test y no hay estado entre tests.

// ---------------------------------------------------------------------------
// 1. El frame no-modal engancha el SyntaxCache del buffer y lo suelta
// ---------------------------------------------------------------------------
// Contrato (no detalle interno): por cada renderScreenDiff hay exactamente
// un attach (puntero al cache del buffer activo) seguido de un detach
// (nullptr). Si el Editor dejara el cache enganchado, un cambio de buffer
// posterior resaltaría con el idioma del buffer anterior.
TEST(spy_render_attaches_and_detaches_syntax_cache) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    const int screenBase = spy.renderScreenDiffCount;
    const int cacheBase = spy.setCacheCount;
    const int nullBase = spy.setCacheNullCount;
    ed.renderFrame();
    CHECK(spy.renderScreenDiffCount == screenBase + 1);
    CHECK(spy.setCacheCount == cacheBase + 2);
    CHECK(spy.setCacheNullCount == nullBase + 1);
}

// ---------------------------------------------------------------------------
// 2. Ctor (clipboard, watcher) trae Null neutro; el spy se inyecta igual
// ---------------------------------------------------------------------------
TEST(spy_null_renderer_ctor_no_emit_theme_toggle) {
    auto clipboard = std::make_unique<FakeClipboard>();
    auto watcher = std::make_unique<NullFileWatcher>();
    Editor ed(std::move(clipboard), std::move(watcher));

    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    CHECK(spy.isDarkTheme());
    const int toggleBase = spy.toggleCount;
    ed.executeCommand("theme.toggle");
    CHECK(!spy.isDarkTheme());
    CHECK(spy.toggleCount == toggleBase + 1);
}

// ---------------------------------------------------------------------------
// 3. Modal BufferSelector: invalida en entrada y salida, no en frames intermedios
// (con 1 buffer el selector es no-op: se crean 2 antes)
// ---------------------------------------------------------------------------
TEST(spy_modal_selector_invalidates_entry_and_exit) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);
    newBuffer(ed); // 2 buffers para que el selector abra

    ed.renderFrame();
    CHECK(!spy.pendingInvalidation);
    const int invBeforeEvent = spy.invalidateCount;

    openSelector(ed);
    CHECK(ed.getStateForTesting() == State::BufferSelector);

    // El evento solo cambia el estado: no invalida. La invalidación viene
    // de la transición wasModal_ dentro de renderFrame (única fuente).
    CHECK(spy.invalidateCount == invBeforeEvent);
    const int invBase = spy.invalidateCount;
    const int listBase = spy.renderBufferListCount;
    const int screenBase = spy.renderScreenDiffCount;
    ed.renderFrame();
    CHECK(spy.invalidateCount > invBase);
    CHECK(spy.renderBufferListCount == listBase + 1);
    CHECK(spy.renderScreenDiffCount == screenBase);
    CHECK(!spy.pendingInvalidation); // el render la consumió
    // Contenido correcto: la lista de buffers y el índice activo.
    CHECK(spy.lastBufferNames == ed.bufferNames());
    CHECK(spy.lastBufferListSelected == ed.buffers.activeIndex());

    // Frame modal intermedio: cero invalidaciones extra
    const int invEntry = spy.invalidateCount;
    const int listEntry = spy.renderBufferListCount;
    ed.renderFrame();
    CHECK(spy.invalidateCount == invEntry);
    CHECK(spy.renderBufferListCount == listEntry + 1);

    press(ed, InputEventType::Escape);
    CHECK(ed.getStateForTesting() == State::Navegacion);

    // Salida: el Escape tampoco invalida; solo el renderFrame de transición.
    CHECK(spy.invalidateCount == invEntry);
    // Salida: al menos una invalidación + vuelta al render de pantalla
    const int invExit = spy.invalidateCount;
    const int screenExit = spy.renderScreenDiffCount;
    const int listExit = spy.renderBufferListCount;
    ed.renderFrame();
    CHECK(spy.invalidateCount > invExit);
    CHECK(spy.renderScreenDiffCount == screenExit + 1);
    CHECK(spy.renderBufferListCount == listExit);
}

// ---------------------------------------------------------------------------
// 4. Modal FileBrowser: mismo contrato que BufferSelector
// ---------------------------------------------------------------------------
TEST(spy_modal_filebrowser_invalidates_entry_and_exit) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    ed.renderFrame();
    const int invBeforeEvent = spy.invalidateCount;

    TempDir tmp;
    CwdGuard cwd;
    cwd.enter(tmp.path);
    tmp.file("a.txt"); // al menos una entrada para afirmar contenido

    openFileBrowser(ed);
    CHECK(ed.getStateForTesting() == State::FileBrowser);

    // Igual que el selector: el evento no invalida, solo renderFrame.
    CHECK(spy.invalidateCount == invBeforeEvent);
    const int invBase = spy.invalidateCount;
    const int fileBase = spy.renderFileListCount;
    const int screenBase = spy.renderScreenDiffCount;
    ed.renderFrame();
    CHECK(spy.invalidateCount > invBase);
    CHECK(spy.renderFileListCount == fileBase + 1);
    CHECK(spy.renderScreenDiffCount == screenBase);
    CHECK(!spy.pendingInvalidation);
    // Contenido correcto: ruta actual y 1:1 con las entradas del browser.
    CHECK(spy.lastFilePath == ed.fileBrowser.path_);
    CHECK(spy.lastFileItems.size() == ed.fileBrowser.entries_.size());
    CHECK(!spy.lastFileItems.empty());

    const int invEntry = spy.invalidateCount;
    const int fileEntry = spy.renderFileListCount;
    ed.renderFrame();
    CHECK(spy.invalidateCount == invEntry);
    CHECK(spy.renderFileListCount == fileEntry + 1);

    press(ed, InputEventType::Escape);
    CHECK(ed.getStateForTesting() == State::Navegacion);

    CHECK(spy.invalidateCount == invEntry);
    const int invExit = spy.invalidateCount;
    const int screenExit = spy.renderScreenDiffCount;
    const int fileExit = spy.renderFileListCount;
    ed.renderFrame();
    CHECK(spy.invalidateCount > invExit);
    CHECK(spy.renderScreenDiffCount == screenExit + 1);
    CHECK(spy.renderFileListCount == fileExit);
}

// ---------------------------------------------------------------------------
// 4b. Modal Guardar como: métricas propias, separadas de las de Abrir
// ---------------------------------------------------------------------------
TEST(spy_modal_saveas_has_own_metrics) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    TempDir tmp;
    CwdGuard cwd;
    cwd.enter(tmp.path);
    tmp.file("a.txt"); // al menos una entrada para afirmar contenido

    openSaveAs(ed);
    CHECK(ed.getStateForTesting() == State::SaveAsFileBrowser);
    clearPrompt(ed);
    typePrompt(ed, "nota.txt");

    const int fileBase = spy.renderFileListCount;
    const int saveAsBase = spy.renderSaveAsFileListCount;
    ed.renderFrame();
    // Guardar como suma en su contador y NO contamina el de Abrir.
    CHECK(spy.renderSaveAsFileListCount == saveAsBase + 1);
    CHECK(spy.renderFileListCount == fileBase);
    CHECK(!spy.pendingInvalidation);
    // Snapshot: items del browser + input en el mensaje (fila de mensajes).
    CHECK(spy.lastFilePath == ed.fileBrowser.path_);
    CHECK(spy.lastFileItems.size() == ed.fileBrowser.entries_.size());
    CHECK(!spy.lastFileItems.empty());
    CHECK(spy.lastMessage.text.find("Nombre del Archivo: nota.txt") != std::string::npos);

    // Con mensaje activo (confirmación de overwrite) manda el mensaje:
    // el input cede la fila hasta que el aviso se resuelva.
    clearPrompt(ed);
    typePrompt(ed, "a.txt");  // existe en tmp -> arma aviso
    saveAsConfirm(ed);
    CHECK(ed.statusMessage_.text.find("ya existe") != std::string::npos);
    ed.renderFrame();
    CHECK(spy.lastMessage.text.find("ya existe") != std::string::npos);
    CHECK(spy.lastMessage.text.find("Nombre del Archivo:") == std::string::npos);

    // Y al revés: Abrir no toca la métrica de Guardar como.
    press(ed, InputEventType::Escape);
    openFileBrowser(ed);
    CHECK(ed.getStateForTesting() == State::FileBrowser);
    const int fileBase2 = spy.renderFileListCount;
    const int saveAsBase2 = spy.renderSaveAsFileListCount;
    ed.renderFrame();
    CHECK(spy.renderFileListCount == fileBase2 + 1);
    CHECK(spy.renderSaveAsFileListCount == saveAsBase2);
}

// ---------------------------------------------------------------------------
// 5. Cambio de buffer invalida (deltas, no conteo exacto global)
// ---------------------------------------------------------------------------
TEST(spy_activate_buffer_invalidates) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    newBuffer(ed);
    CHECK_EQ(ed.buffers.count(), 2);

    const int invBase = spy.invalidateCount;
    const int screenBase = spy.renderScreenDiffCount;
    ed.activateBuffer(1);
    ed.renderFrame();
    CHECK(spy.invalidateCount > invBase);
    CHECK(spy.renderScreenDiffCount == screenBase + 1);

    const int invMid = spy.invalidateCount;
    ed.activateBuffer(0);
    ed.renderFrame();
    CHECK(spy.invalidateCount > invMid);
    CHECK(spy.renderScreenDiffCount == screenBase + 2);
}

TEST(spy_create_buffer_invalidates) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    const int invBase = spy.invalidateCount;
    newBuffer(ed);
    ed.renderFrame();
    CHECK(spy.invalidateCount >= invBase + 1);
}

TEST(spy_close_buffer_invalidates) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    newBuffer(ed);
    CHECK_EQ(ed.buffers.count(), 2);

    // Rama Removed: con 2 buffers, cerrar elimina y activa el vecino.
    const int invBase = spy.invalidateCount;
    closeBuffer(ed);
    CHECK_EQ(ed.buffers.count(), 1);
    ed.renderFrame();
    CHECK(spy.invalidateCount > invBase);

    // Rama ResetLast: con 1 buffer, cerrar reinicia en vez de eliminar.
    const int invMid = spy.invalidateCount;
    closeBuffer(ed);
    CHECK_EQ(ed.buffers.count(), 1);
    ed.renderFrame();
    CHECK(spy.invalidateCount > invMid);
}

// loadIntoActiveBuffer reutiliza el buffer activo (no hay cambio de buffer):
// el contenido lo detecta el diff, sin invalidación. Se documenta el
// contrato real en vez de exigir un invalidate que no existe.
TEST(spy_open_file_no_invalidate_diff_handles_content) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    TempDir tmp;
    CwdGuard cwd;
    cwd.enter(tmp.path);
    const std::string p = tmp.file("foo.txt");

    const int invBase = spy.invalidateCount;
    const int screenBase = spy.renderScreenDiffCount;
    CHECK(ed.loadIntoActiveBuffer(p));
    ed.renderFrame();
    CHECK(spy.invalidateCount == invBase);
    CHECK(spy.renderScreenDiffCount == screenBase + 1);
}

// ---------------------------------------------------------------------------
// 6. resize NO invalida (el diff detecta geometría)
// ---------------------------------------------------------------------------
// La igualdad exacta SÍ es el contrato aquí: resize() nunca llama a
// invalidateCache por diseño (ver comentario en Editor::resize); el backend
// detecta el cambio de geometría solo. Se cubren ambos casos: mismo tamaño
// (24x80 es el fallback inicial del Editor) y tamaño distinto.
TEST(spy_resize_no_invalidate) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    ed.renderFrame();
    const int invBase = spy.invalidateCount;

    ed.resize(24, 80);
    ed.renderFrame();
    CHECK(spy.invalidateCount == invBase);

    ed.resize(30, 100);
    ed.renderFrame();
    CHECK(spy.invalidateCount == invBase);
}

// ---------------------------------------------------------------------------
// 7. toggleTheme delega al renderer
// ---------------------------------------------------------------------------
TEST(spy_toggle_theme_delegates) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    CHECK(spy.isDarkTheme());
    const int toggleBase = spy.toggleCount;

    ed.executeCommand("theme.toggle");
    CHECK(!spy.isDarkTheme());
    CHECK(spy.toggleCount == toggleBase + 1);

    // El toggle deja mensaje de acción con el tema nuevo (texto + tipo).
    ed.renderFrame();
    CHECK(spy.lastMessage.text == "Tema claro");
    CHECK(spy.lastMessage.kind == MessageKind::Info);

    ed.executeCommand("theme.toggle");
    CHECK(spy.isDarkTheme());
    CHECK(spy.toggleCount == toggleBase + 2);

    ed.renderFrame();
    CHECK(spy.lastMessage.text == "Tema oscuro");
    CHECK(spy.lastMessage.kind == MessageKind::Info);
}

// ---------------------------------------------------------------------------
// 8. Dispatch por estado: cada estado llama a su render correspondiente
// ---------------------------------------------------------------------------
TEST(spy_render_dispatch_by_state) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);
    newBuffer(ed); // 2 buffers para que el selector abra

    const int screenBase = spy.renderScreenDiffCount;
    const int listBase = spy.renderBufferListCount;
    const int fileBase = spy.renderFileListCount;
    ed.renderFrame();
    CHECK(spy.renderScreenDiffCount == screenBase + 1);
    CHECK(spy.renderBufferListCount == listBase);
    CHECK(spy.renderFileListCount == fileBase);

    enterInteraccion(ed);
    ed.renderFrame();
    CHECK(spy.renderScreenDiffCount == screenBase + 2);
    CHECK(spy.renderBufferListCount == listBase);
    CHECK(spy.renderFileListCount == fileBase);

    // Volver a Navegacion antes de abrir modales (priorState limpio)
    press(ed, InputEventType::Escape);
    openSelector(ed);
    CHECK(ed.getStateForTesting() == State::BufferSelector);
    ed.renderFrame();
    CHECK(spy.renderBufferListCount == listBase + 1);
    CHECK(spy.renderScreenDiffCount == screenBase + 2);
    CHECK(spy.lastBufferNames == ed.bufferNames());
    CHECK(spy.lastBufferListSelected == ed.buffers.activeIndex());

    press(ed, InputEventType::Escape);
    CHECK(ed.getStateForTesting() == State::Navegacion);

    TempDir tmp;
    CwdGuard cwd;
    cwd.enter(tmp.path);
    tmp.file("a.txt");

    openFileBrowser(ed);
    CHECK(ed.getStateForTesting() == State::FileBrowser);
    ed.renderFrame();
    CHECK(spy.renderFileListCount == fileBase + 1);
    CHECK(spy.renderScreenDiffCount == screenBase + 2);
    CHECK(spy.renderBufferListCount == listBase + 1);
    CHECK(spy.lastFilePath == ed.fileBrowser.path_);
    CHECK(spy.lastFileItems.size() == ed.fileBrowser.entries_.size());
    CHECK(!spy.lastFileItems.empty());
}

// ---------------------------------------------------------------------------
// 9. El snapshot se limpia: un frame sin selección no conserva la anterior
// ---------------------------------------------------------------------------
TEST(spy_selection_snapshot_clears_when_no_selection) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);
    type(ed, "hello");

    selectFirstChars(ed, 3);
    CHECK(ed.hasSelection());
    ed.renderFrame();
    CHECK(spy.lastSelection.has_value());

    press(ed, InputEventType::Escape);
    CHECK(!ed.hasSelection());
    ed.renderFrame();
    CHECK(!spy.lastSelection.has_value());
}

// ---------------------------------------------------------------------------
// 10. invalidateScreen() público invalida; el render la consume
// ---------------------------------------------------------------------------
TEST(spy_invalidate_screen_public_api) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);

    ed.renderFrame();
    CHECK(!spy.pendingInvalidation);
    const int invBase = spy.invalidateCount;

    ed.invalidateScreen();
    CHECK(spy.invalidateCount == invBase + 1);
    CHECK(spy.pendingInvalidation);

    ed.renderFrame();
    CHECK(!spy.pendingInvalidation);
}

// ---------------------------------------------------------------------------
// 11. La invalidación pendiente también la consume un render modal
// (renderBufferList, no solo el diff de pantalla)
// ---------------------------------------------------------------------------
TEST(spy_invalidation_consumed_in_modal) {
    Editor ed;
    SpyScreenRenderer& spy = injectSpy(ed);
    NullSink sink;
    ed.setSink(sink);
    newBuffer(ed); // 2 buffers para que el selector abra

    openSelector(ed);
    CHECK(ed.getStateForTesting() == State::BufferSelector);
    ed.renderFrame();
    CHECK(!spy.pendingInvalidation);
    const int listBase = spy.renderBufferListCount;

    ed.invalidateScreen();
    CHECK(spy.pendingInvalidation);

    ed.renderFrame(); // sigue en modal
    CHECK(ed.getStateForTesting() == State::BufferSelector);
    CHECK(spy.renderBufferListCount == listBase + 1);
    CHECK(!spy.pendingInvalidation);
}
