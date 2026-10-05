#include "test_support.h"
#include "helpers/FakeClipboard.h"
#define private public
#include "filesystem/InotifyFileWatcher.h"
#undef private

static void openRename(Editor& ed) {
    press(ed, InputEventType::Prefix);
    pressEvent(ed, insert('r'));
}
static void clearRename(Editor& ed) {
    while (!ed.renameQuery_.empty()) press(ed, InputEventType::Backspace);
}
static void confirmRename(Editor& ed) { press(ed, InputEventType::InsertNewline); }

TEST(rename_sin_nombre_cancela) {
    Editor ed;
    CHECK(ed.active().filename.empty());
    openRename(ed);
    // sin nombre: cancela solo, no entra a Renombrar
    CHECK(static_cast<int>(ed.state_) != static_cast<int>(State::Renombrar));
}

TEST(rename_ok) {
    TempDir dir;
    std::string orig = dir.file("orig.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK_EQ(ed.renameQuery_, std::string("orig.txt"));
    clearRename(ed);
    typeBytes(ed, "nuevo.txt");
    confirmRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
    std::string expected = dir.path + "/nuevo.txt";
    CHECK_EQ(ed.active().filename, expected);
    CHECK(!std::filesystem::exists(orig));
    CHECK(std::filesystem::exists(expected));
}

TEST(rename_escape_cancela) {
    TempDir dir;
    std::string orig = dir.file("a.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openRename(ed);
    typeBytes(ed, "XXX");
    press(ed, InputEventType::Escape);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().filename, orig);
    CHECK(std::filesystem::exists(orig));
}

TEST(rename_con_ruta_no_crea_dirs) {
    TempDir dir;
    std::string orig = dir.file("a.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openRename(ed);
    clearRename(ed);
    CHECK(ed.renameQuery_.empty());
    // Contrato: el input con '/' se RECHAZA entero (no se filtra a
    // "subdir.txt"): el query queda intacto. Se envia como un solo
    // evento para verificar que nada parcial se aplica.
    pressEvent(ed, insertBytes("sub/dir.txt"));
    CHECK(ed.renameQuery_.empty());
    CHECK(!std::filesystem::exists(dir.path + "/sub"));
    CHECK(std::filesystem::exists(orig));
    // '\' se rechaza igual.
    pressEvent(ed, insertBytes("a\\b.txt"));
    CHECK(ed.renameQuery_.empty());
    CHECK(std::filesystem::exists(orig));
    // Enter con query vacio no renombra y sigue en el prompt.
    confirmRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK(std::filesystem::exists(orig));
    CHECK(!std::filesystem::exists(dir.path + "/sub"));
}

TEST(rename_vacio_y_largo) {
    TempDir dir;
    std::string orig = dir.file("a.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openRename(ed);
    clearRename(ed);
    confirmRename(ed);
    // vacio: se queda en Renombrar
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK(std::filesystem::exists(orig));
    press(ed, InputEventType::Escape);
}

TEST(rename_existente_rechaza) {
    TempDir dir;
    std::string a = dir.file("a.txt");
    std::string b = dir.file("b.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(a));
    openRename(ed);
    clearRename(ed);
    typeBytes(ed, "b.txt");
    confirmRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK(std::filesystem::exists(a));
    CHECK(std::filesystem::exists(b));
    CHECK_EQ(ed.active().filename, a);
}

TEST(rename_modificado_bloquea) {
    TempDir dir;
    std::string orig = dir.file("a.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    type(ed, "X");
    CHECK(ed.active().modified);

    openRename(ed);

    CHECK(static_cast<int>(ed.state_) != static_cast<int>(State::Renombrar));
    CHECK_EQ(ed.active().filename, orig);
    CHECK(ed.statusMessage_.text.find("Guarda antes de renombrar.") !=
          std::string::npos);
}

TEST(rename_limite_255_aceptado) {
    TempDir dir;
    std::string orig = dir.file("a.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openRename(ed);
    clearRename(ed);
    typeBytes(ed, std::string(255, 'x'));
    CHECK_EQ(ed.renameQuery_.size(), size_t(255));
}

TEST(rename_limite_256_rechazado) {
    TempDir dir;
    std::string orig = dir.file("a.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openRename(ed);
    clearRename(ed);
    typeBytes(ed, std::string(255, 'x'));
    typeBytes(ed, "y");
    CHECK_EQ(ed.renameQuery_.size(), size_t(255));
    CHECK_EQ(ed.renameQuery_, std::string(255, 'x'));
    CHECK(std::filesystem::exists(orig));
}

TEST(rename_utf8_cuenta_bytes) {
    // 😀 = 4 bytes UTF-8: el limite de 255 es en bytes, no caracteres.
    const std::string emoji = "\U0001F600";
    CHECK_EQ(emoji.size(), size_t(4));
    TempDir dir;
    std::string orig = dir.file("a.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openRename(ed);
    clearRename(ed);
    // 254 bytes + emoji (4) = 258 > 255: rechazado, query intacto.
    typeBytes(ed, std::string(254, 'x'));
    pressEvent(ed, insertBytes(emoji));
    CHECK_EQ(ed.renameQuery_.size(), size_t(254));
    CHECK(std::filesystem::exists(orig));
    // 251 bytes + emoji (4) = 255: aceptado justo.
    clearRename(ed);
    typeBytes(ed, std::string(251, 'x'));
    pressEvent(ed, insertBytes(emoji));
    CHECK_EQ(ed.renameQuery_.size(), size_t(255));
    CHECK(validUtf8(ed.renameQuery_));
}

TEST(rename_punto_y_puntopunto_rechazados) {
    TempDir dir;
    std::string orig = dir.file("a.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openRename(ed);
    clearRename(ed);
    typeBytes(ed, ".");
    confirmRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK_EQ(ed.active().filename, orig);
    CHECK(std::filesystem::exists(orig));
    clearRename(ed);
    typeBytes(ed, "..");
    confirmRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK_EQ(ed.active().filename, orig);
    CHECK(std::filesystem::exists(orig));
}

TEST(rename_carpeta_destino_rechazada) {
    TempDir dir;
    std::string orig = dir.file("a.txt");
    std::string sub = dir.dir("dest");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openRename(ed);
    clearRename(ed);
    typeBytes(ed, "dest");
    confirmRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK_EQ(ed.active().filename, orig);
    CHECK(std::filesystem::exists(orig));
    CHECK(Editor::isDirectory(sub));
}

TEST(rename_destino_abierto_en_otro_buffer_rechazado) {
    testfw::TempFile f1;
    f1.write("uno");
    testfw::TempFile f2;
    f2.write("dos");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f1.path));
    newBuffer(ed);
    CHECK(ed.loadIntoActiveBuffer(f2.path));
    CHECK_EQ(ed.buffers.count(), 2);

    openRename(ed);
    clearRename(ed);
    typeBytes(ed, std::filesystem::path(f1.path).filename().string());
    confirmRename(ed);  // debe rechazar: sigue en Renombrar

    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK_EQ(ed.buffers.count(), 2);
    CHECK_EQ(ed.active().filename, f2.path);
    CHECK_EQ(fileContent(f1.path), "uno");
    CHECK_EQ(fileContent(f2.path), "dos");
    CHECK(ed.statusMessage_.text.find("ya abierto") != std::string::npos);
}

TEST(rename_ruta_demasiado_larga_sigue_en_modal) {
    // Directorio tan profundo que dir + "/" + nombre supera 4000 bytes
    // (componentes de 99, muy por debajo del NAME_MAX de 255).
    TempDir top;
    std::filesystem::path deep(top.path);
    for (int i = 0; i < 40; ++i)
        deep /= std::string(99, char('a' + (i % 26)));
    std::error_code ec;
    std::filesystem::create_directories(deep, ec);
    CHECK(!ec);
    const std::string orig = (deep / "a.txt").string();
    CHECK(orig.size() < size_t(4096));
    {
        std::ofstream f(orig, std::ios::binary | std::ios::trunc);
        f << "x";
    }
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    openRename(ed);
    clearRename(ed);
    typeBytes(ed, "z");
    confirmRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK_EQ(ed.active().filename, orig);
    CHECK(std::filesystem::exists(orig));
}

TEST(rename_error_rename_sigue_en_modal) {
    testfw::TempFile f;
    f.write("hola");
    const std::string newBase =
        std::filesystem::path(testfw::tmpPath()).filename().string();
    const std::string newAbs =
        (std::filesystem::path(f.path).parent_path() / newBase).string();
    std::remove(newAbs.c_str());

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f.path));
    const std::string oldAbs = ed.active().filename;
    // El archivo desaparece del disco antes de confirmar: rename() falla.
    std::remove(f.path.c_str());

    openRename(ed);
    clearRename(ed);
    typeBytes(ed, newBase);
    confirmRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK_EQ(ed.active().filename, oldAbs);
    CHECK(!std::filesystem::exists(newAbs));
}

TEST(rename_mismo_nombre_es_noop) {
    TempDir dir;
    std::string orig = dir.file("a.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    const std::string abs = ed.active().filename;
    openRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    // Sin editar el query (pre-cargado con el basename): mismo nombre.
    CHECK_EQ(ed.renameQuery_, std::string("a.txt"));
    confirmRename(ed);
    CHECK_EQ(ed.active().filename, abs);
    CHECK(std::filesystem::exists(abs));
    CHECK(ed.statusMessage_.text.find("Sin cambios.") != std::string::npos);
}

TEST(rename_executeCommand_puerta_gui) {
    // El comando debe funcionar via CommandMap directo (boton GUI), sin
    // pasar por Prefix: regla InputEvent/CommandMap de architecture.md.
    TempDir dir;
    std::string orig = dir.file("a.txt");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    CHECK(ed.hasCommand("archivo.renombrar"));

    // Desde Navegacion: entra al prompt con el basename pre-cargado.
    ed.executeCommand("archivo.renombrar");
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK_EQ(ed.renameQuery_, std::string("a.txt"));
    press(ed, InputEventType::Escape);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));

    // Desde Seleccion con texto marcado: Esc vuelve a Seleccion con la
    // seleccion intacta (priorState_ capturado en la via GUI).
    {
        std::ofstream f(orig, std::ios::binary | std::ios::trunc);
        f << "hola mundo";
    }
    CHECK(ed.loadIntoActiveBuffer(orig));
    selectFirstChars(ed, 4);
    CHECK(ed.hasSelection());
    ed.executeCommand("archivo.renombrar");
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    CHECK(ed.hasSelection());
    press(ed, InputEventType::Escape);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    CHECK(std::filesystem::exists(orig));
}

TEST(rename_actualiza_watcher) {    testfw::TempFile fOrig;
    fOrig.write("contenido");
    const std::string newBase =
        std::filesystem::path(testfw::tmpPath()).filename().string();
    const std::string newAbs =
        (std::filesystem::path(fOrig.path).parent_path() / newBase).string();
    std::remove(newAbs.c_str());

    Editor ed(std::make_unique<FakeClipboard>(),
              std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));
    const std::string oldAbs = ed.active().filename;
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    CHECK(w->fileWatches_.find(oldAbs) != w->fileWatches_.end());

    openRename(ed);
    clearRename(ed);
    typeBytes(ed, newBase);
    confirmRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().filename, newAbs);

    CHECK(w->fileWatches_.find(oldAbs) == w->fileWatches_.end());
    CHECK(w->fileWatches_.find(newAbs) != w->fileWatches_.end());
    CHECK(ed.watchedFiles_.find(oldAbs) == ed.watchedFiles_.end());
    CHECK(ed.watchedFiles_.find(newAbs) != ed.watchedFiles_.end());

    std::remove(newAbs.c_str());
}

TEST(rename_desde_seleccion_esc_preserva_invariante) {
    TempDir dir;
    std::string orig = dir.file("a.txt");
    {
        std::ofstream f(orig, std::ios::binary | std::ios::trunc);
        f << "hola mundo";
    }
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(orig));
    // Marcar texto: entrar a Seleccion y extender
    selectFirstChars(ed, 4);
    CHECK(ed.hasSelection());
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Seleccion));
    // Activar renombrar desde Seleccion (Ctrl+K r)
    openRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    // La seleccion sigue viva durante el modal
    CHECK(ed.hasSelection());
    // Esc vuelve a priorState_ (Seleccion), con la seleccion intacta:
    // no debe dejar (hasSelection && Navegacion).
    press(ed, InputEventType::Escape);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    bool bad = ed.hasSelection() &&
               static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion);
    CHECK(!bad);
    CHECK(std::filesystem::exists(orig));
}
