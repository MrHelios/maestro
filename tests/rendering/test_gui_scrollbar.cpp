// Tests de la geometría y lógica del scrollbar de GuiRenderer.
// Se ejecutan sin SDL (GuiRenderer sin initForRenderer: no-op seguro para pintado,
// pero los métodos de geometría funcionan con el Layout calculado).

#include <string>
#include <vector>

#include "test_framework.h"
#include "app/ChromePresentation.h"
#include "app/Editor.h"
#include "app/Message.h"
#include "platform/gui/GuiRunLoop.h"
#include "rendering/Sink.h"
#include "rendering/gui/GuiRenderer.h"
#include "rendering/gui/GuiChrome.h"
#include "layout/Gutter.h"
#include "layout/Layout.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#include <cstdlib>
#include <unistd.h>
#endif

namespace {

class NullTestSink : public Sink {
public:
    bool writeStdout(const std::string&) override { return true; }
};

Viewport makeVp(int h = 10, int w = 80) {
    Viewport vp;
    vp.top = 0;
    vp.left = 0;
    vp.height = h;
    vp.width = w;
    return vp;
}

ChromeRequest navChrome() {
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    return makeChromeRequest(msg, State::Navegacion);
}

void renderDoc(GuiRenderer& gui, NullTestSink& sink, Document& doc,
               Cursor& cur, Viewport& vp, const ChromeRequest& cr) {
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, cr, sink, std::nullopt,
                         std::nullopt, std::nullopt);
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: GuiScrollbar_DisabledWhenContentFits
// ---------------------------------------------------------------------------
TEST(GuiScrollbar_DisabledWhenContentFits) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"line 1", "line 2", "line 3"});
    Viewport vp = makeVp(10, 80);  // viewport height 10 > 3 lines
    Cursor cur;

    renderDoc(gui, sink, doc, cur, vp, navChrome());

    // total=3, visible=10 -> total <= visible, scrollbar disabled
    CHECK(!gui.scrollbarEnabled());

    // Incluso si forzamos scrollInfo con total <= visible
    gui.setScrollInfo({0, 10, 5});
    CHECK(!gui.scrollbarEnabled());

    gui.setScrollInfo({0, 10, 10});
    CHECK(!gui.scrollbarEnabled());
}

// ---------------------------------------------------------------------------
// Test 2: GuiScrollbar_DisabledForLists
// ---------------------------------------------------------------------------
TEST(GuiScrollbar_DisabledForLists) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l"});
    Viewport vp = makeVp(5, 80);
    Cursor cur;

    renderDoc(gui, sink, doc, cur, vp, navChrome());

    // Primero verificar que funciona para contenido normal
    gui.setScrollInfo({0, 5, 12});  // 12 lines, 5 visible
    CHECK(gui.scrollbarEnabled());

    // Ahora simular que estamos en una lista (lastIsList_ = true)
    // Esto se hace internamente en renderBufferList/renderFileList
    // Podemos verificarlo llamando a renderBufferList
    std::vector<std::string> names = {"a.txt", "b.txt", "c.txt"};
    gui.renderBufferList(names, 0, 80, 5, sink);

    // En listas, el scrollbar siempre está deshabilitado
    CHECK(!gui.scrollbarEnabled());

    // Verificar también con renderFileList
    std::vector<FileListItem> items = {{"a.txt", false}, {"b.txt", false}};
    MessageBarData msg{"", MessageKind::Info};
    gui.renderFileList(items, 0, 0, "/tmp", msg, 80, 5, sink);
    CHECK(!gui.scrollbarEnabled());
}

// ---------------------------------------------------------------------------
// Test 3: GuiScrollbar_ThumbAtTopAndBottom
// ---------------------------------------------------------------------------
TEST(GuiScrollbar_ThumbAtTopAndBottom) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    // 30 líneas para tener scroll
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i) lines.push_back("line " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(10, 80);
    Cursor cur;

    renderDoc(gui, sink, doc, cur, vp, navChrome());

    // Configurar scrollInfo: 30 total, 10 visible -> maxTop = 20
    gui.setScrollInfo({0, 10, 30});

    const Layout layout = gui.lastLayout();
    const GuiPixelRect track = gui.scrollbarTrackRect(layout);
    const GuiPixelRect thumb = gui.scrollbarThumbRect(layout);

    CHECK(track.valid);
    CHECK(thumb.valid);

    // Con top=0, el pulgar debe estar al inicio de la pista
    CHECK_EQ(thumb.y, track.y);

    // Con top=maxTop, el pulgar debe estar al final de la pista
    gui.setScrollInfo({20, 10, 30});  // maxTop = 20
    const GuiPixelRect thumbBottom = gui.scrollbarThumbRect(layout);
    CHECK(thumbBottom.valid);

    const int thumbH = thumbBottom.h;
    // El pulgar inferior debe estar en track.y + track.h - thumbH
    CHECK_EQ(thumbBottom.y, track.y + track.h - thumbH);
}

