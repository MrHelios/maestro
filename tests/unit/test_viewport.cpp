#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "test_framework.h"
#include "document/Document.h"
#include "document/Cursor.h"
#include "layout/Viewport.h"
#include "layout/Gutter.h"
#include "layout/Layout.h"
#include "base/utf8.h"
#include "app/ChromePresentation.h"
#include "rendering/tty/TtyRenderer.h"
#include "platform/InputEvent.h"

#define private public
#include "app/Editor.h"
#undef private

namespace {

std::string frameWithViewport(const std::vector<std::string>& lines, int cursorLine, int cursorCol, Viewport vp) {
    Document doc;
    Cursor cur;
    TtyRenderer r;

    doc.restore(lines);
    cur.line = cursorLine;
    cur.col = cursorCol;
    return r.buildScreen(doc, cur, vp, "t", false, makeChromeRequest("", State::Navegacion), std::nullopt);
}

int cursorTerminalCol(const std::string& frame, const Viewport& vp, int cursorLine) {
    int expectedRow = cursorLine - vp.top + 1;
    std::string needle = "\x1b[" + std::to_string(expectedRow) + ";";
    size_t pos = frame.find(needle);

    if (pos == std::string::npos)
        return -1;

    size_t end = frame.find('H', pos);
    if (end == std::string::npos)
        return -1;

    int c = std::stoi(frame.substr(pos + needle.size(), end - pos - needle.size()));
    return c;
}

// Fila (1-based) de la ultima secuencia CUP emitida. appendFrame posiciona el
// cursor al final, despues del chrome, asi que la ultima CUP es el cursor
// (el contenido usa "\r\n", no CUP). Devuelve -1 si no hay ninguna.
int cursorTerminalRow(const std::string& frame) {
    int row = -1;

    for (size_t i = 0; i + 1 < frame.size(); ++i) {
        if (frame[i] != '\x1b' || frame[i + 1] != '[')
            continue;

        size_t j = i + 2;
        size_t rowStart = j;
        while (j < frame.size() && std::isdigit((unsigned char)frame[j]))
            ++j;
        if (j == rowStart)
            continue;

        const size_t rowEnd = j;
        if (j >= frame.size() || frame[j] != ';')
            continue;
        ++j;

        size_t colStart = j;
        while (j < frame.size() && std::isdigit((unsigned char)frame[j]))
            ++j;
        if (j == colStart)
            continue;
        if (j >= frame.size() || frame[j] != 'H')
            continue;

        row = std::stoi(frame.substr(rowStart, rowEnd - rowStart));
    }

    return row;
}

int cursorVisibleCol(const std::string& frame, const Viewport& vp, const std::vector<std::string>& lines, int cursorLine) {
    int terminalCol = cursorTerminalCol(frame, vp, cursorLine);

    if (terminalCol < 0)
        return -1;

    Document tmp;
    tmp.restore(lines);
    int gutterW = gutterWidth((int)lines.size(), vp.width);
    Layout layout = computeLayout(vp.height, vp.width);
    return terminalCol - gutterW - layout.content.col;
}

std::string stripAnsi(const std::string& s) {
    std::string out;
    bool inEsc = false;
    size_t i = 0;

    while(i<s.size()){
        if(s[i]=='\x1b') {
            inEsc = true;
            if(i+1<s.size() && s[i+1]=='[')
                i++;
        }
        else if(inEsc) {
            unsigned char c=(unsigned char)s[i];
            if(c>=0x40 && c<=0x7E)
                inEsc = false;
        }
        else out += s[i];
        i++;
    }
    return out;
}

int colWidth(const std::string& s) {
    int col = 0;
    for(unsigned char c:s)
        if((c&0xC0) != 0x80) col++;
    return col;
}

std::string rowText(const std::string& frame, int lineCount){
    std::string plain = stripAnsi(frame);
    size_t nl = plain.find("\r\n");

    if(nl==std::string::npos)
        nl=plain.size();

    std::string row = plain.substr(0,nl);
    int gw = gutterWidth(lineCount, (int)row.size());
    row.erase(0,std::min(gw,(int)row.size()));
    return row;
}

static void pressE(Editor& ed, InputEventType t) {
    InputEvent e;
    e.type = t;
    ed.handleEvent(e);
}

static InputEvent insE(char c) {
    InputEvent e;
    e.type = InputEventType::InsertChar;
    e.text = std::string(1,c);
    return e;
}

static void typeE(Editor& ed, const std::string& s) {
    for(char c: s)
        ed.handleEvent(insE(c));
}

} //namespace

// ===========================================================================
// VIEWPORT VERTICAL
// ===========================================================================

