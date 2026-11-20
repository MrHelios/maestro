#include <string>
#include <vector>

#include "test_framework.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "base/utf8.h"

namespace {

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

} // namespace

// byte -> visual
TEST(tab_real_byte_to_visual_A_tab_B) {
    Cursor c;
    std::string s = "A\tB";
    
    CHECK_EQ((int)s.size(), 3);
    // 'A' -> columna visual 0
    CHECK_EQ(utf8::columnOf(s, 0), 0);
    // '\t' empieza en la columna visual 1 
    // con TAB_WIDTH=4 avanza al siguiente tabstop: 4
    CHECK_EQ(utf8::columnOf(s, 1), 1);
    // 'B' -> columna visual 4
    CHECK_EQ(utf8::columnOf(s, 2), 4);
    // fin de linea -> columna visual 5
    CHECK_EQ(utf8::columnOf(s, 3), 5);
}

// caso que el tab esta en 0 (columna)
TEST(tab_real_byte_to_visual_tab_A) {
    std::string s = "\tA";
    
    CHECK_EQ((int)s.size(), 2);
    // \t
    CHECK_EQ(utf8::columnOf(s, 0), 0);
    // A
    CHECK_EQ(utf8::columnOf(s, 1), 4);
    // fin de linea
    CHECK_EQ(utf8::columnOf(s, 2), 5);
}

// caso que el tab esta en 1 (columna)
TEST(tab_real_byte_to_visual_A_tab_B_tab_C) {
    std::string s = "A\tB\tC";
    
    CHECK_EQ((int)s.size(), 5);
    // A
    CHECK_EQ(utf8::columnOf(s, 0), 0);
    // tab
    CHECK_EQ(utf8::columnOf(s, 1), 1);
    // B
    CHECK_EQ(utf8::columnOf(s, 2), 4);
    // tab
    CHECK_EQ(utf8::columnOf(s, 3), 5);
    // C
    CHECK_EQ(utf8::columnOf(s, 4), 8);
    // fin de linea
    CHECK_EQ(utf8::columnOf(s, 5), 9);
}

// caso que el tab esta en 2 (columna)
TEST(tab_real_byte_to_visual_A_B_tab_C) {
    std::string s = "AB\tC";
    
    CHECK_EQ((int)s.size(), 4);
    // A
    CHECK_EQ(utf8::columnOf(s, 0), 0);
    // B
    CHECK_EQ(utf8::columnOf(s, 1), 1);
    // tab
    CHECK_EQ(utf8::columnOf(s, 2), 2);
    // C
    CHECK_EQ(utf8::columnOf(s, 3), 4);
    // fin de linea
    CHECK_EQ(utf8::columnOf(s, 4), 5);
}

// caso que el tab esta en 3 (columna)
TEST(tab_real_byte_to_visual_ABC_tab_DEF) {
    std::string s = "ABC\tDEF";
    
    CHECK_EQ((int)s.size(), 7);
    // A
    CHECK_EQ(utf8::columnOf(s, 0), 0);
    // B
    CHECK_EQ(utf8::columnOf(s, 1), 1);
    // C
    CHECK_EQ(utf8::columnOf(s, 2), 2);
    // \t
    CHECK_EQ(utf8::columnOf(s, 3), 3);
    // D
    CHECK_EQ(utf8::columnOf(s, 4), 4);
    // E
    CHECK_EQ(utf8::columnOf(s, 5), 5);
    // F
    CHECK_EQ(utf8::columnOf(s, 6), 6);
    // fin de linea
    CHECK_EQ(utf8::columnOf(s, 7), 7);
}

// gap property:
// TAB always advances to the next multiple of 4
TEST(tab_real_advances_to_next_tab_stop) {
    for (int col = 0; col <= 16; ++col) {
        std::string s(col, 'A');
        s += '\t';

        const int expected = ((col / 4) + 1) * 4;

        CHECK_EQ(utf8::columnOf(s, col), col);
        CHECK_EQ(utf8::columnOf(s, col + 1), expected);
    }
}

TEST(tab_real_using_cursor_right_left) {
    Document d = docOf({"A\tB"});
    
    Cursor c; 
    c.line = 0; 
    c.col = 0;
    
    // A
    CHECK_EQ(c.col, 0);
    CHECK_EQ(expectedVisual("A\tB", 0), 0);
    CHECK_EQ(c.visualColumn(d), 0);
    
    // tab
    c.moveRight(d); 
    CHECK_EQ(c.col, 1);
    CHECK_EQ(expectedVisual("A\tB", 1), 1);
    CHECK_EQ(c.visualColumn(d), 1);
    
    // B
    c.moveRight(d); 
    CHECK_EQ(c.col, 2);
    CHECK_EQ(expectedVisual("A\tB", 2), 4);
    CHECK_EQ(c.visualColumn(d), 4);
    
    // fin de linea
    c.moveRight(d); 
    CHECK_EQ(c.col, 3);
    CHECK_EQ(expectedVisual("A\tB", 3), 5);
    CHECK_EQ(c.visualColumn(d), 5);
    
    // B
    c.moveLeft(d); 
    CHECK_EQ(c.col, 2);
    CHECK_EQ(expectedVisual("A\tB", 2), 4);
    CHECK_EQ(c.visualColumn(d), 4);
    
    // tab
    c.moveLeft(d);
    CHECK_EQ(c.col, 1);
    CHECK_EQ(expectedVisual("A\tB", 1), 1);
    CHECK_EQ(c.visualColumn(d), 1);
    
    // A
    c.moveLeft(d);
    CHECK_EQ(c.col, 0);
    CHECK_EQ(expectedVisual("A\tB", 0), 0);
    CHECK_EQ(c.visualColumn(d), 0);
}

