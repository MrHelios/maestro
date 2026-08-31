#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "test_framework.h"

#define private public
#include "ui/Editor.h"
#include "filesystem/InotifyFileWatcher.h"
#undef private
#include "clipboard/FakeClipboard.h"

using testfw::TempFile;

static void writeFile(const std::string& p, const std::string& c) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << c;
    f.flush();
    f.close();
}

static bool pollOnce(InotifyFileWatcher& w, std::vector<FileChangeEvent>& out) {
    // esperar un poco a que inotify propague
    for (int i = 0; i < 20; ++i) {
        w.pollEvents([&](const FileChangeEvent& ev){ out.push_back(ev); });
        if (!out.empty()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return !out.empty();
}

TEST(watcher_integration_detects_modify_via_poll) {
    TempFile f; f.write("orig\n");
    InotifyFileWatcher w;
    if (w.fd() < 0) return;
    w.watch(f.path);
    writeFile(f.path, "new\n");
    std::vector<FileChangeEvent> evs;
    bool got = pollOnce(w, evs);
    CHECK(got);
    bool found = false;
    for (auto &e : evs) if (e.path == f.path && e.kind == FileChangeKind::Modified) found = true;
    CHECK(found);
    w.unwatch(f.path);
}

TEST(watcher_integration_detects_atomic_replace_via_poll) {
    TempFile f; f.write("v1\n");
    InotifyFileWatcher w;
    if (w.fd() < 0) return;
    w.watch(f.path);
    std::string tmp = f.path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "v2\n";
    }
    std::filesystem::rename(tmp, f.path);
    // contrato InotifyFileWatcher: atomic replace via rename() debe emitir
    // Deleted (file watch IN_MOVE_SELF) + Created (dir watch IN_MOVED_TO).
    // Se recolecta durante 200ms para capturar ambos si llegan en polls separados.
    std::vector<FileChangeEvent> evs;
    for (int i = 0; i < 20; ++i) {
        w.pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    bool hasDeleted = false, hasCreated = false;
    for (auto &e : evs) if (e.path == f.path) {
        if (e.kind == FileChangeKind::Deleted) hasDeleted = true;
        if (e.kind == FileChangeKind::Created) hasCreated = true;
    }
    CHECK(hasDeleted);
    CHECK(hasCreated);
    w.unwatch(f.path);
}

TEST(watcher_integration_detects_delete_via_poll) {
    TempFile f; f.write("keep\n");
    InotifyFileWatcher w;
    if (w.fd() < 0) return;
    w.watch(f.path);
    std::filesystem::remove(f.path);
    std::vector<FileChangeEvent> evs;
    bool got = pollOnce(w, evs);
    CHECK(got);
    bool found = false;
    for (auto &e : evs) if (e.path == f.path && e.kind == FileChangeKind::Deleted) found = true;
    CHECK(found);
    w.unwatch(f.path);
    // tras unwatch no debe generar más eventos. Se sondea en loop (~200ms)
    // y se corta apenas aparece algo: da margen al kernel sin alargar el
    // caso feliz, igual que closing_buffer_removes_watch_integration.
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "recreated\n";
    }
    bool still = false;
    for (int i = 0; i < 20; ++i) {
        std::vector<FileChangeEvent> after;
        w.pollEvents([&](const FileChangeEvent& ev){ if (ev.path == f.path) after.push_back(ev); });
        if (!after.empty()) { still = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(!still);
}

TEST(editor_integration_write_poll_reload) {
    TempFile f; f.write("A\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(f.path));
    CHECK_EQ(ed.active().document.lineAt(0), "A");
    writeFile(f.path, "B\n");
    // poll a través del watcher del editor
    bool reloaded = false;
    for (int i = 0; i < 20; ++i) {
        auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        if (w) {
            std::vector<FileChangeEvent> evs;
            w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
            for (auto &ev : evs) ed.handleFileChange(ev);
            if (ed.active().document.lineAt(0) == "B") { reloaded = true; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "B");
    CHECK(!ed.active().modified);
}

TEST(save_procesa_todos_los_eventos_no_warning) {
    TempFile f; f.write("orig\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(f.path));
    ed.active().document.restore({"nuevo"});
    ed.active().modified = true;
    ed.save();
    CHECK(!ed.active().modified);
    ed.statusMessage_ = Message{};
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    // save puede generar IN_MODIFY + IN_ATTRIB; drenar todos
    std::vector<FileChangeEvent> all;
    for (int i = 0; i < 20; ++i) {
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) {
            all.push_back(ev);
            ed.handleFileChange(ev);
        }
        if (!evs.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        else if (i > 5) break;
    }
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
    CHECK(ed.statusMessage_.text.find("cambi") == std::string::npos);
    CHECK(ed.statusMessage_.text.find("eliminado") == std::string::npos);
    CHECK_EQ(ed.active().document.lineAt(0), "nuevo");
    CHECK(!ed.active().modified);
}

TEST(save_con_modify_y_attrib_no_warning) {
    TempFile f; f.write("orig\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(f.path));
    ed.active().document.restore({"v2"});
    ed.active().modified = true;
    ed.save();
    // forzar IN_ATTRIB adicional
    std::filesystem::permissions(f.path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::filesystem::permissions(f.path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    ed.statusMessage_ = Message{};
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    for (int i = 0; i < 20; ++i) {
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
    CHECK_EQ(ed.active().document.lineAt(0), "v2");
    CHECK(!ed.active().modified);
}

TEST(save_end_to_end_trunc_watch_coupled) {
    TempFile f; f.write("orig\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(f.path));
    struct stat stBefore; stat(f.path.c_str(), &stBefore);
    ed.active().document.restore({"modified_via_save"});
    ed.active().modified = true;
    auto oldIdentity = ed.active().savedIdentity;
    ed.save();
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "modified_via_save");
    // trunc no cambia inode
    struct stat stAfter; stat(f.path.c_str(), &stAfter);
    CHECK_EQ(stBefore.st_ino, stAfter.st_ino);
    // drenar eventos del save, no debe haber warning
    ed.statusMessage_ = Message{};
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    for (int i = 0; i < 20; ++i) {
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
    CHECK(ed.active().savedIdentity.valid);
    CHECK(ed.active().savedIdentity != oldIdentity);
    // externo atomic replace tras save debe detectarse correctamente (inode cambia)
    std::string tmp = f.path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "external_after_save\n";
    }
    std::filesystem::rename(tmp, f.path);
    bool reloaded = false;
    for (int i = 0; i < 30; ++i) {
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        if (ed.active().document.lineAt(0) == "external_after_save") { reloaded = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "external_after_save");
}

TEST(watcher_recovery_double_atomic_replace) {
    TempFile f; f.write("v1\n");
    InotifyFileWatcher w;
    if (w.fd() < 0) return;
    w.watch(f.path);
    for (const char* v : {"v2\n", "v3\n"}) {
        std::string tmp = f.path + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << v;
        }
        std::filesystem::rename(tmp, f.path);
        bool got = false;
        for (int i = 0; i < 30; ++i) {
            std::vector<FileChangeEvent> evs;
            w.pollEvents([&](const FileChangeEvent& ev){ if (ev.path==f.path) evs.push_back(ev); });
            if (!evs.empty()) { got = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(got);
        // verificar que watcher sigue vivo para siguiente reemplazo
        CHECK(w.fd() >= 0);
    }
    w.unwatch(f.path);
    // tras unwatch, no debe generar más
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "v4\n";
    }
    std::vector<FileChangeEvent> evs;
    w.pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
    bool still = false;
    for (auto &e : evs) if (e.path==f.path) still=true;
    CHECK(!still);
}

TEST(editor_recovery_double_atomic_replace) {
    TempFile f; f.write("v1\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(f.path));
    CHECK_EQ(ed.active().document.lineAt(0), "v1");
    for (const char* v : {"v2\n", "v3\n"}) {
        std::string tmp = f.path + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << v;
        }
        std::filesystem::rename(tmp, f.path);
        bool reloaded = false;
        for (int i = 0; i < 30; ++i) {
            auto* ww = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
            if (ww) {
                std::vector<FileChangeEvent> evs;
                ww->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
                for (auto &ev : evs) ed.handleFileChange(ev);
                std::string expected(v);
                expected.pop_back();
                if (ed.active().document.lineAt(0) == expected) { reloaded = true; break; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(reloaded);
        CHECK(!ed.active().modified);
        CHECK(ed.active().savedIdentity.valid);
    }
    CHECK_EQ(ed.active().document.lineAt(0), "v3");
}

TEST(watcher_file_watch_is_idempotent) {
    TempFile f; f.write("x\n");
    InotifyFileWatcher w;
    if (w.fd() < 0) return;
    w.watch(f.path);
    size_t sizeAfterFirst = w.fileWatches_.size();
    size_t wdCountAfterFirst = w.wdToEntry_.size();
    w.watch(f.path);
    CHECK_EQ(w.fileWatches_.size(), sizeAfterFirst);
    CHECK_EQ(w.fileWatches_.size(), 1u);
    CHECK_EQ(w.wdToEntry_.size(), wdCountAfterFirst);
    // write debe generar exactamente un evento lógico, no dos por dos watches
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "y\n";
    }
    std::vector<FileChangeEvent> evs;
    for (int i = 0; i < 20; ++i) {
        w.pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        if (!evs.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    size_t countForFile = 0;
    for (auto &e : evs) if (e.path == f.path && e.kind == FileChangeKind::Modified) countForFile++;
    CHECK_EQ(countForFile, 1u);
    // contrato: watch idempotente, un solo unwatch debe eliminar
    w.unwatch(f.path);
    CHECK(w.fileWatches_.find(f.path) == w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(f.path) == w.trackedFiles_.end());
    // segundo unwatch no debe crashear
    w.unwatch(f.path);
    CHECK(true);
}

TEST(watcher_dir_watch_is_refcounted) {
    TempFile fa, fb;
    fa.write("a\n"); fb.write("b\n");
    InotifyFileWatcher w;
    if (w.fd() < 0) return;
    w.watch(fa.path);
    w.watch(fb.path);
    // ambos archivos comparten dir; watcher debe mantener dir watch vivo para ambos
    // verificamos comportamiento observable: ambos generan eventos y refCount se mantiene
    w.unwatch(fa.path);
    // fb aún debe generar evento
    {
        std::ofstream out(fb.path, std::ios::binary | std::ios::trunc);
        out << "b2\n";
    }
    std::vector<FileChangeEvent> evs;
    for (int i = 0; i < 20; ++i) {
        w.pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        if (!evs.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    bool got = false;
    for (auto &e : evs) if (e.path == fb.path) got = true;
    CHECK(got);
    w.unwatch(fb.path);
    CHECK(w.dirWatches_.empty());
}

TEST(editor_two_buffers_same_file_share_watch_real) {
    TempFile f; f.write("shared\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(f.path));
    // crear segundo buffer con mismo archivo
    Buffer second = ed.active();
    second.unnamedName = "";
    ed.buffers.push(std::move(second));
    ed.buffers.at(1).filename = ed.buffers.at(0).filename;
    ed.buffers.at(1).originalSnapshot_ = ed.buffers.at(0).originalSnapshot_;
    ed.buffers.at(1).savedIdentity = ed.buffers.at(0).savedIdentity;
    ed.buffers.at(1).document.restore({"shared"});
    ed.watchFile(ed.buffers.at(1).filename);
    // ambos deben recargar con un solo evento lógico
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "ext\n";
    }
    bool bothReloaded = false;
    for (int i = 0; i < 20; ++i) {
        auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        if (w) {
            std::vector<FileChangeEvent> evs;
            w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
            for (auto &ev : evs) ed.handleFileChange(ev);
            if (ed.buffers.at(0).document.lineAt(0) == "ext" && ed.buffers.at(1).document.lineAt(0) == "ext") { bothReloaded = true; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(bothReloaded);
    // cerrar uno debe mantener watch para el otro
    ed.buffers.activeBuffer_ = 0;
    ed.closeActiveBuffer();
    CHECK_EQ(ed.buffers.count(), 1);
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "ext2\n";
    }
    bool stillWatched = false;
    for (int i = 0; i < 20; ++i) {
        auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        if (w) {
            std::vector<FileChangeEvent> evs;
            w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
            for (auto &ev : evs) ed.handleFileChange(ev);
            if (ed.active().document.lineAt(0) == "ext2") { stillWatched = true; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(stillWatched);
}

TEST(save_as_new_file_end_to_end_isNew_hadWatch) {
    TempFile fOrig; fOrig.write("orig\n");
    TempFile fNew;
    std::filesystem::remove(fNew.path);
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(fOrig.path));
    ed.active().document.restore({"save_as_content"});
    ed.active().modified = true;
    // simular SaveAs a nuevo path (isNew=true)
    ed.saveAsPath_ = fNew.path;
    // commitSaveAs es privado pero accesible via private hack
    ed.commitSaveAs();
    CHECK_EQ(ed.active().filename, std::filesystem::absolute(fNew.path).lexically_normal().string());
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "save_as_content");
    // viejo archivo no debe estar watchado, nuevo sí
    ed.statusMessage_ = Message{};
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    for (int i = 0; i < 10; ++i) {
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
    // modificar nuevo archivo externamente debe recargar
    writeFile(fNew.path, "new_external\n");
    bool reloaded = false;
    for (int i = 0; i < 20; ++i) {
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        if (ed.active().document.lineAt(0) == "new_external") { reloaded = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "new_external");
    // modificar viejo archivo no debe afectar
    writeFile(fOrig.path, "old_external\n");
    ed.statusMessage_ = Message{};
    for (int i = 0; i < 10; ++i) {
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK_EQ(ed.active().document.lineAt(0), "new_external");
}