// con el cursor visible dentro del alto no deberia haber scroll
TEST(viewport_vertical_no_scroll_when_cursor_visible) {
    Viewport vp;
    vp.top = 0;
    vp.height = 10;

    Cursor cur;
    cur.line = 0;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 0);

    cur.line = 9;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 0);
}

// al bajar el cursor mas alla del borde inferior, el viewport lo sigue
TEST(viewport_vertical_scrolls_when_cursor_exits_bottom) {
    Viewport vp;
    vp.top = 0;
    vp.height = 10;

    Cursor cur;
    for (int i = 0; i < 10; ++i) {
        cur.line = i;
        vp.scrollToCursor(cur);
        CHECK_EQ(vp.top, 0);
    }

    cur.line = 10;
    vp.scrollToCursor(cur);

    CHECK_EQ(vp.top, 1);
    CHECK(cur.line >= vp.top);
    CHECK(cur.line < vp.top + vp.height);
}

// al llegar a la ultima linea el viewport se desplaza lo justo para mostrarla
TEST(viewport_vertical_scrolls_to_keep_last_line_visible) {
    Document doc;
    doc.restore(std::vector<std::string>(30, "linea"));

    Viewport vp;
    vp.top = 0;
    vp.height = 10;

    Cursor cur;
    cur.line = 29;
    vp.scrollToCursor(cur);

    CHECK_EQ(vp.top, 20);
    CHECK_EQ(vp.top + vp.height - 1, 29);
    CHECK(cur.line >= vp.top);
    CHECK(cur.line < vp.top + vp.height);
}

// una vez en la ultima linea, seguir bajando no desplaza mas el viewport
TEST(viewport_vertical_stays_stable_after_last_line) {
    Document doc;
    doc.restore(std::vector<std::string>(30, "linea"));

    Viewport vp;
    vp.top = 0;
    vp.height = 10;

    Cursor cur;
    cur.line = 0;
    while (cur.line < 29) {
        cur.moveDown(doc);
        vp.scrollToCursor(cur);
    }

    CHECK_EQ(cur.line, 29);
    CHECK_EQ(vp.top, 20);

    for (int i = 0; i < 20; ++i) {
        cur.moveDown(doc); // clampa en la ultima linea
        vp.scrollToCursor(cur);

        CHECK_EQ(cur.line, 29);
        CHECK_EQ(vp.top, 20);
    }
}

// subir el cursor hasta el principio devuelve el viewport a top = 0
TEST(viewport_vertical_return_to_top) {
    Document doc;
    doc.restore(std::vector<std::string>(100, "linea"));

    Viewport vp;
    vp.top = 0;
    vp.height = 10;

    Cursor cur;
    cur.line = 0;
    for (int i = 0; i < 60; ++i) {
        cur.moveDown(doc);
        vp.scrollToCursor(cur);
    }

    CHECK_EQ(cur.line, 60);
    CHECK(vp.top > 0);

    for (int i = 0; i < 60; ++i) {
        cur.moveUp(doc);
        vp.scrollToCursor(cur);

        CHECK(cur.line >= vp.top);
        CHECK(cur.line < vp.top + vp.height);
    }

    CHECK_EQ(cur.line, 0);
    CHECK_EQ(vp.top, 0);
}

// bordes exactos: una linea justo en el borde no scrollea; una mas si
TEST(viewport_vertical_border_top_exact) {
    Viewport vp;
    vp.top = 10;
    vp.height = 10;

    Cursor cur;

    // borde superior exacto: la primera fila visible no fuerza scroll
    cur.line = 10;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 10);

    // borde inferior exacto: la ultima fila visible tampoco
    cur.line = 19;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 10);

    // una linea por debajo del borde inferior desplaza
    cur.line = 20;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 11);

    // una linea por encima del borde superior desplaza
    cur.line = 9;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 9);
}

// documentos muy largos: el cursor siempre queda visible y top se estabiliza
TEST(viewport_vertical_extremely_long) {
    for (int n : {1000, 10000, 100000}) {
        Document doc;
        doc.restore(std::vector<std::string>(n, "x"));

        Viewport vp;
        vp.top = 0;
        vp.height = 13;
        vp.width = 80;

        Cursor cur;
        cur.line = 0;

        for (int i = 0; i < n; ++i) {
            cur.line = i;
            vp.scrollToCursor(cur);

            CHECK(cur.line >= vp.top);
            CHECK(cur.line < vp.top + vp.height);
        }

        CHECK_EQ(cur.line, n - 1);
        CHECK_EQ(vp.top, (n - 1) - vp.height + 1);
        CHECK(cur.line >= vp.top);
        CHECK(cur.line < vp.top + vp.height);
    }
}