// ---------------------------------------------------------------------------
// Test 4: GuiScrollbarTopFromY_MapsTrackExtremes
// ---------------------------------------------------------------------------
TEST(GuiScrollbarTopFromY_MapsTrackExtremes) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i) lines.push_back("line " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(10, 80);
    Cursor cur;

    renderDoc(gui, sink, doc, cur, vp, navChrome());
    gui.setScrollInfo({0, 10, 30});  // maxTop = 20

    const Layout layout = gui.lastLayout();
    const GuiPixelRect track = gui.scrollbarTrackRect(layout);
    const int thumbH = gui.scrollbarThumbHeightPx(track.h);
    const int travel = track.h - thumbH;
    const int maxTop = gui.scrollbarMaxTop();

    CHECK(maxTop == 20);
    CHECK(track.h > 0);
    CHECK(thumbH > 0);
    CHECK(travel > 0);

    // Inicio: el pulgar queda en el inicio de la pista (grabOffset = 0) -> top = 0
    int top = gui.scrollbarTopFromY(track.y, 0);
    CHECK_EQ(top, 0);

    // Final: el borde superior del pulgar llega al extremo inferior de la pista.
    // travel = track.h - thumbH es el desplazamiento máximo válido del pulgar.
    top = gui.scrollbarTopFromY(track.y + travel, 0);
    CHECK_EQ(top, maxTop);

    // Centro de la pista -> top aproximadamente maxTop/2
    top = gui.scrollbarTopFromY(track.y + travel / 2, 0);
    CHECK(top > 0);
    CHECK(top < maxTop);
}

// ---------------------------------------------------------------------------
// Test 5: GuiScrollbarPress_RejectsPointsOutsideTrack
// ---------------------------------------------------------------------------
TEST(GuiScrollbarPress_RejectsPointsOutsideTrack) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i) lines.push_back("line " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(10, 80);
    Cursor cur;

    renderDoc(gui, sink, doc, cur, vp, navChrome());
    gui.setScrollInfo({5, 10, 30});  // scrollbar enabled

    const Layout layout = gui.lastLayout();
    const GuiPixelRect track = gui.scrollbarTrackRect(layout);

    CHECK(track.valid);

    // Click a la izquierda de la pista
    auto press = gui.scrollbarPress(track.x - 1, track.y + track.h / 2);
    CHECK(!press.handled);

    // Click a la derecha de la pista
    press = gui.scrollbarPress(track.x + track.w + 1, track.y + track.h / 2);
    CHECK(!press.handled);

    // Click arriba de la pista
    press = gui.scrollbarPress(track.x + track.w / 2, track.y - 1);
    CHECK(!press.handled);

    // Click abajo de la pista
    press = gui.scrollbarPress(track.x + track.w / 2, track.y + track.h + 1);
    CHECK(!press.handled);

    // Click dentro de la pista -> handled = true
    press = gui.scrollbarPress(track.x + track.w / 2, track.y + track.h / 2);
    CHECK(press.handled);
}

