// ===========================================================================
// PERF GUI vs TTY: caché por línea + repintado parcial de GuiRenderer.
//
// Mide la GUI (sin SDL: decisión del diff + snapshots) contra los benchmarks
// TTY actuales (TtyRenderer::buildScreen / buildDiffFrame, ver
// test_render_perf.cpp). Para el coste REAL de pintar (filas, texturas,
// copia a ventana, Present) ver test_gui_sdl_perf.cpp: este archivo NO mide
// eso y sus tiempos no son evidencia de mejora real, solo del coste de
// snapshots+diff.
//
//   1. gui_cache_steady: pantalla quieta -> 0 filas repintadas (TTY diff:
//      frame casi vacío, solo cursor).
//   2. gui_cache_cursor_move: mover cursor -> <=3 filas (TTY: fast path de
//      1-2 filas, buildCursorMoveFrame).
//   3. gui_cache_scroll: scroll 1 línea -> N filas dirty (DOCUMENTA el gap:
//      TTY usa scroll de región, la GUI aún no hace blit; el ahorro GUI ahí
//      es solo el caché de texturas).
//   4. gui_vs_tty_tiempos: us/frame de buildScreen vs buildDiffFrame steady
//      vs renderScreenDiff steady (sin gates duros de tiempo: dependen del
//      hardware; los gates son de FILAS, independientes de la máquina).
//
// Los gates de filas son estructurales: si el diff repinta de más, falla en
// cualquier máquina.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "test_framework.h"
#include "helpers/perf_arch.h"
#include "helpers/perf_helpers.h"
#include "helpers/perf_time_utils.h"

#define private public
#include "app/Editor.h"
#include "app/ChromePresentation.h"
#undef private

#include "rendering/gui/GuiRenderer.h"
#include "rendering/tty/TtyRenderer.h"

namespace {

using perf_time::g_sink;

struct GuiPerfFixture {
    struct NullSinkForGui : public Sink {
        bool writeStdout(const std::string&) override { return true; }
    };

    Document doc;
    Cursor cursor;
    Viewport viewport;
    Message msg;
    GuiRenderer gui;
    TtyRenderer tty;
    NullSinkForGui sink;

    explicit GuiPerfFixture(int lines, int width = 80, int vpH = 24) {
        doc.restore(perf_helpers::makeLines(lines, width));
        cursor.line = lines / 2;
        cursor.col = 0;
        viewport.top = lines / 2 - 5;
        if (viewport.top < 0) viewport.top = 0;
        viewport.left = 0;
        viewport.height = vpH;
        viewport.width = 80;
    }

    ChromeRequest chrome(State st = State::Navegacion) {
        return makeChromeRequest(msg, st);
    }

    void guiFrame() {
        gui.renderScreenDiff(doc, cursor, viewport, "perf.txt", false,
                             chrome(), sink, std::nullopt, std::nullopt,
                             std::nullopt);
        g_sink += gui.lastContentRows().size();
    }

    std::string ttyFrame() { return tty.buildScreen(doc, cursor, viewport,
                                                    "perf.txt", false, chrome(),
                                                    std::nullopt); }
    std::string ttyDiff(State st = State::Navegacion) {
        return tty.buildDiffFrame(doc, cursor, viewport, "perf.txt", false,
                                  makeChromeRequest(msg, st), std::nullopt);
    }
};

}  // namespace

TEST(bench_perf_gui_cache_steady_checked) {
    perf_arch::reportVerbose("\n== perf_gui: steady (pantalla quieta) ==\n");
    GuiPerfFixture fx(300);
    fx.guiFrame();  // full
    CHECK(fx.gui.lastWasFull());
    const int contentH = static_cast<int>(fx.gui.lastContentRows().size());
    CHECK_EQ(fx.gui.lastRepainted(), contentH);
    fx.guiFrame();  // steady
    CHECK(!fx.gui.lastWasFull());
    perf_arch::reportVerbose("%-46s %6d repintadas / %6d reutilizadas\n",
                             "gui steady", fx.gui.lastRepainted(),
                             fx.gui.lastSkipped());
    CHECK_EQ(fx.gui.lastRepainted(), 0);
    CHECK_EQ(fx.gui.lastSkipped(), contentH);
    CHECK(!fx.gui.lastChromeDirty());
    CHECK(g_sink > 0);
}

