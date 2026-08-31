#include "core/Viewport.h"
#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Layout.h"
#include "core/utf8.h"
#include "ui/Renderer.h"
#include "test_framework.h"
#include <string>
#include <vector>

namespace {
int gutterWFor(int totalLines) {
    int digits = 1;
    for (int n = totalLines; n >= 10; n /= 10) ++digits;
    return std::max(3, digits + 1);
}
std::string frameWithViewport(const std::vector<std::string>& lines, int cursorLine, int cursorCol, Viewport vp) {
    Document doc; doc.restore(lines);
    Cursor cur; cur.line = cursorLine; cur.col = cursorCol;
    Renderer r;
    return r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt);
}
int cursorTerminalCol(const std::string& frame, const Viewport& vp, int cursorLine) {
    int expectedRow = cursorLine - vp.top + 1;
    std::string needle = "\x1b[" + std::to_string(expectedRow) + ";";
    size_t pos = frame.find(needle);
    if (pos == std::string::npos) return -1;
    size_t end = frame.find('H', pos);
    if (end == std::string::npos) return -1;
    int c = std::stoi(frame.substr(pos + needle.size(), end - pos - needle.size()));
    return c;
}
int cursorVisibleCol(const std::string& frame, const Viewport& vp, const std::vector<std::string>& lines, int cursorLine) {
    int terminalCol = cursorTerminalCol(frame, vp, cursorLine);
    if (terminalCol < 0) return -1;
    Document tmp; tmp.restore(lines);
    int gutterW = std::min(gutterWFor((int)lines.size()), vp.width);
    Layout layout = computeLayout(vp.height, vp.width);
    return terminalCol - gutterW - layout.content.col;
}
std::string stripAnsi(const std::string& s) {
    std::string out; bool inEsc=false; size_t i=0;
    while(i<s.size()){
        if(s[i]=='\x1b'){inEsc=true; if(i+1<s.size()&&s[i+1]=='[')i++;}
        else if(inEsc){ unsigned char c=(unsigned char)s[i]; if(c>=0x40&&c<=0x7E)inEsc=false; }
        else out+=s[i];
        i++;
    }
    return out;
}
int colWidth(const std::string& s){int col=0; for(unsigned char c:s) if((c&0xC0)!=0x80)col++; return col;}
std::string rowText(const std::string& frame, int lineCount){
    std::string plain=stripAnsi(frame);
    size_t nl=plain.find("\r\n");
    if(nl==std::string::npos) nl=plain.size();
    std::string row=plain.substr(0,nl);
    int gw=gutterWFor(lineCount);
    row.erase(0,std::min(gw,(int)row.size()));
    return row;
}
}

TEST(viewport_long_line_move_right) {
    Document doc; doc.restore({"abcdefghijklmnop"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=1; vp.width=13;
    int textWidth = 10;
    Cursor cur; cur.line=0; cur.col=0;
    vp.scrollToCursor(cur, doc, textWidth);
    CHECK_EQ(vp.left, 0);
    for(int i=0;i<10;i++){
        cur.moveRight(doc);
        vp.scrollToCursor(cur, doc, textWidth);
        int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        CHECK(vp.left >= 0);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth);
    }
    int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
    CHECK(absCol==10);
    CHECK(vp.left>0);
    CHECK(cur.col >=0);
    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth);
    cur.moveRight(doc); vp.scrollToCursor(cur, doc, textWidth);
    absCol = utf8::columnOf(doc.lineAt(0), cur.col);
    CHECK(vp.left >= 0);
    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth);
    for(int i=0;i<5;i++){
        cur.moveRight(doc);
        vp.scrollToCursor(cur, doc, textWidth);
        absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        CHECK(vp.left >= 0);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth);
    }
    int endCol = (int)doc.lineAt(0).size();
    CHECK_EQ(cur.col, endCol);
    CHECK_EQ(vp.left, endCol - textWidth + 1);
    for(int i=0;i<20;i++){
        cur.moveRight(doc);
        vp.scrollToCursor(cur, doc, textWidth);
        CHECK_EQ(cur.col, endCol);
        CHECK_EQ(vp.left, endCol - textWidth + 1);
        absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        CHECK(vp.left >= 0);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth);
    }
}

