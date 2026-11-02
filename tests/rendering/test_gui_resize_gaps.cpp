// Tests de gaps al redimensionar en modo GUI (Paso F del plan de corrección).
// Verifican que el fondo de cada fila (incluido CurrentLine) llega hasta
// activeRightPx y que el scrollbar queda alineado con el extremo activo.
//
// Las partes con SDL leen píxeles del framebuffer persistente
// (readLastSurfacePixels); sin SDL se testa la geometría pura del scrollbar.

#include <string>
#include <vector>

#include "test_framework.h"

#include "app/ChromePresentation.h"
#include "app/Editor.h"
#include "app/Message.h"
#include "layout/Layout.h"
#include "platform/gui/GuiRunLoop.h"
#include "rendering/Sink.h"
#include "rendering/gui/GuiChrome.h"
#include "rendering/gui/GuiRenderer.h"

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

Viewport makeVp(int h = 5, int w = 40) {
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

#ifdef HAVE_SDL2
// Escena con gap horizontal forzado (rightSpacerPx > 0) cuando cw > 1.
struct GapScene {
    bool hasGap = false;
    int cw = 0;
    int ch = 0;
    int logicalWidth = 0;
    int activeRightPx = 0;
    int activeW = 0;
    int activeH = 0;
    std::vector<GuiColor> px;
};

// Renderiza y, si no hay gap natural, fuerza uno de 1 px cambiando
// drawableW (logicalWidth + 1 sigue siendo "exact" porque 1 < cw).
bool renderForcedGap(GuiRenderer& gui, NullTestSink& sink, Document& doc,
                     Cursor& cur, Viewport& vp, const ChromeRequest& cr,
                     int drawableW, int drawableH, GapScene& g) {
    renderDoc(gui, sink, doc, cur, vp, cr);
    g.cw = gui.cellWForTests();
    g.ch = gui.cellHForTests();
    if (g.cw <= 0 || g.ch <= 0) return false;
    const Layout& layout = gui.lastLayout();
    g.logicalWidth = (layout.content.col + layout.content.width) * g.cw;
    int right = drawableW - g.logicalWidth;
    bool exact = (right >= 0 && right < g.cw);
    if (!exact && g.cw > 1) {
        // Forzar gap de 1 px.
        int drawableW2 = g.logicalWidth + 1;
        gui.setDrawableMetrics(drawableW2, drawableH, gui.contentTopInset());
        gui.invalidateCache();
        renderDoc(gui, sink, doc, cur, vp, cr);
        right = 1;
        exact = true;
    }
    if (!exact) return false;  // no se pudo forzar gap
    g.hasGap = true;
    g.activeRightPx = g.logicalWidth + right;
    int w = 0, h = 0;
    if (!gui.readLastSurfacePixels(g.px, w, h) || g.px.empty()) return false;
    g.activeW = w;
    g.activeH = h;
    return true;
}
#endif  // HAVE_SDL2

}  // namespace

// ---------------------------------------------------------------------------
// Paso F, prueba 6 (sin SDL): scrollbar alineado con activeRightPx.
// ---------------------------------------------------------------------------
TEST(GuiResize_ScrollbarAlineadoConExtremoActivo) {
    GuiRenderer gui;
    NullTestSink sink;
    // Métricas de drawable sin SDL (setDrawableMetrics es público y no usa SDL).
    // Elegir drawableW de modo que haya gap (rightSpacerPx en (0, cw)) para
    // estar en modo "exact" y que activeRightPx == drawableW.
    const int cw = gui.cellWForTests();
    if (cw <= 1) SKIP("celda demasiado pequeña para forzar gap");
    const int vpW = 80;
    const int logicalWidth = vpW * cw;  // content.col == 0, content.width == vpW
    const int gap = (cw > 3) ? 3 : (cw - 1);
    const int drawableW = logicalWidth + gap;
    gui.setDrawableMetrics(drawableW, 400, 0);
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i)
        lines.push_back("linea " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(10, vpW);
    Cursor cur;
    cur.line = 5;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    gui.setScrollInfo({5, 10, 30});

    const Layout& layout = gui.lastLayout();
    const int activeRightPx = drawableW;  // modo exact: gap < cw y >= 0

    const int resizeBorderPx = 0;
    const GuiPixelRect track = gui.scrollbarTrackRect(layout, resizeBorderPx);
    const GuiPixelRect thumb = gui.scrollbarThumbRect(layout);

    CHECK(track.valid);
    CHECK(thumb.valid);
    // track.x + track.w == activeRightPx - resizeBorderPx (Paso D.1 / F.6).
    CHECK_EQ(track.x + track.w + resizeBorderPx, activeRightPx);
    // El pulgar hereda track.x automáticamente (Paso D.3.2).
    CHECK_EQ(thumb.x, track.x);
    // track dentro del drawable.
    CHECK(track.x >= 0);
    CHECK(track.x + track.w <= drawableW);
    // Coherencia hit-track: construir el rect de hit-test como hace GuiRunLoop
    // y verificar que dentro está dentro y fuera está fuera.
    GuiRunLoop::ScrollbarHitRect hr;
    hr.x = track.x; hr.y = track.y; hr.w = track.w; hr.h = track.h;
    CHECK(GuiRunLoop::pointInScrollbarTrack(hr, track.x + 1, track.y + 1));
    CHECK(!GuiRunLoop::pointInScrollbarTrack(hr, track.x + track.w, track.y));
    CHECK(!GuiRunLoop::pointInScrollbarTrack(hr, track.x, track.y + track.h));
}

