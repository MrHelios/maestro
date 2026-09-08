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

static bool writeFile(const std::string& p, const std::string& c) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << c;
    f.flush();
    f.close();
    return f.good();
}

struct TempDir {
    std::string path;
    explicit TempDir(const std::string& p) : path(p) { std::filesystem::create_directories(path); }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

template <typename Pred>
static bool pollUntil(InotifyFileWatcher& w, std::vector<FileChangeEvent>& out, Pred pred) {
    for (int i = 0; i < 20; ++i) {
        w.pollEvents([&](const FileChangeEvent& ev){ out.push_back(ev); });
        if (pred(out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred(out);
}

template <typename Pred>
static bool pollEditorUntil(Editor& ed, Pred pred) {
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    if (!w) return false;
    for (int i = 0; i < 30; ++i) {
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

static void drainWatcher(InotifyFileWatcher& w) {
    for (int i = 0; i < 20; ++i) {
        std::vector<FileChangeEvent> evs;
        w.pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        if (evs.empty() && i > 5) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static void drainEditor(Editor& ed) {
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    if (!w) return;
    for (int i = 0; i < 20; ++i) {
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        if (evs.empty() && i > 5) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// No event for the observation window (6 consecutive empty polls).
// Does not guarantee that an event will never arrive, only that none arrived during the window.
template <typename Pred>
static bool pollForQuietPeriod(InotifyFileWatcher& w, std::vector<FileChangeEvent>& out, Pred pred) {
    int emptyStreak = 0;
    for (int i = 0; i < 20; ++i) {
        std::vector<FileChangeEvent> batch;
        w.pollEvents([&](const FileChangeEvent& ev){ batch.push_back(ev); out.push_back(ev); });
        if (pred(out)) return true;
        if (batch.empty()) emptyStreak++; else emptyStreak = 0;
        if (emptyStreak > 5) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred(out);
}

static void checkWatcherInvariants(const InotifyFileWatcher& w) {
    CHECK_EQ(w.fileWatches_.size() + w.dirWatches_.size(), w.wdToEntry_.size());
    CHECK_EQ(w.wdToEntry_.size(), w.refCount_.size());
    for (auto& [path, pr] : w.fileWatches_) {
        int wd = pr.first; uint64_t gen = pr.second;
        auto eit = w.wdToEntry_.find(wd);
        CHECK(eit != w.wdToEntry_.end());
        CHECK_EQ(eit->second.path, path);
        CHECK(!eit->second.isDir);
        CHECK_EQ(eit->second.gen, gen);
        CHECK(w.refCount_.find(wd) != w.refCount_.end());
        CHECK(w.refCount_.at(wd).second >= 1);
    }
    for (auto& [dir, pr] : w.dirWatches_) {
        int wd = pr.first; uint64_t gen = pr.second;
        auto eit = w.wdToEntry_.find(wd);
        CHECK(eit != w.wdToEntry_.end());
        CHECK_EQ(eit->second.path, dir);
        CHECK(eit->second.isDir);
        CHECK_EQ(eit->second.gen, gen);
        CHECK(w.refCount_.find(wd) != w.refCount_.end());
        CHECK(w.refCount_.at(wd).second >= 1);
    }
}

template <typename Pred>
static bool pollEditorUntilWithModifiedCount(Editor& ed, const std::string& path, size_t& outModified, Pred pred) {
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    if (!w) return false;
    outModified = 0;
    for (int i = 0; i < 30; ++i) {
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){
            evs.push_back(ev);
            if (ev.path == path && ev.kind == FileChangeKind::Modified) outModified++;
        });
        for (auto &ev : evs) ed.handleFileChange(ev);
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

// ── Tests de implementación InotifyFileWatcher (contrato interno inotify) ──
TEST(watcher_integration_detects_modify_via_poll) {
    TempFile f; f.write("orig\n");
    InotifyFileWatcher w;
    CHECK(w.fd() >= 0);
    w.watch(f.path);
    CHECK(w.fileWatches_.find(f.path) != w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(f.path) != w.trackedFiles_.end());
    CHECK(writeFile(f.path, "new\n"));
    std::vector<FileChangeEvent> evs;
    bool got = pollUntil(w, evs, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path == f.path && e.kind == FileChangeKind::Modified) return true;
        return false;
    });
    CHECK(got);
    w.unwatch(f.path);
}

TEST(watcher_integration_detects_atomic_replace_via_poll) {
    TempFile f; f.write("v1\n");
    InotifyFileWatcher w;
    CHECK(w.fd() >= 0);
    w.watch(f.path);
    CHECK(w.fileWatches_.find(f.path) != w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(f.path) != w.trackedFiles_.end());
    std::string tmp = f.path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "v2\n";
    }
    std::filesystem::rename(tmp, f.path);
    // TEST DE IMPLEMENTACIÓN - contrato específico de InotifyFileWatcher:
    // atomic replace via rename() emite Deleted (IN_MOVE_SELF sobre file watch)
    // + Created (IN_MOVED_TO sobre dir watch). No es contrato del Editor;
    // los tests de Editor deben verificar comportamiento observable
    // (document == nueva versión, savedIdentity, modified==false), no
    // la secuencia exacta de eventos inotify, que puede variar según
    // recuperación del watch.
    std::vector<FileChangeEvent> evs;
    pollUntil(w, evs, [&](const std::vector<FileChangeEvent>& v){
        bool hasDeleted=false, hasCreated=false;
        for (auto &e : v) if (e.path==f.path) {
            if (e.kind==FileChangeKind::Deleted) hasDeleted=true;
            if (e.kind==FileChangeKind::Created) hasCreated=true;
        }
        return hasDeleted && hasCreated;
    });
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
    CHECK(w.fd() >= 0);
    w.watch(f.path);
    CHECK(w.fileWatches_.find(f.path) != w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(f.path) != w.trackedFiles_.end());
    std::filesystem::remove(f.path);
    std::vector<FileChangeEvent> evs;
    bool got = pollUntil(w, evs, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path == f.path && e.kind == FileChangeKind::Deleted) return true;
        return false;
    });
    CHECK(got);
    w.unwatch(f.path);
    // tras unwatch no debe generar más eventos.
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "recreated\n";
    }
    std::vector<FileChangeEvent> after;
    pollUntil(w, after, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path==f.path) return true;
        return false;
    });
    bool still = false;
    for (auto &e : after) if (e.path==f.path) still=true;
    CHECK(!still);
}

// ── Tests de integración Editor (comportamiento observable, no secuencia inotify) ──
// Estos verifican: document == versión externa, savedIdentity actualizada,
// modified==false. No deben atarse a Deleted+Created exacto.
TEST(editor_integration_write_poll_reload) {
    TempFile f; f.write("A\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(f.path));
    CHECK_EQ(ed.active().document.lineAt(0), "A");
    CHECK(writeFile(f.path, "B\n"));
    bool reloaded = pollEditorUntil(ed, [&]{ return ed.active().document.lineAt(0)=="B"; });
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "B");
    CHECK(!ed.active().modified);
}

TEST(editor_external_change_with_local_modifications_preserves_buffer_and_warns) {
    TempFile f; f.write("A\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(f.path));
    CHECK_EQ(ed.active().document.lineAt(0), "A");
    ed.active().document.restore({"local"});
    ed.active().modified = true;
    auto localIdentity = ed.active().savedIdentity;
    CHECK(writeFile(f.path, "external\n"));
    // external change while modified should not reload, must warn and preserve local
    bool warned = pollEditorUntil(ed, [&]{
        return ed.statusMessage_.text.find("ALERTA") != std::string::npos;
    });
    CHECK(warned);
    CHECK_EQ(ed.active().document.lineAt(0), "local");
    CHECK(ed.active().modified);
    CHECK(ed.active().savedIdentity == localIdentity);
    // ensure subsequent external change still warns (not auto-reload)
    CHECK(writeFile(f.path, "external2\n"));
    ed.statusMessage_ = Message{};
    bool warned2 = pollEditorUntil(ed, [&]{
        return ed.statusMessage_.text.find("ALERTA") != std::string::npos;
    });
    CHECK(warned2);
    CHECK_EQ(ed.active().document.lineAt(0), "local");
}

TEST(editor_dirty_vs_clean_external_change_distinguishes_warning_and_reload) {
    TempFile f; f.write("orig\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(f.path));
    // Phase 1: dirty buffer + external change => warning, preserve local
    ed.active().document.restore({"local_dirty"});
    ed.active().modified = true;
    CHECK(writeFile(f.path, "external_dirty\n"));
    bool warned = pollEditorUntil(ed, [&]{ return ed.statusMessage_.text.find("ALERTA") != std::string::npos; });
    CHECK(warned);
    CHECK_EQ(ed.active().document.lineAt(0), "local_dirty");
    CHECK(ed.active().modified);
    // save own events must not warn (clean transition)
    ed.statusMessage_ = Message{};
    ed.save();
    CHECK(!ed.active().modified);
    drainEditor(ed);
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
    CHECK_EQ(ed.active().document.lineAt(0), "local_dirty");
    // Phase 2: clean buffer + external change => reload, no warning
    CHECK(writeFile(f.path, "external_clean\n"));
    bool reloaded = pollEditorUntil(ed, [&]{ return ed.active().document.lineAt(0) == "external_clean"; });
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "external_clean");
    CHECK(!ed.active().modified);
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
}

TEST(save_procesa_todos_los_eventos_no_warning) {
    TempFile f; f.write("orig\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(f.path));
    ed.active().document.restore({"nuevo"});
    ed.active().modified = true;
    ed.statusMessage_ = Message{};
    ed.save();
    CHECK(!ed.active().modified);
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    // save puede generar IN_MODIFY + IN_ATTRIB; drenar todos
    drainEditor(ed);
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
    CHECK(ed.statusMessage_.text.find("cambi") == std::string::npos);
    CHECK(ed.statusMessage_.text.find("eliminado") == std::string::npos);
    CHECK_EQ(ed.active().document.lineAt(0), "nuevo");
    CHECK(!ed.active().modified);
}

TEST(save_con_modify_y_attrib_no_warning) {
    TempFile f; f.write("orig\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(f.path));
    ed.active().document.restore({"v2"});
    ed.active().modified = true;
    ed.statusMessage_ = Message{};
    ed.save();
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    drainEditor(ed);
    // forzar IN_ATTRIB adicional (InotifyFileWatcher mapea IN_ATTRIB -> Modified)
    std::filesystem::permissions(f.path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::filesystem::permissions(f.path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    bool sawAttrib = false;
    std::vector<FileChangeEvent> attribEvs;
    pollForQuietPeriod(*w, attribEvs, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path==f.path && e.kind==FileChangeKind::Modified) sawAttrib=true;
        return sawAttrib;
    });
    for (auto &e : attribEvs) ed.handleFileChange(e);
    drainEditor(ed);
    CHECK(sawAttrib);
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
    CHECK_EQ(ed.active().document.lineAt(0), "v2");
    CHECK(!ed.active().modified);
}

TEST(save_end_to_end_trunc_watch_coupled) {
    TempFile f; f.write("orig\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(f.path));
    struct stat stBefore; CHECK_EQ(stat(f.path.c_str(), &stBefore), 0);
    ed.active().document.restore({"modified_via_save"});
    ed.active().modified = true;
    auto oldIdentity = ed.active().savedIdentity;
    ed.statusMessage_ = Message{};
    ed.save();
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "modified_via_save");
    // Save-in-place must preserve the existing file identity (device+inode).
    // Truncating/re-writing through the same pathname preserves the inode;
    // atomic-replace would not. This verifies the current save method.
    struct stat stAfter; CHECK_EQ(stat(f.path.c_str(), &stAfter), 0);
    CHECK_EQ(stBefore.st_dev, stAfter.st_dev);
    CHECK_EQ(stBefore.st_ino, stAfter.st_ino);
    // drenar eventos del save, no debe haber warning
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    drainEditor(ed);
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
    CHECK(ed.active().savedIdentity.valid);
    CHECK(ed.active().savedIdentity != oldIdentity);
    CHECK_EQ(ed.active().savedIdentity.dev, stAfter.st_dev);
    CHECK_EQ(ed.active().savedIdentity.ino, stAfter.st_ino);
    // observable Editor: tras atomic replace externo debe recargar document/savedIdentity,
    // sin exigir secuencia Deleted+Created exacta.
    // externo atomic replace tras save debe detectarse correctamente (inode cambia)
    auto beforeReplace = ed.active().savedIdentity;
    struct stat stBeforeReplace; CHECK_EQ(stat(f.path.c_str(), &stBeforeReplace), 0);
    std::string tmp = f.path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "external_after_save\n";
    }
    std::filesystem::rename(tmp, f.path);
    bool reloaded = pollEditorUntil(ed, [&]{ return ed.active().document.lineAt(0)=="external_after_save"; });
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "external_after_save");
    CHECK(ed.active().savedIdentity.valid);
    CHECK(ed.active().savedIdentity != beforeReplace);
    struct stat stAfterReplace; CHECK_EQ(stat(f.path.c_str(), &stAfterReplace), 0);
    CHECK(stAfterReplace.st_ino != stBeforeReplace.st_ino);
    CHECK_EQ(ed.active().savedIdentity.dev, stAfterReplace.st_dev);
    CHECK_EQ(ed.active().savedIdentity.ino, stAfterReplace.st_ino);
}

TEST(watcher_recovery_double_atomic_replace) {
    TempFile f; f.write("v1\n");
    InotifyFileWatcher w;
    CHECK(w.fd() >= 0);
    w.watch(f.path);
    CHECK(w.fileWatches_.find(f.path) != w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(f.path) != w.trackedFiles_.end());
    int modIdx = 0;
    for (const char* v : {"v2\n", "v3\n"}) {
        std::string tmp = f.path + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << v;
        }
        std::filesystem::rename(tmp, f.path);
        std::vector<FileChangeEvent> evs;
        bool gotReplace = false;
        for (int i = 0; i < 30; ++i) {
            std::vector<FileChangeEvent> batch;
            w.pollEvents([&](const FileChangeEvent& ev){ if (ev.path==f.path) batch.push_back(ev); });
            for (auto &e : batch) {
                evs.push_back(e);
                if (e.kind == FileChangeKind::Deleted || e.kind == FileChangeKind::Created) gotReplace = true;
            }
            if (gotReplace) {
                for (int k = 0; k < 5; ++k) {
                    std::vector<FileChangeEvent> extra;
                    w.pollEvents([&](const FileChangeEvent& ev){ if (ev.path==f.path) extra.push_back(ev); });
                    for (auto &e : extra) {
                        evs.push_back(e);
                        if (e.kind == FileChangeKind::Deleted || e.kind == FileChangeKind::Created) gotReplace = true;
                    }
                    if (extra.empty()) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(gotReplace);
        bool hasValidKind = false;
        for (auto &e : evs) if (e.kind == FileChangeKind::Deleted || e.kind == FileChangeKind::Created) hasValidKind = true;
        CHECK(hasValidKind);
        for (int i = 0; i < 10 && w.fileWatches_.find(f.path) == w.fileWatches_.end(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            w.pollEvents([&](const FileChangeEvent& ev){ if (ev.path==f.path) evs.push_back(ev); });
        }
        CHECK(w.fileWatches_.find(f.path) != w.fileWatches_.end());
        CHECK(w.trackedFiles_.find(f.path) != w.trackedFiles_.end());
        std::string modContent = std::string("mod_after_") + std::to_string(modIdx++) + "\n";
        CHECK(writeFile(f.path, modContent));
        std::vector<FileChangeEvent> modEvs;
        bool gotModified = pollUntil(w, modEvs, [&](const std::vector<FileChangeEvent>& vec){
            for (auto &e : vec) if (e.path == f.path && e.kind == FileChangeKind::Modified) return true;
            return false;
        });
        CHECK(gotModified);
        drainWatcher(w);
        checkWatcherInvariants(w);
    }
    w.unwatch(f.path);
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

// Test de integración Editor: comportamiento observable tras reemplazos atómicos.
// Solo importa: document == nueva versión, modified==false, savedIdentity válida.
// No verifica secuencia Deleted+Created.
TEST(editor_recovery_double_atomic_replace) {
    TempFile f; f.write("v1\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(f.path));
    CHECK_EQ(ed.active().document.lineAt(0), "v1");
    for (const char* v : {"v2\n", "v3\n"}) {
        std::string tmp = f.path + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << v;
        }
        std::filesystem::rename(tmp, f.path);
        std::string expected(v);
        expected.pop_back();
        bool reloaded = pollEditorUntil(ed, [&]{ return ed.active().document.lineAt(0)==expected; });
        CHECK(reloaded);
        CHECK(!ed.active().modified);
        CHECK(ed.active().savedIdentity.valid);
    }
    CHECK_EQ(ed.active().document.lineAt(0), "v3");
}

TEST(watcher_file_watch_is_idempotent) {
    TempFile f; f.write("x\n");
    InotifyFileWatcher w;
    CHECK(w.fd() >= 0);
    w.watch(f.path);
    CHECK(w.fileWatches_.find(f.path) != w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(f.path) != w.trackedFiles_.end());
    size_t sizeAfterFirst = w.fileWatches_.size();
    size_t wdCountAfterFirst = w.wdToEntry_.size();
    w.watch(f.path);
    CHECK_EQ(w.fileWatches_.size(), sizeAfterFirst);
    CHECK_EQ(w.fileWatches_.size(), 1u);
    CHECK_EQ(w.wdToEntry_.size(), wdCountAfterFirst);
    checkWatcherInvariants(w);
    // write debe generar exactamente un evento lógico, no dos por dos watches
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "y\n";
    }
    std::vector<FileChangeEvent> evs;
    pollUntil(w, evs, [](const std::vector<FileChangeEvent>& v){ return !v.empty(); });
    size_t countForFile = 0;
    for (auto &e : evs) if (e.path == f.path && e.kind == FileChangeKind::Modified) countForFile++;
    CHECK_EQ(countForFile, 1u);
    // contrato: watch idempotente, un solo unwatch debe eliminar
    w.unwatch(f.path);
    CHECK(w.fileWatches_.find(f.path) == w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(f.path) == w.trackedFiles_.end());
    drainWatcher(w);
    checkWatcherInvariants(w);
    // segundo unwatch no debe crashear
    w.unwatch(f.path);
    drainWatcher(w);
    checkWatcherInvariants(w);
    CHECK(true);
}

// After unwatch(), refCount_ may temporarily contain wd with refcount 0
// until IN_IGNORED is processed. Do not assert that transient state here;
// just wait for the entry to disappear.
TEST(watcher_shared_directory_watch_survives_one_unwatch) {
    TempFile fa, fb;
    fa.write("a\n"); fb.write("b\n");
    InotifyFileWatcher w;
    CHECK(w.fd() >= 0);
    w.watch(fa.path);
    CHECK(w.fileWatches_.find(fa.path) != w.fileWatches_.end());
    std::string dir = std::filesystem::path(fa.path).parent_path().string();
    if (dir.empty()) dir = ".";
    CHECK(w.dirWatches_.find(dir) != w.dirWatches_.end());
    int wd = w.dirWatches_.at(dir).first;
    CHECK_EQ(w.refCount_.at(wd).second, 1);
    CHECK_EQ(w.dirWatches_.size(), 1u);
    w.watch(fb.path);
    CHECK(w.fileWatches_.find(fb.path) != w.fileWatches_.end());
    CHECK(w.fileWatches_.size() == 2u);
    CHECK(w.trackedFiles_.size() == 2u);
    CHECK_EQ(w.dirWatches_.size(), 1u);
    CHECK_EQ(w.dirWatches_.at(dir).first, wd);
    CHECK_EQ(w.refCount_.at(wd).second, 2);
    w.unwatch(fa.path);
    CHECK(w.fileWatches_.find(fa.path) == w.fileWatches_.end());
    CHECK(w.fileWatches_.find(fb.path) != w.fileWatches_.end());
    CHECK(!w.dirWatches_.empty());
    CHECK_EQ(w.dirWatches_.at(dir).first, wd);
    CHECK_EQ(w.refCount_.at(wd).second, 1);
    {
        std::ofstream out(fb.path, std::ios::binary | std::ios::trunc);
        out << "b2\n";
    }
    std::vector<FileChangeEvent> evs;
    pollUntil(w, evs, [](const std::vector<FileChangeEvent>& v){ return !v.empty(); });
    bool got = false;
    for (auto &e : evs) if (e.path == fb.path) got = true;
    CHECK(got);
    w.unwatch(fb.path);
    CHECK(w.fileWatches_.empty());
    CHECK(w.dirWatches_.empty());
    {
        std::vector<FileChangeEvent> dummy;
        pollUntil(w, dummy, [&](const std::vector<FileChangeEvent>&){ return w.refCount_.find(wd)==w.refCount_.end(); });
    }
    CHECK(w.refCount_.find(wd) == w.refCount_.end());
    CHECK(w.wdToEntry_.find(wd) == w.wdToEntry_.end());
}

TEST(editor_two_buffers_same_file_share_watch_real) {
    TempFile f; f.write("shared\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(f.path));
    // Construct the second buffer manually because Editor has no public/API
    // operation to open the same path twice (openFileInBuffer deduplicates).
    // This is an internal test using private members via #define private public.
    Buffer second = ed.active();
    second.unnamedName = "";
    ed.buffers.push(std::move(second));
    ed.buffers.at(1).filename = ed.buffers.at(0).filename;
    ed.buffers.at(1).originalSnapshot_ = ed.buffers.at(0).originalSnapshot_;
    ed.buffers.at(1).savedIdentity = ed.buffers.at(0).savedIdentity;
    ed.buffers.at(1).document.restore({"shared"});
    ed.watchFile(ed.buffers.at(1).filename);
    {
        auto* w0 = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        CHECK(w0 != nullptr);
        CHECK_EQ(w0->fileWatches_.size(), 1u);
        CHECK_EQ(ed.watchedFiles_.size(), 1u);
    }
    // ambos deben recargar con un solo evento lógico
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "ext\n";
    }
    size_t modifiedEvents = 0;
    bool bothReloaded = pollEditorUntilWithModifiedCount(ed, f.path, modifiedEvents, [&]{
        return ed.buffers.at(0).document.lineAt(0)=="ext" && ed.buffers.at(1).document.lineAt(0)=="ext";
    });
    CHECK(bothReloaded);
    CHECK_EQ(modifiedEvents, 1u);
    {
        auto* w0 = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        CHECK_EQ(w0->fileWatches_.size(), 1u);
    }
    // cerrar uno debe mantener watch para el otro
    ed.buffers.activeBuffer_ = 0;
    ed.closeActiveBuffer();
    CHECK_EQ(ed.buffers.count(), 1);
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "ext2\n";
    }
    bool stillWatched = pollEditorUntil(ed, [&]{ return ed.active().document.lineAt(0)=="ext2"; });
    CHECK(stillWatched);
}

TEST(save_as_new_file_end_to_end_isNew_hadWatch) {
    TempFile fOrig; fOrig.write("orig\n");
    TempFile fNew;
    std::filesystem::remove(fNew.path);
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));
    ed.active().document.restore({"save_as_content"});
    ed.active().modified = true;
    auto oldIdentity = ed.active().savedIdentity;
    // simular SaveAs a nuevo path (isNew=true)
    ed.saveAsPath_ = fNew.path;
    // commitSaveAs es privado pero accesible via private hack
    ed.commitSaveAs();
    CHECK_EQ(ed.active().filename, std::filesystem::absolute(fNew.path).lexically_normal().string());
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "save_as_content");
    CHECK(ed.active().savedIdentity.valid);
    CHECK(ed.active().savedIdentity != oldIdentity);
    {
        std::string newAbs = std::filesystem::absolute(fNew.path).lexically_normal().string();
        struct stat stNew; CHECK_EQ(stat(newAbs.c_str(), &stNew), 0);
        CHECK_EQ(ed.active().savedIdentity.dev, stNew.st_dev);
        CHECK_EQ(ed.active().savedIdentity.ino, stNew.st_ino);
    }
    // viejo archivo no debe estar watchado, nuevo sí
    ed.statusMessage_ = Message{};
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    {
        std::string oldAbs = std::filesystem::absolute(fOrig.path).lexically_normal().string();
        std::string newAbs = std::filesystem::absolute(fNew.path).lexically_normal().string();
        CHECK(w->fileWatches_.find(oldAbs) == w->fileWatches_.end());
        CHECK(w->fileWatches_.find(newAbs) != w->fileWatches_.end());
        CHECK(w->trackedFiles_.find(oldAbs) == w->trackedFiles_.end());
        CHECK(w->trackedFiles_.find(newAbs) != w->trackedFiles_.end());
    }
    drainEditor(ed);
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
    // modificar nuevo archivo externamente debe recargar
    CHECK(writeFile(fNew.path, "new_external\n"));
    bool reloaded = pollEditorUntil(ed, [&]{ return ed.active().document.lineAt(0)=="new_external"; });
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "new_external");
    // modificar viejo archivo no debe afectar
    CHECK(writeFile(fOrig.path, "old_external\n"));
    ed.statusMessage_ = Message{};
    drainEditor(ed);
    CHECK_EQ(ed.active().document.lineAt(0), "new_external");
}

TEST(save_as_existing_file_overwrites_and_updates_watch) {
    TempFile fOrig; fOrig.write("orig\n");
    TempFile fExisting; fExisting.write("existing\n");
    struct stat stExistingBefore; CHECK_EQ(stat(fExisting.path.c_str(), &stExistingBefore), 0);
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));
    ed.active().document.restore({"save_as_overwrite"});
    ed.active().modified = true;
    auto oldIdentity = ed.active().savedIdentity;
    std::string oldAbs = std::filesystem::absolute(fOrig.path).lexically_normal().string();
    std::string newAbs = std::filesystem::absolute(fExisting.path).lexically_normal().string();
    CHECK(oldAbs != newAbs);
    ed.saveAsPath_ = fExisting.path;
    ed.commitSaveAs();
    CHECK_EQ(ed.active().filename, newAbs);
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "save_as_overwrite");
    CHECK(ed.active().savedIdentity.valid);
    CHECK(ed.active().savedIdentity != oldIdentity);
    struct stat stExistingAfter; CHECK_EQ(stat(newAbs.c_str(), &stExistingAfter), 0);
    CHECK_EQ(stExistingBefore.st_dev, stExistingAfter.st_dev);
    CHECK_EQ(stExistingBefore.st_ino, stExistingAfter.st_ino);
    CHECK_EQ(ed.active().savedIdentity.dev, stExistingAfter.st_dev);
    CHECK_EQ(ed.active().savedIdentity.ino, stExistingAfter.st_ino);
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    CHECK(w->fileWatches_.find(oldAbs) == w->fileWatches_.end());
    CHECK(w->fileWatches_.find(newAbs) != w->fileWatches_.end());
    CHECK(w->trackedFiles_.find(oldAbs) == w->trackedFiles_.end());
    CHECK(w->trackedFiles_.find(newAbs) != w->trackedFiles_.end());
    ed.statusMessage_ = Message{};
    drainEditor(ed);
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
    CHECK(writeFile(newAbs, "new_external_overwrite\n"));
    bool reloaded = pollEditorUntil(ed, [&]{ return ed.active().document.lineAt(0)=="new_external_overwrite"; });
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "new_external_overwrite");
    CHECK(writeFile(oldAbs, "old_external_overwrite\n"));
    ed.statusMessage_ = Message{};
    drainEditor(ed);
    CHECK_EQ(ed.active().document.lineAt(0), "new_external_overwrite");
}

