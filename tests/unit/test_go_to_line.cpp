#include "test_framework.h"
#include "ui/Editor.h"
#include "terminal/Event.h"
#include "core/Document.h"
#include <string>

static Event makeCharEvent(const std::string& text) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = text;
    return e;
}

static Event makeKeyEvent(EventType type) {
    Event e;
    e.type = type;
    e.text = "";
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
    editor.processEventForTesting(makeKeyEvent(EventType::Backspace));
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
    editor.processEventForTesting(makeKeyEvent(EventType::InsertNewline));
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
    editor.processEventForTesting(makeKeyEvent(EventType::InsertNewline));
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
    editor.processEventForTesting(makeKeyEvent(EventType::InsertNewline));
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::Navegacion));
    CHECK_EQ(buffer.cursor.line, 99);
    CHECK_EQ(buffer.cursor.col, 0);
}

TEST(go_to_line_zero_invalid) {
    Editor editor;
    setupDocumentWithLines(editor, 100);
    editor.processEventForTesting(makeCharEvent("g"));
    editor.processEventForTesting(makeCharEvent("0"));
    editor.processEventForTesting(makeKeyEvent(EventType::InsertNewline));
    CHECK_EQ(static_cast<int>(editor.getStateForTesting()), static_cast<int>(State::IrAFila));
    CHECK_EQ(editor.getGoToLineQueryForTesting(), std::string(""));
}
