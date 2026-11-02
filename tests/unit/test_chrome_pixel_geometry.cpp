// Tests unitarios de la geometría pura del chrome inferior (sin SDL).
// Verifica activación, posiciones, residuo y casos degenerados.

#include <string>

#include "test_framework.h"
#include "rendering/gui/GuiChromePixels.h"

using guichrome::BottomChromeGeometry;
using guichrome::bottomChromeGeometry;

namespace {

// Helper: ch típico 19 px, 2 filas de chrome
constexpr int ch = 19;
constexpr int chromeH = 2;

void check(const BottomChromeGeometry& g, bool expAnchored,
           int expContentBottom, int expGapTop, int expGapPx,
           int expChromeTop, int expStatusTop, int expMessageTop,
           int expChromeHeight) {
    CHECK_EQ(g.anchored, expAnchored);
    CHECK_EQ(g.contentBottom, expContentBottom);
    CHECK_EQ(g.gapTop, expGapTop);
    CHECK_EQ(g.gapPx, expGapPx);
    CHECK_EQ(g.chromeTop, expChromeTop);
    CHECK_EQ(g.statusTop, expStatusTop);
    CHECK_EQ(g.messageTop, expMessageTop);
    CHECK_EQ(g.chromeHeight, expChromeHeight);
}

} // namespace

TEST(chrome_pixels_anchored_r_zero) {
    // H es múltiplo exacto: r = 0
    // contentH = 20, chromeH = 2, Nlayout = 22
    // H = 22 * 19 = 418
    int H = 418;
    int contentH = 20;
    auto g = bottomChromeGeometry(H, ch, contentH, chromeH);
    check(g, true,
          20 * ch,  // contentBottom
          20 * ch,  // gapTop
          0,        // gapPx
          20 * ch,  // chromeTop = H - 2*ch = 418 - 38 = 380
          20 * ch,  // statusTop
          21 * ch,  // messageTop
          2 * ch);  // chromeHeight
}

TEST(chrome_pixels_anchored_r_mid) {
    // r = 10 (0 < r < ch)
    // contentH = 20, H = 418 + 10 = 428
    int H = 428;
    int contentH = 20;
    auto g = bottomChromeGeometry(H, ch, contentH, chromeH);
    check(g, true,
          20 * ch,      // contentBottom = 380
          20 * ch,      // gapTop = 380
          10,           // gapPx
          390,          // chromeTop = 428 - 38 = 390
          390,          // statusTop
          390 + ch,     // messageTop = 409
          2 * ch);      // chromeHeight = 38
}

TEST(chrome_pixels_anchored_r_max) {
    // r = ch - 1 = 18
    // H = 418 + 18 = 436
    int H = 436;
    int contentH = 20;
    auto g = bottomChromeGeometry(H, ch, contentH, chromeH);
    check(g, true,
          20 * ch,      // contentBottom = 380
          20 * ch,      // gapTop = 380
          ch - 1,       // gapPx = 18
          398,          // chromeTop = 436 - 38 = 398
          398,          // statusTop
          398 + ch,     // messageTop = 417
          2 * ch);      // chromeHeight = 38
}

TEST(chrome_pixels_not_anchored_r_equals_ch) {
    // r = ch -> NO anclado (legacy)
    // H = 418 + 19 = 437 -> Nlayout pasa a 23, r vuelve a 0 en legacy
    int H = 437;
    int contentH = 20;
    auto g = bottomChromeGeometry(H, ch, contentH, chromeH);
    CHECK(!g.anchored);
}

TEST(chrome_pixels_not_anchored_negative_r) {
    // H < Nlayout * ch -> r negativo
    int H = 417;
    int contentH = 20;
    auto g = bottomChromeGeometry(H, ch, contentH, chromeH);
    CHECK(!g.anchored);
}

TEST(chrome_pixels_not_anchored_small_H) {
    // H < chromeH * ch (no cabe ni el chrome)
    int H = 10;
    int contentH = 5;
    auto g = bottomChromeGeometry(H, ch, contentH, chromeH);
    CHECK(!g.anchored);
}