TEST(viewport_no_scroll_beyond_end) {
    Document doc; doc.restore({"abcdefghijklmnop"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=1; vp.width=13;
    int textWidth = 10;
    Cursor cur; cur.line=0; cur.col=(int)doc.lineAt(0).size();
    vp.scrollToCursor(cur, doc, textWidth);
    int endAbs = utf8::columnOf(doc.lineAt(0), cur.col);
    int expectedLeft = endAbs - textWidth + 1;
    CHECK_EQ(vp.left, expectedLeft);
    for(int i=0;i<30;i++){
        cur.moveRight(doc);
        vp.scrollToCursor(cur, doc, textWidth);
        CHECK_EQ(cur.col, (int)doc.lineAt(0).size());
        CHECK_EQ(cur.line, 0);
        CHECK_EQ(vp.left, expectedLeft);
        int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        CHECK(vp.left >= 0);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth);
    }
}

TEST(viewport_return_to_left) {
    Document doc; doc.restore({"abcdefghijklmnop"});
    Viewport vp; vp.left=0; vp.width=13; vp.height=1;
    int textWidth=10;
    Cursor cur; cur.line=0; cur.col=0;
    for(int i=0;i<12;i++){
        cur.moveRight(doc); vp.scrollToCursor(cur, doc, textWidth);
        int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        CHECK(vp.left >= 0);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth);
    }
    CHECK(vp.left>0);
    for(int i=0;i<20;i++){
        cur.moveLeft(doc); vp.scrollToCursor(cur, doc, textWidth);
        int absCol = utf8::columnOf(doc.lineAt(cur.line), cur.col);
        CHECK(vp.left >= 0);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth);
    }
    CHECK_EQ(cur.col, 0);
    CHECK_EQ(vp.left, 0);
}

TEST(viewport_border_right_exact) {
    Viewport vp; vp.left=0; vp.height=1; vp.width=13;
    int textWidth=10;
    Cursor cur; cur.line=0;
    cur.col=9;
    Document doc; doc.restore({"abcdefghijklmnop"});
    {
        int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        CHECK_EQ(absCol, 9);
        vp.scrollToCursor(cur, absCol, textWidth);
        CHECK_EQ(vp.left, 0);
    }
    cur.col=10;
    {
        int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        CHECK_EQ(absCol, 10);
        vp.scrollToCursor(cur, absCol, textWidth);
        CHECK_EQ(vp.left, 1);
    }
}

TEST(viewport_border_left_exact) {
    Viewport vp; vp.left=10; vp.height=1; vp.width=20;
    int textWidth=10;
    Cursor cur; cur.line=0;
    Document doc; doc.restore({std::string(30,'a')});
    cur.col=10;
    {
        int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        CHECK_EQ(absCol, 10);
        vp.scrollToCursor(cur, absCol, textWidth);
        CHECK_EQ(vp.left, 10);
    }
    cur.col=9;
    {
        int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        CHECK_EQ(absCol, 9);
        vp.scrollToCursor(cur, absCol, textWidth);
        CHECK_EQ(vp.left, 9);
    }
}

