// Tests de cursor e IME con chrome anclado.
// Todos los tests llevan el prefijo `chrome_anchored_` para poder filtrarlos.

#include "test_framework.h"
#include "rendering/gui/GuiRenderer.h"
#include "rendering/gui/GuiChromePixels.h"

using guichrome::BottomChromeGeometry;
using guichrome::bottomChromeGeometry;

namespace {

constexpr int kCw = 10;
constexpr int kCh = 19;
constexpr int kH = 428;   // anclado (residuo de 10px)
constexpr int kHLegacy = 437;  // 437 % 19 == 0 -> NO anclado

Layout makeLayout(int contentH, int chromeH = 2) {
    Layout lo;
    lo.content = {0, 0, 80, contentH};
    lo.chrome = {0, contentH, 80, chromeH};
    return lo;
}

FrameCursor makeCursor(int row, int col, FrameCursorShape shape = FrameCursorShape::Block) {
    FrameCursor c;
    c.cell = CellPos{col, row};
    c.shape = shape;
    c.visible = true;
    return c;
}

// Fila visual (en píxeles) de una celda lógica. Es la lógica compartida por
// cursorPixelRect() e imeRectPx(): ambos deben consultar la misma geometría.
// El topInset se suma UNA sola vez, aquí.
int cellTopPx(const FrameCursor& cur, const Layout& lo,
              const BottomChromeGeometry& geom, int topInset) {
    int y = cur.cell.row * kCh;
    if (geom.anchored && cur.cell.row >= lo.chrome.row) {
        const int chromeRow = cur.cell.row - lo.chrome.row;
        if (chromeRow == 0) y = geom.statusTop;
        else if (chromeRow == 1) y = geom.messageTop;
    }
    return y + topInset;
}

GuiPixelRect cursorRect(const FrameCursor& cur, const Layout& lo,
                        const BottomChromeGeometry& geom, int topInset) {
    GuiPixelRect r;
    r.x = cur.cell.col * kCw;
    r.y = cellTopPx(cur, lo, geom, topInset);
    r.w = (cur.shape == FrameCursorShape::Bar) ? 2 : kCw;
    r.h = kCh;
    r.valid = true;
    return r;
}

GuiPixelRect imeRect(const FrameCursor& cur, const Layout& lo,
                     const BottomChromeGeometry& geom, int topInset) {
    GuiPixelRect r;
    r.x = cur.cell.col * kCw;
    r.y = cellTopPx(cur, lo, geom, topInset);
    r.w = kCw;
    r.h = kCh;
    r.valid = true;
    return r;
}

} // namespace

// Cursor en StatusBar -> rect dentro de su celda visual
TEST(chrome_anchored_cursor_rect_statusbar) {
    const Layout lo = makeLayout(20);
    const BottomChromeGeometry geom = bottomChromeGeometry(kH, kCh, lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);

    const FrameCursor cur = makeCursor(lo.chrome.row, 5, FrameCursorShape::Block);
    const GuiPixelRect r = cursorRect(cur, lo, geom, 0);

    CHECK(r.valid);
    CHECK_EQ(r.y, geom.statusTop);
    CHECK_EQ(r.h, kCh);
    CHECK_EQ(r.x, 5 * kCw);
    CHECK_EQ(r.w, kCw);
}

// Cursor en MessageBar -> rect dentro de su celda visual
TEST(chrome_anchored_cursor_rect_messagebar) {
    const Layout lo = makeLayout(20);
    const BottomChromeGeometry geom = bottomChromeGeometry(kH, kCh, lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);

    const FrameCursor cur = makeCursor(lo.chrome.row + 1, 8, FrameCursorShape::Bar);
    const GuiPixelRect r = cursorRect(cur, lo, geom, 0);

    CHECK(r.valid);
    CHECK_EQ(r.y, geom.messageTop);
    CHECK_EQ(r.h, kCh);
    CHECK_EQ(r.x, 8 * kCw);
    CHECK_EQ(r.w, 2);
}

// IME en StatusBar -> misma celda visual que el cursor
TEST(chrome_anchored_ime_rect_statusbar) {
    const Layout lo = makeLayout(20);
    const BottomChromeGeometry geom = bottomChromeGeometry(kH, kCh, lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);

    const FrameCursor cur = makeCursor(lo.chrome.row, 3);
    const GuiPixelRect ime = imeRect(cur, lo, geom, 0);
    const GuiPixelRect cr = cursorRect(cur, lo, geom, 0);

    CHECK_EQ(ime.y, geom.statusTop);
    CHECK_EQ(ime.h, kCh);
    CHECK_EQ(ime.w, kCw);
    CHECK_EQ(ime.y, cr.y);
    CHECK_EQ(ime.x, cr.x);
}

// IME en MessageBar -> misma celda visual que el cursor
TEST(chrome_anchored_ime_rect_messagebar) {
    const Layout lo = makeLayout(20);
    const BottomChromeGeometry geom = bottomChromeGeometry(kH, kCh, lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);

    const FrameCursor cur = makeCursor(lo.chrome.row + 1, 12, FrameCursorShape::Bar);
    const GuiPixelRect ime = imeRect(cur, lo, geom, 0);
    const GuiPixelRect cr = cursorRect(cur, lo, geom, 0);

    CHECK_EQ(ime.y, geom.messageTop);
    CHECK_EQ(ime.h, kCh);
    CHECK_EQ(ime.w, kCw);
    CHECK_EQ(ime.y, cr.y);
    CHECK_EQ(ime.x, cr.x);
}

// El inset de la barra de título se aplica una sola vez (cursor e IME)
TEST(chrome_anchored_title_inset_applied_once) {
    const int topInset = 28;
    const Layout lo = makeLayout(20);
    const BottomChromeGeometry geom = bottomChromeGeometry(kH, kCh, lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);

    const FrameCursor cur = makeCursor(lo.chrome.row + 1, 7);
    CHECK_EQ(cursorRect(cur, lo, geom, topInset).y, geom.messageTop + topInset);
    CHECK_EQ(imeRect(cur, lo, geom, topInset).y, geom.messageTop + topInset);
}

// Geometría no anclada -> comportamiento legacy (fila * ch)
TEST(chrome_anchored_legacy_not_anchored_keeps_old_behavior) {
    const Layout lo = makeLayout(20);
    const BottomChromeGeometry geom = bottomChromeGeometry(kHLegacy, kCh, lo.content.height, lo.chrome.height);
    CHECK(!geom.anchored);

    const FrameCursor cur = makeCursor(lo.chrome.row + 1, 5);
    CHECK_EQ(cursorRect(cur, lo, geom, 0).y, (lo.chrome.row + 1) * kCh);
    CHECK_EQ(imeRect(cur, lo, geom, 0).y, (lo.chrome.row + 1) * kCh);
}