TEST(save_as_failure_keeps_buffer_and_watch) {
    TempFile fOrig; fOrig.write("orig\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));
    ed.active().document.restore({"new_content"});
    ed.active().modified = true;
    auto oldFilename = ed.active().filename;
    auto oldIdentity = ed.active().savedIdentity;
    std::string oldAbs = std::filesystem::absolute(fOrig.path).lexically_normal().string();
    std::string badPath = "/tmp/maestro_saveas_failure_" + std::to_string(::getpid()) + "/file.txt";
    std::filesystem::remove_all(badPath);
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    CHECK(w->fileWatches_.find(oldAbs) != w->fileWatches_.end());
    ed.saveAsPath_ = badPath;
    ed.commitSaveAs();
    CHECK_EQ(ed.active().filename, oldFilename);
    CHECK(ed.active().modified);
    CHECK(ed.active().savedIdentity == oldIdentity);
    CHECK_EQ(ed.active().document.lineAt(0), "new_content");
    CHECK(ed.statusMessage_.text.find("Error") != std::string::npos || ed.statusMessage_.text.find("error") != std::string::npos || ed.statusMessage_.text.find("No se pudo") != std::string::npos);
    std::string badAbs = std::filesystem::absolute(badPath).lexically_normal().string();
    CHECK(w->fileWatches_.find(badAbs) == w->fileWatches_.end());
    CHECK(w->trackedFiles_.find(badAbs) == w->trackedFiles_.end());
    CHECK(w->fileWatches_.find(oldAbs) != w->fileWatches_.end());
    CHECK(w->trackedFiles_.find(oldAbs) != w->trackedFiles_.end());
    // old file still watched: modify it externally and verify no crash and still handled
    CHECK(writeFile(oldAbs, "old_external_after_failed_saveas\n"));
    bool reloaded = pollEditorUntil(ed, [&]{ return ed.active().document.lineAt(0) == "old_external_after_failed_saveas"; });
    // Since buffer is modified, external change should produce ALERTA, not reload
    CHECK(!reloaded);
    CHECK(ed.statusMessage_.text.find("ALERTA") != std::string::npos);
    CHECK_EQ(ed.active().document.lineAt(0), "new_content");
    CHECK(ed.active().modified);
}

TEST(watcher_recovery_delete_recreate_modify) {
    TempFile f; f.write("orig\n");
    InotifyFileWatcher w;
    CHECK(w.fd() >= 0);
    w.watch(f.path);
    CHECK(w.fileWatches_.find(f.path) != w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(f.path) != w.trackedFiles_.end());
    std::filesystem::remove(f.path);
    std::vector<FileChangeEvent> evDeleted;
    bool gotDeleted = pollUntil(w, evDeleted, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path==f.path && e.kind==FileChangeKind::Deleted) return true;
        return false;
    });
    CHECK(gotDeleted);
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "recreated\n";
    }
    std::vector<FileChangeEvent> evCreated;
    bool gotCreated = pollUntil(w, evCreated, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path==f.path && e.kind==FileChangeKind::Created) return true;
        return false;
    });
    CHECK(gotCreated);
    CHECK(w.fileWatches_.find(f.path) != w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(f.path) != w.trackedFiles_.end());
    CHECK(writeFile(f.path, "modified_after_recreate\n"));
    std::vector<FileChangeEvent> evModified;
    bool gotModified = pollUntil(w, evModified, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path==f.path && e.kind==FileChangeKind::Modified) return true;
        return false;
    });
    CHECK(gotModified);
    w.unwatch(f.path);
}

