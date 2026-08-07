#include <string>
#include <vector>

#include "Cursor.h"
#include "Document.h"
#include "test_framework.h"

using Lines = std::vector<std::string>;

static Document docOf(const std::vector<const char*>& lines) {
    Document d;
    std::vector<std::string> v(lines.begin(), lines.end());
    d.restore(v);
    return d;
}

// Comprueba los invariantes del cursor siempre (grupo 14).
static void assertCursorConsistent(const Cursor& c, const Document& d) {
    CHECK(c.line >= 0);
    CHECK(c.col >= 0);
    CHECK(c.line < d.lineCount());
    CHECK(c.col <= d.lineLength(c.line));
}

// ---------------------------------------------------------------------------
// 4. MoveLeft
// ---------------------------------------------------------------------------
TEST(cursor_left_mid_line) {
    Document d = docOf({"abc"});
    Cursor c;
    c.col = 1;
    c.moveLeft(d);
    CHECK_EQ(c.col, 0);
    assertCursorConsistent(c, d);
}

TEST(cursor_left_start_wraps_to_prev) {
    Document d = docOf({"abcdef", "ghi"});
    Cursor c;
    c.line = 1;
    c.col = 0;
    c.moveLeft(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 6);
    assertCursorConsistent(c, d);
}

TEST(cursor_left_empty_doc) {
    Document d;
    Cursor c;
    c.moveLeft(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
}

TEST(cursor_left_first_position_noop) {
    Document d = docOf({"abc"});
    Cursor c;
    c.moveLeft(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
}

// ---------------------------------------------------------------------------
// 4. MoveRight
// ---------------------------------------------------------------------------
TEST(cursor_right_mid_line) {
    Document d = docOf({"abc"});
    Cursor c;
    c.col = 1;
    c.moveRight(d);
    CHECK_EQ(c.col, 2);
}

TEST(cursor_right_end_wraps_to_next) {
    Document d = docOf({"abc", "xyz"});
    Cursor c;
    c.line = 0;
    c.col = 3;
    c.moveRight(d);
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 0);
}

TEST(cursor_right_empty_doc) {
    Document d;
    Cursor c;
    c.moveRight(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
}

TEST(cursor_right_last_char_noop) {
    Document d = docOf({"abc"});
    Cursor c;
    c.line = 0;
    c.col = 3;
    c.moveRight(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 3);
}

// ---------------------------------------------------------------------------
// 5. MoveUp / MoveDown
// ---------------------------------------------------------------------------
TEST(cursor_up_middle) {
    Document d = docOf({"aaaa", "bb", "cccc"});
    Cursor c;
    c.line = 2;
    c.col = 3;
    c.moveUp(d);
    CHECK_EQ(c.line, 1);
    assertCursorConsistent(c, d);
}

TEST(cursor_up_first_line_noop) {
    Document d = docOf({"abc"});
    Cursor c;
    c.moveUp(d);
    CHECK_EQ(c.line, 0);
}

TEST(cursor_up_empty_doc) {
    Document d;
    Cursor c;
    c.moveUp(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
}

TEST(cursor_up_shorter_line_clamps_and_remembers) {
    Document d = docOf({"aaaaa", "bb", "aaaaa"});
    Cursor c;
    c.line = 2;
    for (int i = 0; i < 4; ++i)
        c.moveRight(d); // col y preferredCol pasan a 4
    CHECK_EQ(c.col, 4);
    c.moveUp(d); // -> linea 1, corta: se recorta a 2
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 2);
    c.moveUp(d); // -> linea 0, larga: recupera la columna preferida (4)
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 4);
    assertCursorConsistent(c, d);
}

TEST(cursor_up_longer_line) {
    Document d = docOf({"aaaa", "bb", "cccccc"});
    Cursor c;
    c.line = 2;
    c.moveRight(d); // preferredCol = 1
    c.moveUp(d);
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 1);
}

