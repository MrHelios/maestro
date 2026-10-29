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