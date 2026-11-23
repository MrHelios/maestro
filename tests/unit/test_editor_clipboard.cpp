// Tests de integracion del Editor contra la interfaz SystemClipboard, usando FakeClipboard.
//
// El Editor depende de la interfaz abstracta SystemClipboard y lo inyecta por el constructor. 
// Estos tests ejercitan el camino de copia/pega del Editor 
// (setClipboardBlock -> clipboard_->copy, getClipboardBlock -> clipboard_->paste) 
// pero sustituyen el backend real (SDL/X11) por un FakeClipboard.

#include <cstdlib>
#include <memory>
#include <string>

#include "test_framework.h"
#include "platform/InputEvent.h"  // usa InputEvent comun
#include "helpers/FakeClipboard.h"
#include "platform/clipboard/SystemClipboard.h"

#define private public
#include "app/Editor.h"
#undef private

namespace {
static InputEvent insertChar(const std::string& s) {
    InputEvent e;
    
    e.type = InputEventType::InsertChar;
    e.text = s;
    return e;
}

static InputEvent esc() { 
    InputEvent e;
    
    e.type = InputEventType::Escape;
    return e;
}

static void press(Editor& ed, InputEventType t) { 
    InputEvent e;
    
    e.type = t; 
    ed.handleEvent(e); 
}

static void type(Editor& ed, const std::string& s) {
    if (ed.state_ != State::Interaccion) {
        if (ed.state_ == State::Seleccion) ed.handleEvent(esc());
        ed.handleEvent(insertChar("i"));
    }
    for (unsigned char c : s) ed.handleEvent(insertChar(std::string(1, (char)c)));
}
} // namespace

TEST(editor_clipboard_copy_paste_simple) {
    FakeClipboard::resetGlobal();

    auto fc = std::make_unique<FakeClipboard>();
    FakeClipboard* raw = fc.get();
    Editor ed(std::move(fc));

    type(ed, "hola");
    press(ed, InputEventType::Escape);
    press(ed, InputEventType::MoveHome);
    ed.handleEvent(insertChar("s"));
    press(ed, InputEventType::MoveRight);
    press(ed, InputEventType::MoveRight);
    ed.handleEvent(insertChar("c"));
    CHECK_EQ(raw->paste().value(), "ho");
    CHECK(!ed.hasSelection());

    press(ed, InputEventType::MoveEnd);
    ed.handleEvent(insertChar("p"));
    CHECK_EQ(ed.active().document.lineAt(0), "holaho");
}

TEST(editor_clipboard_paste_empty) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    Editor ed(std::move(fc));

    type(ed, "abc");
    press(ed, InputEventType::Escape);

    size_t undoBefore = ed.active().undoStack.size();
    ed.handleEvent(insertChar("p"));
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
}

TEST(editor_clipboard_utf8) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    Editor ed(std::move(fc));

    ed.active().document.restore({std::string("caf\xC3\xA9")});
    ed.active().cursor.col = 0;
    press(ed, InputEventType::MoveHome);

    ed.handleEvent(insertChar("s"));
    press(ed, InputEventType::MoveEnd);

    ed.handleEvent(insertChar("c"));
    press(ed, InputEventType::MoveEnd);

    ed.handleEvent(insertChar("p"));
    CHECK_EQ(ed.active().document.lineAt(0), std::string("caf\xC3\xA9")+std::string("caf\xC3\xA9"));
}

TEST(editor_clipboard_multiline) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    Editor ed(std::move(fc));

    ed.active().document.restore({"linea 1", "linea 2", "linea 3"});
    ed.active().cursor.line = 0; 
    ed.active().cursor.col = 0;
    press(ed, InputEventType::MoveHome);

    ed.handleEvent(insertChar("s"));
    press(ed, InputEventType::MoveEnd);
    press(ed, InputEventType::MoveDown);
    press(ed, InputEventType::MoveDown);
    ed.handleEvent(insertChar("c"));

    auto block = ed.getClipboardBlock();
    CHECK(block == (std::vector<std::string>{"linea 1","linea 2","linea 3"}));

    ed.active().document.restore({"X"});
    ed.active().cursor.line = 0; 
    ed.active().cursor.col = 1;
    ed.handleEvent(insertChar("p"));
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"Xlinea 1","linea 2","linea 3"}));
}

TEST(editor_clipboard_cut_failure_aborts) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    FakeClipboard* raw = fc.get();
    Editor ed(std::move(fc));

    type(ed, "hola");
    press(ed, InputEventType::Escape);
    press(ed, InputEventType::MoveHome);

    ed.handleEvent(insertChar("s"));
    press(ed, InputEventType::MoveRight);
    press(ed, InputEventType::MoveRight);
    raw->setFailCopy(true);
    size_t undoBefore = ed.active().undoStack.size();
    ed.handleEvent(insertChar("x"));
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK(ed.hasSelection());
}

TEST(editor_clipboard_ownsClipboard) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    FakeClipboard* raw = fc.get();
    Editor ed(std::move(fc));

    CHECK(!raw->ownsClipboard());
    type(ed, "ab");
    press(ed, InputEventType::Escape);
    press(ed, InputEventType::MoveHome);
    ed.handleEvent(insertChar("s"));
    press(ed, InputEventType::MoveEnd);
    ed.handleEvent(insertChar("c"));
    CHECK(raw->ownsClipboard());

    raw->simulateExternalCopy("external");
    CHECK(!raw->ownsClipboard());

    press(ed, InputEventType::MoveEnd);
    ed.handleEvent(insertChar("p"));
    CHECK_EQ(ed.active().document.lineAt(0), "abexternal");
}

TEST(editor_clipboard_external_is_source_of_truth) {
    FakeClipboard::resetGlobal();
    auto fc = std::make_unique<FakeClipboard>();
    FakeClipboard* raw = fc.get();
    Editor ed(std::move(fc));

    type(ed, "A");
    press(ed, InputEventType::Escape);
    press(ed, InputEventType::MoveHome);
    ed.handleEvent(insertChar("s"));
    press(ed, InputEventType::MoveEnd);
    ed.handleEvent(insertChar("c"));
    CHECK_EQ(raw->paste().value(), "A");

    raw->simulateExternalCopy("B");
    press(ed, InputEventType::MoveEnd);
    ed.handleEvent(insertChar("p"));
    CHECK_EQ(ed.active().document.lineAt(0), "AB");
}