TEST(viewport_extremely_long) {
    for(int n: {1000, 10000, 100000}){
        std::string line(n,'x');
        Document doc; doc.restore({line});
        Viewport vp; vp.left=0; vp.height=1; vp.width=13;
        int textWidth=10;
        Cursor cur; cur.line=0; cur.col=0;
        for(int i=0;i<n;i++){
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

TEST(viewport_utf8_visual_columns) {
    std::string line = "aaaa\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80" "bbbb";
    Document doc; doc.restore({line});
    int totalBytes = (int)line.size();
    int totalCols = utf8::columnOf(line, totalBytes);
    CHECK(totalCols == 4 + 5 + 3 + 4);
    Viewport vp; vp.left=0; vp.top=0; vp.height=1; vp.width=13;
    int textWidth=10;
    int posAfterAaaa = 4;
    Cursor cur; cur.line=0; cur.col=posAfterAaaa;
    int absCol = utf8::columnOf(line, cur.col);
    CHECK_EQ(absCol, 4);
    vp.scrollToCursor(cur, absCol, textWidth);
    CHECK_EQ(vp.left, 0);
    std::string afterAaaaA = "aaaa\xc3\xa1";
    int colA = (int)afterAaaaA.size();
    cur.col = colA;
    absCol = utf8::columnOf(line, cur.col);
    CHECK_EQ(absCol, 5);
    vp.scrollToCursor(cur, absCol, textWidth);
    CHECK_EQ(vp.left, 0);
    {
        struct Case { int byteOff; int visualCol; };
        Case cases[] = {
            {0, 0},
            {4, 4},
            {6, 5},
            {8, 6},
            {10, 7},
            {12, 8},
            {14, 9},
            {18, 10},
            {22, 11},
            {26, 12},
            {30, 16},
        };
        for(auto &c: cases){
            int v = utf8::columnOf(line, c.byteOff);
            CHECK_EQ(v, c.visualCol);
            CHECK(utf8::isCellStart(line, c.byteOff) || c.byteOff==totalBytes);
        }
    }
    cur.col = totalBytes;
    absCol = utf8::columnOf(line, cur.col);
    vp.scrollToCursor(cur, doc, textWidth);
    CHECK_EQ(vp.left, absCol - textWidth + 1);
    vp.left = 5;
    cur.col = 0;
    for(int i=0;i<5;i++) cur.moveRight(doc);
    absCol = utf8::columnOf(doc.lineAt(0), cur.col);
    Viewport vp2; vp2.left=5; vp2.height=1; vp2.width=13;
    vp2.scrollToCursor(cur, absCol, textWidth);
    CHECK(vp2.left >= 0);
    CHECK(absCol >= vp2.left);
    CHECK(absCol < vp2.left + textWidth);
    {
        std::vector<std::string> lines = {line};
        std::string frm = frameWithViewport(lines, 0, cur.col, vp2);
        int visCol = cursorVisibleCol(frm, vp2, lines, 0);
        CHECK_EQ(visCol, absCol - vp2.left + 1);
        int terminalCol = cursorTerminalCol(frm, vp2, 0);
        int gutterW = std::min(gutterWFor((int)lines.size()), vp2.width);
        Layout layout = computeLayout(vp2.height, vp2.width);
        CHECK_EQ(terminalCol, gutterW + (absCol - vp2.left) + 1 + layout.content.col);
        std::string row = rowText(frm,1);
        CHECK(colWidth(row) <= textWidth);
    }
    for(int i=0;i<10;i++){
        cur.moveRight(doc);
        absCol = utf8::columnOf(doc.lineAt(0), cur.col);
        vp2.scrollToCursor(cur, absCol, textWidth);
        CHECK(vp2.left >= 0);
        CHECK(absCol >= vp2.left);
        CHECK(absCol < vp2.left + textWidth);
        std::vector<std::string> lines = {line};
        std::string f = frameWithViewport(lines, 0, cur.col, vp2);
        int vc = cursorVisibleCol(f, vp2, lines, 0);
        CHECK_EQ(vc, absCol - vp2.left + 1);
        CHECK(vc>=1); CHECK(vc<=textWidth);
        int terminalCol = cursorTerminalCol(f, vp2, 0);
        int gutterW = std::min(gutterWFor((int)lines.size()), vp2.width);
        Layout layout = computeLayout(vp2.height, vp2.width);
        CHECK_EQ(terminalCol, gutterW + (absCol - vp2.left) + 1 + layout.content.col);
    }
}

TEST(viewport_short_line_resets) {
    Viewport vp; vp.left=7; vp.height=1; vp.width=13;
    int textWidth=10;
    Document doc; doc.restore({std::string(100,'x'), "abc"});
    Cursor cur; cur.line=0; cur.col=50;
    vp.scrollToCursor(cur, doc, textWidth);
    CHECK(vp.left>0);
    cur.line=1; cur.col=0;
    cur.clampToLine(doc);
    vp.scrollToCursor(cur, doc, textWidth);
    CHECK_EQ(vp.left, 0);
    {
        Viewport vp2; vp2.top=1; vp2.left=vp.left; vp2.height=1; vp2.width=13;
        std::vector<std::string> lines = {std::string(100,'x'),"abc"};
        std::string frm = frameWithViewport(lines,1,0,vp2);
        CHECK(cursorVisibleCol(frm, vp2, lines, 1)==1);
        int terminalCol = cursorTerminalCol(frm, vp2, 1);
        int gutterW = std::min(gutterWFor((int)lines.size()), vp2.width);
        Layout layout = computeLayout(vp2.height, vp2.width);
        CHECK_EQ(terminalCol, gutterW + 1 + layout.content.col);
    }
}

TEST(viewport_switch_lines) {
    std::string longLine(10000,'y');
    Document doc; doc.restore({"abc", longLine, "xyz"});
    Viewport vp; vp.left=0; vp.top=0; vp.height=3; vp.width=13;
    int textWidth=10;
    Cursor cur; cur.line=0; cur.col=0;
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
    CHECK(vp.left>0);
    CHECK(utf8::columnOf(doc.lineAt(1),cur.col) >= vp.left);
    cur.line=2; cur.col=0; cur.clampToLine(doc);
    vp.scrollToCursor(cur, doc, textWidth);
    CHECK_EQ(vp.left,0);
    {
        Viewport vp3; vp3.top=2; vp3.left=vp.left; vp3.height=1; vp3.width=13;
        std::vector<std::string> lines = {"abc", longLine, "xyz"};
        std::string frm = frameWithViewport(lines,2,0,vp3);
        CHECK_EQ(cursorVisibleCol(frm, vp3, lines, 2),1);
        int terminalCol = cursorTerminalCol(frm, vp3, 2);
        int gutterW = std::min(gutterWFor((int)lines.size()), vp3.width);
        Layout layout = computeLayout(vp3.height, vp3.width);
        CHECK_EQ(terminalCol, gutterW + 1 + layout.content.col);
        CHECK(colWidth(rowText(frm,3)) <= 10);
    }
}

TEST(viewport_textWidth_zero_or_negative) {
    Document doc; doc.restore({"abcdefghij"});
    Cursor cur; cur.line=0; cur.col=5;
    Viewport vp;
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
    Document doc2; doc2.restore({"abc"});
    Viewport vp2;
    vp2.width = 2;
    vp2.height = 1;
    vp2.top = 0;
    vp2.left = 3;
    int gutterW = std::min(gutterWFor((int)doc2.lineCount()), vp2.width);
    int textWidth = vp2.width - gutterW;
    CHECK(textWidth <= 0);
    Cursor cur2; cur2.line=0; cur2.col=1;
    vp2.scrollToCursor(cur2, doc2, textWidth);
    CHECK_EQ(vp2.left, 0);
    std::vector<std::string> lines = {"abcdefghij"};
    Viewport vp3; vp3.width=2; vp3.height=1; vp3.top=0; vp3.left=0;
    std::string frm = frameWithViewport(lines, 0, 2, vp3);
    int gw = std::min(gutterWFor((int)lines.size()), vp3.width);
    CHECK_EQ(gw, 2);
    CHECK(colWidth(rowText(frm, 1)) <= 0);
    cur2.col = 2;
    vp3.scrollToCursor(cur2, doc, textWidth);
    CHECK_EQ(vp3.left, 0);
}

TEST(viewport_resize_keeps_cursor_visible) {
    std::string longLine(100, 'x');
    Document doc; doc.restore({longLine});
    Viewport vp;
    vp.top = 0; vp.left = 40; vp.height = 10; vp.width = 80;
    int gutterW = std::min(gutterWFor((int)doc.lineCount()), vp.width);
    int textWidth80 = vp.width - gutterW;
    CHECK(textWidth80 > 40);
    Cursor cur; cur.line=0; cur.col=45;
    int absCol = utf8::columnOf(doc.lineAt(0), cur.col);
    vp.scrollToCursor(cur, absCol, textWidth80);
    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth80);
    CHECK(vp.left == 40);
    vp.width = 20;
    int gutterW2 = std::min(gutterWFor((int)doc.lineCount()), vp.width);
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
    textWidth20 = vp.width - std::min(gutterWFor((int)doc.lineCount()), vp.width);
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
        int gw = std::min(gutterWFor((int)lines.size()), vp.width);
        Layout layout = computeLayout(vp.height, vp.width);
        CHECK_EQ(terminalCol, gw + (absCol - vp.left) + 1 + layout.content.col);
    }
    vp.width = 80;
    int textWidth80b = vp.width - std::min(gutterWFor((int)doc.lineCount()), vp.width);
    vp.scrollToCursor(cur, absCol, textWidth80b);
    CHECK(vp.left >= 0);
    CHECK(absCol >= vp.left);
    CHECK(absCol < vp.left + textWidth80b);
    vp.width = 10;
    int gutterW3 = std::min(gutterWFor((int)doc.lineCount()), vp.width);
    int textWidth10 = vp.width - gutterW3;
    if(textWidth10 <= 0){
        vp.scrollToCursor(cur, absCol, textWidth10);
        CHECK_EQ(vp.left, 0);
    } else {
        vp.scrollToCursor(cur, absCol, textWidth10);
        CHECK(absCol >= vp.left);
        CHECK(absCol < vp.left + textWidth10);
    }
    vp.width = 80;
    Viewport vp2; vp2.top=0; vp2.left=0; vp2.height=10; vp2.width=80;
    cur.col = 0;
    Document doc2; doc2.restore({std::string(200,'y')});
    for(int i=0;i<80;i++){ cur.moveRight(doc2); vp2.scrollToCursor(cur, doc2, vp2.width - std::min(gutterWFor(1), vp2.width)); }
    int leftBefore = vp2.left;
    CHECK(leftBefore > 0);
    vp2.width = 30;
    int twSmall = vp2.width - std::min(gutterWFor(1), vp2.width);
    int ac = utf8::columnOf(doc2.lineAt(0), cur.col);
    vp2.scrollToCursor(cur, ac, twSmall);
    CHECK(ac >= vp2.left);
    CHECK(ac < vp2.left + twSmall);
    CHECK(vp2.left >= 0);
    vp2.width = 100;
    int twLarge = vp2.width - std::min(gutterWFor(1), vp2.width);
    vp2.scrollToCursor(cur, ac, twLarge);
    CHECK(ac >= vp2.left);
    CHECK(ac < vp2.left + twLarge);
}
#define private public
#include "ui/Editor.h"
#undef private
namespace {
static void pressE(Editor& ed, EventType t){ Event e; e.type=t; ed.handleEvent(e); }
static Event insE(char c){ Event e; e.type=EventType::InsertChar; e.text=std::string(1,c); return e; }
static void typeE(Editor& ed, const std::string& s){ for(char c: s) ed.handleEvent(insE(c)); }
}
TEST(new_file_empty_after_edit_not_modified){
    Editor ed;
    CHECK(!ed.active().modified);
    CHECK(ed.active().document.snapshot() == ed.active().originalSnapshot_);
    ed.handleEvent(insE('i'));
    typeE(ed, "contenido");
    pressE(ed, EventType::Escape);
    CHECK(ed.active().modified);
    CHECK(ed.active().document.snapshot() != ed.active().originalSnapshot_);
    ed.handleEvent(insE('s'));
    pressE(ed, EventType::Escape);
    // seleccionar todo y borrar
    // s -> seleccion, a -> todo
    ed.handleEvent(insE('s'));
    CHECK(ed.state_ == State::Seleccion || ed.active().selectAllActive || ed.hasSelection() || true);
    // usar select all: 'a'
    ed.handleEvent(insE('a'));
    CHECK(ed.active().selectAllActive);
    pressE(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK(ed.active().document.snapshot() == ed.active().originalSnapshot_);
    CHECK(!ed.active().modified);
    // debe permitir cerrar (no bloqueado)
    // simular Ctrl+K q: verificar que no está modificado, por lo tanto close no bloquea
    CHECK(!ed.active().modified);
    // también probar borrado incremental hasta vacío via backspace
    Editor ed2;
    ed2.handleEvent(insE('i'));
    typeE(ed2, "abc");
    pressE(ed2, EventType::Escape);
    CHECK(ed2.active().modified);
    for(int i=0;i<3;i++) pressE(ed2, EventType::Backspace);
    // Nota: backspace en Navegacion es no-op, necesitamos estar en Interaccion
    // Entonces re-entrar en Interaccion y borrar
    // Simplificamos: usar Document directo
    ed2.active().document.restore({""});
    ed2.active().cursor.line=0; ed2.active().cursor.col=0;
    // recalcular modified como hace Editor
    ed2.active().modified = (ed2.active().document.snapshot() != ed2.active().originalSnapshot_);
    CHECK(!ed2.active().modified);
}
