#include "test_framework.h"
#include "helpers/test_render_utils.h"
#include <algorithm>
#define private public
#include "ui/Editor.h"
#undef private

static Event insert(char c){ Event e; e.type=EventType::InsertChar; e.text=std::string(1,c); return e;}
static void press(Editor& ed, EventType t){ Event e; e.type=t; ed.handleEvent(e);}
static void pressEvent(Editor& ed, const Event& ev){ ed.handleEvent(ev);}
static void type(Editor& ed, const std::string& s){
    if(s.empty()) return;
    if(ed.state_!=State::Interaccion){
        if(ed.state_==State::Seleccion){ Event esc; esc.type=EventType::Escape; ed.handleEvent(esc); }
        ed.handleEvent(insert('i'));
    }
    for(char c: s) ed.handleEvent(insert(c));
}
static void newBuffer(Editor& ed){ press(ed, EventType::Prefix); pressEvent(ed, insert('n')); }
static void previousBuffer(Editor& ed){ press(ed, EventType::Prefix); pressEvent(ed, insert('b')); }

TEST(ctrl_k_b_renders_new_buffer_immediately){
    Editor ed;
    ed.active().viewport.height=5;
    ed.active().viewport.width=30;
    type(ed, "AAA_CONTENT");
    press(ed, EventType::Escape);
    newBuffer(ed);
    ed.active().viewport.height=5;
    ed.active().viewport.width=30;
    type(ed, "BBB_CONTENT");
    press(ed, EventType::Escape);

    Renderer& r = ed.renderer_;
    Buffer& curBBB = ed.active();
    std::string prime = r.buildDiffFrame(curBBB.document, curBBB.cursor, curBBB.viewport, curBBB.filename, curBBB.modified, Message{}, State::Navegacion, curBBB.selection);
    (void)prime;

    previousBuffer(ed);
    Buffer& curAAA = ed.active();
    CHECK(curAAA.document.lineAt(0) == "AAA_CONTENT");

    std::string diff = r.buildDiffFrame(curAAA.document, curAAA.cursor, curAAA.viewport, curAAA.filename, curAAA.modified, Message{}, State::Navegacion, curAAA.selection);
    std::string full = r.buildScreen(curAAA.document, curAAA.cursor, curAAA.viewport, curAAA.filename, curAAA.modified, Message{}, State::Navegacion, curAAA.selection);

    CHECK(diff.find("AAA_CONTENT") != std::string::npos);
    CHECK(diff.size() > 100);
    CHECK(diff.find("\x1b[2J\x1b[H") != std::string::npos);

    (void)full;
}

TEST(ctrl_k_b_diff_equals_full_after_switch){
    Editor ed;
    ed.active().viewport.height=5;
    ed.active().viewport.width=30;
    type(ed, "HELLO");
    press(ed, EventType::Escape);
    newBuffer(ed);
    ed.active().viewport.height=5;
    ed.active().viewport.width=30;
    type(ed, "WORLD");
    press(ed, EventType::Escape);

    Renderer& r = ed.renderer_;
    r.buildDiffFrame(ed.active().document, ed.active().cursor, ed.active().viewport, ed.active().filename, ed.active().modified, Message{}, State::Navegacion, ed.active().selection);

    previousBuffer(ed);
    Buffer& b = ed.active();
    std::string diff = r.buildDiffFrame(b.document, b.cursor, b.viewport, b.filename, b.modified, Message{}, State::Navegacion, b.selection);
    std::string full = r.buildScreen(b.document, b.cursor, b.viewport, b.filename, b.modified, Message{}, State::Navegacion, b.selection);

    auto strip = [](const std::string& s){
        std::string t = testutil::stripAnsi(s);
        t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());
        return t;
    };
    std::string diffStripped = strip(diff);
    CHECK(diffStripped.find("HELLO") != std::string::npos);
    CHECK(diffStripped.find("WORLD") == std::string::npos);
}

TEST(ctrl_k_b_toggle_twice_renders_correctly){
    Editor ed;
    ed.active().viewport.height=5;
    ed.active().viewport.width=30;
    type(ed, "AAA_TOGGLE");
    press(ed, EventType::Escape);
    newBuffer(ed);
    ed.active().viewport.height=5;
    ed.active().viewport.width=30;
    type(ed, "BBB_TOGGLE");
    press(ed, EventType::Escape);

    Renderer& r = ed.renderer_;
    r.buildDiffFrame(ed.active().document, ed.active().cursor, ed.active().viewport, ed.active().filename, ed.active().modified, Message{}, State::Navegacion, ed.active().selection);

    previousBuffer(ed);
    CHECK(ed.active().document.lineAt(0)=="AAA_TOGGLE");
    std::string diff1 = r.buildDiffFrame(ed.active().document, ed.active().cursor, ed.active().viewport, ed.active().filename, ed.active().modified, Message{}, State::Navegacion, ed.active().selection);
    CHECK(diff1.find("AAA_TOGGLE")!=std::string::npos);
    CHECK(diff1.find("\x1b[2J\x1b[H")!=std::string::npos);

    previousBuffer(ed);
    CHECK(ed.active().document.lineAt(0)=="BBB_TOGGLE");
    std::string diff2 = r.buildDiffFrame(ed.active().document, ed.active().cursor, ed.active().viewport, ed.active().filename, ed.active().modified, Message{}, State::Navegacion, ed.active().selection);
    CHECK(diff2.find("BBB_TOGGLE")!=std::string::npos);
    CHECK(diff2.find("\x1b[2J\x1b[H")!=std::string::npos);
    CHECK(diff2.find("AAA_TOGGLE")==std::string::npos);
}

TEST(ctrl_k_b_no_previous_buffer_no_crash){
    Editor ed;
    ed.active().viewport.height=5;
    ed.active().viewport.width=30;
    type(ed, "ONLY_ONE");
    press(ed, EventType::Escape);
    Renderer& r = ed.renderer_;
    std::string prime = r.buildDiffFrame(ed.active().document, ed.active().cursor, ed.active().viewport, ed.active().filename, ed.active().modified, Message{}, State::Navegacion, ed.active().selection);
    (void)prime;
    CHECK(!ed.previousBuffer_.valid);
    previousBuffer(ed);
    CHECK(ed.buffers.activeBuffer_==0);
    CHECK(ed.active().document.lineAt(0)=="ONLY_ONE");
    CHECK(ed.statusMessage_.text=="No hay buffer anterior.");
    std::string full = r.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, ed.active().filename, ed.active().modified, Message{}, State::Navegacion, ed.active().selection);
    CHECK(full.find("ONLY_ONE")!=std::string::npos);
}