// ---------------------------------------------------------------------------
// Test 6: GuiScrollbarPress_DetectsThumbAndTrackClicks
// ---------------------------------------------------------------------------
TEST(GuiScrollbarPress_DetectsThumbAndTrackClicks) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i) lines.push_back("line " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(10, 80);
    Cursor cur;

    renderDoc(gui, sink, doc, cur, vp, navChrome());
    gui.setScrollInfo({5, 10, 30});  // maxTop = 20, current top = 5

    const Layout layout = gui.lastLayout();
    const GuiPixelRect track = gui.scrollbarTrackRect(layout);
    const GuiPixelRect thumb = gui.scrollbarThumbRect(layout);

    CHECK(track.valid);
    CHECK(thumb.valid);

    // Click en el pulgar -> onThumb = true, grabOffsetPx = offset dentro del pulgar
    const int thumbCenterY = thumb.y + thumb.h / 2;
    auto press = gui.scrollbarPress(thumb.x + thumb.w / 2, thumbCenterY);

    CHECK(press.handled);
    CHECK(press.onThumb);
    // grabOffsetPx debe ser la distancia desde el borde superior del pulgar
    CHECK_EQ(press.grabOffsetPx, thumbCenterY - thumb.y);

    // Click en la pista (fuera del pulgar) -> onThumb = false
    // Click en la parte superior de la pista (arriba del pulgar)
    press = gui.scrollbarPress(track.x + track.w / 2, thumb.y - 1);
    CHECK(press.handled);
    CHECK(!press.onThumb);
    // grabOffsetPx = thumbH / 2 (centro del pulgar por defecto)
    CHECK_EQ(press.grabOffsetPx, thumb.h / 2);

    // Click en la parte inferior de la pista (abajo del pulgar)
    press = gui.scrollbarPress(track.x + track.w / 2, thumb.y + thumb.h + 1);
    CHECK(press.handled);
    CHECK(!press.onThumb);
    CHECK_EQ(press.grabOffsetPx, thumb.h / 2);
}

// ---------------------------------------------------------------------------
// Test 7: GuiScrollbar_HitTestPredicate
// ---------------------------------------------------------------------------
TEST(GuiScrollbar_HitTestPredicate) {
    // El predicado puro del hit-test: track desplazada hacia dentro por
    // kResizeBorderPx para no solapar con el borde de redimensionado.
    const int winW = 800;
    const int border = 8;  // kResizeBorderPx
    // track.x = winW - border - track.w = 800 - 8 - 8 = 784
    const GuiRunLoop::ScrollbarHitRect track{winW - border - 8, 40, 8, 500};

    // Dentro de la pista (semiabierto [x, x+w) × [y, y+h))
    CHECK(GuiRunLoop::pointInScrollbarTrack(track, track.x + 2, track.y + 10));
    // Esquina superior-izquierda incluida
    CHECK(GuiRunLoop::pointInScrollbarTrack(track, track.x, track.y));
    // Esquina inferior-derecha EXCLUIDA
    CHECK(!GuiRunLoop::pointInScrollbarTrack(track, track.x + track.w, track.y + track.h));
    // Límite derecho excluido
    CHECK(!GuiRunLoop::pointInScrollbarTrack(track, track.x + track.w, track.y));
    // Límite inferior excluido
    CHECK(!GuiRunLoop::pointInScrollbarTrack(track, track.x, track.y + track.h));

    // Borde derecho de la ventana: zona de redimensionado, FUERA de la pista
    CHECK(!GuiRunLoop::pointInScrollbarTrack(track, winW - 2, track.y + 10));
    // Fuera de ambas regiones
    CHECK(!GuiRunLoop::pointInScrollbarTrack(track, 100, 100));

    // Pista vacía => nunca interactiva
    CHECK(!GuiRunLoop::pointInScrollbarTrack(GuiRunLoop::ScrollbarHitRect{}, 0, 0));
}

// ---------------------------------------------------------------------------
// Test 9: GuiScrollbar_Composition_TrackReachesChromeTop
// Verifica que el track del scrollbar llega exactamente hasta el inicio del
// chrome inferior (StatusBar), cubriendo el gap vertical sobrante.
// ---------------------------------------------------------------------------
TEST(GuiScrollbar_Composition_TrackReachesChromeTop) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i) lines.push_back("line " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(10, 80);
    Cursor cur;

    const int cw = gui.cellWForTests();
    const int ch = gui.cellHForTests();
    if (cw <= 0 || ch <= 0) SKIP("sin fuente");

    const int vpW = 80;
    const int drawableW = vpW * cw;
    // drawableH debe ser tal que usableH - logicalHeight < ch (modo anchored)
    // para que gapHeight > 0 y el test cubra el escenario de gap vertical.
    const int contentH = 10;
    const int chromeH = 2;
    const int logicalHeight = (contentH + chromeH) * ch;
    const int drawableH = logicalHeight + ch / 2;  // gap vertical < ch

    gui.setDrawableMetrics(drawableW, drawableH, 0);
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    gui.setScrollInfo({0, 10, 30});

    const Layout& layout = gui.lastLayout();
    const auto& chromeGeom = gui.lastChromeGeometry();
    CHECK(chromeGeom.gapHeight > 0);

    const GuiPixelRect track = gui.scrollbarTrackRect(layout);
    CHECK(track.valid);

    // El track empieza donde empieza el contenido
    const int expectedY = gui.contentTopInset() + layout.content.row * ch;
    CHECK_EQ(track.y, expectedY);

    // El track termina donde empieza el chrome inferior (StatusBar)
    int expectedChromeTop;
    if (chromeGeom.anchored) {
        expectedChromeTop = gui.contentTopInset() + chromeGeom.statusTop;
    } else {
        expectedChromeTop = drawableH - layout.chrome.height * ch;
    }
    CHECK_EQ(track.y + track.h, expectedChromeTop);
}

