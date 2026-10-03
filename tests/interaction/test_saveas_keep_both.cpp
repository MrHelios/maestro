#include "test_support.h"
#include "helpers/FakeClipboard.h"
#define private public
#include "filesystem/InotifyFileWatcher.h"
#undef private

// BUG: Ctrl+K Ctrl+S (Guardar como) con nuevo nombre reemplaza el buffer.
// En disco quedan los dos archivos, pero en memoria se pierde el viejo:
// buffers.count() sigue en 1 y el filename original ya no existe en
// ningun buffer. Lo esperado: conservar ambos (viejo + nuevo).

TEST(save_as_new_path_keeps_both_buffers) {
    testfw::TempFile fOrig;
    fOrig.write("contenido X");
    testfw::TempFile fNew;
    // fNew debe NO existir antes del SaveAs para probar creacion.
    std::remove(fNew.path.c_str());

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));
    CHECK_EQ(ed.buffers.count(), 1);
    std::string oldAbs = ed.active().filename;
    std::string newFileName = std::filesystem::path(fNew.path).filename().string();

    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    clearPrompt(ed);
    typePrompt(ed, newFileName);
    saveAsConfirm(ed);  // Ctrl+S: commitSaveAsFileBrowser

    // Disco: ambos archivos existen con el mismo contenido.
    CHECK_EQ(fileContent(fOrig.path), "contenido X");
    CHECK_EQ(fileContent(fNew.path), "contenido X");

    // Buffer: deben existir AMBOS (viejo + nuevo).
    CHECK_EQ(ed.buffers.count(), 2);
    bool hasOld = false, hasNew = false;
    std::string newAbs;
    for (int i = 0; i < ed.buffers.count(); ++i) {
        if (ed.buffers.at(i).filename == oldAbs) hasOld = true;
        // nuevo path normalizado a absoluta (resolveAbsolutePath)
        if (fileContent(ed.buffers.at(i).filename) == "contenido X" &&
            ed.buffers.at(i).filename != oldAbs) {
            hasNew = true;
            newAbs = ed.buffers.at(i).filename;
        }
    }
    CHECK(hasOld);
    CHECK(hasNew);
    // El activo debe ser el nuevo archivo guardado.
    if (hasNew) CHECK_EQ(ed.active().filename, newAbs);
    CHECK(!ed.active().modified);
}

TEST(save_as_same_path_does_not_duplicate_buffer) {
    testfw::TempFile f;
    f.write("hola");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f.path));
    type(ed, "X");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    // Sin editar el prompt: guardar sobre el mismo path (saveAsFileName_ ya tiene el nombre actual).
    saveAsConfirm(ed);
    CHECK_EQ(ed.buffers.count(), 1);
    CHECK(!ed.active().modified);
}

TEST(save_as_new_buffer_gets_fresh_id_and_activates) {
    testfw::TempFile fOrig;
    fOrig.write("contenido X");
    testfw::TempFile fNew;
    std::remove(fNew.path.c_str());

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));
    const int oldId = ed.active().id;
    std::string newFileName = std::filesystem::path(fNew.path).filename().string();

    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, newFileName);
    saveAsConfirm(ed);

    CHECK_EQ(ed.buffers.count(), 2);
    // push() asigna id fresco y deja activo el nuevo (sin activateBuffer).
    CHECK(ed.active().id != oldId);
    CHECK_EQ(ed.buffers.activeIndex(), ed.buffers.count() - 1);
    CHECK_EQ(ed.buffers.at(0).id, oldId);
    CHECK(ed.active().filename != ed.buffers.at(0).filename);
}

TEST(save_as_to_already_open_buffer_rejected) {
    testfw::TempFile f1;
    f1.write("uno");
    testfw::TempFile f2;
    f2.write("dos");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f1.path));
    newBuffer(ed);
    CHECK(ed.loadIntoActiveBuffer(f2.path));
    CHECK_EQ(ed.buffers.count(), 2);

    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, std::filesystem::path(f1.path).filename().string());
    saveAsConfirm(ed);  // Ctrl+S: debe rechazar

    CHECK_EQ(ed.buffers.count(), 2);
    CHECK_EQ(ed.active().filename, f2.path);
    CHECK_EQ(fileContent(f1.path), "uno");  // disco intacto
    CHECK(ed.statusMessage_.text.find("ya abierto") != std::string::npos);
}

