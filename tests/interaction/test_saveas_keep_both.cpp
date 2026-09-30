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

    openSaveAs(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
    clearPrompt(ed);
    typePrompt(ed, fNew.path);
    press(ed, InputEventType::InsertNewline);  // Enter: commitSaveAs

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
    // Sin editar el prompt: guardar sobre el mismo path.
    press(ed, InputEventType::InsertNewline);
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

    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, fNew.path);
    press(ed, InputEventType::InsertNewline);

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
    typePrompt(ed, f1.path);
    press(ed, InputEventType::InsertNewline);  // Enter: debe rechazar

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

    ed.saveAsPath_ = f.path;
    ed.commitSaveAs();
    ed.commitSaveAs();  // re-guardar mismo path: no debe duplicar el watch
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

    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, fDest.path);
    press(ed, InputEventType::InsertNewline);  // 1er Enter: arma aviso
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
    CHECK(ed.statusMessage_.text.find("ya existe") != std::string::npos);
    CHECK_EQ(fileContent(fDest.path), "contenido B viejo");  // disco intacto
    CHECK_EQ(ed.buffers.count(), 1);

    press(ed, InputEventType::InsertNewline);  // 2do Enter: confirma
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
    typePrompt(ed, fDest.path);
    press(ed, InputEventType::InsertNewline);  // arma aviso
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
    typePrompt(ed, fDest.path);
    press(ed, InputEventType::InsertNewline);  // arma aviso para fDest
    CHECK(ed.statusMessage_.text.find("ya existe") != std::string::npos);
    clearPrompt(ed);
    typePrompt(ed, fFresh.path);  // cambia a ruta nueva
    press(ed, InputEventType::InsertNewline);  // guarda directo, sin 2do Enter
    CHECK_EQ(ed.buffers.count(), 2);
    CHECK_EQ(fileContent(fFresh.path), "contenido A");
    CHECK_EQ(fileContent(fDest.path), "contenido B viejo");
}

TEST(save_as_unnamed_to_existing_file_requires_confirm) {
    testfw::TempFile fDest;
    fDest.write("ajeno");
    Editor ed;
    newBuffer(ed);
    type(ed, "nuevo");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, fDest.path);
    press(ed, InputEventType::InsertNewline);  // 1er Enter: arma aviso
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
    CHECK(ed.active().filename.empty());
    CHECK_EQ(fileContent(fDest.path), "ajeno");
    press(ed, InputEventType::InsertNewline);  // 2do Enter: confirma
    CHECK_EQ(ed.active().filename, fDest.path);
    CHECK_EQ(fileContent(fDest.path), "nuevo");
}

TEST(save_as_unnamed_to_already_open_buffer_rejected) {
    testfw::TempFile f1;
    f1.write("uno");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f1.path));  // buffer 0 -> f1
    newBuffer(ed);                            // buffer 1 sin nombre, activo
    type(ed, "nuevo");
    press(ed, InputEventType::Escape);

    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, f1.path);
    press(ed, InputEventType::InsertNewline);  // Enter: debe rechazar

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
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, fNew.path);
    press(ed, InputEventType::InsertNewline);
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
    std::string badPath =
        "/tmp/maestro_saveas_failure_" + std::to_string(::getpid()) + "/file.txt";
    std::filesystem::remove_all(badPath);
    typePrompt(ed, badPath);
    press(ed, InputEventType::InsertNewline);

    CHECK_EQ(ed.buffers.count(), countBefore);
    CHECK_EQ(ed.active().filename, oldFilename);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::SaveAs));
}

TEST(save_as_unnamed_buffer_does_not_duplicate_buffer) {
    testfw::TempFile f;
    Editor ed;
    newBuffer(ed);
    const int countBefore = ed.buffers.count();  // 2: SinNombre + SinNombre1
    type(ed, "hello");
    press(ed, InputEventType::Escape);
    openSaveAs(ed);
    clearPrompt(ed);
    typePrompt(ed, f.path);
    press(ed, InputEventType::InsertNewline);
    // Buffer sin nombre previo: solo toma el nombre, no duplica.
    CHECK_EQ(ed.buffers.count(), countBefore);
    CHECK_EQ(ed.active().filename, f.path);
    CHECK(!ed.active().modified);
}