// ── Tests de contrato interno: move/rename ──
// Verifican el contrato específico de InotifyFileWatcher para rename/move,
// no el comportamiento observable del Editor. El comportamiento es contraintuitivo:
// tras rename, el watch sigue al inode movido (no se autocierra).
TEST(watcher_move_rename_produces_deleted) {
    TempFile f; f.write("orig\n");
    InotifyFileWatcher w;
    CHECK(w.fd() >= 0);
    w.watch(f.path);
    CHECK(w.fileWatches_.find(f.path) != w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(f.path) != w.trackedFiles_.end());
    std::string newPath = f.path + ".moved";
    std::filesystem::remove(newPath);
    std::filesystem::rename(f.path, newPath);
    std::vector<FileChangeEvent> evs;
    bool gotDeleted = pollUntil(w, evs, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path==f.path && e.kind==FileChangeKind::Deleted) return true;
        return false;
    });
    CHECK(gotDeleted);
    bool hasCreatedForOld = false;
    for (auto &e : evs) if (e.path==f.path && e.kind==FileChangeKind::Created) hasCreatedForOld = true;
    CHECK(!hasCreatedForOld);
    // Contrato interno: IN_MOVE_SELF no remueve automáticamente el watch;
    // fileWatches permanece asociado al viejo pathname aunque el inode ahora
    // esté en newPath. El watch sigue al inode movido.
    CHECK(w.fileWatches_.find(f.path) != w.fileWatches_.end());
    {
        int wd = w.fileWatches_.at(f.path).first;
        auto eit = w.wdToEntry_.find(wd);
        CHECK(eit != w.wdToEntry_.end());
        CHECK_EQ(eit->second.path, f.path);
        CHECK(!eit->second.isDir);
        CHECK_EQ(eit->second.gen, w.fileWatches_.at(f.path).second);
        CHECK(w.refCount_.find(wd) != w.refCount_.end());
    }
    CHECK(w.fileWatches_.find(newPath) == w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(newPath) == w.trackedFiles_.end());
    CHECK(writeFile(newPath, "after_move\n"));
    std::vector<FileChangeEvent> afterMove;
    pollForQuietPeriod(w, afterMove, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path==f.path && e.kind==FileChangeKind::Modified) return true;
        return false;
    });
    bool gotForOldAfterMove = false;
    for (auto &e : afterMove) if (e.path==f.path && e.kind==FileChangeKind::Modified) gotForOldAfterMove = true;
    CHECK(gotForOldAfterMove);
    std::filesystem::remove(newPath);
    w.unwatch(f.path);
    CHECK(w.trackedFiles_.find(f.path) == w.trackedFiles_.end());
}