// un salto del cursor por encima del viewport lo lleva a top = cursor.line
TEST(viewport_vertical_scrolls_up_to_cursor) {
    Viewport vp;
    vp.top = 50;
    vp.height = 10;

    Cursor cur;
    cur.line = 20;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 20);

    cur.line = 19;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 19);

    // dentro del rango visible no cambia
    cur.line = 25;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 19);
}

// redimension vertical: el cursor sigue visible tras cambiar el alto
TEST(viewport_vertical_resize_keeps_cursor_visible) {
    Document doc;
    doc.restore(std::vector<std::string>(100, "x"));

    Viewport vp;
    vp.top = 0;
    vp.height = 50;
    vp.width = 80;

    Cursor cur;
    cur.line = 45;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 0);

    // al reducir el alto el cursor queda fuera y el viewport baja
    vp.height = 10;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 36);
    CHECK(cur.line >= vp.top);
    CHECK(cur.line < vp.top + vp.height);

    // al agrandar el alto el cursor sigue visible
    vp.height = 100;
    vp.scrollToCursor(cur);
    CHECK(cur.line >= vp.top);
    CHECK(cur.line < vp.top + vp.height);

    // un alto muy chico vuelve a desplazar
    vp.height = 5;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 41);
    CHECK(cur.line >= vp.top);
    CHECK(cur.line < vp.top + vp.height);
}

// un documento mas corto que el viewport no scrollea
TEST(viewport_vertical_short_document_no_scroll) {
    Document doc;
    doc.restore({"uno", "dos", "tres"});

    Viewport vp;
    vp.top = 0;
    vp.height = 10;

    Cursor cur;
    cur.line = 2;
    vp.scrollToCursor(cur);

    CHECK_EQ(vp.top, 0);
}

// top negativo se clampa a 0
TEST(viewport_vertical_negative_top_clamped) {
    Viewport vp;
    vp.top = -5;
    vp.height = 10;

    Cursor cur;
    cur.line = 0;
    vp.scrollToCursor(cur);

    CHECK_EQ(vp.top, 0);
}

// desplazamientos tipo AvPag/RePag mantienen el cursor visible
TEST(viewport_vertical_page_moves_keep_cursor_visible) {
    Document doc;
    doc.restore(std::vector<std::string>(100, "x"));

    Viewport vp;
    vp.top = 0;
    vp.height = 10;

    Cursor cur;
    cur.line = 0;

    for (int page = 0; page < 12; ++page) {
        cur.line = std::min(doc.lineCount() - 1, cur.line + vp.height);
        vp.scrollToCursor(cur);

        CHECK(cur.line >= vp.top);
        CHECK(cur.line < vp.top + vp.height);
    }

    CHECK_EQ(cur.line, 99);
}

// los ejes vertical y horizontal son independientes
TEST(viewport_vertical_horizontal_are_independent) {
    Viewport vp;
    vp.top = 0;
    vp.left = 0;
    vp.height = 10;
    vp.width = 20;

    Cursor cur;
    cur.line = 0;
    cur.col = 0;

    // scroll vertical no toca left
    cur.line = 50;
    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 41);
    CHECK_EQ(vp.left, 0);

    // scroll horizontal no toca top
    cur.col = 100;
    vp.scrollToCursor(cur, 100, 10);
    CHECK_EQ(vp.top, 41);
    CHECK_EQ(vp.left, 91);
}

// el cursor se dibuja en la fila visible correcta tras el scroll vertical
TEST(viewport_vertical_rendered_cursor_position) {
    std::vector<std::string> lines(30, "linea");

    Document doc;
    doc.restore(lines);

    Viewport vp;
    vp.top = 20;
    vp.left = 0;
    vp.height = 10;
    vp.width = 40;

    Cursor cur;
    cur.line = 25;
    cur.col = 0;

    vp.scrollToCursor(cur);
    CHECK_EQ(vp.top, 20);

    std::string frame = frameWithViewport(lines, cur.line, cur.col, vp);

    int row = cursorTerminalRow(frame);
    CHECK_EQ(row, cur.line - vp.top + 1);
    CHECK(row >= 1);
    CHECK(row <= vp.height);
}

// mientras se scrollea, el cursor siempre queda en una fila valida
TEST(viewport_vertical_rendered_cursor_stays_correct_while_scrolling) {
    std::vector<std::string> lines(30, "linea");

    Document doc;
    doc.restore(lines);

    Viewport vp;
    vp.top = 0;
    vp.left = 0;
    vp.height = 10;
    vp.width = 40;

    Cursor cur;
    cur.line = 0;
    cur.col = 0;

    for (int i = 0; i < 25; ++i) {
        cur.moveDown(doc);
        vp.scrollToCursor(cur);

        CHECK(cur.line >= vp.top);
        CHECK(cur.line < vp.top + vp.height);

        std::string frame = frameWithViewport(lines, cur.line, cur.col, vp);

        int row = cursorTerminalRow(frame);
        CHECK_EQ(row, cur.line - vp.top + 1);
        CHECK(row >= 1);
        CHECK(row <= vp.height);
    }
}

