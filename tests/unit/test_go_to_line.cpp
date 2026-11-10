#include <string>

#include "test_framework.h"
#include "app/Editor.h"
#include "helpers/FakeClipboard.h"
#include "platform/InputEvent.h"
#include "document/Document.h"

namespace {
// Helpers inline de construcción de InputEvent para los tests.
inline InputEvent makeCharEvent(const std::string& text) {
    InputEvent e;
    e.type = InputEventType::InsertChar;
    e.text = text;
    return e;
}

inline InputEvent makeKeyEvent(InputEventType type) {
    InputEvent e;
    e.type = type;
    return e;
}

static void setupDocumentWithLines(Editor& editor, int numLines) {
    Buffer& buffer = editor.getActiveBufferForTesting();
    std::vector<std::string> lines;
    for (int i = 0; i < numLines; ++i) {
        lines.push_back("Line " + std::to_string(i + 1));
    }
    buffer.document.restore(lines);
}
} // namespace

TEST(go_to_line_enter) {
    Editor editor;
    
    setupDocumentWithLines(editor, 100);
    editor.processEventForTesting(makeCharEvent("g"));
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::IrAFila));
    CHECK_EQ(editor.getGoToLineQueryForTesting(), std::string(""));
}

TEST(go_to_line_digits) {
    Editor editor;
    
    setupDocumentWithLines(editor, 100);
    editor.processEventForTesting(makeCharEvent("g"));
    editor.processEventForTesting(makeCharEvent("1"));
    editor.processEventForTesting(makeCharEvent("2"));
    editor.processEventForTesting(makeCharEvent("3"));
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::IrAFila));
    CHECK_EQ(editor.getGoToLineQueryForTesting(), std::string("123"));
}

TEST(go_to_line_invalid_chars) {
    Editor editor;
    
    setupDocumentWithLines(editor, 100);
    editor.processEventForTesting(makeCharEvent("g"));
    editor.processEventForTesting(makeCharEvent("a"));
    editor.processEventForTesting(makeCharEvent("x"));
    editor.processEventForTesting(makeCharEvent("-"));
    editor.processEventForTesting(makeCharEvent("."));
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::IrAFila));
    CHECK_EQ(editor.getGoToLineQueryForTesting(), std::string(""));
}

TEST(go_to_line_backspace) {
    Editor editor;
    setupDocumentWithLines(editor, 100);
    editor.processEventForTesting(makeCharEvent("g"));
    editor.processEventForTesting(makeCharEvent("1"));
    editor.processEventForTesting(makeCharEvent("2"));
    editor.processEventForTesting(makeKeyEvent(InputEventType::Backspace));
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::IrAFila));
    CHECK_EQ(editor.getGoToLineQueryForTesting(), std::string("1"));
}

TEST(go_to_line_valid_50) {
    Editor editor;
    setupDocumentWithLines(editor, 100);
    Buffer& buffer = editor.getActiveBufferForTesting();
    editor.processEventForTesting(makeCharEvent("g"));
    editor.processEventForTesting(makeCharEvent("5"));
    editor.processEventForTesting(makeCharEvent("0"));
    editor.processEventForTesting(makeKeyEvent(InputEventType::InsertNewline));
    
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::Navegacion));
    CHECK_EQ(buffer.cursor.line, 49);
    CHECK_EQ(buffer.cursor.col, 0);
}

TEST(go_to_line_first) {
    Editor editor;
    
    setupDocumentWithLines(editor, 100);
    Buffer& buffer = editor.getActiveBufferForTesting();
    editor.processEventForTesting(makeCharEvent("g"));
    editor.processEventForTesting(makeCharEvent("1"));
    editor.processEventForTesting(makeKeyEvent(InputEventType::InsertNewline));
    
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::Navegacion));
    CHECK_EQ(buffer.cursor.line, 0);
    CHECK_EQ(buffer.cursor.col, 0);
}

TEST(go_to_line_last) {
    Editor editor;
    
    setupDocumentWithLines(editor, 100);
    Buffer& buffer = editor.getActiveBufferForTesting();
    editor.processEventForTesting(makeCharEvent("g"));
    editor.processEventForTesting(makeCharEvent("1"));
    editor.processEventForTesting(makeCharEvent("0"));
    editor.processEventForTesting(makeCharEvent("0"));
    editor.processEventForTesting(makeKeyEvent(InputEventType::InsertNewline));
    
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::Navegacion));
    CHECK_EQ(buffer.cursor.line, 99);
    CHECK_EQ(buffer.cursor.col, 0);
}

TEST(go_to_line_paste_digits) {
    FakeClipboard::resetGlobal();
    auto clipboard = std::make_unique<FakeClipboard>();
    
    clipboard->simulateExternalCopy("42");
    Editor ed(std::move(clipboard));
    Buffer& buffer = ed.getActiveBufferForTesting();
    
    setupDocumentWithLines(ed, 100);
    ed.processEventForTesting(makeCharEvent("g"));
    ed.processEventForTesting(makeKeyEvent(InputEventType::Paste));
    CHECK_EQ(ed.getGoToLineQueryForTesting(), std::string("42"));
    ed.processEventForTesting(makeKeyEvent(InputEventType::InsertNewline));
    
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()), static_cast<int>(State::Navegacion));
    CHECK_EQ(buffer.cursor.line, 41);
    CHECK_EQ(buffer.cursor.col, 0);
}