TEST(watcher_dir_watch_filters_untracked_file) {
    TempFile fa; fa.write("a\n");
    std::string untracked = fa.path + ".untracked";
    std::filesystem::remove(untracked);
    InotifyFileWatcher w;
    CHECK(w.fd() >= 0);
    w.watch(fa.path);
    CHECK(w.fileWatches_.find(fa.path) != w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(fa.path) != w.trackedFiles_.end());
    {
        std::ofstream out(untracked, std::ios::binary | std::ios::trunc);
        out << "b\n";
    }
    std::vector<FileChangeEvent> evs;
    pollForQuietPeriod(w, evs, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path == fa.path) return true;
        return false;
    });
    bool gotForA = false;
    for (auto &e : evs) if (e.path == fa.path) gotForA = true;
    CHECK(!gotForA);
    bool gotForUntracked = false;
    for (auto &e : evs) if (e.path == untracked) gotForUntracked = true;
    CHECK(!gotForUntracked);
    CHECK(w.fileWatches_.find(untracked) == w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(untracked) == w.trackedFiles_.end());
    CHECK(w.fileWatches_.find(fa.path) != w.fileWatches_.end());
    CHECK(writeFile(untracked, "b2\n"));
    std::vector<FileChangeEvent> evs2;
    pollForQuietPeriod(w, evs2, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path == fa.path) return true;
        return false;
    });
    bool gotForAAfterModifyB = false;
    for (auto &e : evs2) if (e.path == fa.path) gotForAAfterModifyB = true;
    CHECK(!gotForAAfterModifyB);
    CHECK(writeFile(fa.path, "a2\n"));
    std::vector<FileChangeEvent> evs3;
    bool gotForA2 = pollUntil(w, evs3, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path == fa.path && e.kind == FileChangeKind::Modified) return true;
        return false;
    });
    CHECK(gotForA2);
    w.unwatch(fa.path);
    std::filesystem::remove(untracked);
}

