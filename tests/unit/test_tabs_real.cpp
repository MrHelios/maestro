#include "core/Cursor.h"
#include "core/Document.h"
#include "core/utf8.h"
#include "test_framework.h"
#include <string>
#include <vector>

static int expectedVisual(const std::string& line, int byteCol) {
    int v = 0;
    int n = (int)line.size();
    int limit = byteCol < n ? byteCol : n;
    int i = 0;
    while (i < limit) {
        unsigned char c = (unsigned char)line[i];
        if (c == '\t') { v = ((v / utf8::TAB_WIDTH) + 1) * utf8::TAB_WIDTH; i += 1; }
        else { v += 1; i += utf8::cellLen(line, i, n); }
    }
    return v;
}
static Document docOf(const std::vector<const char*>& lines) {
    Document d; std::vector<std::string> v(lines.begin(), lines.end()); d.restore(v); return d;
}

// byte -> visual
TEST(tab_real_byte_to_visual_A_tab_B) {
    std::string s = "A\tB";
    CHECK_EQ((int)s.size(), 3);
    CHECK_EQ(utf8::columnOf(s, 0), expectedVisual(s, 0));
    CHECK_EQ(utf8::columnOf(s, 1), expectedVisual(s, 1));
    CHECK_EQ(utf8::columnOf(s, 2), expectedVisual(s, 2));
    CHECK_EQ(utf8::columnOf(s, 3), expectedVisual(s, 3));
    Cursor c; c.col = 0; CHECK_EQ(c.visualColumn(s), expectedVisual(s, 0));
    c.col = 1; CHECK_EQ(c.visualColumn(s), expectedVisual(s, 1));
    c.col = 2; CHECK_EQ(c.visualColumn(s), expectedVisual(s, 2));
    c.col = 3; CHECK_EQ(c.visualColumn(s), expectedVisual(s, 3));
}
TEST(tab_real_byte_to_visual_A_tab_B_tab_C) {
    std::string s = "A\tB\tC";
    CHECK_EQ(utf8::columnOf(s, 0), expectedVisual(s, 0));
    CHECK_EQ(utf8::columnOf(s, 1), expectedVisual(s, 1));
    CHECK_EQ(utf8::columnOf(s, 2), expectedVisual(s, 2));
    CHECK_EQ(utf8::columnOf(s, 3), expectedVisual(s, 3));
    CHECK_EQ(utf8::columnOf(s, 4), expectedVisual(s, 4));
    CHECK_EQ(utf8::columnOf(s, 5), expectedVisual(s, 5));
}
TEST(tab_real_byte_to_visual_tab_A) {
    std::string s = "\tA";
    CHECK_EQ(utf8::columnOf(s, 0), expectedVisual(s, 0));
    CHECK_EQ(utf8::columnOf(s, 1), expectedVisual(s, 1));
    CHECK_EQ(utf8::columnOf(s, 2), expectedVisual(s, 2));
    Cursor c; c.col = 1; CHECK_EQ(c.visualColumn(s), expectedVisual(s, 1));
}
TEST(tab_real_byte_to_visual_ABC_tab_DEF) {
    std::string s = "ABC\tDEF";
    CHECK_EQ(utf8::columnOf(s, 3), expectedVisual(s, 3));
    CHECK_EQ(utf8::columnOf(s, 4), expectedVisual(s, 4));
    CHECK_EQ(utf8::columnOf(s, 7), expectedVisual(s, 7));
    CHECK_EQ(utf8::columnOf(s, 0), expectedVisual(s, 0));
}

// gap property: visual after tab jumps >1, not 1
TEST(tab_real_visual_gap_jump) {
    std::string s = "A\tB";
    int v1 = utf8::columnOf(s, 1);
    int v2 = utf8::columnOf(s, 2);
    CHECK(v2 > v1 + 1);
    s = "\tA";
    v1 = utf8::columnOf(s, 0);
    v2 = utf8::columnOf(s, 1);
    CHECK(v2 > v1 + 1);
    s = "ABC\tDEF";
    v1 = utf8::columnOf(s, 3);
    v2 = utf8::columnOf(s, 4);
    CHECK_EQ(v2, 4);
    s = "A\tB\tC";
    v1 = utf8::columnOf(s, 3);
    v2 = utf8::columnOf(s, 4);
    CHECK(v2 > v1 + 1);
}