// ---------------------------------------------------------------------------
// Test 10: GuiScrollbar_Composition_ThumbWithinTrackBounds
// Verifica que el pulgar queda dentro del track y que el track no invade
// el área del StatusBar.
// ---------------------------------------------------------------------------
TEST(GuiScrollbar_Composition_ThumbWithinTrackBounds) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i) lines.push_back("line " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(10, 80);
    Cursor cur;

    const int cw = gui.cellWForTests();
    const int ch = gui.cellHForTests();
    if (cw <= 0 || ch <= 0) SKIP("sin fuente");

    const int vpW = 80;
    const int drawableW = vpW * cw;
    // drawableH debe ser tal que usableH - logicalHeight < ch (modo anchored)
    // para que gapHeight > 0 y el test cubra el escenario de gap vertical.
    const int contentH = 10;
    const int chromeH = 2;
    const int logicalHeight = (contentH + chromeH) * ch;
    const int drawableH = logicalHeight + ch / 2;  // gap vertical < ch

    gui.setDrawableMetrics(drawableW, drawableH, 0);
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    gui.setScrollInfo({0, 10, 30});

    const Layout& layout = gui.lastLayout();
    const auto& chromeGeom = gui.lastChromeGeometry();
    CHECK(chromeGeom.gapHeight > 0);

    const GuiPixelRect track = gui.scrollbarTrackRect(layout);
    const GuiPixelRect thumb = gui.scrollbarThumbRect(layout);
    CHECK(track.valid);
    CHECK(thumb.valid);

    // El pulgar está dentro del track
    CHECK(thumb.y >= track.y);
    CHECK(thumb.y + thumb.h <= track.y + track.h);

    // El track no pasa el inicio del StatusBar
    int chromeTop;
    if (chromeGeom.anchored) {
        chromeTop = gui.contentTopInset() + chromeGeom.statusTop;
    } else {
        chromeTop = drawableH - layout.chrome.height * ch;
    }
    // El track no invade el StatusBar y llega exactamente hasta él
    CHECK_EQ(track.y + track.h, chromeTop);
}

// ---------------------------------------------------------------------------
// Test 11: GuiScrollbar_Composition_ResizeVerticalConsistency
// Verifica que la geometría del track se mantiene consistente al cambiar
// drawableH (resize vertical): track.y + track.h == chromeTop en ambos casos.
// ---------------------------------------------------------------------------
TEST(GuiScrollbar_Composition_ResizeVerticalConsistency) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i) lines.push_back("line " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(10, 80);
    Cursor cur;

    const int cw = gui.cellWForTests();
    const int ch = gui.cellHForTests();
    if (cw <= 0 || ch <= 0) SKIP("sin fuente");

    const int vpW = 80;
    const int drawableW = vpW * cw;

    // Escenario A: drawableH grande
    const int drawableH1 = 600;
    gui.setDrawableMetrics(drawableW, drawableH1, 0);
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    gui.setScrollInfo({0, 10, 30});

    const Layout& layout1 = gui.lastLayout();
    const auto& chromeGeom1 = gui.lastChromeGeometry();
    const GuiPixelRect track1 = gui.scrollbarTrackRect(layout1);
    CHECK(track1.valid);

    int chromeTop1;
    if (chromeGeom1.anchored) {
        chromeTop1 = gui.contentTopInset() + chromeGeom1.statusTop;
    } else {
        chromeTop1 = drawableH1 - layout1.chrome.height * ch;
    }
    CHECK_EQ(track1.y + track1.h, chromeTop1);

    // Escenario B: drawableH chico (resize vertical)
    const int drawableH2 = 300;
    gui.setDrawableMetrics(drawableW, drawableH2, 0);
    gui.invalidateCache();  // forzar render completo
    renderDoc(gui, sink, doc, cur, vp, navChrome());

    const Layout& layout2 = gui.lastLayout();
    const auto& chromeGeom2 = gui.lastChromeGeometry();
    const GuiPixelRect track2 = gui.scrollbarTrackRect(layout2);
    CHECK(track2.valid);

    int chromeTop2;
    if (chromeGeom2.anchored) {
        chromeTop2 = gui.contentTopInset() + chromeGeom2.statusTop;
    } else {
        chromeTop2 = drawableH2 - layout2.chrome.height * ch;
    }
    CHECK_EQ(track2.y + track2.h, chromeTop2);

    // Tras invalidateCache el render debe ser completo
    CHECK(gui.lastWasFull());
}