TEST(watcher_dir_watches_independent_for_different_dirs) {
    std::string base = "/tmp/maestro_test_dirs_" + std::to_string(::getpid());
    std::string dirA = base + "_A";
    std::string dirB = base + "_B";
    TempDir tdA(dirA);
    TempDir tdB(dirB);
    std::string pathA = dirA + "/file.txt";
    std::string pathB = dirB + "/file.txt";
    {
        std::ofstream out(pathA); out << "a\n";
        std::ofstream out2(pathB); out2 << "b\n";
    }
    InotifyFileWatcher w;
    CHECK(w.fd() >= 0);
    w.watch(pathA);
    CHECK(w.fileWatches_.find(pathA) != w.fileWatches_.end());
    CHECK(w.dirWatches_.find(dirA) != w.dirWatches_.end());
    int wdA = w.dirWatches_.at(dirA).first;
    CHECK_EQ(w.refCount_.at(wdA).second, 1);
    CHECK_EQ(w.dirWatches_.size(), 1u);
    w.watch(pathB);
    CHECK(w.fileWatches_.find(pathB) != w.fileWatches_.end());
    CHECK_EQ(w.fileWatches_.size(), 2u);
    CHECK_EQ(w.dirWatches_.size(), 2u);
    CHECK(w.dirWatches_.find(dirB) != w.dirWatches_.end());
    int wdB = w.dirWatches_.at(dirB).first;
    CHECK(wdA != wdB);
    CHECK_EQ(w.refCount_.at(wdB).second, 1);
    CHECK_EQ(w.refCount_.at(wdA).second, 1);
    w.unwatch(pathA);
    CHECK(w.fileWatches_.find(pathA) == w.fileWatches_.end());
    CHECK(w.fileWatches_.find(pathB) != w.fileWatches_.end());
    CHECK(w.dirWatches_.find(dirA) == w.dirWatches_.end());
    CHECK(w.dirWatches_.find(dirB) != w.dirWatches_.end());
    {
        std::vector<FileChangeEvent> dummy;
        pollUntil(w, dummy, [&](const std::vector<FileChangeEvent>&){ return w.refCount_.find(wdA)==w.refCount_.end(); });
    }
    CHECK(w.refCount_.find(wdA) == w.refCount_.end());
    CHECK_EQ(w.refCount_.at(wdB).second, 1);
    {
        std::ofstream out(pathB, std::ios::binary | std::ios::trunc);
        out << "b2\n";
    }
    std::vector<FileChangeEvent> evs;
    pollUntil(w, evs, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path==pathB && e.kind==FileChangeKind::Modified) return true;
        return false;
    });
    bool gotB = false;
    for (auto &e : evs) if (e.path==pathB && e.kind==FileChangeKind::Modified) gotB = true;
    CHECK(gotB);
    bool gotA = false;
    for (auto &e : evs) if (e.path==pathA) gotA = true;
    CHECK(!gotA);
    w.unwatch(pathB);
    CHECK(w.fileWatches_.empty());
    CHECK(w.dirWatches_.empty());
    CHECK(w.trackedFiles_.empty());
}