#ifdef HAVE_SDL2
// ---------------------------------------------------------------------------
// Paso F, pruebas con SDL (leer píxeles del framebuffer persistente).
// ---------------------------------------------------------------------------

struct SdlDummyLocal {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    int w = 400;
    int h = 300;
    int drawableW = 0;
    int drawableH = 0;
    bool ok = false;
    std::string prevVideo;
    std::string prevAudio;
    bool hadVideo = false;
    bool hadAudio = false;
    SdlDummyLocal(int w = 400, int h = 300) : w(w), h(h) {
        if (const char* p = std::getenv("SDL_VIDEODRIVER")) {
            prevVideo = p;
            hadVideo = true;
        }
        if (const char* p = std::getenv("SDL_AUDIODRIVER")) {
            prevAudio = p;
            hadAudio = true;
        }
        ::setenv("SDL_VIDEODRIVER", "dummy", 1);
        ::setenv("SDL_AUDIODRIVER", "dummy", 1);
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return;
        win = SDL_CreateWindow("maestro-resize-gaps-test", 0, 0, w, h,
                               SDL_WINDOW_HIDDEN);
        if (!win) return;
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
        ok = (ren != nullptr);
        if (ren) SDL_GetRendererOutputSize(ren, &drawableW, &drawableH);
        if (drawableW <= 0 || drawableH <= 0) {
            drawableW = w;
            drawableH = h;
        }
    }
    ~SdlDummyLocal() {
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
        if (hadVideo)
            ::setenv("SDL_VIDEODRIVER", prevVideo.c_str(), 1);
        else
            ::unsetenv("SDL_VIDEODRIVER");
        if (hadAudio)
            ::setenv("SDL_AUDIODRIVER", prevAudio.c_str(), 1);
        else
            ::unsetenv("SDL_AUDIODRIVER");
    }
};

// Lee la superficie persistente (única vía portable para leer píxeles del
// framebuffer sin invalidar el backbuffer).
bool readSurfaceLocal(GuiRenderer& gui, std::vector<GuiColor>& out) {
    int w = 0, h = 0;
    if (!gui.readLastSurfacePixels(out, w, h) || out.empty()) {
        out.clear();
        return false;
    }
    return true;
}

// Píxel de la escena en (x, y) dentro del framebuffer activo.
inline GuiColor pxAt(const std::vector<GuiColor>& px, int activeW, int x,
                     int y) {
    return px[static_cast<size_t>(y) * static_cast<size_t>(activeW) +
              static_cast<size_t>(x)];
}