// ---------------------------------------------------------------------------
// Test 12: GuiScrollbar_ContentAndCursorStayLeftOfTrack
// Regresión: con scrollbar visible y desplazamiento horizontal, el cursor se
// pintaba encima de la pista/pulgar porque el contenido ocupaba el ancho
// completo de la ventana. La reserva horizontal reduce el área de contenido
// para que el cursor nunca llegue al track, SIN desplazar viewport.left (el
// mapeo de entrada del Editor se mantiene sincronizado con lo que se pinta).
//
// Garantías verificadas:
//   A. El borde derecho del contenido pintado no llega al track (en píxeles).
//   B. FrameBuilder clampea el cursor contra layoutEff.content.width:
//      si el cursor está en la columna reservada, queda inválido y no se pinta.
//   C. Si el cursor está dentro del área útil, queda a la izquierda del track.
//   D. viewport.left no se desplaza: el mapeo de entrada (mouse) sigue
//      sincronizado con lo que se pinta.
//   E. Los fast paths (tryFastCursorMove / tryFastScrollLine) reciben
//      layoutEff y mantienen la invariante.
// ---------------------------------------------------------------------------
TEST(GuiScrollbar_ContentAndCursorStayLeftOfTrack) {
    GuiRenderer gui;
    NullTestSink sink;
    // Varias líneas largas para permitir movimientos verticales válidos.
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i) lines.push_back(std::string(200, 'x'));
    Document doc;
    doc.restore(lines);

    const int cw = gui.cellWForTests();
    const int ch = gui.cellHForTests();
    if (cw <= 0 || ch <= 0) SKIP("sin fuente");

    const int vpW = 80;
    Viewport vp = makeVp(10, vpW);
    Cursor cur;
    cur.line = 0;

    const int gutter = gutterWidth(doc.lineCount(), vpW);
    // Columna de texto dentro del área útil (primera columna, siempre válida
    // si hay al menos 1 columna útil, lo cual garantiza el renderer).
    const int usefulTextCol = 0;
    // Columna de texto en el borde derecho de la pantalla (reservada al
    // scrollbar cuando está visible).
    const int reservedTextCol = vpW - 1 - gutter;

    // Control explícito de la geometría física del drawable:
    // drawableW = vpW * cw (sin gap, modo legacy), drawableH = 400.
    // scrollbarTrackRect usa drawablePixelW_ para calcular activeRightPx.
    gui.setDrawableMetrics(vpW * cw, 400, 0);

    // El scrollbar debe estar habilitado ANTES del render (como en el loop
    // real, que sincroniza scrollInfo antes de renderFrame).
    gui.setScrollInfo({0, vp.height, 30});

    // Helper local para verificar la invariante tras cada render.
    // `cursorEnAreaUtil` indica si el cursor debe ser válido y a la
    // izquierda del track.
    auto verificarInvariante = [&](bool cursorEnAreaUtil) {
        const Layout layout = gui.lastLayout();
        const GuiPixelRect track = gui.scrollbarTrackRect(layout);
        const GuiPixelRect cursorRect = gui.cursorPixelRect();

        CHECK(track.valid);
        // A. El borde derecho del contenido pintado no llega al track
        //    (en píxeles, no solo en columnas lógicas).
        CHECK(layout.content.width < vpW);
        CHECK(layout.content.width * cw <= track.x);
        // D. viewport.left no se desplaza.
        CHECK_EQ(vp.left, 0);

        if (cursorEnAreaUtil) {
            // B/C. El cursor debe ser válido y quedar a la izquierda del
            //    track (sin solapamiento con track ni pulgar).
            CHECK(cursorRect.valid);
            CHECK(cursorRect.x + cursorRect.w <= track.x);
        } else {
            // B. Si el cursor está en la columna reservada, FrameBuilder lo
            //    clampea y no se pinta (cursorPixelRect().valid == false).
            CHECK(!cursorRect.valid);
        }
    };

    // Render 1 (camino completo): cursor en la columna reservada ->
    // FrameBuilder lo clampea y no se pinta.
    cur.col = reservedTextCol;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    verificarInvariante(false);

    // Render 2 (camino completo): cursor dentro del área útil ->
    // se pinta y queda a la izquierda del track.
    cur.col = usefulTextCol;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    verificarInvariante(true);

    // Render 3 (equivalencia tras movimiento de cursor, misma geometría):
    // cursor dentro del área útil, línea 1.
    cur.col = usefulTextCol;
    cur.line = 1;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    verificarInvariante(true);

    // Render 4 (equivalencia tras cambio de viewport.top):
    // Cambiar scrollInfo_ no basta; debe cambiar vp.top, que es lo que
    // recibe el renderer. Esto verifica que el resultado es equivalente
    // tras un desplazamiento vertical de una línea, sea cual sea el camino
    // interno (camino completo o tryFastScrollLine).
    vp.top = 1;
    cur.col = usefulTextCol;
    cur.line = 1;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    verificarInvariante(true);

    // Render 5 (equivalencia tras nuevo render con misma geometría):
    cur.col = usefulTextCol;
    cur.line = 2;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    verificarInvariante(true);
}

