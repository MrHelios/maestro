// FASE E paso 3 — ida y vuelta del cursor por el borde TTY.
//
// El común habla CellPos 0-based; el +1 a CUP 1-based vive solo en
// TtyEncoder::moveCursorTo(CellPos) y el -1 simétrico en decodeMouseSgr.
// Este test cierra el lazo: CellPos -> CUP -> SGR -> CellPos, en bordes,
// más el caso fuera de viewport (visible=false no pinta).
#include <algorithm>
#include <string>
#include <vector>

#include "test_framework.h"

#include "app/ChromePresentation.h"
#include "app/EditorState.h"
#include "app/Message.h"
#include "base/utf8.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "layout/Viewport.h"
#include "platform/CellPos.h"
#include "platform/InputEvent.h"
#include "platform/tty/TtyMouse.h"
#include "rendering/frame/Frame.h"
#include "rendering/frame/FrameBuilder.h"
#include "rendering/tty/TtyEncoder.h"

namespace {

// Parsea el último CUP "\x1b[{Y};{X}H" del buffer a 1-based.
bool lastCup(const std::string& out, int& row1, int& col1) {
    const std::size_t h = out.rfind('H');
    if (h == std::string::npos) return false;
    const std::size_t e = out.rfind("\x1b[", h);
    if (e == std::string::npos) return false;
    const std::size_t semi = out.find(';', e);
    if (semi == std::string::npos || semi > h) return false;
    try {
        row1 = std::stoi(out.substr(e + 2, semi - e - 2));
        col1 = std::stoi(out.substr(semi + 1, h - semi - 1));
    } catch (const std::exception&) {
        return false;
    }
    return row1 >= 1 && col1 >= 1;
}

Document numberedDoc(int lines) {
    Document doc;
    std::vector<std::string> v;
    for (int i = 0; i < lines; ++i)
        v.push_back("line " + std::to_string(1000 + i));
    doc.restore(v);
    return doc;
}

Viewport contentVp(int top, int height, int width = 80) {
    Viewport vp;
    vp.top = top;
    vp.left = 0;
    vp.height = height;
    vp.width = width;
    return vp;
}

// Vuelta completa: celda -> CUP -> SGR press -> celda. Devuelve la celda
// decodificada y exige paridad CUP == cell+1 en el camino.
CellPos roundtrip(CellPos cell) {
    TtyEncoder enc;
    std::string out;
    enc.moveCursorTo(out, cell);
    int row1 = 0, col1 = 0;
    CHECK(lastCup(out, row1, col1));
    CHECK_EQ(row1, cell.row + 1);
    CHECK_EQ(col1, cell.col + 1);
    // Mismo 1-based por el camino inverso (press, sin modificadores).
    const std::string sgr =
        "[<0;" + std::to_string(col1) + ";" + std::to_string(row1) + "M";
    InputEvent e;
    CellPos back;
    CHECK(decodeMouseSgr(sgr, e, back));
    CHECK(e.type == InputEventType::MousePress);
    CHECK(e.cell == back);
    return back;
}

}  // namespace

TEST(cursor_cellpos_cup_directo_bordes) {
    TtyEncoder enc;
    std::string out;
    enc.moveCursorTo(out, CellPos(0, 0));
    CHECK_EQ(out, std::string("\x1b[1;1H"));
    out.clear();
    enc.moveCursorTo(out, CellPos(79, 23));
    CHECK_EQ(out, std::string("\x1b[24;80H"));
}

TEST(cursor_cellpos_roundtrip_esquinas_resolver) {
    Document doc = numberedDoc(100);
    FrameBuilder b;
    const int h = 10;
    // Primera fila visible, col 0.
    {
        Viewport vp = contentVp(20, h);
        Cursor cur;
        cur.line = 20;
        cur.col = 0;
        CellPos cell;
        CHECK(b.editorCursorPos(doc, cur, vp, cell));
        CHECK(cell.valid());
        CHECK_EQ(cell.row, 0);
        CHECK(roundtrip(cell) == cell);
    }
    // Última fila visible, misma columna: misma X (gutter idéntico).
    {
        Viewport vp = contentVp(20, h);
        Cursor cur;
        cur.line = 20 + h - 1;
        cur.col = 0;
        CellPos cell;
        CHECK(b.editorCursorPos(doc, cur, vp, cell));
        CHECK_EQ(cell.row, h - 1);
        CHECK(roundtrip(cell) == cell);
    }
    // Columna no-cero: +3 visibles respecto a col 0 (independiente del gutter).
    {
        Viewport vp = contentVp(20, h);
        Cursor cur0;
        cur0.line = 20;
        cur0.col = 0;
        CellPos c0;
        CHECK(b.editorCursorPos(doc, cur0, vp, c0));
        Cursor cur;
        cur.line = 20;
        cur.col = 3;
        CellPos cell;
        CHECK(b.editorCursorPos(doc, cur, vp, cell));
        CHECK_EQ(cell.col, c0.col + 3);
        CHECK(roundtrip(cell) == cell);
    }
}

TEST(cursor_cellpos_resolver_acuerda_con_frame) {
    Document doc = numberedDoc(100);
    FrameBuilder b;
    Viewport vp = contentVp(20, 10);
    Cursor cur;
    cur.line = 25;
    cur.col = 2;
    CellPos cell;
    CHECK(b.editorCursorPos(doc, cur, vp, cell));
    Frame f = b.buildFrame(doc, cur, vp, "t", false, makeChromeRequest("", State::Navegacion),
                           std::nullopt);
    CHECK(f.cursor.visible);
    CHECK(f.cursor.cell == cell);
    CHECK(roundtrip(f.cursor.cell) == cell);
}