TEST(editor_open_normalizes_dot_path) {
    std::string cwd = std::filesystem::current_path().string();
    std::string pid = std::to_string(::getpid());
    std::string fileName = "test_norm_dot_" + pid + ".txt";
    std::string absPath = cwd + "/" + fileName;
    {
        std::ofstream out(absPath, std::ios::binary | std::ios::trunc);
        out << "orig\n";
    }
    std::string relDotSlash = "./" + fileName;
    std::string expectedAbs = std::filesystem::absolute(relDotSlash).lexically_normal().string();
    {
        Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
        CHECK(ed.loadIntoActiveBuffer(relDotSlash));
        CHECK_EQ(ed.active().filename, expectedAbs);
        auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        CHECK(w != nullptr);
        CHECK(w->fileWatches_.find(expectedAbs) != w->fileWatches_.end());
        CHECK(writeFile(absPath, "via_dot_slash\n"));
        bool reloaded = pollEditorUntil(ed, [&]{ return ed.active().document.lineAt(0) == "via_dot_slash"; });
        CHECK(reloaded);
        // A second external modification must still be observed after normalization.
        CHECK(writeFile(absPath, "via_dot_slash2\n"));
        bool reloaded2 = pollEditorUntil(ed, [&]{ return ed.active().document.lineAt(0) == "via_dot_slash2"; });
        CHECK(reloaded2);
    }
    std::filesystem::remove(absPath);
}