// Left / Right with tab: byte steps are 1 but visual jumps
TEST(tab_real_Left_Right) {
    Document d = docOf({"A\tB", "ABC\tDEF"});
    Cursor c; c.line = 0; c.col = 0;
    c.moveRight(d); CHECK_EQ(c.col, 1);
    CHECK_EQ(c.visualColumn(d), expectedVisual("A\tB", 1));
    c.moveRight(d); CHECK_EQ(c.col, 2);
    CHECK_EQ(c.visualColumn(d), expectedVisual("A\tB", 2));
    c.moveRight(d); CHECK_EQ(c.col, 3);
    c.moveLeft(d); CHECK_EQ(c.col, 2);
    CHECK_EQ(c.visualColumn(d), expectedVisual("A\tB", 2));
    c.moveLeft(d); CHECK_EQ(c.col, 1);
    c.moveLeft(d); CHECK_EQ(c.col, 0);
}

// Home / End visual
TEST(tab_real_Home_End) {
    Document d = docOf({"A\tB", "\tA", "ABC\tDEF"});
    Cursor c;
    c.line = 0; c.col = 2; c.moveHome(); CHECK_EQ(c.col, 0); CHECK_EQ(c.visualColumn(d), 0);
    c.moveEnd(d); CHECK_EQ(c.col, 3); CHECK_EQ(c.visualColumn(d), expectedVisual("A\tB", 3));
    c.line = 1; c.moveEnd(d); CHECK_EQ(c.col, 2); CHECK_EQ(c.visualColumn(d), expectedVisual("\tA", 2));
    c.moveHome(); CHECK_EQ(c.col, 0);
    c.line = 2; c.moveEnd(d); CHECK_EQ(c.col, 7); CHECK_EQ(c.visualColumn(d), expectedVisual("ABC\tDEF", 7));
}

TEST(tab_real_Up_Down_visual) {
    Document d = docOf({"A\tB", "0123456789", "\tA"});
    Cursor c;
    c.line = 0;
    c.col = 0;
    c.moveRight(d);
    c.moveRight(d);
    CHECK_EQ(c.col, 2);
    CHECK_EQ(c.visualColumn(d), 4);
    c.moveDown(d);
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 4);
    CHECK_EQ(c.visualColumn(d), 4);
    c.moveDown(d);
    CHECK_EQ(c.line, 2);
    CHECK_EQ(c.col, 1);
    CHECK_EQ(c.visualColumn(d), 4);
    c.moveUp(d);
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 4);
    c.moveUp(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 2);
    CHECK_EQ(c.visualColumn(d), 4);
}

// Backspace with tab
TEST(tab_real_Backspace) {
    Document d = docOf({"A\tB"});
    Cursor c; c.line = 0; c.col = 2;
    int bytes = d.deleteCharBefore(0, c.col);
    CHECK_EQ(bytes, 1);
    c.col -= bytes;
    CHECK_EQ(d.lineAt(0), "AB");
    CHECK_EQ(c.col, 1);
    CHECK_EQ(utf8::columnOf(d.lineAt(0), c.col), 1);
    Document d2 = docOf({"\tA"});
    c.line = 0; c.col = 1;
    bytes = d2.deleteCharBefore(0, c.col);
    CHECK_EQ(bytes, 1);
    c.col -= bytes;
    CHECK_EQ(d2.lineAt(0), "A");
}

// Delete with tab
TEST(tab_real_Delete) {
    Document d = docOf({"A\tB"});
    Cursor c; c.line = 0; c.col = 1;
    int bytes = d.deleteCharAt(0, c.col);
    CHECK_EQ(bytes, 1);
    CHECK_EQ(d.lineAt(0), "AB");
    CHECK_EQ(c.col, 1);
    Document d2 = docOf({"\tA"});
    c.col = 0;
    bytes = d2.deleteCharAt(0, 0);
    CHECK_EQ(bytes, 1);
    CHECK_EQ(d2.lineAt(0), "A");
}

// End visual should be expanded, not byte length
TEST(tab_real_End_visual_expanded) {
    std::string s = "A\tB";
    CHECK(utf8::columnOf(s, (int)s.size()) > (int)s.size());
    s = "\tA";
    CHECK(utf8::columnOf(s, (int)s.size()) > (int)s.size());
    s = "ABC\tDEF";
    CHECK(utf8::columnOf(s, (int)s.size()) >= (int)s.size());
    CHECK_EQ(utf8::columnOf(s, (int)s.size()), expectedVisual(s, (int)s.size()));
}

TEST(tab_real_byte_to_visual_long_ascii_prefix) {
    std::string s = "1234567\tA";
    CHECK_EQ(utf8::columnOf(s, 8), expectedVisual(s, 8));
    CHECK_EQ(utf8::columnOf(s, 9), expectedVisual(s, 9));
    s = "12345678\tA";
    CHECK_EQ(utf8::columnOf(s, 8), expectedVisual(s, 8));
    CHECK_EQ(utf8::columnOf(s, 9), expectedVisual(s, 9));
    s = "1234567890123456\tX";
    CHECK_EQ(utf8::columnOf(s, 17), expectedVisual(s, 17));
}