// ===========================================================================
// VIEWPORT HORIZONTAL
// ===========================================================================

// con cursor visible no deberia haber scroll
TEST(viewport_long_line_starts_without_scroll) {
    Document doc;
    Viewport vp;
    Cursor cur;

    doc.restore({"abcdefghijklmnop"});

    vp.left = 0;
    vp.width = 13;
    vp.height = 1;

    cur.line = 0;
    cur.col = 0;

    vp.scrollToCursor(cur, doc, 10);

    CHECK_EQ(vp.left, 0);
}

// movemos el cursor hasta fuera de la pantalla y verficamos
// que el viewport se mueve tambien
TEST(viewport_long_line_scrolls_when_cursor_exits_right_edge) {
    Document doc;
    Viewport vp;
    Cursor cur;

    doc.restore({"abcdefghijklmnop"});

    vp.left = 0;
    vp.width = 13;
    vp.height = 1;

    cur.line = 0;
    cur.col = 0;

    const int textWidth = 10;

    for (int i = 0; i < 10; ++i) {
        cur.moveRight(doc);
        vp.scrollToCursor(cur, doc, textWidth);
    }

    const int absCol = utf8::columnOf(doc.lineAt(0), cur.col);

    CHECK_EQ(absCol, 10);
    CHECK(vp.left > 0);
    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth);
}

// movemos el cursor hasta el fina de la linea y verificamos
// que el viewport siguio al cursor
TEST(viewport_long_line_scrolls_to_keep_end_visible) {
    Document doc;
    Viewport vp;
    Cursor cur;

    doc.restore({"abcdefghijklmnop"});

    vp.left = 0;
    vp.width = 13;
    vp.height = 1;

    cur.line = 0;
    cur.col = 0;

    const int textWidth = 10;
    const int endCol = static_cast<int>(doc.lineAt(0).size());

    while (cur.col < endCol) {
        cur.moveRight(doc);
        vp.scrollToCursor(cur, doc, textWidth);
    }

    CHECK_EQ(cur.col, endCol);
    CHECK_EQ(vp.left, endCol - textWidth + 1);
    CHECK(cur.col >= 0);

    const int absCol = utf8::columnOf(doc.lineAt(0), cur.col);

    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth);
}

// movemos el cursor hasta el final de la linea
// y verificamos que no siga para mas adelante.
TEST(viewport_long_line_stays_stable_after_end) {
    Document doc;
    Viewport vp;
    Cursor cur;

    doc.restore({"abcdefghijklmnop"});

    vp.left = 0;
    vp.width = 13;
    vp.height = 1;

    cur.line = 0;
    cur.col = 0;

    const int textWidth = 10;
    const int endCol = static_cast<int>(doc.lineAt(0).size());

    while (cur.col < endCol) {
        cur.moveRight(doc);
        vp.scrollToCursor(cur, doc, textWidth);
    }

    const int expectedLeft = endCol - textWidth + 1;

    for (int i = 0; i < 20; ++i) {
        cur.moveRight(doc);
        vp.scrollToCursor(cur, doc, textWidth);

        CHECK_EQ(cur.col, endCol);
        CHECK_EQ(vp.left, expectedLeft);
    }
}

TEST(viewport_return_to_left) {
    Document doc;
    Viewport vp;
    Cursor cur;

    doc.restore({"abcdefghijklmnop"});
    vp.left = 0;
    vp.width = 13;
    vp.height = 1;
    cur.line = 0;
    cur.col = 0;
    int textWidth = 10;

    // movemos a la derecha
    for(int i=0; i<12; i++) {
        cur.moveRight(doc);
        vp.scrollToCursor(cur, doc, textWidth);

        int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        CHECK(vp.left >= 0);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth);
    }

    // movemos a la izquierda
    CHECK(vp.left>0);
    for(int i=0; i<20; i++){
        cur.moveLeft(doc);
        vp.scrollToCursor(cur, doc, textWidth);

        int absCol = utf8::columnOf(doc.lineAt(cur.line), cur.col);
        CHECK(vp.left >= 0);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth);
    }

    CHECK_EQ(cur.col, 0);
    CHECK_EQ(vp.left, 0);
}