// ---------------------------------------------------------------------------
// Test 13: GuiScrollbar_Aparece_InvalidaFastPath
// El scrollbar puede aparecer sin que cambien viewport.width ni
// viewport.height, pero layoutEff.content.width se reduce. Los fast paths
// deben detectar el cambio de geometría de contenido y forzar un render
// completo (no reutilizar filas construidas con otro ancho de contenido).
//
// Estrategia:
//   1. Render sin scrollbar + cursor en línea 0 (establece cache).
//   2. Render sin scrollbar + cursor en línea 1 (movimiento de cursor: el
//      renderer intenta fast path; mismo layoutEff, debe funcionar).
//   3. Render con scrollbar + cursor en línea 2 (cambio de layoutEff +
//      movimiento de cursor: el fast path debe invalidarse y hacer full
//      repaint).
// ---------------------------------------------------------------------------
TEST(GuiScrollbar_Aparece_InvalidaFastPath) {
    GuiRenderer gui;
    NullTestSink sink;
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i) lines.push_back(std::string(200, 'x'));
    Document doc;
    doc.restore(lines);

    const int cw = gui.cellWForTests();
    const int ch = gui.cellHForTests();
    if (cw <= 0 || ch <= 0) SKIP("sin fuente");

    const int vpW = 80;
    Viewport vp = makeVp(10, vpW);
    Cursor cur;
    const int gutter = gutterWidth(doc.lineCount(), vpW);
    cur.col = vpW - 1 - gutter;  // columna reservada al scrollbar

    gui.setDrawableMetrics(vpW * cw, 400, 0);

    // Render 1: sin scrollbar (total == visible), cursor en línea 0.
    cur.line = 0;
    gui.setScrollInfo({0, vp.height, vp.height});
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    const Layout lo1 = gui.lastLayout();
    CHECK_EQ(lo1.content.width, vpW);  // sin reducción
    CHECK(gui.cursorPixelRect().valid);  // cursor pinta en última columna

    // Render 2: sin scrollbar, cursor en línea 1 (movimiento, mismo layout).
    cur.line = 1;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());  // fast path reutiliza cache (mismo layoutEff)

    // Render 3: aparece el scrollbar, cursor en línea 2.
    // Cambio de layoutEff.content.width + movimiento: debe invaladar
    // fast path y hacer render completo.
    cur.line = 2;
    gui.setScrollInfo({0, vp.height, 30});
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    const Layout lo3 = gui.lastLayout();
    CHECK(lo3.content.width < vpW);  // se redujo por la reserva
    CHECK(!gui.cursorPixelRect().valid);  // cursor clampeado, no se pinta
    CHECK(gui.lastWasFull());  // full repaint por cambio de geometría
}