TEST(editor_open_normalizes_dotdot_path) {
    std::string cwd = std::filesystem::current_path().string();
    std::string pid = std::to_string(::getpid());
    std::string subdir = cwd + "/test_norm_subdir_" + pid + "_dotdot";
    std::filesystem::create_directories(subdir);
    std::string fileName = "test_norm_dotdot_" + pid + ".txt";
    std::string absPath = cwd + "/" + fileName;
    {
        std::ofstream out(absPath, std::ios::binary | std::ios::trunc);
        out << "orig\n";
    }
    std::string relDotDot = "test_norm_subdir_" + pid + "_dotdot/../" + fileName;
    std::string expectedAbs = std::filesystem::absolute(relDotDot).lexically_normal().string();
    {
        Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
        CHECK(ed.loadIntoActiveBuffer(relDotDot));
        CHECK_EQ(ed.active().filename, expectedAbs);
        auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        CHECK(w != nullptr);
        CHECK(w->fileWatches_.find(expectedAbs) != w->fileWatches_.end());
        CHECK(writeFile(absPath, "via_dotdot\n"));
        bool reloaded = pollEditorUntil(ed, [&]{ return ed.active().document.lineAt(0) == "via_dotdot"; });
        CHECK(reloaded);
    }
    std::filesystem::remove(absPath);
    std::filesystem::remove(subdir);
}