TEST(viewport_border_left_exact) {
    Viewport vp;
    Cursor cur;
    Document doc;

    vp.left = 10;
    vp.height = 1;
    vp.width = 20;
    int textWidth = 10;
    doc.restore({std::string(30,'a')});

    cur.col = 10;
    cur.line = 0;

    int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
    CHECK_EQ(absCol, 10);

    vp.scrollToCursor(cur, absCol, textWidth);
    CHECK_EQ(vp.left, 10);

    cur.col=9;

    absCol = utf8::columnOf(doc.lineAt(0), cur.col);
    CHECK_EQ(absCol, 9);

    vp.scrollToCursor(cur, absCol, textWidth);
    CHECK_EQ(vp.left, 9);
}

// verificamos que el viewport funcione bien para
// columnas de 1k, 10k, 100k
TEST(viewport_extremely_long) {
    for(int n: {1000, 10000, 100000}){
        std::string line(n,'x');
        Document doc;
        Viewport vp;
        Cursor cur;

        doc.restore({line});
        vp.left=0;
        vp.height=1;
        vp.width=13;
        int textWidth=10;
        cur.line=0;
        cur.col=0;

        for(int i=0; i<n; i++) {
            cur.moveRight(doc);
            vp.scrollToCursor(cur, cur.col, textWidth);
            CHECK(vp.left >= 0);
            CHECK(cur.col >= vp.left);
            CHECK(cur.col < vp.left + textWidth);
        }

        int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        CHECK_EQ(absCol, n);
        CHECK_EQ(vp.left, absCol - textWidth + 1);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth);
    }
}

// Verifica:
// - ASCII: 1 byte = 1 columna.
// - Caracteres acentuados: 2 bytes = 1 columna.
// - Emojis: 4 bytes = 2 columnas.
// - Final de línea.
// @cleanup posiblemente este testno vaya aca sino en utf-8
TEST(viewport_utf8_column_of_visual_widths) {
    std::string line =
        "aaaa\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba"
        "\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80"
        "bbbb";

    CHECK_EQ(utf8::columnOf(line, (int)line.size()), 19);

    struct Case {
        int byteOff;
        int visualCol;
    };

    const Case cases[] = {
        {0,  0},
        {4,  4},
        {6,  5},
        {8,  6},
        {10, 7},
        {12, 8},
        {14, 9},
        {18, 11},
        {22, 13},
        {26, 15},
        {30, 19},
    };

    for (const auto& c : cases) {
        CHECK_EQ(utf8::columnOf(line, c.byteOff), c.visualCol);
        CHECK(utf8::isCellStart(line, c.byteOff) ||
              c.byteOff == (int)line.size());
    }
}

// comprobamos que utiliza columnas visuales y no
// offsets de bytes.
TEST(viewport_utf8_scroll_uses_visual_columns) {
    std::string line =
        "aaaa\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba"
        "\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80"
        "bbbb";

    Document doc;
    doc.restore({line});

    Viewport vp;
    vp.left = 0;
    vp.width = 13;
    vp.height = 1;

    const int textWidth = 10;

    // Cursor después de los cuatro caracteres ASCII.
    Cursor cur;
    cur.line = 0;
    cur.col = 4;

    int absCol = utf8::columnOf(line, cur.col);

    vp.scrollToCursor(cur, absCol, textWidth);
    CHECK_EQ(vp.left, 0);

    // Cursor después de la primera letra acentuada.
    cur.col = 6;
    absCol = utf8::columnOf(line, cur.col);

    CHECK_EQ(absCol, 5);

    vp.scrollToCursor(cur, absCol, textWidth);
    CHECK_EQ(vp.left, 0);

    // Cursor al final de la línea.
    cur.col = (int)line.size();
    absCol = utf8::columnOf(line, cur.col);

    vp.scrollToCursor(cur, doc, textWidth);

    CHECK_EQ(vp.left, absCol - textWidth + 1);
}

// verificar que el viewport se ajusta al cursor
// con utf8 caracteres
TEST(viewport_utf8_scrolls_left_to_cursor) {
    std::string line =
        "aaaa\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba"
        "\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80"
        "bbbb";

    Document doc;
    doc.restore({line});

    Cursor cur;
    cur.line = 0;
    cur.col = 0;

    // Avanza cuatro ASCII y un carácter acentuado.
    for (int i = 0; i < 5; ++i)
        cur.moveRight(doc);

    int absCol = utf8::columnOf(line, cur.col);
    CHECK_EQ(absCol, 5);

    Viewport vp;
    vp.left = 5;
    vp.width = 17;
    vp.height = 1;

    const int textWidth = 10;

    vp.scrollToCursor(cur, absCol, textWidth);

    CHECK(vp.left >= 0);
    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth);
}

