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
#include "filesystem/NullFileWatcher.h"

using testfw::TempFile;

static Editor makeNullEditor() {
    return Editor(std::make_unique<FakeClipboard>(), std::make_unique<NullFileWatcher>());
}
static void writeFile(const std::string& p, const std::string& c) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << c;
}

template<typename Pred>
static bool waitFor(Pred pred, int timeoutMs = 500) {
    for (int i = 0; i < timeoutMs / 10; ++i) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

TEST(external_change_reloads_clean_buffer) {
    TempFile f; f.write("old\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    writeFile(f.path, "new\n");
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK_EQ(ed.active().document.lineAt(0), "new");
    CHECK(!ed.active().modified);
    CHECK(ed.active().originalSnapshot_ == ed.active().document.snapshot());
}

TEST(external_change_preserves_modified_buffer) {
    TempFile f; f.write("A\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    ed.active().document.restore({"A_prime"});
    ed.active().modified = true;
    writeFile(f.path, "B\n");
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK_EQ(ed.active().document.lineAt(0), "A_prime");
    CHECK(ed.active().modified);
    CHECK(ed.statusMessage_.kind == MessageKind::Warning);
}

TEST(external_change_updates_saved_lines) {
    TempFile f; f.write("A\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    auto oldSaved = ed.active().originalSnapshot_;
    writeFile(f.path, "B\n");
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK(ed.active().originalSnapshot_ == ed.active().document.snapshot());
    CHECK(ed.active().originalSnapshot_ != oldSaved);
}

TEST(external_change_clamps_cursor) {
    TempFile f; f.write("a\nb\nc\nd\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    ed.active().cursor.line = 3;
    ed.active().cursor.col = 0;
    writeFile(f.path, "a\n");
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK(ed.active().cursor.line < ed.active().document.lineCount());
    CHECK(ed.active().cursor.col <= ed.active().document.lineLength(ed.active().cursor.line));
    // expand
    writeFile(f.path, "a\nb\nc\nd\ne\nf\n");
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK(ed.active().cursor.line < ed.active().document.lineCount());
}

TEST(save_does_not_trigger_external_change) {
    TempFile f; f.write("orig\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    ed.active().document.restore({"mod"});
    ed.active().modified = true;
    ed.save();
    CHECK(!ed.active().modified);
    ed.statusMessage_ = Message{};
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "mod");
}

TEST(multiple_saves_do_not_trigger_external_change) {
    TempFile f; f.write("a\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    for (int i = 0; i < 2; ++i) {
        ed.active().document.restore({std::string("v") + std::to_string(i)});
        ed.active().modified = true;
        ed.save();
        ed.statusMessage_ = Message{};
        ed.handleFileChange({f.path, FileChangeKind::Modified});
        CHECK(ed.statusMessage_.text.find("ALERTA") == std::string::npos);
        CHECK(!ed.active().modified);
    }
}

TEST(multiple_external_writes_reload_latest_content) {
    TempFile f; f.write("0\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    writeFile(f.path, "1\n");
    writeFile(f.path, "2\n");
    writeFile(f.path, "final\n");
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK_EQ(ed.active().document.lineAt(0), "final");
    CHECK(!ed.active().modified);
}

TEST(deleted_file_is_detected) {
    TempFile f; f.write("keep\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(f.path));
    std::filesystem::remove(f.path);
    bool got = waitFor([&]{
        auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        if (!w) return false;
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        return ed.statusMessage_.text.find("eliminado") != std::string::npos;
    });
    CHECK(got);
    CHECK(!ed.active().savedIdentity.valid);
}

TEST(deleted_then_recreated_file_is_reloaded) {
    TempFile f; f.write("orig\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(f.path));
    std::filesystem::remove(f.path);
    bool gone = waitFor([&]{
        auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        if (!w) return false;
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        return !ed.active().savedIdentity.valid;
    });
    CHECK(gone);
    CHECK(!ed.active().savedIdentity.valid);
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "nuevo\n";
    }
    bool reloaded = waitFor([&]{
        auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        if (!w) return false;
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        return ed.active().document.lineAt(0) == "nuevo";
    });
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "nuevo");
    CHECK(!ed.active().modified);
    CHECK(ed.active().savedIdentity.valid);
}

TEST(atomic_file_replacement_is_detected) {
    TempFile f; f.write("v1\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(f.path));
    std::string tmp = f.path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "v2\n";
    }
    std::filesystem::rename(tmp, f.path);
    bool reloaded = waitFor([&]{
        auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        if (!w) return false;
        std::vector<FileChangeEvent> evs;
        w->pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &ev : evs) ed.handleFileChange(ev);
        return ed.active().document.lineAt(0) == "v2";
    }, 600);
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "v2");
    CHECK(!ed.active().modified);
}

TEST(closing_buffer_does_not_process_old_path) {
    TempFile f; f.write("x\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    std::string path = ed.active().filename;
    ed.closeActiveBuffer();
    CHECK(ed.active().filename != path);
    writeFile(f.path, "y\n");
    ed.handleFileChange({path, FileChangeKind::Modified});
    CHECK(ed.active().document.lineAt(0) != "y");
}

TEST(closing_buffer_removes_watch_integration) {
    TempFile f; f.write("x\n");
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(f.path));
    std::string path = ed.active().filename;
    ed.closeActiveBuffer();
    CHECK(ed.active().filename != path);
    writeFile(f.path, "y\n");
    bool got = false;
    for (int i = 0; i < 20; ++i) {
        auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        if (w) {
            std::vector<FileChangeEvent> evs;
            w->pollEvents([&](const FileChangeEvent& ev){ if (ev.path == path) evs.push_back(ev); });
            if (!evs.empty()) { got = true; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(!got);
}

TEST(same_file_two_buffers_share_watch) {
    TempFile f; f.write("shared\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    Buffer second = ed.active();
    second.unnamedName = "";
    ed.buffers.push(std::move(second));
    ed.buffers.at(1).filename = ed.buffers.at(0).filename;
    ed.buffers.at(1).originalSnapshot_ = ed.buffers.at(0).originalSnapshot_;
    ed.buffers.at(1).savedIdentity = ed.buffers.at(0).savedIdentity;
    ed.buffers.at(1).document.restore({"shared"});
    writeFile(f.path, "ext\n");
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK_EQ(ed.buffers.at(0).document.lineAt(0), "ext");
    CHECK_EQ(ed.buffers.at(1).document.lineAt(0), "ext");
}

TEST(closing_one_shared_buffer_keeps_watch) {
    TempFile f; f.write("shared\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    Buffer second = ed.active();
    second.unnamedName = "";
    ed.buffers.push(std::move(second));
    ed.buffers.at(1).filename = ed.buffers.at(0).filename;
    ed.buffers.at(1).originalSnapshot_ = ed.buffers.at(0).originalSnapshot_;
    ed.buffers.at(1).savedIdentity = ed.buffers.at(0).savedIdentity;
    ed.buffers.at(1).document.restore({"shared"});
    ed.buffers.activeBuffer_ = 0;
    ed.closeActiveBuffer();
    CHECK_EQ(ed.buffers.count(), 1);
    writeFile(f.path, "ext2\n");
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK_EQ(ed.active().document.lineAt(0), "ext2");
}

TEST(different_files_have_independent_watches) {
    TempFile fa, fb;
    fa.write("A1\n"); fb.write("B1\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(fa.path));
    ed.createBuffer();
    CHECK(ed.openFile(fb.path));
    CHECK_EQ(ed.buffers.count(), 2);
    int idxA = -1, idxB = -1;
    for (int i = 0; i < ed.buffers.count(); ++i) {
        if (ed.buffers.at(i).filename == std::filesystem::absolute(fa.path).lexically_normal().string()) idxA = i;
        if (ed.buffers.at(i).filename == std::filesystem::absolute(fb.path).lexically_normal().string()) idxB = i;
    }
    CHECK(idxA >= 0); CHECK(idxB >= 0);
    writeFile(fa.path, "A2\n");
    ed.handleFileChange({fa.path, FileChangeKind::Modified});
    CHECK_EQ(ed.buffers.at(idxA).document.lineAt(0), "A2");
    CHECK_EQ(ed.buffers.at(idxB).document.lineAt(0), "B1");
    writeFile(fb.path, "B2\n");
    ed.handleFileChange({fb.path, FileChangeKind::Modified});
    CHECK_EQ(ed.buffers.at(idxB).document.lineAt(0), "B2");
    CHECK_EQ(ed.buffers.at(idxA).document.lineAt(0), "A2");
}

TEST(unnamed_buffer_has_no_watch) {
    Editor ed = makeNullEditor();
    CHECK(ed.active().filename.empty());
    // handle change for random path must not affect unnamed buffer
    ed.handleFileChange({"/tmp/no_such_file_xyz", FileChangeKind::Modified});
    CHECK(ed.active().filename.empty());
    CHECK(!ed.active().modified);
}

TEST(rapid_external_changes) {
    TempFile f; f.write("0\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    for (int i = 1; i <= 10; ++i) writeFile(f.path, std::to_string(i) + "\n");
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK_EQ(ed.active().document.lineAt(0), "10");
}

TEST(modify_and_chmod_generates_no_false_warning) {
    TempFile f; f.write("a\n");
    InotifyFileWatcher w;
    if (w.fd() < 0) return;
    w.watch(f.path);
    writeFile(f.path, "b\n");
    std::filesystem::permissions(f.path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    writeFile(f.path, "c\n");
    std::vector<FileChangeEvent> evs;
    bool hasModified = waitFor([&]{
        w.pollEvents([&](const FileChangeEvent& ev){ evs.push_back(ev); });
        for (auto &e : evs) if (e.path == f.path && e.kind == FileChangeKind::Modified) return true;
        return false;
    });
    CHECK(hasModified);
    w.unwatch(f.path);
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    CHECK(ed.openFile(f.path));
    writeFile(f.path, "d\n");
    std::filesystem::permissions(f.path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    writeFile(f.path, "e\n");
    bool reloaded = waitFor([&]{
        auto* ww = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
        if (!ww) return false;
        std::vector<FileChangeEvent> evs2;
        ww->pollEvents([&](const FileChangeEvent& ev){ evs2.push_back(ev); });
        for (auto &ev : evs2) ed.handleFileChange(ev);
        return ed.active().document.lineAt(0) == "e";
    });
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "e");
    CHECK(ed.statusMessage_.kind != MessageKind::Warning || ed.statusMessage_.text.find("ALERTA")==std::string::npos);
}

TEST(save_then_external_change) {
    TempFile f; f.write("orig\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    ed.active().document.restore({"local"});
    ed.active().modified = true;
    ed.save();
    CHECK(!ed.active().modified);
    writeFile(f.path, "external\n");
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK_EQ(ed.active().document.lineAt(0), "external");
    CHECK(!ed.active().modified);
}

TEST(external_change_then_local_edit) {
    TempFile f; f.write("orig\n");
    Editor ed = makeNullEditor();
    CHECK(ed.openFile(f.path));
    writeFile(f.path, "ext\n");
    ed.handleFileChange({f.path, FileChangeKind::Modified});
    CHECK_EQ(ed.active().document.lineAt(0), "ext");
    ed.active().document.restore({"ext_local"});
    ed.active().modified = true;
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "ext_local");
}

TEST(open_close_many_buffers_does_not_leak_watches) {
    TempFile fa, fb, fc;
    fa.write("A\n"); fb.write("B\n"); fc.write("C\n");
    InotifyFileWatcher probe;
    if (probe.fd() < 0) return;
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<InotifyFileWatcher>());
    // :e sobre el mismo buffer (sin createBuffer): no debe acumular watches
    // de fa/fb/fc. Tras cerrar, los mapas internos tienen que quedar vacíos.
    ed.openFile(fa.path);
    ed.openFile(fb.path);
    ed.openFile(fc.path);
    ed.openFile(fa.path);
    ed.openFile(fb.path);
    ed.openFile(fc.path);
    auto* w = dynamic_cast<InotifyFileWatcher*>(ed.watcher_.get());
    CHECK(w != nullptr);
    CHECK_EQ(w->fileWatches_.size(), 1u);
    while (ed.buffers.count() > 1) ed.closeActiveBuffer();
    ed.closeActiveBuffer();
    CHECK_EQ(ed.buffers.count(), 1);
    CHECK(ed.active().filename.empty());
    CHECK_EQ(w->fileWatches_.size(), 0u);
    CHECK_EQ(w->dirWatches_.size(), 0u);
}
