// Clipboard SDL + watcher en el loop GUI.
// Se corre con: make test-one FILTER='sdlclipboard*'
// o '..guiwatcher*' para la parte del watcher.
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "test_framework.h"
#include "platform/clipboard/ClipboardFactory.h"
#include "platform/clipboard/SdlClipboard.h"
#include "filesystem/InotifyFileWatcher.h"
#include "platform/gui/GuiRunLoop.h"
#include "rendering/Sink.h"

#define private public
#include "app/Editor.h"
#undef private
#include "helpers/FakeClipboard.h"

using testfw::TempFile;

static void muteRender(Editor& ed) {
    thread_local NullSink nullSink;
    ed.setSink(nullSink);
}

TEST(sdlclipboard_factory_returns_nonnull) {
    auto cb = makeSdlClipboard();
    CHECK(cb != nullptr);
}

TEST(sdlclipboard_copy_paste_simple) {
    SdlClipboard cb;
    CHECK(cb.copy("hola"));
    CHECK(cb.ownsClipboard());
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "hola");
}

TEST(sdlclipboard_empty_initial) {
    SdlClipboard cb;
    CHECK(!cb.ownsClipboard());
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "");
}

TEST(sdlclipboard_utf8_multiline) {
    SdlClipboard cb;
    std::string cafe = std::string("caf\xC3\xA9");
    std::string emoji = std::string("\xF0\x9F\x98\x80");
    std::string multi = "linea 1\nlinea 2\nlinea 3";
    for (auto s : {cafe, emoji, multi}) {
        CHECK(cb.copy(s));
        auto p = cb.paste();
        CHECK(p.has_value());
        CHECK_EQ(*p, s);
    }
}

TEST(sdlclipboard_sync_no_fd_no_pending) {
    SdlClipboard cb;
    CHECK_EQ(cb.fd(), -1);
    CHECK(!cb.hasPending());
    cb.copy("x");
    cb.processEvents();  // no-op, no debe romper nada
    CHECK(!cb.hasPending());
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "x");
}

TEST(sdlclipboard_editor_copy_paste) {
    auto sc = std::make_unique<SdlClipboard>();
    SdlClipboard* raw = sc.get();
    Editor ed(std::move(sc));
    muteRender(ed);
    // type "hola" en Interacción, vuelve a Navegación, copia "ho", pega al final.
    InputEvent i;
    i.type = InputEventType::InsertChar;
    i.text = "i";
    ed.handleEvent(i);
    for (char c : std::string("hola")) {
        InputEvent ch;
        ch.type = InputEventType::InsertChar;
        ch.text = std::string(1, c);
        ed.handleEvent(ch);
    }
    InputEvent esc;
    esc.type = InputEventType::Escape;
    ed.handleEvent(esc);
    InputEvent home;
    home.type = InputEventType::MoveHome;
    ed.handleEvent(home);
    InputEvent s;
    s.type = InputEventType::InsertChar;
    s.text = "s";
    ed.handleEvent(s);
    InputEvent r;
    r.type = InputEventType::MoveRight;
    ed.handleEvent(r);
    ed.handleEvent(r);
    InputEvent c;
    c.type = InputEventType::InsertChar;
    c.text = "c";
    ed.handleEvent(c);
    CHECK_EQ(raw->paste().value(), "ho");
    InputEvent end;
    end.type = InputEventType::MoveEnd;
    ed.handleEvent(end);
    InputEvent p;
    p.type = InputEventType::InsertChar;
    p.text = "p";
    ed.handleEvent(p);
    CHECK_EQ(ed.active().document.lineAt(0), "holaho");
}

// El loop GUI bombea el watcher reusado (inotify tal cual) UNA vez al tope
// de cada vuelta de run(), sin fd ni ppoll; idleStep() es solo tick+render.
// Este test replica esa secuencia (bombeo + idleStep): un cambio externo en
// disco recarga antes del render.
TEST(guiwatcher_idle_step_reloads_external_change) {
    TempFile f;
    f.write("A\n");
    auto sc = std::make_unique<SdlClipboard>();
    auto w = std::make_unique<InotifyFileWatcher>();
    Editor ed(std::move(sc), std::move(w));
    muteRender(ed);
    CHECK(ed.loadIntoActiveBuffer(f.path));
    CHECK_EQ(ed.active().document.lineAt(0), "A");
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "B\n";
    }
    GuiRunLoop loop(ed);
    bool reloaded = false;
    for (int k = 0; k < 30; ++k) {
        ed.pollExternalEvents();  // lo que run() hace al tope de la vuelta
        loop.idleStep(std::chrono::steady_clock::now());
        if (ed.active().document.lineAt(0) == "B") {
            reloaded = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "B");
    CHECK(!ed.active().modified);
}