TEST(cursor_cellpos_borde_derecho_resolver) {
    // Última columna visible (colHi) -> true; una más -> false.
    // Es el borde donde viven los off-by-one (colHi = width-1 con left=0).
    Document doc;
    std::vector<std::string> v;
    v.push_back(std::string(300, 'x'));
    for (int i = 1; i < 100; ++i) v.push_back("line " + std::to_string(i));
    doc.restore(v);
    FrameBuilder b;
    Viewport vp = contentVp(0, 10, 80);
    const auto g = b.editorGeometry(doc, vp);
    // Línea ASCII: columna visual == columna byte; left=0.
    const int lastC = g.layout.content.width - 1 - g.gutterW;
    CHECK(lastC > 0);
    CHECK(lastC + 1 < static_cast<int>(v[0].size()));
    {
        Cursor cur;
        cur.line = 0;
        cur.col = lastC;
        CellPos cell;
        CHECK(b.editorCursorPos(doc, cur, vp, cell));
        CHECK_EQ(cell.row, 0);
        CHECK_EQ(cell.col, vp.width - 1);  // colHi exacto
        CHECK(roundtrip(cell) == cell);
    }
    {
        Cursor cur;
        cur.line = 0;
        cur.col = lastC + 1;
        CellPos cell;
        CHECK(!b.editorCursorPos(doc, cur, vp, cell));
        CHECK(!cell.valid());
    }
}

TEST(cursor_visual_siempre_en_inicio_de_celda) {
    // Invariancia que sostiene al cursor Block GUI: la columna visual del
    // cursor siempre cae en inicio de celda, nunca en la segunda mitad de
    // un carácter ancho ni en el interior de una expansión de tab. La
    // garantizan Cursor::visualColumn (alignStart) en la ida y
    // utf8::byteForColumn (devuelve el lead) en la vuelta del mouse; el
    // resolver GUI (blockGlyphInContent) trata la 2ª mitad como
    // inalcanzable a partir de esta garantía.
    const std::string line =
        std::string("a\xe6\xbc\xa2") + "\tb" + std::string("\xe4\xb8\xad") + "x";
    const int n = static_cast<int>(line.size());
    // Inicios de celda visuales con la misma expansión de tab que el pintor.
    std::vector<int> starts;
    {
        int col = 0, i = 0;
        starts.push_back(0);
        while (i < n) {
            if (line[i] == '\t') {
                col += utf8::TAB_WIDTH - (col % utf8::TAB_WIDTH);
                ++i;
            } else {
                int w = utf8::cellWidth(line, i, n);
                if (w <= 0) w = 1;
                int len = utf8::cellLen(line, i, n);
                if (len <= 0) len = 1;
                col += w;
                i += len;
            }
            starts.push_back(col);
        }
    }
    // La línea tiene segundas mitades e interiores de tab reales (2 y 6 no
    // son inicios; el tab interior tampoco): el test no es vacuo.
    CHECK(std::find(starts.begin(), starts.end(), 2) == starts.end());
    CHECK(std::find(starts.begin(), starts.end(), 6) == starts.end());
    auto isStart = [&](int v) {
        return std::find(starts.begin(), starts.end(), v) != starts.end();
    };
    // Ida (teclado/programa): todo byte resuelve a inicio de celda.
    Cursor cur;
    cur.line = 0;
    for (int b = 0; b <= n; ++b) {
        cur.col = b;
        CHECK(isStart(cur.visualColumn(line)));
    }
    // Vuelta (mouse): todo target visual mapea a byte alineado...
    const int totalW = starts.back();
    for (int t = 0; t <= totalW + 1; ++t) {
        const int byte = utf8::byteForColumn(line, t);
        CHECK(byte == n || utf8::isCellStart(line, byte));
        // ...cuya columna visual vuelve a ser inicio de celda.
        cur.col = byte;
        CHECK(isStart(cur.visualColumn(line)));
    }
    // El resolver del cursor GUI consume esta misma columna (+ gutter).
    {
        Document doc;
        doc.restore({line});
        FrameBuilder b;
        Viewport vp = contentVp(0, 5);
        const auto g = b.editorGeometry(doc, vp);
        for (int byte = 0; byte <= n; ++byte) {
            Cursor c;
            c.line = 0;
            c.col = byte;
            CellPos cell;
            if (!b.editorCursorPos(doc, c, vp, g, cell)) continue;
            CHECK(isStart(cell.col - g.layout.content.col - g.gutterW));
        }
    }
}

TEST(cursor_cellpos_fuera_de_viewport_no_pinta) {
    Document doc = numberedDoc(100);
    FrameBuilder b;
    Viewport vp = contentVp(17, 10);
    Cursor cur;
    cur.line = 29;  // debajo del viewport (caso rueda)
    cur.col = 0;
    CellPos cell;
    CHECK(!b.editorCursorPos(doc, cur, vp, cell));
    CHECK(!cell.valid());
    Frame f = b.buildFrame(doc, cur, vp, "t", false, makeChromeRequest("", State::Navegacion),
                           std::nullopt);
    CHECK(!f.cursor.visible);
    TtyEncoder enc;
    const std::string out = enc.encodeFrame(f);
    CHECK(out.find("\x1b[?25h") == std::string::npos);
    CHECK(out.find(" q") == std::string::npos);
}
