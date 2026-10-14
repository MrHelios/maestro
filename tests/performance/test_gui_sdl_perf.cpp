// ===========================================================================
// PERF GUI/SDL: coste REAL de pintar (framebuffer + texturas + composite).
//
// test_gui_perf.cpp mide la GUI sin SDL: construir snapshots + calcular el
// diferencial. Eso sirve para estudiar ese coste, pero NO es evidencia del
// rendimiento real: no pinta filas, no crea texturas, no copia al destino de
// ventana ni presenta. Este archivo mide el camino SDL completo con renderer
// software headless (dummy): Frame + snapshot + diff + pintado (lleno o
// parcial) sobre la textura persistente + copia a ventana + Present.
//
// Los tiempos son INFORMATIVOS (dependen del hardware/driver; dummy+software
// no representa una GPU real, pero full-vs-parcial sí es comparable porque
// solo cambia cuánto se pinta). Los gates son estructurales (filas
// repintadas, independientes de la máquina), igual que en test_gui_perf.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "test_framework.h"
#include "helpers/perf_arch.h"
#include "helpers/perf_time_utils.h"

#include "app/ChromePresentation.h"
#include "base/utf8.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "layout/Viewport.h"
#include "rendering/Sink.h"
#include "rendering/gui/GuiRenderer.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

#include <cstdlib>
#endif

namespace {

using perf_time::g_sink;

struct NullSinkGui : public Sink {
    bool writeStdout(const std::string&) override { return true; }
};

// Líneas C++ realistas: fragmentan cada fila en varios segmentos (keyword,
// type, number, comment) como en uso real, en vez de una sola run de 'x'.
// Eso estresa el caché de texturas igual que el editor de verdad.
std::vector<std::string> makeCppLines(int n) {
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        out.push_back("int var" + std::to_string(i) + " = " + std::to_string(i) +
                      "; // comentario " + std::to_string(i));
    return out;
}

#ifdef HAVE_SDL2
struct SdlBench {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    bool ok = false;
    std::string prevVideo;
    std::string prevAudio;
    bool hadVideo = false;
    bool hadAudio = false;
    SdlBench(int w = 800, int h = 600) {
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
        win = SDL_CreateWindow("maestro-sdl-perf", 0, 0, w, h, SDL_WINDOW_HIDDEN);
        if (!win) return;
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
        ok = (ren != nullptr);
    }
    ~SdlBench() {
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

struct SdlPerfFixture {
    Document doc;
    Cursor cursor;
    Viewport viewport;
    Message msg;
    GuiRenderer gui;
    NullSinkGui sink;

    SdlPerfFixture() {
        doc.restore(makeCppLines(300));
        cursor.line = 150;
        cursor.col = 0;
        viewport.top = 145;
        viewport.left = 0;
        viewport.height = 24;
        viewport.width = 80;
    }

    ChromeRequest navChrome() {
        return makeChromeRequest(msg, State::Navegacion);
    }

    // Un frame editor completo (construye Frame + snapshot + diff + pinta).
    // Devuelve filas repintadas para acumular en g_sink (anti-optimización).
    int frame(const char* filename = "perf.cpp") {
        gui.renderScreenDiff(doc, cursor, viewport, filename, false,
                             navChrome(), sink, std::nullopt, std::nullopt,
                             std::nullopt);
        int r = gui.lastRepainted();
        g_sink += static_cast<size_t>(r) + gui.lastContentRows().size();
        return r;
    }
};
#endif

}  // namespace

// Camino SDL completo: full (invalidate por frame) vs steady (0 filas).
TEST(bench_perf_gui_sdl_full_vs_steady_checked) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlBench sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    SdlPerfFixture fx;
    fx.gui.initForRenderer(sdl.ren);
    perf_arch::reportVerbose("\n== perf_gui_sdl: full vs steady (camino real) ==\n");
    perf_arch::reportVerbose("font: %s\n",
                             fx.gui.hasFont() ? fx.gui.fontPath().c_str()
                                              : "(degradado, solo fills)");
    // Primar cachés (sintaxis + texturas) fuera de la medición.
    fx.frame();
    fx.frame();
    CHECK(!fx.gui.lastWasFull());

    perf_time::bench_ns("sdl full (RenderClear+todo+composite)", 200, [&] {
        fx.gui.invalidateCache();
        fx.frame();
    });
    // Tras el loop de fulls, un steady debe seguir dando 0 filas: el
    // framebuffer persiste entre Presents (si no, este CHECK caería y los
    // píxeles delatarían imagen vieja, ver test_gui_diff).
    fx.frame();
    CHECK(!fx.gui.lastWasFull());
    CHECK_EQ(fx.gui.lastRepainted(), 0);
    perf_time::bench_ns("sdl steady (0 filas+composite)", 2000,
                        [&] { fx.frame(); });
    CHECK_EQ(fx.gui.lastRepainted(), 0);
    CHECK(g_sink > 0);
#endif
}

// Navegación típica: mover el cursor 1 línea por frame (2 filas CurrentLine
// + chrome por la posición). Camino SDL real con hits en el caché de
// texturas (las filas no tocadas no pagan TTF_Render).
TEST(bench_perf_gui_sdl_cursor_move_checked) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlBench sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    SdlPerfFixture fx;
    fx.gui.initForRenderer(sdl.ren);
    perf_arch::reportVerbose("\n== perf_gui_sdl: cursor move 1 linea/frame ==\n");
    fx.frame();
    fx.frame();
    int dir = 1;
    perf_time::bench_ns("sdl cursor-move (<=3 filas+chrome)", 1000, [&] {
        fx.cursor.line += dir;
        if (fx.cursor.line > 155 || fx.cursor.line < 145) dir = -dir;
        const int r = fx.frame();
        if (r > 3) {
            std::printf("repaint inesperado: %d\n", r);
            CHECK(r <= 3);
        }
    });
    CHECK(g_sink > 0);
#endif
}

