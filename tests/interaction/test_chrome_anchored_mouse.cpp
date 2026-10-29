// Tests de mouse con chrome anclado (gapAsBottom).
// Todos los tests llevan el prefijo `chrome_anchored_` para poder filtrarlos.

#include "test_support.h"
#include "platform/gui/GuiSdlMapper.h"
#include "rendering/gui/GuiChromePixels.h"

#include <algorithm>

using guichrome::BottomChromeGeometry;
using guichrome::bottomChromeGeometry;

namespace {

constexpr int kCw = 10;
constexpr int kCh = 19;
constexpr int kCols = 80;
constexpr int kH = 428;  // 428 % 19 = 10 -> residuo de 10px, chrome anclado

// Layout típico: contenido + 2 filas de chrome
Layout makeLayout(int contentH, int chromeH = 2) {
    Layout lo;
    lo.content = {0, 0, kCols, contentH};
    lo.chrome = {0, contentH, kCols, chromeH};
    return lo;
}

// Replica de los tres caminos de GuiRunLoop::mouseCell():
// contenido normal, espacio residual y chrome anclado.
// NOTA: la fila de chrome usa layout.chrome.row (no layout.content.height).
CellPos anchoredMouseCell(int px, int py, const Layout& lo,
                          const BottomChromeGeometry& geom, bool gapAsBottom) {
    const int rows = kH / kCh;
    if (!geom.anchored) {
        return guiCellFromPixelsBounded(px, py, kCw, kCh, kCols, rows);
    }
    const int col = std::clamp(px / kCw, 0, kCols - 1);
    if (py < geom.contentBottom) {
        return CellPos{col, py / kCh};                      // contenido normal
    }
    if (py < geom.chromeTop) {
        // espacio residual
        return gapAsBottom ? CellPos{col, lo.content.height} : CellPos{};
    }
    const int chromeRow = (py - geom.chromeTop) / kCh;      // chrome anclado
    return CellPos{col, lo.chrome.row + chromeRow};
}

} // namespace

// 1. Clic en el centro del StatusBar -> fila lógica = layout.chrome.row
TEST(chrome_anchored_mouse_click_statusbar_center) {
    const Layout lo = makeLayout(20);
    const BottomChromeGeometry geom =
        bottomChromeGeometry(kH, kCh, lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);

    const CellPos cell = anchoredMouseCell(40 * kCw, geom.statusTop + kCh / 2, lo, geom, false);

    CHECK(cell.valid());
    CHECK_EQ(cell.row, lo.chrome.row);
    CHECK_EQ(cell.col, 40);
}

// 2. Clic en el centro del MessageBar -> fila lógica = layout.chrome.row + 1
TEST(chrome_anchored_mouse_click_messagebar_center) {
    const Layout lo = makeLayout(20);
    const BottomChromeGeometry geom =
        bottomChromeGeometry(kH, kCh, lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);

    const CellPos cell = anchoredMouseCell(40 * kCw, geom.messageTop + kCh / 2, lo, geom, false);

    CHECK(cell.valid());
    CHECK_EQ(cell.row, lo.chrome.row + 1);
    CHECK_EQ(cell.col, 40);
}

// 3. Clic en el espacio residual -> CellPos{} inválido (no es chrome)
TEST(chrome_anchored_mouse_click_gap_residual_invalid) {
    const Layout lo = makeLayout(20);
    const BottomChromeGeometry geom =
        bottomChromeGeometry(kH, kCh, lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);
    CHECK(geom.gapPx > 0);

    const CellPos cell = anchoredMouseCell(40 * kCw, geom.gapTop + geom.gapPx / 2, lo, geom, false);

    CHECK(!cell.valid());
}

// 4. Arrastre dentro del espacio residual -> gapAsBottom devuelve la fila
//    inmediatamente debajo del contenido (permite autoscroll)
TEST(chrome_anchored_mouse_drag_gap_as_bottom_autoscroll) {
    const Layout lo = makeLayout(20);
    const BottomChromeGeometry geom =
        bottomChromeGeometry(kH, kCh, lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);
    CHECK(geom.gapPx > 0);

    const CellPos cell = anchoredMouseCell(40 * kCw, geom.gapTop + geom.gapPx / 2, lo, geom, true);

    CHECK(cell.valid());
    CHECK_EQ(cell.row, lo.content.height);
    CHECK_EQ(cell.col, 40);
}

// 5. Soltar en el espacio residual -> release usa gapAsBottom=false -> inválido
TEST(chrome_anchored_mouse_release_gap_invalid) {
    const Layout lo = makeLayout(20);
    const BottomChromeGeometry geom =
        bottomChromeGeometry(kH, kCh, lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);
    CHECK(geom.gapPx > 0);

    const CellPos cell = anchoredMouseCell(40 * kCw, geom.gapTop + geom.gapPx / 2, lo, geom, false);

    CHECK(!cell.valid());
}

// 6. Si chrome.row difiere de content.height, la fila sale de layout.chrome.row
TEST(chrome_anchored_mouse_chrome_row_uses_layout_chrome_row) {
    Layout lo = makeLayout(20);
    lo.chrome.row = 22;  // hipotético: chrome desacoplado de content.height
    const BottomChromeGeometry geom =
        bottomChromeGeometry(kH, kCh, lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);

    const CellPos cell = anchoredMouseCell(40 * kCw, geom.statusTop + kCh / 2, lo, geom, false);

    CHECK(cell.valid());
    CHECK_EQ(cell.row, 22);
}