TEST(chrome_pixels_chromeH_zero) {
    // Sin chrome -> nunca anclado
    int H = 418;
    int contentH = 20;
    auto g = bottomChromeGeometry(H, ch, contentH, 0);
    CHECK(!g.anchored);
}

TEST(chrome_pixels_chromeH_one) {
    // Solo StatusBar (chromeH = 1)
    int contentH = 20;
    int H = contentH * ch + ch; // 399
    auto g = bottomChromeGeometry(H, ch, contentH, 1);
    CHECK(g.anchored);
    CHECK_EQ(g.chromeTop, H - ch);
    CHECK_EQ(g.statusTop, H - ch);
    // messageTop no se usa, pero el helper lo calcula igual
    CHECK_EQ(g.messageTop, H);
    CHECK_EQ(g.chromeHeight, ch);
}

TEST(chrome_pixels_contentH_zero) {
    // Sin contenido (edge case): solo chrome
    int contentH = 0;
    int H = 2 * ch; // 38
    auto g = bottomChromeGeometry(H, ch, contentH, chromeH);
    CHECK(g.anchored);
    CHECK_EQ(g.contentBottom, 0);
    CHECK_EQ(g.gapTop, 0);
    CHECK_EQ(g.gapPx, 0);
    CHECK_EQ(g.chromeTop, 0);
    CHECK_EQ(g.statusTop, 0);
    CHECK_EQ(g.messageTop, ch);
    CHECK_EQ(g.chromeHeight, 2 * ch);
}

TEST(chrome_pixels_consecutive_heights) {
    // Recorre alturas consecutivas y verifica que chromeTop baja 1 px por px
    int contentH = 20;
    int baseH = (contentH + chromeH) * ch; // 418
    int prevChromeTop = -1;
    for (int H = baseH; H < baseH + ch; ++H) {
        auto g = bottomChromeGeometry(H, ch, contentH, chromeH);
        CHECK(g.anchored);
        if (prevChromeTop >= 0) {
            CHECK_EQ(g.chromeTop, prevChromeTop + 1);
        }
        prevChromeTop = g.chromeTop;
        // statusTop y messageTop acompañan a chromeTop
        CHECK_EQ(g.statusTop, g.chromeTop);
        CHECK_EQ(g.messageTop, g.chromeTop + ch);
        // gapPx crece de 0 a ch-1
        CHECK_EQ(g.gapPx, H - baseH);
    }
}

TEST(chrome_pixels_gap_is_background_between_content_and_chrome) {
    // El residuo queda entre contentBottom y chromeTop
    int contentH = 20;
    int H = 428; // r = 10
    auto g = bottomChromeGeometry(H, ch, contentH, chromeH);
    CHECK(g.anchored);
    CHECK_EQ(g.gapTop, g.contentBottom);
    CHECK_EQ(g.chromeTop, g.gapTop + g.gapPx);
    CHECK_EQ(g.chromeTop + g.chromeHeight, H); // chrome llega al borde inferior
}

// ---------------------------------------------------------------------------
// Geometría física del frame: los dos sobrantes de guiGridSize se incorporan
// como pseudo fila/columna para que el frame activo cubra el drawable sin
// estirar celdas. No modifican las dimensiones lógicas.
// ---------------------------------------------------------------------------

TEST(chrome_pixels_frame_exact_incluye_gaps) {
    Layout lo;
    lo.content = {0, 0, 80, 20};
    lo.chrome = {20, 0, 80, 2};
    const int cw = 10, chh = 19;
    const int logicalW = 80 * cw;            // 800
    const int logicalH = (20 + 2) * chh;     // 418
    const int drawableW = logicalW + 7;      // sobrante < cw
    const int usableH = logicalH + 10;       // sobrante < ch
    const auto f = guichrome::framePixelGeometry(lo, drawableW, usableH, cw,
                                                 chh);
    CHECK(f.valid);
    CHECK(f.exact);
    CHECK_EQ(f.logicalWidth, logicalW);
    CHECK_EQ(f.logicalHeight, logicalH);
    CHECK_EQ(f.width, drawableW);
    CHECK_EQ(f.height, usableH);
    CHECK_EQ(f.rightSpacerPx, 7);
    CHECK_EQ(f.preChromeSpacerPx, 10);
}