TEST(save_as_same_path_twice_single_watch_released_on_close) {
    testfw::TempFile f;
    f.write("hola");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(f.path));
    std::string abs = ed.active().filename;
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);

    // Usar el nuevo flujo: abrir SaveAsFileBrowser y confirmar con Ctrl+S
    openSaveAs(ed);
    saveAsConfirm(ed);  // primer guardado
    saveAsConfirm(ed);  // re-guardar mismo path: no debe duplicar el watch
    CHECK_EQ(w->fileWatches_.size(), size_t(1));
    CHECK(w->fileWatches_.find(abs) != w->fileWatches_.end());

    closeBuffer(ed);  // unico buffer limpio -> ResetLast -> unwatch
    CHECK(w->fileWatches_.find(abs) == w->fileWatches_.end());
    CHECK(w->trackedFiles_.find(abs) == w->trackedFiles_.end());
}

TEST(save_as_to_existing_file_requires_confirm) {
    testfw::TempFile fOrig;
    fOrig.write("contenido A");
    testfw::TempFile fDest;
    fDest.write("contenido B viejo");

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));
    std::string oldAbs = ed.active().filename;
    std::string destFileName = std::filesystem::path(fDest.path).filename().string();

    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, destFileName);
    saveAsConfirm(ed);  // 1er Ctrl+S: arma aviso
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK(ed.statusMessage_.text.find("ya existe") != std::string::npos);
    CHECK_EQ(fileContent(fDest.path), "contenido B viejo");  // disco intacto
    CHECK_EQ(ed.buffers.count(), 1);

    saveAsConfirm(ed);  // 2do Ctrl+S: confirma
    CHECK_EQ(ed.buffers.count(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "contenido A");
    CHECK_EQ(fileContent(fDest.path), "contenido A");
    CHECK_EQ(fileContent(fOrig.path), "contenido A");
    bool hasOld = false;
    for (int i = 0; i < ed.buffers.count(); ++i)
        if (ed.buffers.at(i).filename == oldAbs) hasOld = true;
    CHECK(hasOld);
}

TEST(save_as_to_existing_file_escape_cancels) {
    testfw::TempFile fOrig;
    fOrig.write("contenido A");
    testfw::TempFile fDest;
    fDest.write("contenido B viejo");

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, std::filesystem::path(fDest.path).filename().string());
    saveAsConfirm(ed);  // arma aviso
    CHECK(ed.statusMessage_.text.find("ya existe") != std::string::npos);
    press(ed, InputEventType::Escape);  // cancela
    CHECK_EQ(ed.buffers.count(), 1);
    CHECK_EQ(fileContent(fDest.path), "contenido B viejo");
    CHECK_EQ(ed.statusMessage_, "Guardado cancelado.");
}

TEST(save_as_confirm_editing_path_rearms) {
    testfw::TempFile fOrig;
    fOrig.write("contenido A");
    testfw::TempFile fDest;
    fDest.write("contenido B viejo");
    testfw::TempFile fFresh;
    std::remove(fFresh.path.c_str());  // no existe

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, std::filesystem::path(fDest.path).filename().string());
    saveAsConfirm(ed);  // arma aviso para fDest
    CHECK(ed.statusMessage_.text.find("ya existe") != std::string::npos);
    clearPrompt(ed);
    typePrompt(ed, std::filesystem::path(fFresh.path).filename().string());  // cambia a ruta nueva
    saveAsConfirm(ed);  // guarda directo, sin 2do Enter
    CHECK_EQ(ed.buffers.count(), 2);
    CHECK_EQ(fileContent(fFresh.path), "contenido A");
    CHECK_EQ(fileContent(fDest.path), "contenido B viejo");
}

TEST(save_as_unnamed_to_existing_file_requires_confirm) {
    testfw::TempFile fDest;
    fDest.write("ajeno");
    // El browser arranca en cwd: entrar al directorio del destino.
    CwdGuard g;
    g.enter(std::filesystem::path(fDest.path).parent_path().string());

    Editor ed;
    newBuffer(ed);
    type(ed, "nuevo");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, std::filesystem::path(fDest.path).filename().string());
    saveAsConfirm(ed);  // 1er Ctrl+S: arma aviso
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK(ed.active().filename.empty());
    CHECK_EQ(fileContent(fDest.path), "ajeno");
    saveAsConfirm(ed);  // 2do Ctrl+S: confirma
    CHECK_EQ(ed.active().filename, fDest.path);
    CHECK_EQ(fileContent(fDest.path), "nuevo");
}