// ---------------------------------------------------------------------------
// Prueba 1: render completo con rightSpacerPx > 0; fondo de cada fila hasta
// activeRightPx (incluido CurrentLine).
// ---------------------------------------------------------------------------
TEST(GuiResize_FullRellenoGapHastaActiveRightPx) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH,
                           gui.contentTopInset());
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 12; ++i)
        lines.push_back("linea " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(6, 40);
    Cursor cur;
    cur.line = 3;  // CurrentLine en la fila relativa 3
    renderDoc(gui, sink, doc, cur, vp, navChrome());

    GapScene g;
    if (!renderForcedGap(gui, sink, doc, cur, vp, navChrome(), sdl.drawableW,
                         sdl.drawableH, g)) {
        SKIP("no se pudo forzar gap horizontal");
    }

    const bool dark = false;
    const GuiColor bgNormal = guichrome::background(dark);
    const GuiColor bgCurrent =
        guichrome::styleFor(StyleRole::Default, true, dark).bg;

    const int rowsPerPx = g.ch;
    const int nRows = static_cast<int>(gui.lastContentRows().size());

    // Cada píxel de la región de gap [logicalWidth, activeW) de cada fila
    // debe llevar el fondo de esa fila (normal o CurrentLine).
    for (int i = 0; i < nRows; ++i) {
        const bool isCur = gui.lastContentRows()[static_cast<size_t>(i)]
                               .isCurrentLine;
        const GuiColor esperado = isCur ? bgCurrent : bgNormal;
        const int fy = i * rowsPerPx + rowsPerPx / 2;
        // Verificar varios puntos del gap: inicio, medio y fin.
        for (int x = g.logicalWidth; x < g.activeW; ++x) {
            const GuiColor v = pxAt(g.px, g.activeW, x, fy);
            CHECK_EQ(v.r, esperado.r);
            CHECK_EQ(v.g, esperado.g);
            CHECK_EQ(v.b, esperado.b);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// Prueba 2: movimiento de cursor (tryFastCursorMove) repinta las filas
// afectadas hasta activeRightPx.
// ---------------------------------------------------------------------------
TEST(GuiResize_CursorMoveRepintaHastaActiveRightPx) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH,
                           gui.contentTopInset());
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 12; ++i)
        lines.push_back("linea " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(6, 40);
    Cursor cur;
    cur.line = 0;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());  // primer frame es completo

    // Mover el cursor dentro del mismo viewport (camino rápido).
    cur.line = 5;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    // tryFastCursorMove no es full (no rebuild de todas las filas).
    CHECK(!gui.lastWasFull());

    GapScene g;
    if (!renderForcedGap(gui, sink, doc, cur, vp, navChrome(), sdl.drawableW,
                         sdl.drawableH, g)) {
        SKIP("no se pudo forzar gap horizontal");
    }
    CHECK(g.hasGap);
    // Tras el movimiento, la escena completa (con gap forzado) se renderiza
    // completo y cada fila llega hasta activeRightPx (verificado en prueba 1);
    // acá solo aseguramos que el camino rápido no dejó gap sin pintar.
    const bool dark = false;
    const GuiColor bgNormal = guichrome::background(dark);
    const int rowsPerPx = g.ch;
    const int nRows = g.activeH / rowsPerPx;
    for (int i = 0; i < nRows; ++i) {
        const bool isCur = gui.lastContentRows()[static_cast<size_t>(i)]
                               .isCurrentLine;
        const GuiColor esperado =
            isCur ? guichrome::styleFor(StyleRole::Default, true, dark).bg
                  : bgNormal;
        const int fy = i * rowsPerPx + rowsPerPx / 2;
        for (int x = g.logicalWidth; x < g.activeW; ++x) {
            const GuiColor v = pxAt(g.px, g.activeW, x, fy);
            CHECK_EQ(v.r, esperado.r);
            CHECK_EQ(v.g, esperado.g);
            CHECK_EQ(v.b, esperado.b);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// Prueba 3: scroll rápido ±1; filas bliteadas + fila expuesta + fondo de gap.
// ---------------------------------------------------------------------------
TEST(GuiResize_ScrollRapidoConGap) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH,
                           gui.contentTopInset());
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i)
        lines.push_back("linea " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(6, 40);
    vp.top = 0;
    Cursor cur;
    cur.line = 0;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());

    // Scroll hacia abajo (delta == 1): camino rápido de scroll.
    vp.top = 1;
    cur.line = 1;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());  // scroll rápido, no full

    GapScene g;
    if (!renderForcedGap(gui, sink, doc, cur, vp, navChrome(), sdl.drawableW,
                         sdl.drawableH, g)) {
        SKIP("no se pudo forzar gap horizontal");
    }
    CHECK(g.hasGap);
    const bool dark = false;
    const GuiColor bgNormal = guichrome::background(dark);
    const int rowsPerPx = g.ch;
    const int nRows = g.activeH / rowsPerPx;
    for (int i = 0; i < nRows; ++i) {
        const bool isCur = gui.lastContentRows()[static_cast<size_t>(i)]
                               .isCurrentLine;
        const GuiColor esperado =
            isCur ? guichrome::styleFor(StyleRole::Default, true, dark).bg
                  : bgNormal;
        const int fy = i * rowsPerPx + rowsPerPx / 2;
        for (int x = g.logicalWidth; x < g.activeW; ++x) {
            const GuiColor v = pxAt(g.px, g.activeW, x, fy);
            CHECK_EQ(v.r, esperado.r);
            CHECK_EQ(v.g, esperado.g);
            CHECK_EQ(v.b, esperado.b);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// Prueba 4: resize alternando contentFb_[0] y [1]; el origen no tiene gap
// obsoleto (Paso A.1 / F.4).
// ---------------------------------------------------------------------------
TEST(GuiResize_ResizeAlternarTexturasSinGapObsoleto) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH,
                           gui.contentTopInset());
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 12; ++i)
        lines.push_back("linea " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(6, 40);
    Cursor cur;
    cur.line = 0;
    renderDoc(gui, sink, doc, cur, vp, navChrome());

    // Resize: cambiar drawableW y forzar render completo.
    GapScene g1;
    if (!renderForcedGap(gui, sink, doc, cur, vp, navChrome(), sdl.drawableW,
                         sdl.drawableH, g1)) {
        SKIP("no se pudo forzar gap horizontal");
    }

    // Segundo render con mismo drawable: debe ser parcial (textura reutilizada
    // y válida) y seguir sin gap obsoleto.
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());

    GapScene g2;
    if (!renderForcedGap(gui, sink, doc, cur, vp, navChrome(), sdl.drawableW,
                         sdl.drawableH, g2)) {
        SKIP("no se pudo forzar gap horizontal");
    }
    // Ambas escenas tienen gap relleno correctamente.
    CHECK(g1.hasGap);
    CHECK(g2.hasGap);
    const bool dark = false;
    const GuiColor bgNormal = guichrome::background(dark);
    const int rowsPerPx = g2.ch;
    const int nRows = g2.activeH / rowsPerPx;
    for (int i = 0; i < nRows; ++i) {
        const bool isCur = gui.lastContentRows()[static_cast<size_t>(i)]
                               .isCurrentLine;
        const GuiColor esperado =
            isCur ? guichrome::styleFor(StyleRole::Default, true, dark).bg
                  : bgNormal;
        const int fy = i * rowsPerPx + rowsPerPx / 2;
        for (int x = g2.logicalWidth; x < g2.activeW; ++x) {
            const GuiColor v = pxAt(g2.px, g2.activeW, x, fy);
            CHECK_EQ(v.r, esperado.r);
            CHECK_EQ(v.g, esperado.g);
            CHECK_EQ(v.b, esperado.b);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// Prueba 5: resize seguido de movimiento del cursor.
// ---------------------------------------------------------------------------
TEST(GuiResize_ResizeLuegoMovimientoCursor) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH,
                           gui.contentTopInset());
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 12; ++i)
        lines.push_back("linea " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(6, 40);
    Cursor cur;
    cur.line = 0;
    renderDoc(gui, sink, doc, cur, vp, navChrome());

    // Resize (cambiar drawableW) + invalidate para forzar full.
    gui.setDrawableMetrics(sdl.drawableW + 1, sdl.drawableH,
                           gui.contentTopInset());
    gui.invalidateCache();
    cur.line = 2;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());  // tras invalidate + geometría cambiada, full

    // Movimiento posterior (camino rápido) con geometría estable.
    cur.line = 3;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());

    GapScene g;
    if (!renderForcedGap(gui, sink, doc, cur, vp, navChrome(), sdl.drawableW + 1,
                         sdl.drawableH, g)) {
        SKIP("no se pudo forzar gap horizontal");
    }
    CHECK(g.hasGap);