TEST(cursor_down_middle) {
    Document d = docOf({"aaaa", "bb", "cccc"});
    Cursor c;
    c.col = 1;
    c.moveDown(d);
    CHECK_EQ(c.line, 1);
    assertCursorConsistent(c, d);
}

TEST(cursor_down_last_line_noop) {
    Document d = docOf({"abc"});
    Cursor c;
    c.line = 0;
    c.moveDown(d);
    CHECK_EQ(c.line, 0);
}

TEST(cursor_down_empty_doc) {
    Document d;
    Cursor c;
    c.moveDown(d);
    CHECK_EQ(c.line, 0);
}

TEST(cursor_down_shorter_line_clamps_and_remembers) {
    Document d = docOf({"aaaaa", "bb"});
    Cursor c;
    for (int i = 0; i < 4; ++i)
        c.moveRight(d); // preferredCol = 4
    c.moveDown(d); // -> linea 1 corta -> col 2
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 2);
    c.moveDown(d); // ya no hay mas
    CHECK_EQ(c.line, 1);
    assertCursorConsistent(c, d);
}

// ---------------------------------------------------------------------------
// 6. MoveHome
// ---------------------------------------------------------------------------
TEST(cursor_home_empty_line) {
    Document d = docOf({"", "abc"});
    Cursor c;
    c.line = 1;
    c.col = 2;
    c.moveHome();
    CHECK_EQ(c.col, 0);
}

TEST(cursor_home_normal) {
    Document d = docOf({"hola mundo"});
    Cursor c;
    c.col = 5;
    c.moveHome();
    CHECK_EQ(c.col, 0);
}

TEST(cursor_home_already_start) {
    Document d = docOf({"abc"});
    Cursor c;
    c.moveHome();
    CHECK_EQ(c.col, 0);
}

TEST(cursor_home_at_end_line) {
    Document d = docOf({"abc"});
    Cursor c;
    c.col = 3;
    c.moveHome();
    CHECK_EQ(c.col, 0);
}

// ---------------------------------------------------------------------------
// 7. MoveEnd
// ---------------------------------------------------------------------------
TEST(cursor_end_empty_line) {
    Document d = docOf({"", "abc"});
    Cursor c;
    c.line = 1;
    c.col = 0;
    c.moveEnd(d);
    CHECK_EQ(c.col, 3);
}

TEST(cursor_end_normal) {
    Document d = docOf({"hola"});
    Cursor c;
    c.col = 2;
    c.moveEnd(d);
    CHECK_EQ(c.col, 4);
}

TEST(cursor_end_from_start) {
    Document d = docOf({"abcd"});
    Cursor c;
    c.moveEnd(d);
    CHECK_EQ(c.col, 4);
}

TEST(cursor_end_already_end) {
    Document d = docOf({"abcd"});
    Cursor c;
    c.col = 4;
    c.moveEnd(d);
    CHECK_EQ(c.col, 4);
}

// ---------------------------------------------------------------------------
// 14. Consistencia tras muchas operaciones
// ---------------------------------------------------------------------------
TEST(cursor_consistent_after_edits) {
    Document d = docOf({"hola", "mundo", "cruel"});
    Cursor c;
    for (int i = 0; i < 200; ++i) {
        switch (i % 4) {
            case 0: c.moveLeft(d); break;
            case 1: c.moveRight(d); break;
            case 2: c.moveUp(d); break;
            case 3: c.moveDown(d); break;
        }
        assertCursorConsistent(c, d);
    }
}

TEST(cursor_clamp_after_backspace) {
    Document d = docOf({"abcdef"});
    Cursor c;
    c.col = 6;
    c.clampToLine(d);
    CHECK_EQ(c.col, 6);
    d.deleteCharAt(0, 5);
    c.clampToLine(d);
    CHECK(c.col <= d.lineLength(0));
    assertCursorConsistent(c, d);
}

TEST(cursor_col_never_negative) {
    Document d = docOf({"abc"});
    Cursor c;
    c.col = 50;
    c.moveHome();
    c.moveLeft(d);
    CHECK(c.col >= 0);
    CHECK(c.line >= 0);
}