TEST(bench_perf_gui_cache_cursor_move_checked) {
    perf_arch::reportVerbose("\n== perf_gui: mover cursor (1 linea) ==\n");
    GuiPerfFixture fx(300);
    fx.guiFrame();
    (void)fx.ttyDiff();  // prima el caché del TTY (full) antes del cambio
    fx.cursor.line += 1;  // CurrentLine cambia en 2 filas (+ chrome por cursor)
    fx.guiFrame();
    CHECK(!fx.gui.lastWasFull());
    perf_arch::reportVerbose("%-46s %6d repintadas / %6d reutilizadas\n",
                             "gui cursor-move", fx.gui.lastRepainted(),
                             fx.gui.lastSkipped());
    CHECK(fx.gui.lastRepainted() <= 3);
    CHECK(fx.gui.lastRepainted() >= 1);
    // Referencia TTY: el fast path mueve el cursor con 1-2 patches de fila.
    const std::string moveBytes = fx.ttyDiff();
    const std::string fullBytes = fx.ttyFrame();
    perf_arch::reportVerbose("%-46s %6zu bytes (diff) vs %6zu (full)\n",
                             "tty cursor-move", moveBytes.size(), fullBytes.size());
    CHECK(moveBytes.size() < fullBytes.size());
    CHECK(g_sink > 0);
}

TEST(bench_perf_gui_cache_scroll_checked) {
    perf_arch::reportVerbose("\n== perf_gui: scroll 1 linea (blit) ==\n");
    GuiPerfFixture fx(300);
    fx.guiFrame();
    (void)fx.ttyDiff();  // prima el caché del TTY (full) antes del cambio
    fx.viewport.top += 1;
    fx.cursor.line += 1;
    fx.guiFrame();
    const int contentH = static_cast<int>(fx.gui.lastContentRows().size());
    (void)contentH;
    // Con scroll-blit (ping-pong + filas dirty), el scroll ±1 es O(1):
    // entrante + CurrentLine vieja/nueva (+ cursor, que suele coincidir).
    // El diff por índice ingenuo quedó solo como fallback (saltos grandes,
    // edición, selección/búsqueda cambiante). Sin SDL no hay blit de
    // píxeles, pero las stats del diferencial son las mismas (O(1)).
    perf_arch::reportVerbose("%-46s %6d repintadas / %6d totales\n",
                             "gui scroll-top+1", fx.gui.lastRepainted(),
                             contentH);
    CHECK(fx.gui.lastRepainted() <= 5);
    CHECK(fx.gui.lastRepainted() >= 1);
    // TTY sí lo optimiza: scroll de región + filas entrantes.
    const std::string scrollBytes = fx.ttyDiff();
    const std::string fullBytes = fx.ttyFrame();
    perf_arch::reportVerbose("%-46s %6zu bytes (diff) vs %6zu (full)\n",
                             "tty scroll-top+1", scrollBytes.size(),
                             fullBytes.size());
    CHECK(scrollBytes.size() < fullBytes.size());
    CHECK(g_sink > 0);
}

TEST(bench_perf_gui_vs_tty_tiempos_checked) {
    perf_arch::reportVerbose("\n== perf_gui vs tty: us/frame (informativo) ==\n");
    GuiPerfFixture fx(300);
    // Primar ambos cachés.
    fx.guiFrame();
    (void)fx.ttyDiff();
    perf_time::bench_ns("tty buildScreen (full)", 500,
                        [&] { g_sink += fx.ttyFrame().size(); });
    perf_time::bench_ns("tty buildDiffFrame (steady)", 2000,
                        [&] { g_sink += fx.ttyDiff().size(); });
    perf_time::bench_ns("gui renderScreenDiff (steady, sin SDL)", 2000,
                        [&] { fx.guiFrame(); });
    // Tras 2000 steadys la GUI no debe haber hecho ningún full extra.
    CHECK_EQ(fx.gui.totalFull(), 1);
    CHECK_EQ(fx.gui.lastRepainted(), 0);
    CHECK(g_sink > 0);
}