TEST(go_to_line_paste_filters_non_digits) {
    FakeClipboard::resetGlobal();
    auto clipboard = std::make_unique<FakeClipboard>();
    
    clipboard->simulateExternalCopy("línea 42");
    Editor ed(std::move(clipboard));
    Buffer& buffer = ed.getActiveBufferForTesting();
    
    setupDocumentWithLines(ed, 100);
    ed.processEventForTesting(makeCharEvent("g"));
    ed.processEventForTesting(makeKeyEvent(InputEventType::Paste));
    CHECK_EQ(ed.getGoToLineQueryForTesting(), std::string("42"));
    ed.processEventForTesting(makeKeyEvent(InputEventType::InsertNewline));
    
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()), static_cast<int>(State::Navegacion));
    CHECK_EQ(buffer.cursor.line, 41);
    CHECK_EQ(buffer.cursor.col, 0);
}

TEST(go_to_line_paste_noop_on_no_digits) {
    FakeClipboard::resetGlobal();
    auto clipboard = std::make_unique<FakeClipboard>();
    
    clipboard->simulateExternalCopy("abc");
    Editor ed(std::move(clipboard));
    setupDocumentWithLines(ed, 100);
    ed.processEventForTesting(makeCharEvent("g"));
    ed.processEventForTesting(makeKeyEvent(InputEventType::Paste));
    CHECK_EQ(ed.getGoToLineQueryForTesting(), std::string(""));
}

TEST(go_to_line_paste_concatenates_digits) {
    FakeClipboard::resetGlobal();
    auto clipboard = std::make_unique<FakeClipboard>();
    
    clipboard->simulateExternalCopy("1\n2");
    Editor ed(std::move(clipboard));
    setupDocumentWithLines(ed, 100);
    ed.processEventForTesting(makeCharEvent("g"));
    ed.processEventForTesting(makeKeyEvent(InputEventType::Paste));
    CHECK_EQ(ed.getGoToLineQueryForTesting(), std::string("12"));
}

TEST(go_to_line_zero_invalid) {
    Editor editor;
    
    setupDocumentWithLines(editor, 100);
    editor.processEventForTesting(makeCharEvent("g"));
    editor.processEventForTesting(makeCharEvent("0"));
    editor.processEventForTesting(makeKeyEvent(InputEventType::InsertNewline));
    
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::IrAFila));
    CHECK_EQ(editor.getGoToLineQueryForTesting(), std::string(""));
}

TEST(go_to_line_cancel) {
    Editor editor;
    
    setupDocumentWithLines(editor, 100);
    Buffer& buffer = editor.getActiveBufferForTesting();
    editor.processEventForTesting(makeCharEvent("g"));
    editor.processEventForTesting(makeCharEvent("4"));
    editor.processEventForTesting(makeCharEvent("2"));
    editor.processEventForTesting(makeKeyEvent(InputEventType::Escape));
    
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::Navegacion));
    CHECK_EQ(editor.getGoToLineQueryForTesting(), std::string(""));
    CHECK_EQ(buffer.cursor.line, 0);
    CHECK_EQ(buffer.cursor.col, 0);
}

TEST(go_to_line_max_limit) {
    Editor editor;
    
    setupDocumentWithLines(editor, 100);
    editor.processEventForTesting(makeCharEvent("g"));
    for (char digit : std::string("123456789012")) {
        editor.processEventForTesting(makeCharEvent(std::string(1, digit)));
    }
    
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::IrAFila));
    CHECK_EQ(editor.getGoToLineQueryForTesting(), std::string("1234567890"));
}

TEST(go_to_line_paste_after_one_digit) {
    FakeClipboard::resetGlobal();
    auto clipboard = std::make_unique<FakeClipboard>();
    
    clipboard->simulateExternalCopy("1234567890");
    Editor ed(std::move(clipboard));
    
    setupDocumentWithLines(ed, 100);
    ed.processEventForTesting(makeCharEvent("g"));
    ed.processEventForTesting(makeCharEvent("9"));
    ed.processEventForTesting(makeKeyEvent(InputEventType::Paste));
    
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()), static_cast<int>(State::IrAFila));
    CHECK_EQ(ed.getGoToLineQueryForTesting(), std::string("9123456789"));
}

TEST(go_to_line_paste_after_five_digits) {
    FakeClipboard::resetGlobal();
    auto clipboard = std::make_unique<FakeClipboard>();
    
    clipboard->simulateExternalCopy("1234567890");
    Editor ed(std::move(clipboard));
    
    setupDocumentWithLines(ed, 100);
    ed.processEventForTesting(makeCharEvent("g"));
    for (char digit : std::string("54321")) {
        ed.processEventForTesting(makeCharEvent(std::string(1, digit)));
    }
    ed.processEventForTesting(makeKeyEvent(InputEventType::Paste));
    
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()), static_cast<int>(State::IrAFila));
    CHECK_EQ(ed.getGoToLineQueryForTesting(), std::string("5432112345"));
}

TEST(go_to_line_paste_after_ten_digits) {
    FakeClipboard::resetGlobal();
    auto clipboard = std::make_unique<FakeClipboard>();
    
    clipboard->simulateExternalCopy("1234567890");
    Editor ed(std::move(clipboard));
    
    setupDocumentWithLines(ed, 100);
    ed.processEventForTesting(makeCharEvent("g"));
    for (char digit : std::string("9876543210")) {
        ed.processEventForTesting(makeCharEvent(std::string(1, digit)));
    }
    ed.processEventForTesting(makeKeyEvent(InputEventType::Paste));
    
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()), static_cast<int>(State::IrAFila));
    CHECK_EQ(ed.getGoToLineQueryForTesting(), std::string("9876543210"));
}
