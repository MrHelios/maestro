#include <string>

#include "test_framework.h"
#include "rendering/gui/GuiChromePixels.h"

using guichrome::BottomChromeGeometry;
using guichrome::bottomChromeGeometry;

namespace {

// Helper: celdas_height_pixeles típico 19 px
constexpr int celdas_height_pixeles = 19;
// Helper: 2 filas de chrome
constexpr int chrome_filas = 2;

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

/*
*  Este test verifica la altura de la geometria de chrome cuando gap = 0
* drawableHeight es múltiplo exacto: gap = 0
* filasContenido = 20, chrome_filas = 2, filasTotal = 22
* drawableHeight = 22 * 19 = 418
*/
TEST(chrome_pixels_height_anchored_gap_zero) {
    
    int drawableHeight = 418;
    int filasContenido = 20;
    auto g = bottomChromeGeometry(drawableHeight, celdas_height_pixeles, filasContenido, chrome_filas);
    check(g, true,
          20 * celdas_height_pixeles,   // contentBottom
          20 * celdas_height_pixeles,   // gapTop
          0,                            // gapPx
          20 * celdas_height_pixeles,   // chromeTop = drawableHeight - 2*celdas_height_pixeles = 418 - 38 = 380
          20 * celdas_height_pixeles,   // statusTop
          21 * celdas_height_pixeles,   // messageTop
          2  * celdas_height_pixeles);  // chromeHeight
}

/*
*  Este test verifica la altura de la geometria de chrome cuando gap = 10
*  gap = 10 (0 < gap < celdas_height_pixeles)
*  filasContenido = 20, 
*  drawableHeight = 418 + 10 = 428
*/
TEST(chrome_pixels_height_anchored_gap_mid) {    
    int drawableHeight = 428;
    int filasContenido = 20;
    auto g = bottomChromeGeometry(drawableHeight, celdas_height_pixeles, filasContenido, chrome_filas);
    check(g, true,
          20 * celdas_height_pixeles,      // contentBottom = 380
          20 * celdas_height_pixeles,      // gapTop = 380
          10,                              // gapPx
          390,                             // chromeTop = 428 - 38 = 390
          390,                             // statusTop
          390 + celdas_height_pixeles,     // messageTop = 409
          2 * celdas_height_pixeles);      // chromeHeight = 38
}

/*
* Este test verifica la altura de la geometria de chrome cuando gap = 18
* gap = celdas_height_pixeles - 1 = 18
* drawableHeight = 418 + 18 = 436
*/
TEST(chrome_pixels_height_anchored_gap_max) {    
    int drawableHeight = 436;
    int filasContenido = 20;
    auto g = bottomChromeGeometry(drawableHeight, celdas_height_pixeles, filasContenido, chrome_filas);
    check(g, true,
          20 * celdas_height_pixeles,      // contentBottom = 380
          20 * celdas_height_pixeles,      // gapTop = 380
          celdas_height_pixeles - 1,       // gapPx = 18
          398,                             // chromeTop = 436 - 38 = 398
          398,                             // statusTop
          398 + celdas_height_pixeles,     // messageTop = 417
          2 * celdas_height_pixeles);      // chromeHeight = 38
}

/*
*  Si la altura del gap es igual o mayor a celdas_height_pixeles no se ancla
*  y se activa el modo legacy
*  gap = celdas_height_pixeles -> NO anclado
*  drawableHeight = 418 + 19 = 437 -> filasTotal pasa a 23, gap vuelve a 0 en legacy
*/
TEST(chrome_pixels_not_anchored_gap_equals_ch) {
    int drawableHeight = 437;
    int filasContenido = 20;
    auto g = bottomChromeGeometry(drawableHeight, celdas_height_pixeles, filasContenido, chrome_filas);
    CHECK(!g.anchored);
}

/*
*  Si la altura del gap es negativa entonces no se ancla y se activa el modo legacy
*  drawableHeight < filasTotal * celdas_height_pixeles -> gap negativo
*/
TEST(chrome_pixels_not_anchored_negative_gap) { 
    int drawableHeight = 417;
    int filasContenido = 20;
    auto g = bottomChromeGeometry(drawableHeight, celdas_height_pixeles, filasContenido, chrome_filas);
    CHECK(!g.anchored);
}

/*
*  Si no hay espacio suficiente en el drawable, entonces no se ancla y se activa legacy mode.
*  drawableHeight < chrome_filas * celdas_height_pixeles (no cabe ni el chrome)
*/
TEST(chrome_pixels_not_anchored_small_drawableHeight) {    
    int drawableHeight = 10;
    int filasContenido = 5;
    auto g = bottomChromeGeometry(drawableHeight, celdas_height_pixeles, filasContenido, chrome_filas);
    CHECK(!g.anchored);
}

/*
*  Si no hay filas para chrome entonces no se ancla y se activa legacy mode.
*  Sin chrome -> nunca anclado
*/
TEST(chrome_pixels_not_anchored_chrome_filas_zero) {
    int drawableHeight = 418;
    int filasContenido = 20;
    auto g = bottomChromeGeometry(drawableHeight, celdas_height_pixeles, filasContenido, 0);
    CHECK(!g.anchored);
}

/*
*  Para el caso de que chrome solo tenga una fila.
*  Solo StatusBar (chrome_filas = 1)
*/
TEST(chrome_pixels_chrome_filas_one) {
    
    int filasContenido = 20;
    int drawableHeight = filasContenido * celdas_height_pixeles + celdas_height_pixeles; // 399
    auto g = bottomChromeGeometry(drawableHeight, celdas_height_pixeles, filasContenido, 1);
    
    CHECK(g.anchored);
    // statusTop tiene que ser de la altura del chrome.
    CHECK_EQ(g.chromeTop, drawableHeight - celdas_height_pixeles);
    CHECK_EQ(g.statusTop, drawableHeight - celdas_height_pixeles);
    // messageTop no se usa, pero el helper lo calcula igual.
    CHECK_EQ(g.messageTop, drawableHeight);
    CHECK_EQ(g.chromeHeight, celdas_height_pixeles);
}

/*
*  Para el caso de que no hayas filas para contenido pero si para chrome
*  Sin contenido (edge case): solo chrome
*/
TEST(chrome_pixels_filas_contenido_zero) {
    int filasContenido = 0;
    int drawableHeight = 2 * celdas_height_pixeles; // 38
    auto g = bottomChromeGeometry(drawableHeight, celdas_height_pixeles, filasContenido, chrome_filas);
    
    CHECK(g.anchored);
    CHECK_EQ(g.contentBottom, 0);
    CHECK_EQ(g.gapTop, 0);
    CHECK_EQ(g.gapPx, 0);
    CHECK_EQ(g.chromeTop, 0);
    CHECK_EQ(g.statusTop, 0);
    CHECK_EQ(g.messageTop, celdas_height_pixeles);
    CHECK_EQ(g.chromeHeight, 2 * celdas_height_pixeles);
}

/* 
* el test verifica que a medida que el drawableHeight aumenta en uno
* el chrometop baja en uno
*/
TEST(chrome_pixels_consecutive_heights) {
    // Recorre alturas consecutivas y verifica que chromeTop baja 1 px por px
    int filasContenido = 20;
    int baseH = (filasContenido + chrome_filas) * celdas_height_pixeles; // 418
    int prevChromeTop = -1;
    
    for (int drawableHeight = baseH; drawableHeight < baseH + celdas_height_pixeles; ++drawableHeight) {
        auto g = bottomChromeGeometry(drawableHeight, celdas_height_pixeles, filasContenido, chrome_filas);
        CHECK(g.anchored);
        
        if (prevChromeTop >= 0) {
            CHECK_EQ(g.chromeTop, prevChromeTop + 1);
        }
        prevChromeTop = g.chromeTop;
        // statusTop y messageTop acompañan a chromeTop
        CHECK_EQ(g.statusTop, g.chromeTop);
        CHECK_EQ(g.messageTop, g.chromeTop + celdas_height_pixeles);
        // gapPx crece de 0 a celdas_height_pixeles-1
        CHECK_EQ(g.gapPx, drawableHeight - baseH);
    }
}

/*
*  El test verifica que el gap queda entre el contenido y el chrome.
*/
TEST(chrome_pixels_gap_is_background_between_content_and_chrome) {
    int filasContenido = 20;
    int drawableHeight = 428; // gap = 10
    auto g = bottomChromeGeometry(drawableHeight, celdas_height_pixeles, filasContenido, chrome_filas);
    
    CHECK(g.anchored);
    CHECK_EQ(g.gapTop, g.contentBottom);
    CHECK_EQ(g.chromeTop, g.gapTop + g.gapPx);
    CHECK_EQ(g.chromeTop + g.chromeHeight, drawableHeight); // chrome llega al borde inferior
}

// ---------------------------------------------------------------------------
// Geometría física del frame: los dos sobrantes de guiGridSize se incorporan
// como pseudo fila/columna para que el frame activo cubra el drawable sin
// estirar celdas. No modifican las dimensiones lógicas.
// ---------------------------------------------------------------------------

TEST(chrome_pixels_frame_exact_incluye_gaps) {
    Layout lo;
    lo.content = {0, 0, 80, 20};
    lo.chrome  = {20, 0, 80, 2};
    
    const int cellWidth = 10, cellHeight = 19;
    const int logicalW = 80 * cellWidth;            // 800
    const int logicalH = (20 + 2) * cellHeight;     // 418
    const int drawableW = logicalW + 7;             // sobrante < cellWidth
    const int drawableHeight = logicalH + 10;       // sobrante < celdas_height_pixeles
    
    const auto f = guichrome::framePixelGeometry(lo, drawableW, drawableHeight, cellWidth,
                                                 cellHeight);
    
    CHECK(f.valid);
    CHECK(f.exact);
    CHECK_EQ(f.logicalWidth, logicalW);
    CHECK_EQ(f.logicalHeight, logicalH);
    CHECK_EQ(f.width, drawableW);
    CHECK_EQ(f.height, drawableHeight);
    
    // aca se verifica gap width y gap height
    CHECK_EQ(f.rightSpacerPx, 7);
    CHECK_EQ(f.preChromeSpacerPx, 10);
}

TEST(chrome_pixels_frame_sin_sobrantes_sigue_exacto) {
    Layout lo;
    lo.content = {0, 0, 80, 20};
    lo.chrome = {20, 0, 80, 2};
    
    const int cellWidth = 10, cellHeight = 19;
    const int logicalW = 80 * cellWidth;            // 800
    const int logicalH = (20 + 2) * cellHeight;     // 418
    
    const auto f = guichrome::framePixelGeometry(lo, logicalW, logicalH, cellWidth, cellHeight);
    
    CHECK(f.valid);
    CHECK(f.exact);
    CHECK_EQ(f.width, logicalW);
    CHECK_EQ(f.height, logicalH);
    
    // aca se verifica que no hay sobrantes
    CHECK_EQ(f.rightSpacerPx, 0);
    CHECK_EQ(f.preChromeSpacerPx, 0);
}

TEST(chrome_pixels_frame_sobrante_de_una_celda_no_es_exacto) {
    Layout lo;
    lo.content = {0, 0, 80, 20};
    lo.chrome = {20, 0, 80, 2};
    // Un sobrante >= celda significa que falta una columna/fila lógica: no es
    // el residuo de guiGridSize y se conserva la geometría legacy.

    const int cellWidth = 10, cellHeight = 19;
    const int logicalW = 80 * cellWidth;            // 800
    const int logicalH = (20 + 2) * cellHeight;     // 418

    // Caso 1: sobrante derecho = 0 (ancho exacto), sobrante altura >= celda (no exacto en alto)
    auto alto_no_exacto = guichrome::framePixelGeometry(lo, logicalW, logicalH + cellHeight, cellWidth, cellHeight);
    CHECK(alto_no_exacto.valid);
    CHECK(!alto_no_exacto.exact);
    CHECK_EQ(alto_no_exacto.width, logicalW);
    CHECK_EQ(alto_no_exacto.height, logicalH);
    CHECK_EQ(alto_no_exacto.rightSpacerPx, 0);

    // Caso 2: sobrante derecho >= celda (no exacto en ancho), sobrante altura = 0 (alto exacto)
    auto ancho_no_exacto = guichrome::framePixelGeometry(lo, logicalW + cellWidth, logicalH, cellWidth, cellHeight);
    CHECK(ancho_no_exacto.valid);
    CHECK(!ancho_no_exacto.exact);
    CHECK_EQ(ancho_no_exacto.width, logicalW);
    CHECK_EQ(ancho_no_exacto.height, logicalH);
    CHECK_EQ(ancho_no_exacto.rightSpacerPx, 0);

    // Caso 3: ambos sobrantes >= celda (no exacto en ancho y en alto)
    auto ambos_no_exactos = guichrome::framePixelGeometry(lo, logicalW + cellWidth, logicalH + cellHeight, cellWidth, cellHeight);
    CHECK(ambos_no_exactos.valid);
    CHECK(!ambos_no_exactos.exact);
    CHECK_EQ(ambos_no_exactos.width, logicalW);
    CHECK_EQ(ambos_no_exactos.height, logicalH);
    CHECK_EQ(ambos_no_exactos.rightSpacerPx, 0);
    CHECK_EQ(ambos_no_exactos.preChromeSpacerPx, 0);
}

TEST(chrome_pixels_frame_sin_chrome_tambien_cubre_sobrantes) {
    Layout lo;
    lo.content = {0, 0, 80, 40};
    lo.chrome = {40, 0, 80, 0};
    
    const int cellWidth = 10, cellHeight = 19;
    const int logicalW = 80 * cellWidth;            // 800
    const int logicalH = (40 + 0) * cellHeight;     // 760
    const int drawableW = logicalW + 5;             // sobrante < cellWidth
    const int drawableHeight = logicalH + 3;        // sobrante < cellHeight
    
    const auto f = guichrome::framePixelGeometry(lo, drawableW, drawableHeight, cellWidth,
                                                 cellHeight);
    
    CHECK(f.valid);
    CHECK(f.exact);
    CHECK_EQ(f.logicalWidth, logicalW);
    CHECK_EQ(f.logicalHeight, logicalH);
    CHECK_EQ(f.rightSpacerPx, 5);
    CHECK_EQ(f.preChromeSpacerPx, 3);
}

TEST(chrome_pixels_frame_geometria_invalida) {
    Layout lo;
    lo.content = {0, 0, 80, 20};
    lo.chrome = {20, 0, 80, 2};

    int cellWidth = 10, cellHeight = 19;
    const int logicalW = 80 * cellWidth;            // 800
    const int logicalH = (20 + 2) * cellHeight;     // 418
    int drawableW = logicalW + 7;                   // sobrante < cellWidth
    int drawableHeight = logicalH + 10;             // sobrante < celdas_height_pixeles

    // cw invalido (<= 0)
    cellWidth = 0;
    cellHeight = 19;
    drawableW = logicalW + 7;
    drawableHeight = logicalH + 10;
    auto f = guichrome::framePixelGeometry(lo, drawableW, drawableHeight, cellWidth, cellHeight);
    CHECK(!f.valid);

    // ch invalido (<= 0)
    cellWidth = 10;
    cellHeight = 0;
    drawableW = logicalW + 7;
    drawableHeight = logicalH + 10;
    f = guichrome::framePixelGeometry(lo, drawableW, drawableHeight, cellWidth, cellHeight);
    CHECK(!f.valid);

    // drawableW invalido (<= 0)
    cellWidth = 10;
    cellHeight = 19;
    drawableW = 0;
    drawableHeight = logicalH + 10;
    f = guichrome::framePixelGeometry(lo, drawableW, drawableHeight, cellWidth, cellHeight);
    CHECK(!f.valid);

    // drawableHeight invalido (<= 0)
    cellWidth = 10;
    cellHeight = 19;
    drawableW = logicalW + 7;
    drawableHeight = 0;
    f = guichrome::framePixelGeometry(lo, drawableW, drawableHeight, cellWidth, cellHeight);
    CHECK(!f.valid);
}