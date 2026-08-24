#include <cstdlib>
#include <string>

#include "test_framework.h"
#include "clipboard/FakeClipboard.h"
#include "clipboard/SystemClipboard.h"
#include "clipboard/X11Clipboard.h"
#define private public
#include "ui/Editor.h"
#undef private

static Event insertChar(const std::string& s) {
    Event e; e.type = EventType::InsertChar; e.text = s; return e;
}
static Event esc() { Event e; e.type = EventType::Escape; return e; }
static void press(Editor& ed, EventType t) { Event e; e.type = t; ed.handleEvent(e); }
static void type(Editor& ed, const std::string& s) {
    if (ed.state_ != State::Interaccion) {
        if (ed.state_ == State::Seleccion) ed.handleEvent(esc());
        ed.handleEvent(insertChar("i"));
    }
    for (unsigned char c : s) ed.handleEvent(insertChar(std::string(1, (char)c)));
}

TEST(system_clipboard_fake_copy_paste_simple) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;
    CHECK(cb.copy("hola"));
    CHECK(cb.ownsClipboard());
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "hola");
}

TEST(system_clipboard_fake_empty_initial) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;
    CHECK(!cb.ownsClipboard());
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "");
}

TEST(system_clipboard_fake_utf8) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;
    std::string cafe = std::string("caf\xC3\xA9");
    std::string ae = std::string("\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA");
    std::string em = std::string("\xE2\x80\x94");
    std::string jp = std::string("\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF");
    std::string emoji = std::string("\xF0\x9F\x98\x80");
    for (auto s : {cafe, ae, em, jp, emoji}) {
        CHECK(cb.copy(s));
        auto p = cb.paste();
        CHECK(p.has_value());
        CHECK_EQ(*p, s);
    }
}

TEST(system_clipboard_fake_multiline) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;
    std::string multi = "linea 1\nlinea 2\nlinea 3";
    CHECK(cb.copy(multi));
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, multi);
}

TEST(system_clipboard_fake_ownership_loss) {
    FakeClipboard::resetGlobal();
    FakeClipboard a, b;
    CHECK(a.copy("A"));
    CHECK(a.ownsClipboard());
    CHECK(b.copy("B"));
    CHECK(b.ownsClipboard());
    CHECK(!a.ownsClipboard());
    auto p = a.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "B");
}

TEST(system_clipboard_fake_external_copy) {
    FakeClipboard::resetGlobal();
    FakeClipboard a;
    CHECK(a.copy("A"));
    a.simulateExternalCopy("B");
    CHECK(!a.ownsClipboard());
    auto p = a.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "B");
}

TEST(system_clipboard_fake_copy_error) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;
    cb.setFailCopy(true);
    CHECK(!cb.copy("hola"));
    CHECK(!cb.ownsClipboard());
}

TEST(system_clipboard_fake_paste_error) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;
    cb.copy("hola");
    cb.setFailPaste(true);
    auto p = cb.paste();
    CHECK(!p.has_value());
}

TEST(system_clipboard_fake_processEvents_noop) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;
    cb.copy("x");
    cb.processEvents();
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "x");
}

TEST(system_clipboard_editor_copy_paste_simple) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    FakeClipboard* raw = fc.get();
    Editor ed(std::move(fc));
    type(ed, "hola");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    ed.handleEvent(insertChar("s"));
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);
    ed.handleEvent(insertChar("c"));
    CHECK_EQ(raw->paste().value(), "ho");
    CHECK(!ed.hasSelection());
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insertChar("p"));
    CHECK_EQ(ed.active().document.lineAt(0), "holaho");
}

TEST(system_clipboard_editor_paste_empty) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    Editor ed(std::move(fc));
    type(ed, "abc");
    press(ed, EventType::Escape);
    size_t undoBefore = ed.active().undoStack.size();
    ed.handleEvent(insertChar("p"));
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
}

TEST(system_clipboard_editor_utf8) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    Editor ed(std::move(fc));
    ed.active().document.restore({std::string("caf\xC3\xA9")});
    ed.active().cursor.col = 0;
    press(ed, EventType::MoveHome);
    ed.handleEvent(insertChar("s"));
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insertChar("c"));
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insertChar("p"));
    CHECK_EQ(ed.active().document.lineAt(0), std::string("caf\xC3\xA9")+std::string("caf\xC3\xA9"));
}

TEST(system_clipboard_editor_multiline) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    Editor ed(std::move(fc));
    ed.active().document.restore({"linea 1", "linea 2", "linea 3"});
    ed.active().cursor.line = 0; ed.active().cursor.col = 0;
    press(ed, EventType::MoveHome);
    ed.handleEvent(insertChar("s"));
    press(ed, EventType::MoveEnd);
    press(ed, EventType::MoveDown);
    press(ed, EventType::MoveDown);
    ed.handleEvent(insertChar("c"));
    auto block = ed.getClipboardBlock();
    CHECK(block == (std::vector<std::string>{"linea 1","linea 2","linea 3"}));
    ed.active().document.restore({"X"});
    ed.active().cursor.line = 0; ed.active().cursor.col = 1;
    ed.handleEvent(insertChar("p"));
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"Xlinea 1","linea 2","linea 3"}));
}

TEST(system_clipboard_editor_cut_failure_aborts) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    FakeClipboard* raw = fc.get();
    Editor ed(std::move(fc));
    type(ed, "hola");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    ed.handleEvent(insertChar("s"));
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);
    raw->setFailCopy(true);
    size_t undoBefore = ed.active().undoStack.size();
    ed.handleEvent(insertChar("x"));
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK(ed.hasSelection());
}

TEST(system_clipboard_editor_ownsClipboard) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    FakeClipboard* raw = fc.get();
    Editor ed(std::move(fc));
    CHECK(!raw->ownsClipboard());
    type(ed, "ab");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    ed.handleEvent(insertChar("s"));
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insertChar("c"));
    CHECK(raw->ownsClipboard());
    raw->simulateExternalCopy("external");
    CHECK(!raw->ownsClipboard());
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insertChar("p"));
    CHECK_EQ(ed.active().document.lineAt(0), "abexternal");
}

TEST(system_clipboard_editor_external_is_source_of_truth) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    FakeClipboard* raw = fc.get();
    Editor ed(std::move(fc));
    type(ed, "A");
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    ed.handleEvent(insertChar("s"));
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insertChar("c"));
    CHECK_EQ(raw->paste().value(), "A");
    raw->simulateExternalCopy("B");
    press(ed, EventType::MoveEnd);
    ed.handleEvent(insertChar("p"));
    CHECK_EQ(ed.active().document.lineAt(0), "AB");
}

TEST(system_clipboard_x11_fallback_no_display) {
    const char* oldDisplay = std::getenv("DISPLAY");
    std::string saved = oldDisplay ? oldDisplay : "";
    bool hadDisplay = oldDisplay != nullptr;
    ::unsetenv("DISPLAY");
    X11Clipboard cb;
    if (hadDisplay) ::setenv("DISPLAY", saved.c_str(), 1);
    else ::unsetenv("DISPLAY");
    CHECK(!cb.isAvailable());
    CHECK(cb.copy("fallback"));
    CHECK(cb.ownsClipboard());
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "fallback");
    cb.processEvents();
    CHECK(cb.ownsClipboard());
}