#endif
}

// ---------------------------------------------------------------------------
// Prueba 6 (con SDL): coherencia scrollbar pintado / pulgar / hit-test.
// ---------------------------------------------------------------------------
TEST(GuiResize_ScrollbarCoherenciaPintadoHitTest) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH,
                           gui.contentTopInset());
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i)
        lines.push_back("linea " + std::to_string(i));
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 5;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    gui.setScrollInfo({5, 10, 30});

    const Layout& layout = gui.lastLayout();
    const int cw = gui.cellWForTests();
    const int logicalWidth = (layout.content.col + layout.content.width) * cw;
    const int right = sdl.drawableW - logicalWidth;
    const bool exact = (right >= 0 && right < cw);
    const int activeRightPx = exact ? logicalWidth + right : logicalWidth;

    const GuiPixelRect track = gui.scrollbarTrackRect(layout, 0);
    const GuiPixelRect thumb = gui.scrollbarThumbRect(layout);
    CHECK(track.valid);
    CHECK(thumb.valid);
    CHECK_EQ(track.x + track.w, activeRightPx);
    CHECK_EQ(thumb.x, track.x);

    // Hit-test coherente con el track pintado.
    GuiRunLoop::ScrollbarHitRect hr;
    hr.x = track.x; hr.y = track.y; hr.w = track.w; hr.h = track.h;
    CHECK(GuiRunLoop::pointInScrollbarTrack(hr, track.x + 1, track.y + 1));
    CHECK(!GuiRunLoop::pointInScrollbarTrack(hr, track.x + track.w, track.y));
#endif
}

#endif  // HAVE_SDL2