TEST(tab_real_using_cursor_right_left_multiline) {
    Document d = docOf({
        "\tAB",
        "A\tB",
        "AB\t"
    });

    Cursor c;
    c.line = 0;
    c.col = 0;

    // Recorrer hacia la derecha hasta el final del archivo.
    while (true) {
        const std::string& line = d.lineAt(c.line);

        CHECK_EQ(c.visualColumn(d), expectedVisual(line, c.col));

        if (c.line == d.lineCount() - 1 &&
            c.col == static_cast<int>(line.size())) {
            break;
        }

        c.moveRight(d);
    }

    // Recorrer hacia la izquierda hasta el inicio del archivo.
    while (true) {
        const std::string& line = d.lineAt(c.line);

        CHECK_EQ(c.visualColumn(d), expectedVisual(line, c.col));

        if (c.line == 0 && c.col == 0) {
            break;
        }

        c.moveLeft(d);
    }

    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
    CHECK_EQ(c.visualColumn(d), 0);
}

TEST(tab_real_home_end) {
    Document d = docOf({"A\tB", "\tA", "ABC\tDEF"});
    
    Cursor c;
    c.line = 0; 
    c.col = 2; 
    
    c.moveHome(); 
    CHECK_EQ(c.col, 0); 
    CHECK_EQ(c.visualColumn(d), 0);
    CHECK_EQ(expectedVisual("A\tB", 0), 0);
    
    c.moveEnd(d);
    CHECK_EQ(c.col, 3); 
    CHECK_EQ(c.visualColumn(d), 5);
    CHECK_EQ(expectedVisual("A\tB", 3), 5);
    
    c.line = 1;
    c.moveEnd(d);
    CHECK_EQ(c.col, 2);
    CHECK_EQ(c.visualColumn(d), 5);
    CHECK_EQ(expectedVisual("\tA", 2), 5);
    
    c.moveHome();
    CHECK_EQ(c.col, 0);
    CHECK_EQ(c.visualColumn(d), 0);
    CHECK_EQ(expectedVisual("\tA", 0), 0);
    
    c.line = 2;
    c.moveEnd(d);
    CHECK_EQ(c.col, 7); 
    CHECK_EQ(c.visualColumn(d), expectedVisual("ABC\tDEF", 7));
    CHECK_EQ(c.visualColumn(d), 7);
}

TEST(tab_real_up_down_visual) {
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

TEST(tab_real_backspace) {
    Document d = docOf({"A\tB"});
    
    Cursor c;
    c.line = 0;
    c.col = 2;
    
    int bytes = d.deleteCharBefore(0, c.col);
    CHECK_EQ(bytes, 1);
    
    c.col -= bytes;
    CHECK_EQ(d.lineAt(0), "AB");
    CHECK_EQ(c.col, 1);
    CHECK_EQ(utf8::columnOf(d.lineAt(0), c.col), 1);
    
    Document d2 = docOf({"\tA"});
    
    c.line = 0; 
    c.col = 1;
    
    bytes = d2.deleteCharBefore(0, c.col);
    CHECK_EQ(bytes, 1);
    
    c.col -= bytes;
    CHECK_EQ(d2.lineAt(0), "A");
    CHECK_EQ(c.col, 0);
    CHECK_EQ(utf8::columnOf(d2.lineAt(0), c.col), 0);
    
    Document d3 = docOf({"AB\t"});
    
    c.line = 0;
    c.col = 3;
    
    bytes = d3.deleteCharBefore(0, c.col);
    CHECK_EQ(bytes, 1);
    
    c.col -= bytes;
    CHECK_EQ(d3.lineAt(0), "AB");
    CHECK_EQ(c.col, 2);
    CHECK_EQ(utf8::columnOf(d3.lineAt(0), c.col), 2);
}

TEST(tab_real_delete) {
    // Tabulador en medio.
    Document d = docOf({"A\tB"});

    Cursor c;
    c.line = 0;
    c.col = 1;

    int bytes = d.deleteCharAt(0, c.col);
    CHECK_EQ(bytes, 1);
    CHECK_EQ(d.lineAt(0), "AB");
    CHECK_EQ(c.col, 1);
    CHECK_EQ(utf8::columnOf(d.lineAt(0), c.col), 1);

    // Tabulador al inicio.
    Document d2 = docOf({"\tA"});

    c.line = 0;
    c.col = 0;

    bytes = d2.deleteCharAt(0, c.col);
    CHECK_EQ(bytes, 1);
    CHECK_EQ(d2.lineAt(0), "A");
    CHECK_EQ(c.col, 0);
    CHECK_EQ(utf8::columnOf(d2.lineAt(0), c.col), 0);

    // Tabulador al final.
    Document d3 = docOf({"AB\t"});

    c.line = 0;
    c.col = 2;

    bytes = d3.deleteCharAt(0, c.col);
    CHECK_EQ(bytes, 1);
    CHECK_EQ(d3.lineAt(0), "AB");
    CHECK_EQ(c.col, 2);
    CHECK_EQ(utf8::columnOf(d3.lineAt(0), c.col), 2);
}

// la expansion debe ser visual, no por bytes
TEST(tab_real_end_visual_expanded) {
    std::string s = "A\tB";
    CHECK(utf8::columnOf(s, (int)s.size()) > (int)s.size());
    
    s = "\tA";
    CHECK(utf8::columnOf(s, (int)s.size()) > (int)s.size());
    
    s = "ABC\tDEF";
    CHECK(utf8::columnOf(s, (int)s.size()) >= (int)s.size());
    CHECK_EQ(utf8::columnOf(s, (int)s.size()), expectedVisual(s, (int)s.size()));
}