TEST(save_as_unnamed_to_already_open_buffer_rejected) {
    testfw::TempFile f1;
    f1.write("uno");
    // El browser arranca en cwd: entrar al directorio del destino.
    CwdGuard g;
    g.enter(std::filesystem::path(f1.path).parent_path().string());

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f1.path));  // buffer 0 -> f1
    newBuffer(ed);                            // buffer 1 sin nombre, activo
    type(ed, "nuevo");
    press(ed, InputEventType::Escape);

    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, std::filesystem::path(f1.path).filename().string());
    saveAsConfirm(ed);  // Ctrl+S: debe rechazar

    CHECK_EQ(ed.buffers.count(), 2);
    CHECK(ed.active().filename.empty());  // sigue sin nombre
    CHECK_EQ(ed.active().document.lineAt(0), "nuevo");
    CHECK(ed.active().modified);
    CHECK_EQ(fileContent(f1.path), "uno");  // disco intacto
    CHECK(ed.statusMessage_.text.find("ya abierto") != std::string::npos);
}

TEST(save_as_then_ctrl_k_b_returns_to_old) {
    testfw::TempFile fOrig;
    fOrig.write("contenido X");
    testfw::TempFile fNew;
    std::remove(fNew.path.c_str());

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));
    std::string oldAbs = ed.active().filename;
    std::string newFileName = std::filesystem::path(fNew.path).filename().string();
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, newFileName);
    saveAsConfirm(ed);
    CHECK_EQ(ed.buffers.count(), 2);

    previousBuffer(ed);  // Ctrl+K b -> vuelve a X
    CHECK_EQ(ed.active().filename, oldAbs);
    CHECK_EQ(ed.active().document.lineAt(0), "contenido X");
}

TEST(save_as_failure_creates_no_clone) {
    testfw::TempFile fOrig;
    fOrig.write("orig");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));
    const int countBefore = ed.buffers.count();
    const std::string oldFilename = ed.active().filename;

    openSaveAs(ed);
    clearPrompt(ed);
    // Nombre imposible de crear (componente > NAME_MAX -> ENAMETOOLONG):
    // provoca un error real de escritura sin salirse del contrato
    // basename (sin slashes: el input ya no los acepta).
    typePrompt(ed, std::string(300, 'a') + ".txt");
    saveAsConfirm(ed);

    CHECK_EQ(ed.buffers.count(), countBefore);
    CHECK_EQ(ed.active().filename, oldFilename);
    CHECK(ed.statusMessage_.text.find("Error al guardar") != std::string::npos);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
}

TEST(save_as_unnamed_buffer_does_not_duplicate_buffer) {
    testfw::TempFile f;
    // El browser arranca en cwd: entrar al directorio del destino.
    CwdGuard g;
    g.enter(std::filesystem::path(f.path).parent_path().string());

    Editor ed;
    newBuffer(ed);
    const int countBefore = ed.buffers.count();  // 2: SinNombre + SinNombre1
    type(ed, "hello");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, std::filesystem::path(f.path).filename().string());
    saveAsConfirm(ed);
    // Buffer sin nombre previo: solo toma el nombre, no duplica.
    CHECK_EQ(ed.buffers.count(), countBefore);
    CHECK_EQ(ed.active().filename, f.path);
    CHECK(!ed.active().modified);
}

// Traba SaveAsFileBrowser: Enter sobre un archivo es no-op (no lo abre,
// no cambia de buffer ni de directorio, se sigue en el modal).
TEST(saveas_browser_enter_on_file_is_noop) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    { std::ofstream f(t.path + "/visible.txt", std::ios::binary); f << "data"; }
    std::filesystem::create_directories(t.path + "/sub");

    Editor ed;
    type(ed, "x");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    const int countBefore = ed.buffers.count();
    const std::string pathBefore = ed.fileBrowser.path_;
    // enterDirectoryOnly() promete no tocar pendingPath(): fijarlo antes.
    const std::string pendingBefore = ed.fileBrowser.pendingPath();

    // Localizar un archivo (no-directorio) en el listado y seleccionarlo.
    int fileIdx = -1;
    for (int i = 0; i < static_cast<int>(ed.fileBrowser.entries_.size()); ++i) {
        if (!ed.fileBrowser.entries_[static_cast<size_t>(i)].isDirectory) {
            fileIdx = i;
            break;
        }
    }
    CHECK(fileIdx >= 0);
    while (ed.fileBrowser.index_ < fileIdx) press(ed, InputEventType::MoveDown);
    while (ed.fileBrowser.index_ > fileIdx) press(ed, InputEventType::MoveUp);
    press(ed, InputEventType::InsertNewline);  // Enter sobre archivo

    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK_EQ(ed.buffers.count(), countBefore);
    CHECK(ed.active().filename.empty());  // no se abrio nada
    CHECK_EQ(ed.fileBrowser.path_, pathBefore);  // no se cambio de directorio
    CHECK_EQ(ed.fileBrowser.pendingPath(), pendingBefore);  // sin ruta pendiente
}