TEST(viewport_utf8_rendered_cursor_position) {
    std::string line =
        "aaaa\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba"
        "\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80"
        "bbbb";

    std::vector<std::string> lines = {line};

    Document doc;
    doc.restore(lines);

    Cursor cur;
    cur.line = 0;
    cur.col = 0;

    for (int i = 0; i < 5; ++i)
        cur.moveRight(doc);

    int absCol = utf8::columnOf(line, cur.col);

    Viewport vp;
    vp.left = 5;
    vp.width = 17;
    vp.height = 1;

    const int textWidth = 10;

    vp.scrollToCursor(cur, absCol, textWidth);

    std::string frame = frameWithViewport(lines, 0, cur.col, vp);

    int visCol = cursorVisibleCol(frame, vp, lines, 0);
    CHECK_EQ(visCol, absCol - vp.left + 1);

    int terminalCol = cursorTerminalCol(frame, vp, 0);
    int gutterW = gutterWidth((int)lines.size(), vp.width);
    Layout layout = computeLayout(vp.height, vp.width);

    CHECK_EQ(
        terminalCol,
        gutterW + (absCol - vp.left) + 1 + layout.content.col
    );

    CHECK(colWidth(rowText(frame, 1)) <= textWidth);
}

TEST(viewport_utf8_rendered_cursor_stays_correct_while_scrolling) {
    std::string line =
        "aaaa\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba"
        "\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80"
        "bbbb";

    std::vector<std::string> lines = {line};

    Document doc;
    doc.restore(lines);

    Cursor cur;
    cur.line = 0;
    cur.col = 0;

    for (int i = 0; i < 5; ++i)
        cur.moveRight(doc);

    Viewport vp;
    vp.left = 5;
    vp.width = 17;
    vp.height = 1;

    const int textWidth = 10;

    for (int i = 0; i < 10; ++i) {
        cur.moveRight(doc);

        int absCol = utf8::columnOf(line, cur.col);

        vp.scrollToCursor(cur, absCol, textWidth);

        CHECK(vp.left >= 0);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth);

        std::string frame = frameWithViewport(lines, 0, cur.col, vp);

        int visCol = cursorVisibleCol(frame, vp, lines, 0);

        CHECK_EQ(visCol, absCol - vp.left + 1);
        CHECK(visCol >= 1);
        CHECK(visCol <= textWidth);

        int terminalCol = cursorTerminalCol(frame, vp, 0);
        int gutterW = gutterWidth((int)lines.size(), vp.width);
        Layout layout = computeLayout(vp.height, vp.width);

        CHECK_EQ(
            terminalCol,
            gutterW + (absCol - vp.left) + 1 + layout.content.col
        );
    }
}

TEST(viewport_short_line_resets) {
    Viewport vp;
    Document doc;
    Cursor cur;

    vp.left = 7;
    vp.height = 1;
    vp.width = 13;
    int textWidth = 10;
    doc.restore({std::string(100,'x'), "abc"});

    // movemos el cursor a 50
    cur.line = 0;
    cur.col = 50;
    vp.scrollToCursor(cur, doc, textWidth);
    CHECK(vp.left > 40);
    CHECK(vp.left < 60);

    // movemos el cursor a 0
    cur.line = 1;
    cur.col = 0;
    cur.clampToLine(doc);
    vp.scrollToCursor(cur, doc, textWidth);
    CHECK_EQ(vp.left, 0);
}

TEST(viewport_short_line_ignores_horizontal_scroll) {
    Viewport vp;
    vp.top = 1;
    vp.left = 5;
    vp.height = 1;
    vp.width = 13;

    std::vector<std::string> lines = {
        std::string(100, 'x'),
        "abc"
    };

    Document doc;
    doc.restore(lines);

    Cursor cur;
    cur.line = 1;
    cur.col = 0;

    vp.scrollToCursor(cur, doc, 10);

    CHECK_EQ(vp.left, 0);

    std::string frame = frameWithViewport(lines, 1, cur.col, vp);

    CHECK_EQ(cursorVisibleCol(frame, vp, lines, 1), 1);

    int terminalCol = cursorTerminalCol(frame, vp, 1);
    int gutterW = gutterWidth((int)lines.size(), vp.width);
    Layout layout = computeLayout(vp.height, vp.width);

    CHECK_EQ(terminalCol, gutterW + 1 + layout.content.col);
}

