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
#include "layout/Layout.h"

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