// Traba SaveAsFileBrowser: Enter sobre una carpeta SI entra (navegacion
// del directorio destino sigue funcionando).
TEST(saveas_browser_enter_on_directory_enters) {
    TempDir t;
    CwdGuard g;
    g.enter(t.path);
    std::filesystem::create_directories(t.path + "/sub");

    Editor ed;
    type(ed, "x");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    const int countBefore = ed.buffers.count();
    const std::string pathBefore = ed.fileBrowser.path_;

    int dirIdx = -1;
    for (int i = 0; i < static_cast<int>(ed.fileBrowser.entries_.size()); ++i) {
        const auto& e = ed.fileBrowser.entries_[static_cast<size_t>(i)];
        if (e.isDirectory && e.name == "sub") { dirIdx = i; break; }
    }
    CHECK(dirIdx >= 0);
    while (ed.fileBrowser.index_ < dirIdx) press(ed, InputEventType::MoveDown);
    while (ed.fileBrowser.index_ > dirIdx) press(ed, InputEventType::MoveUp);
    press(ed, InputEventType::InsertNewline);  // Enter sobre carpeta

    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK_EQ(ed.buffers.count(), countBefore);  // entrar no abre buffers
    CHECK(ed.fileBrowser.path_ != pathBefore);
    CHECK(ed.fileBrowser.path_.size() >= 4 &&
          ed.fileBrowser.path_.compare(ed.fileBrowser.path_.size() - 3, 3, "sub") == 0);
}

// Navegación real "..": el mecanismo central del feature para cambiar de
// directorio (reemplaza escribir rutas arbitrarias). Desde temp/a/b sube dos
// niveles con Enter y guarda en el directorio final alcanzado.
TEST(saveas_browser_dotdot_navigates_up_and_saves_there) {
    TempDir t;
    std::filesystem::create_directories(t.path + "/a/b");
    CwdGuard g;
    g.enter(t.path + "/a/b");

    Editor ed;
    type(ed, "x");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    const int countBefore = ed.buffers.count();
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/a/b");

    auto selectDotDot = [&]() {
        int dotIdx = -1;
        for (int i = 0; i < static_cast<int>(ed.fileBrowser.entries_.size()); ++i) {
            if (ed.fileBrowser.entries_[static_cast<size_t>(i)].name == "..") {
                dotIdx = i;
                break;
            }
        }
        CHECK(dotIdx >= 0);
        while (ed.fileBrowser.index_ < dotIdx) press(ed, InputEventType::MoveDown);
        while (ed.fileBrowser.index_ > dotIdx) press(ed, InputEventType::MoveUp);
    };

    selectDotDot();
    press(ed, InputEventType::InsertNewline);  // Enter en ".." -> temp/a
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK_EQ(ed.buffers.count(), countBefore);  // entrar no abre buffers
    CHECK_EQ(ed.fileBrowser.path_, t.path + "/a");

    selectDotDot();
    press(ed, InputEventType::InsertNewline);  // Enter en ".." -> temp
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAsFileBrowser));
    CHECK_EQ(ed.buffers.count(), countBefore);
    CHECK_EQ(ed.fileBrowser.path_, t.path);

    // Guardar en el directorio seleccionado tras navegar.
    clearPrompt(ed);
    typePrompt(ed, "subido.txt");
    saveAsConfirm(ed);  // Ctrl+S
    CHECK_EQ(ed.active().filename, t.path + "/subido.txt");
    CHECK(!ed.active().modified);
    CHECK_EQ(fileContent(t.path + "/subido.txt"), "x");
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
}