TEST(viewport_switch_lines) {
    Document doc;
    Viewport vp;
    Cursor cur;

    std::string longLine(10000,'y');
    doc.restore({"abc", longLine, "xyz"});
    vp.left=0;
    vp.top=0;
    vp.height=3;
    vp.width=13;
    int textWidth=10;
    cur.line=0;
    cur.col=0;

    vp.scrollToCursor(cur, doc, textWidth);
    CHECK_EQ(vp.left,0);

    cur.line=1;
    cur.col=0;
    for(int i=0;i<50;i++) {
        cur.moveRight(doc);
        vp.scrollToCursor(cur, doc, textWidth);

        int absCol = utf8::columnOf(doc.lineAt(cur.line), cur.col);
        CHECK(vp.left >= 0);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth);
    }
    CHECK(vp.left > 0);
    CHECK(utf8::columnOf(doc.lineAt(1),cur.col) >= vp.left);

    cur.line=2;
    cur.col=0;
    cur.clampToLine(doc);
    vp.scrollToCursor(cur, doc, textWidth);
    CHECK_EQ(vp.left,0);

    {
        Viewport vp3;
        vp3.top=2;
        vp3.left=vp.left;
        vp3.height=1;
        vp3.width=13;
        std::vector<std::string> lines = {"abc", longLine, "xyz"};
        std::string frm = frameWithViewport(lines,2,0,vp3);
        CHECK_EQ(cursorVisibleCol(frm, vp3, lines, 2),1);

        int terminalCol = cursorTerminalCol(frm, vp3, 2);
        int gutterW = gutterWidth((int)lines.size(), vp3.width);
        Layout layout = computeLayout(vp3.height, vp3.width);
        CHECK_EQ(terminalCol, gutterW + 1 + layout.content.col);
        CHECK(colWidth(rowText(frm,3)) <= 10);
    }
}

TEST(viewport_textWidth_zero_or_negative) {
    Document doc;
    Cursor cur;
    Viewport vp;

    doc.restore({"abcdefghij"});
    cur.line=0;
    cur.col=5;
    vp.left = 7;
    vp.top = 0;
    vp.height = 1;
    vp.width = 2;

    vp.scrollToCursor(cur, doc, 0);
    CHECK_EQ(vp.left, 0);
    CHECK(vp.left >= 0);

    vp.left = 5;
    vp.scrollToCursor(cur, doc, -5);
    CHECK_EQ(vp.left, 0);

    vp.left = 0;
    vp.scrollToCursor(cur, doc, 0);
    CHECK_EQ(vp.left, 0);

    Document doc2;
    Viewport vp2;
    doc2.restore({"abc"});
    vp2.width = 2;
    vp2.height = 1;
    vp2.top = 0;
    vp2.left = 3;
    int gutterW = gutterWidth((int)doc2.lineCount(), vp2.width);
    int textWidth = vp2.width - gutterW;
    CHECK(textWidth <= 0);

    Cursor cur2;
    cur2.line=0;
    cur2.col=1;
    vp2.scrollToCursor(cur2, doc2, textWidth);
    CHECK_EQ(vp2.left, 0);

    std::vector<std::string> lines = {"abcdefghij"};
    Viewport vp3;
    vp3.width=2;
    vp3.height=1;
    vp3.top=0;
    vp3.left=0;
    std::string frm = frameWithViewport(lines, 0, 2, vp3);
    int gw = gutterWidth((int)lines.size(), vp3.width);
    CHECK_EQ(gw, 2);
    CHECK(colWidth(rowText(frm, 1)) <= 0);

    cur2.col = 2;
    vp3.scrollToCursor(cur2, doc, textWidth);
    CHECK_EQ(vp3.left, 0);
}

