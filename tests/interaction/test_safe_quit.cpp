#include "test_framework.h"

#define private public
#include "ui/Editor.h"
#undef private
#include "clipboard/FakeClipboard.h"

static Event insert(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

static void press(Editor& ed, EventType t) {
    Event e;
    e.type = t;
    ed.handleEvent(e);
}

static void pressEvent(Editor& ed, const Event& ev) {
    ed.handleEvent(ev);
}

static void type(Editor& ed, const std::string& s) {
    if (s.empty()) return;
    if (ed.state_ != State::Interaccion) {
        if (ed.state_ == State::Seleccion) {
            Event esc; esc.type = EventType::Escape; ed.handleEvent(esc);
        }
        ed.handleEvent(insert('i'));
    }
    for (char c : s) ed.handleEvent(insert(c));
}

static void safeQuit(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('q'));
}

static void forcedQuit(Editor& ed) {
    press(ed, EventType::Prefix);
    press(ed, EventType::Quit);
}

static void save(Editor& ed) {
    press(ed, EventType::Prefix);
    press(ed, EventType::Save);
}

static void newBuffer(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('n'));
}

TEST(safe_quit_single_saved_exits) {
    Editor ed(std::make_unique<FakeClipboard>());
    CHECK(!ed.active().modified);
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_single_modified_blocks) {
    Editor ed(std::make_unique<FakeClipboard>());
    type(ed, "hola");
    press(ed, EventType::Escape);
    CHECK(ed.active().modified);
    safeQuit(ed);
    CHECK(ed.running_);
    CHECK(ed.statusMessage_.text.find("sin guardar") != std::string::npos);
}

TEST(safe_quit_one_saved_one_modified_from_A) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    type(ed, "modB");
    press(ed, EventType::Escape);
    ed.buffers.activate(0);
    CHECK(!ed.buffers.at(0).modified);
    CHECK(ed.buffers.at(1).modified);
    safeQuit(ed);
    CHECK(ed.running_);
}

TEST(safe_quit_active_saved_inactive_modified) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    type(ed, "x");
    press(ed, EventType::Escape);
    ed.buffers.activate(0);
    safeQuit(ed);
    CHECK(ed.running_);
}

TEST(safe_quit_active_modified_others_saved) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    newBuffer(ed);
    type(ed, "modActive");
    press(ed, EventType::Escape);
    CHECK(!ed.buffers.at(0).modified);
    CHECK(!ed.buffers.at(1).modified);
    CHECK(ed.buffers.at(2).modified);
    safeQuit(ed);
    CHECK(ed.running_);
}

TEST(safe_quit_varios_modificados) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    newBuffer(ed);
    for (int i = 0; i < 3; ++i) ed.buffers.at(i).modified = true;
    safeQuit(ed);
    CHECK(ed.running_);
    CHECK(ed.statusMessage_.text.find("sin guardar") != std::string::npos);
}

TEST(safe_quit_todos_guardados_despues) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    newBuffer(ed);
    for (int i = 0; i < 3; ++i) {
        ed.buffers.at(i).modified = true;
        ed.buffers.at(i).savedLines = ed.buffers.at(i).document.snapshot();
        ed.buffers.at(i).modified = false;
    }
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_no_destruye_buffers) {
    Editor ed(std::make_unique<FakeClipboard>());
    type(ed, "abc");
    press(ed, EventType::Escape);
    int cnt = ed.buffers.count();
    auto snap = ed.active().document.snapshot();
    safeQuit(ed);
    CHECK(ed.running_);
    CHECK_EQ(ed.buffers.count(), cnt);
    CHECK(ed.active().document.snapshot() == snap);
}

TEST(safe_quit_reintentar_despues_de_guardar) {
    Editor ed(std::make_unique<FakeClipboard>());
    type(ed, "xyz");
    press(ed, EventType::Escape);
    safeQuit(ed);
    CHECK(ed.running_);
    ed.active().modified = false;
    ed.active().savedLines = ed.active().document.snapshot();
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_bloquea_forzado_permite) {
    Editor ed(std::make_unique<FakeClipboard>());
    type(ed, "hola");
    press(ed, EventType::Escape);
    safeQuit(ed);
    CHECK(ed.running_);
    forcedQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_forzado_varios_modificados) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    newBuffer(ed);
    for (int i = 0; i < 3; ++i) ed.buffers.at(i).modified = true;
    safeQuit(ed);
    CHECK(ed.running_);
    forcedQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_buffer_nuevo_vacio) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    CHECK(!ed.active().modified);
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_buffer_nuevo_con_contenido) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    type(ed, "contenido");
    press(ed, EventType::Escape);
    CHECK(ed.active().modified);
    safeQuit(ed);
    CHECK(ed.running_);
}

TEST(safe_quit_buffer_nuevo_guardado_posterior) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    type(ed, "contenido");
    press(ed, EventType::Escape);
    ed.active().modified = false;
    ed.active().savedLines = ed.active().document.snapshot();
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_mixto_integracion) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    type(ed, "modB");
    press(ed, EventType::Escape);
    newBuffer(ed);
    newBuffer(ed);
    type(ed, "modD");
    press(ed, EventType::Escape);
    CHECK(!ed.buffers.at(0).modified);
    CHECK(ed.buffers.at(1).modified);
    CHECK(!ed.buffers.at(2).modified);
    CHECK(ed.buffers.at(3).modified);
    safeQuit(ed);
    CHECK(ed.running_);
    ed.buffers.at(1).modified = false;
    ed.buffers.at(3).modified = false;
    safeQuit(ed);
    CHECK(!ed.running_);
}