// ---------------------------------------------------------------------------
// Test 14: GuiScrollbar_Desaparece_InvalidaFastPath
// El scrollbar puede desaparecer sin que cambien viewport.width ni
// viewport.height, pero layoutEff.content.width vuelve al ancho completo.
// Los fast paths deben detectar el cambio de geometría de contenido y
// forzar un render completo.
// ---------------------------------------------------------------------------
TEST(GuiScrollbar_Desaparece_InvalidaFastPath) {
    GuiRenderer gui;
    NullTestSink sink;
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i) lines.push_back(std::string(200, 'x'));
    Document doc;
    doc.restore(lines);

    const int cw = gui.cellWForTests();
    const int ch = gui.cellHForTests();
    if (cw <= 0 || ch <= 0) SKIP("sin fuente");

    const int vpW = 80;
    Viewport vp = makeVp(10, vpW);
    Cursor cur;
    const int gutter = gutterWidth(doc.lineCount(), vpW);
    cur.col = vpW - 1 - gutter;  // columna reservada al scrollbar

    gui.setDrawableMetrics(vpW * cw, 400, 0);

    // Render 1: con scrollbar, cursor en línea 0.
    cur.line = 0;
    gui.setScrollInfo({0, vp.height, 30});
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    const Layout lo1 = gui.lastLayout();
    CHECK(lo1.content.width < vpW);  // reducido
    CHECK(!gui.cursorPixelRect().valid);  // cursor clampeado

    // Render 2: con scrollbar, cursor en línea 1 (movimiento, mismo layout).
    cur.line = 1;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());  // fast path reutiliza cache (mismo layoutEff)

    // Render 3: desaparece el scrollbar, cursor en línea 2.
    // Cambio de layoutEff.content.width + movimiento: debe invaladar
    // fast path y hacer render completo.
    cur.line = 2;
    gui.setScrollInfo({0, vp.height, vp.height});
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    const Layout lo3 = gui.lastLayout();
    CHECK_EQ(lo3.content.width, vpW);  // vuelve al ancho completo
    CHECK(gui.cursorPixelRect().valid);  // cursor pinta en última columna
    CHECK(gui.lastWasFull());  // full repaint por cambio de geometría
}

// ---------------------------------------------------------------------------
// Test 8: GuiScrollbar_BorderConsistency
// ---------------------------------------------------------------------------
TEST(GuiScrollbar_BorderConsistency) {
    // El hit-test nativo y el fallback manual deben usar el MISMO
    // ancho de borde (kResizeBorderPx = 8). Antes el fallback usaba
    // std::max(4, std::min(8, titleH/4)) que podía dar 7 con titleH=28.
    using namespace std::string_literals;

    // Simular el fallback: guiWindowEdgeAt con el nuevo borderPoints
    // que ahora devuelve kResizeBorderPts (8).
    const int winW = 800;
    const int winH = 600;
    const int border = kResizeBorderPts;  // 8

    // Justo en el límite: x = winW - border = 792.
    // Con border=8, x=792 está en [792, 800) -> zona de redimensionado (Right).
    // Con border=7, x=792 estaba en [793, 800) -> zona de redimensionado era más estrecha.
    const auto edgeRight = guiWindowEdgeAt(winW - border, 300, winW, winH, border);
    CHECK(edgeRight == GuiWindowEdge::Right);

    // Un píxel más a la izquierda: x = 791 -> NO es borde (con border=8).
    // Con border=7, 791 habría caído en zona de redimensionado.
    const auto edgeInside = guiWindowEdgeAt(winW - border - 1, 300, winW, winH, border);
    CHECK(edgeInside == GuiWindowEdge::None);

    // La pista del scrollbar empieza en winW - border - trackW = 784.
    // Con border=8 y trackW=8: track = [784, 792).
    // El borde derecho empieza en 792. No hay solapamiento.
    const int trackX = winW - border - 8;
    CHECK(trackX + 8 == winW - border);  // track termina donde empieza el borde

    // Verificar que pointInScrollbarTrack da false en el borde derecho.
    const GuiRunLoop::ScrollbarHitRect track{trackX, 40, 8, 500};
    CHECK(!GuiRunLoop::pointInScrollbarTrack(track, winW - 2, 300));  // en el borde
}