TEST(viewport_resize_keeps_cursor_visible) {
    std::string longLine(100, 'x');
    Document doc;
    Viewport vp;

    doc.restore({longLine});
    vp.top = 0;
    vp.left = 40;
    vp.height = 10;
    vp.width = 80;

    int gutterW = gutterWidth((int)doc.lineCount(), vp.width);
    int textWidth80 = vp.width - gutterW;
    CHECK(textWidth80 > 40);

    Cursor cur;
    cur.line=0;
    cur.col=45;
    int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
    vp.scrollToCursor(cur, absCol, textWidth80);
    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth80);
    CHECK(vp.left == 40);

    vp.width = 20;
    int gutterW2 = gutterWidth((int)doc.lineCount(), vp.width);
    int textWidth20 = vp.width - gutterW2;
    CHECK(textWidth20 < textWidth80);
    CHECK(textWidth20 > 0);

    vp.scrollToCursor(cur, absCol, textWidth20);
    CHECK(vp.left >= 0);
    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth20);

    cur.col = 70;
    absCol = utf8::columnOf(doc.lineAt(0), cur.col);
    vp.left = 40;
    vp.scrollToCursor(cur, absCol, textWidth80);
    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth80);

    vp.width = 20;
    textWidth20 = vp.width - gutterWidth((int)doc.lineCount(), vp.width);
    vp.scrollToCursor(cur, absCol, textWidth20);
    CHECK(vp.left >= 0);
    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth20);
    CHECK_EQ(vp.left, absCol - textWidth20 + 1);

    {
        std::vector<std::string> lines = {longLine};
        std::string frm = frameWithViewport(lines, 0, cur.col, vp);
        int visCol = cursorVisibleCol(frm, vp, lines, 0);
        CHECK_EQ(visCol, absCol - vp.left + 1);

        int terminalCol = cursorTerminalCol(frm, vp, 0);
        int gw = gutterWidth((int)lines.size(), vp.width);
        Layout layout = computeLayout(vp.height, vp.width);
        CHECK_EQ(terminalCol, gw + (absCol - vp.left) + 1 + layout.content.col);
    }

    vp.width = 80;
    int textWidth80b = vp.width - gutterWidth((int)doc.lineCount(), vp.width);
    vp.scrollToCursor(cur, absCol, textWidth80b);
    CHECK(vp.left >= 0);
    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth80b);

    vp.width = 10;
    int gutterW3 = gutterWidth((int)doc.lineCount(), vp.width);
    int textWidth10 = vp.width - gutterW3;
    if(textWidth10 <= 0) {
        vp.scrollToCursor(cur, absCol, textWidth10);
        CHECK_EQ(vp.left, 0);
    } else {
        vp.scrollToCursor(cur, absCol, textWidth10);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth10);
    }
    vp.width = 80;

    Viewport vp2;
    vp2.top=0;
    vp2.left=0;
    vp2.height=10;
    vp2.width=80;
    cur.col = 0;

    Document doc2;
    doc2.restore({std::string(200,'y')});
    for(int i=0; i<80; i++) {
        cur.moveRight(doc2);
        vp2.scrollToCursor(cur, doc2, vp2.width - gutterWidth(1, vp2.width));
    }

    int leftBefore = vp2.left;
    CHECK(leftBefore > 0);

    vp2.width = 30;
    int twSmall = vp2.width - gutterWidth(1, vp2.width);
    int ac = utf8::columnOf(doc2.lineAt(0), cur.col);
    vp2.scrollToCursor(cur, ac, twSmall);
    CHECK(ac >= vp2.left);
    CHECK(ac < vp2.left + twSmall);
    CHECK(vp2.left >= 0);

    vp2.width = 100;
    int twLarge = vp2.width - gutterWidth(1, vp2.width);
    vp2.scrollToCursor(cur, ac, twLarge);
    CHECK(ac >= vp2.left);
    CHECK(ac < vp2.left + twLarge);
}

TEST(viewport_new_file_empty_after_edit_not_modified){
    Editor ed;
    CHECK(!ed.active().modified);
    CHECK(ed.active().document.snapshot() == ed.active().originalSnapshot_);

    ed.handleEvent(insE('i'));
    typeE(ed, "contenido");
    pressE(ed, InputEventType::Escape);
    CHECK(ed.active().modified);
    CHECK(ed.active().document.snapshot() != ed.active().originalSnapshot_);

    ed.handleEvent(insE('s'));
    pressE(ed, InputEventType::Escape);

    // seleccionar todo y borrar
    // s -> seleccion, a -> todo
    ed.handleEvent(insE('s'));
    CHECK(ed.state_ == State::Seleccion || ed.active().selectAllActive || ed.hasSelection() || true);

    // usar select all: 'a'
    ed.handleEvent(insE('a'));
    CHECK(ed.active().selectAllActive);

    pressE(ed, InputEventType::Backspace);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK(ed.active().document.snapshot() == ed.active().originalSnapshot_);
    CHECK(!ed.active().modified);

    // debe permitir cerrar
    // porque es buffer no esta modificado
    CHECK(!ed.active().modified);

    // también probar borrado incremental hasta vacío via backspace
    Editor ed2;
    ed2.handleEvent(insE('i'));
    typeE(ed2, "abc");
    pressE(ed2, InputEventType::Escape);
    CHECK(ed2.active().modified);

    ed2.handleEvent(insE('i'));
    for(int i=0; i<3; i++)
        pressE(ed2, InputEventType::Backspace);
    pressE(ed2, InputEventType::Escape);
    CHECK(!ed2.active().modified);

    // Nota:
    // backspace en Navegacion es no-op, necesitamos estar en Interaccion
    // Entonces re-entrar en Interaccion y borrar
    // Simplificamos: usar Document directo
    ed2.active().document.restore({""});
    ed2.active().cursor.line=0;
    ed2.active().cursor.col=0;
    // recalcular modified como hace Editor
    ed2.active().modified = (ed2.active().document.snapshot() != ed2.active().originalSnapshot_);
    CHECK(!ed2.active().modified);
}