TEST(watcher_preserves_direct_watch_path) {
    std::string cwd = std::filesystem::current_path().string();
    std::string pid = std::to_string(::getpid());
    std::string fileName = "test_norm_direct_" + pid + ".txt";
    std::string absPath = cwd + "/" + fileName;
    {
        std::ofstream out(absPath, std::ios::binary | std::ios::trunc);
        out << "orig\n";
    }
    std::string relDotSlash = "./" + fileName;
    std::string normPath = std::filesystem::path(relDotSlash).lexically_normal().string();
    InotifyFileWatcher w;
    CHECK(w.fd() >= 0);
    w.watch(relDotSlash);
    CHECK(w.fileWatches_.find(normPath) != w.fileWatches_.end());
    CHECK(writeFile(absPath, "direct_rel\n"));
    std::vector<FileChangeEvent> evs;
    bool got = pollUntil(w, evs, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path == normPath && e.kind == FileChangeKind::Modified) return true;
        return false;
    });
    CHECK(got);
}
TEST(watcher_recreate_with_relative_path) {
    std::string cwd = std::filesystem::current_path().string();
    std::string pid = std::to_string(::getpid());
    std::string fileName = "test_norm_recreate_" + pid + ".txt";
    std::string absPath = cwd + "/" + fileName;
    {
        std::ofstream out(absPath, std::ios::binary | std::ios::trunc);
        out << "orig\n";
    }
    std::string relDotSlash = "./" + fileName;
    std::string normPath = std::filesystem::path(relDotSlash).lexically_normal().string();
    InotifyFileWatcher w;
    CHECK(w.fd() >= 0);
    w.watch(relDotSlash);
    CHECK(w.fileWatches_.find(normPath) != w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(normPath) != w.trackedFiles_.end());
    
    // Delete the file
    std::filesystem::remove(absPath);
    std::vector<FileChangeEvent> evDeleted;
    bool gotDeleted = pollUntil(w, evDeleted, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path==normPath && e.kind==FileChangeKind::Deleted) return true;
        return false;
    });
    CHECK(gotDeleted);
    
    // Recreate the file
    {
        std::ofstream out(absPath, std::ios::binary | std::ios::trunc);
        out << "recreated\n";
    }
    
    // Should get Created event with the normalized path
    std::vector<FileChangeEvent> evCreated;
    bool gotCreated = pollUntil(w, evCreated, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path==normPath && e.kind==FileChangeKind::Created) return true;
        return false;
    });
    CHECK(gotCreated);
    
    // Watch should be re-established
    CHECK(w.fileWatches_.find(normPath) != w.fileWatches_.end());
    CHECK(w.trackedFiles_.find(normPath) != w.trackedFiles_.end());
    
    // Subsequent modification should be detected
    CHECK(writeFile(absPath, "modified_after_recreate\n"));
    std::vector<FileChangeEvent> evModified;
    bool gotModified = pollUntil(w, evModified, [&](const std::vector<FileChangeEvent>& v){
        for (auto &e : v) if (e.path==normPath && e.kind==FileChangeKind::Modified) return true;
        return false;
    });
    CHECK(gotModified);
    
    w.unwatch(normPath);
    std::filesystem::remove(absPath);
}

TEST(editor_destructor_cleans_all_watches) {
    TempFile fa; fa.write("a\n");
    TempFile fb; fb.write("b\n");
    std::string pathA = fa.path;
    std::string pathB = fb.path;
    int oldFd = -1;
    {
        auto watcher = std::make_unique<InotifyFileWatcher>();
        oldFd = watcher->fd();
        CHECK(oldFd >= 0);
        Editor ed(std::make_unique<FakeClipboard>(), std::move(watcher));
        CHECK(ed.loadIntoActiveBuffer(pathA));
        ed.openFileInBuffer(pathB);
        CHECK_EQ(ed.buffers.count(), 2);
        auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        CHECK(w != nullptr);
        CHECK_EQ(w->fileWatches_.size(), 2u);
        CHECK(w->trackedFiles_.find(pathA) != w->trackedFiles_.end());
        CHECK(w->trackedFiles_.find(pathB) != w->trackedFiles_.end());
        CHECK(w->fd() == oldFd);
        // Destruction releases the watcher; a new watcher remains fully usable.
        // Real fd-close verification needs sanitizers or destructor observability.
    }
    {
        InotifyFileWatcher w2;
        CHECK(w2.fd() >= 0);
        CHECK(w2.fileWatches_.empty());
        w2.watch(pathA);
        w2.watch(pathB);
        CHECK_EQ(w2.fileWatches_.size(), 2u);
        CHECK(writeFile(pathA, "after_destroy\n"));
        std::vector<FileChangeEvent> evs;
        bool got = pollUntil(w2, evs, [&](const std::vector<FileChangeEvent>& v){
            for (auto &e : v) if (e.path==pathA && e.kind==FileChangeKind::Modified) return true;
            return false;
        });
        CHECK(got);
        w2.unwatch(pathA);
        w2.unwatch(pathB);
    }
}

TEST(editor_handleFileChange_atomic_replace_sequence_clean_reloads_once) {
    TempFile f; f.write("v1\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(f.path));
    CHECK(!ed.active().modified);
    auto id1 = ed.active().savedIdentity;
    CHECK(id1.valid);
    std::string tmp = f.path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "v2\n";
    }
    std::filesystem::rename(tmp, f.path);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    FileChangeEvent evDel{f.path, FileChangeKind::Deleted};
    FileChangeEvent evCre{f.path, FileChangeKind::Created};
    FileChangeEvent evMod{f.path, FileChangeKind::Modified};
    ed.handleFileChange(evDel);
    CHECK_EQ(ed.active().document.lineAt(0), "v2");
    CHECK(!ed.active().modified);
    CHECK(ed.active().savedIdentity.valid);
    CHECK(ed.active().savedIdentity != id1);
    auto id2 = ed.active().savedIdentity;
    ed.handleFileChange(evCre);
    CHECK_EQ(ed.active().document.lineAt(0), "v2");
    CHECK(ed.active().savedIdentity == id2);
    ed.handleFileChange(evMod);
    CHECK_EQ(ed.active().document.lineAt(0), "v2");
    CHECK(ed.active().savedIdentity == id2);
}

TEST(editor_handleFileChange_atomic_replace_sequence_dirty_warns_not_reload) {
    TempFile f; f.write("v1\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.loadIntoActiveBuffer(f.path));
    ed.active().document.restore({"local"});
    ed.active().modified = true;
    auto id = ed.active().savedIdentity;
    std::string tmp = f.path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "v2\n";
    }
    std::filesystem::rename(tmp, f.path);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    FileChangeEvent evDel{f.path, FileChangeKind::Deleted};
    ed.handleFileChange(evDel);
    CHECK(ed.statusMessage_.text.find("ALERTA") != std::string::npos);
    CHECK_EQ(ed.active().document.lineAt(0), "local");
    CHECK(ed.active().modified);
    CHECK(ed.active().savedIdentity == id);
    FileChangeEvent evCre{f.path, FileChangeKind::Created};
    ed.statusMessage_ = Message{};
    ed.handleFileChange(evCre);
    CHECK(ed.statusMessage_.text.find("ALERTA") != std::string::npos);
    CHECK_EQ(ed.active().document.lineAt(0), "local");
}