// Scroll típico ↑/↓ en borde: viewport.top ±1 con el cursor acompañando.
// Camino SDL real con scroll-blit (ping-pong: copia desplazada + O(1) filas
// dirty + chrome + cursor). Los tiempos son informativos; los gates son
// estructurales: sin fulls en la sesión, repintadas O(1) por frame, caché
// acotado sin flush total, y misses/evicciones acotados por frame (puñado de
// rasters de la fila entrante + gutter, no cientos).
TEST(bench_perf_gui_sdl_scroll_checked) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlBench sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    SdlPerfFixture fx;
    fx.gui.initForRenderer(sdl.ren);
    perf_arch::reportVerbose("\n== perf_gui_sdl: scroll +-1 con cursor ==\n");
    perf_arch::reportVerbose("font: %s\n",
                             fx.gui.hasFont() ? fx.gui.fontPath().c_str()
                                              : "(degradado, solo fills)");
    fx.frame();
    fx.frame();
    // Prima cachés (sintaxis + texturas) fuera de la medición.
    // Rango amplio (top 20..240, ~220 líneas × segmentos únicos) para superar
    // las 512 entradas y ejercitar la evicción LRU, no solo los hits.
    int dir = 1;
    fx.viewport.top = 20;
    fx.cursor.line = 25;
    for (int i = 0; i < 250; ++i) {
        fx.viewport.top += dir;
        fx.cursor.line += dir;
        if (fx.viewport.top > 240 || fx.viewport.top < 20) {
            dir = -dir;
            fx.viewport.top += 2 * dir;
            fx.cursor.line += 2 * dir;
        }
        fx.frame();
    }
    CHECK_EQ(fx.gui.totalFull(), 1);  // ningún full en la sesión
    CHECK_EQ(fx.gui.fontCacheSize(), fx.gui.fontMaxCacheSize());
    const unsigned long long m0 = fx.gui.fontCacheMisses();
    const unsigned long long e0 = fx.gui.fontCacheEvictions();
    perf_time::bench_ns("sdl scroll +-1 (blit O1 + chrome)", 400, [&] {
        fx.viewport.top += dir;
        fx.cursor.line += dir;
        if (fx.viewport.top > 240 || fx.viewport.top < 20) {
            dir = -dir;
            fx.viewport.top += 2 * dir;
            fx.cursor.line += 2 * dir;
        }
        const int r = fx.frame();
        if (r > 5) {
            std::printf("repaint inesperado: %d\n", r);
            CHECK(r <= 5);
        }
    });
    const double missPerFrame =
        static_cast<double>(fx.gui.fontCacheMisses() - m0) / 400.0;
    const double evictPerFrame =
        static_cast<double>(fx.gui.fontCacheEvictions() - e0) / 400.0;
    perf_arch::reportVerbose(
        "%-46s %6.1f misses/frame %6.1f evict/frame (cache %zu/%zu)\n",
        "sdl scroll raster", missPerFrame, evictPerFrame,
        fx.gui.fontCacheSize(), fx.gui.fontMaxCacheSize());
    CHECK_EQ(fx.gui.totalFull(), 1);
    CHECK_EQ(fx.gui.fontCacheSize(), fx.gui.fontMaxCacheSize());
    // Entrante + gutter + chrome de posición: puñado de rasters, no cientos
    // (con el viejo flush total habría picos de 512+misses por frame).
    CHECK(missPerFrame <= 12.0);
    CHECK(evictPerFrame <= 12.0);
    CHECK(g_sink > 0);
#endif
}

// Prompt con input (Bar en el MessageBar): 0 filas de contenido pero chrome
// por frame (animación/borrado de la barra, ver P2). Mide el coste del
// chrome solo en el camino SDL real.
TEST(bench_perf_gui_sdl_prompt_chrome_checked) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlBench sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    SdlPerfFixture fx;
    fx.gui.initForRenderer(sdl.ren);
    perf_arch::reportVerbose("\n== perf_gui_sdl: prompt con Bar (chrome/frame) ==\n");
    Message prompt{std::string("Find: var15"), MessageKind::Prompt, std::nullopt,
                   11, 6};
    ChromeRequest cr = makeChromeRequest(prompt, State::Busqueda);
    // Primar fuera de la medición.
    fx.gui.renderScreenDiff(fx.doc, fx.cursor, fx.viewport, "perf.cpp", false,
                            cr, fx.sink, std::nullopt, std::nullopt,
                            std::nullopt);
    perf_time::bench_ns("sdl prompt steady (0 filas+chrome)", 1000, [&] {
        fx.gui.renderScreenDiff(fx.doc, fx.cursor, fx.viewport, "perf.cpp",
                                false, cr, fx.sink, std::nullopt, std::nullopt,
                                std::nullopt);
        g_sink += static_cast<size_t>(fx.gui.lastRepainted()) +
                  fx.gui.lastContentRows().size();
    });
    CHECK(!fx.gui.lastWasFull());
    CHECK_EQ(fx.gui.lastRepainted(), 0);
    CHECK(g_sink > 0);
#endif
}