TEST(chrome_pixels_frame_sin_sobrantes_sigue_exacto) {
    Layout lo;
    lo.content = {0, 0, 80, 20};
    lo.chrome = {20, 0, 80, 2};
    const auto f = guichrome::framePixelGeometry(lo, 800, 418, 10, 19);
    CHECK(f.valid);
    CHECK(f.exact);
    CHECK_EQ(f.width, 800);
    CHECK_EQ(f.height, 418);
    CHECK_EQ(f.rightSpacerPx, 0);
    CHECK_EQ(f.preChromeSpacerPx, 0);
}

TEST(chrome_pixels_frame_sobrante_de_una_celda_no_es_exacto) {
    Layout lo;
    lo.content = {0, 0, 80, 20};
    lo.chrome = {20, 0, 80, 2};
    // Un sobrante >= celda significa que falta una columna/fila lógica: no es
    // el residuo de guiGridSize y se conserva la geometría legacy.
    auto right = guichrome::framePixelGeometry(lo, 810, 418, 10, 19);
    CHECK(right.valid);
    CHECK(!right.exact);
    CHECK_EQ(right.width, 800);
    CHECK_EQ(right.height, 418);
    CHECK_EQ(right.rightSpacerPx, 0);

    auto bottom = guichrome::framePixelGeometry(lo, 800, 437, 10, 19);
    CHECK(bottom.valid);
    CHECK(!bottom.exact);
    CHECK_EQ(bottom.width, 800);
    CHECK_EQ(bottom.height, 418);
    CHECK_EQ(bottom.preChromeSpacerPx, 0);
}

TEST(chrome_pixels_frame_no_entra_en_el_drawable_es_legacy) {
    Layout lo;
    lo.content = {0, 0, 80, 20};
    lo.chrome = {20, 0, 80, 2};
    // Sobrantes negativos (grilla más grande que el área): legacy con
    // clipping posterior, nunca exact.
    auto narrow = guichrome::framePixelGeometry(lo, 799, 418, 10, 19);
    CHECK(narrow.valid);
    CHECK(!narrow.exact);
    CHECK_EQ(narrow.width, 800);
    CHECK_EQ(narrow.height, 418);

    auto shortH = guichrome::framePixelGeometry(lo, 800, 417, 10, 19);
    CHECK(shortH.valid);
    CHECK(!shortH.exact);
    CHECK_EQ(shortH.width, 800);
    CHECK_EQ(shortH.height, 418);
}

TEST(chrome_pixels_frame_sin_chrome_tambien_cubre_sobrantes) {
    Layout lo;
    lo.content = {0, 0, 80, 40};
    lo.chrome = {40, 0, 80, 0};
    const auto f = guichrome::framePixelGeometry(lo, 805, 763, 10, 19);
    CHECK(f.valid);
    CHECK(f.exact);
    CHECK_EQ(f.logicalWidth, 800);
    CHECK_EQ(f.logicalHeight, 760);
    CHECK_EQ(f.rightSpacerPx, 5);
    CHECK_EQ(f.preChromeSpacerPx, 3);
}

TEST(chrome_pixels_frame_geometria_invalida) {
    Layout lo;
    lo.content = {0, 0, 80, 20};
    lo.chrome = {20, 0, 80, 2};
    CHECK(!guichrome::framePixelGeometry(lo, 800, 418, 0, 19).valid);
    CHECK(!guichrome::framePixelGeometry(lo, 800, 418, 10, 0).valid);
    CHECK(!guichrome::framePixelGeometry(lo, 0, 418, 10, 19).valid);
    CHECK(!guichrome::framePixelGeometry(lo, 800, 0, 10, 19).valid);
}