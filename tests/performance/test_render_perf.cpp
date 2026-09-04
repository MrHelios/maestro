// ===========================================================================
// INSTRUMENTACION TEMPORAL: perfil de CPU de renderFrame/buildScreen.
//
// La medicion de allocations mostro que el scroll no asigna memoria y que el
// costo pendiente esta en el render completo por evento. Aca medimos TIEMPO:
//
//   1. Escalado: buildScreen con documentos de 300 / 3k / 30k lineas y
//      viewport fijo. Si el tiempo crece con el documento => hay trabajo
//      O(documento); si queda plano => es O(viewport) y esta bien.
//   2. Desglose por fase: beginFrame/endFrame, layout, contenido
//      (renderEditorContent), status bar y posicion de cursor, medidos
//      por separado contra el total de buildScreen.
//   3. Ciclo real de tecleo: handleEvent + buildScreen por tecla.
//   4. Bytes por evento: frame completo vs render diferencial (buildDiffFrame)
//      - mide cuanto debe reprocesar el emulador por tecla/scroll.
//
// No verifica comportamiento: imprime numeros para decidir SI conviene
// optimizar y DONDE.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "test_framework.h"
#include "helpers/perf_time_utils.h"
#include "helpers/test_render_utils.h"
#include "helpers/perf_helpers.h"

#define private public
#include "ui/Editor.h"
#undef private

#include "core/utf8.h"

namespace {

using perf_time::g_sink;

struct RenderFixture {
    Editor ed;
    Message msg;

    explicit RenderFixture(int lines, int width = 80) {
        ed.active().document.restore(perf_helpers::makeLines(lines, width));
        ed.active().cursor.line = lines / 2;
        ed.active().viewport.top = std::max(0, lines / 2 - 5);
        ed.active().viewport.height = 24;
        ed.active().viewport.width = 80;
    }

    std::string frame() {
        return ed.renderer_.buildScreen(ed.active().document,
                                        ed.active().cursor,
                                        ed.active().viewport, "perf.txt", false,
                                        msg, State::Navegacion, std::nullopt);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// 1. Escala con el TAMANO DEL DOCUMENTO o solo con el viewport?
// ---------------------------------------------------------------------------
TEST(perf_render_escalado_con_documento) {
    std::printf("\n== perf: buildScreen vs tamanio del documento ==\n");
    const int frames[] = {2000, 1000, 200};
    const int sizes[] = {300, 3000, 30000};
    for (int i = 0; i < 3; ++i) {
        RenderFixture fx(sizes[i]);
        perf_time::bench_ns("doc", frames[i], [&fx] {
            perf_time::g_sink += fx.frame().size();
        });
    }
    CHECK(perf_time::g_sink > 0);
}

// ---------------------------------------------------------------------------
// 2. Desglose: cuanto tarda cada fase dentro de buildScreen?
// ---------------------------------------------------------------------------
TEST(perf_render_desglose_fases) {
    RenderFixture fx(300);
    Renderer& r = fx.ed.renderer_;
    const Buffer& b = fx.ed.active();
    const Document& doc = b.document;
    const Cursor& cur = b.cursor;
    const Viewport& vp = b.viewport;

    const int gutterW = std::min(testutil::gutterWidth(300), vp.width);
    const Layout layout = computeLayout(vp.height + kStatusBarRows, vp.width);

    // Espejo de editorBarData (Renderer.cpp, internal linkage): los campos
    // que la barra realmente pinta. La costura es trivial; lo que se mide
    // es StatusBar::render.
    auto barData = [&] {
        StatusBarData d;
        d.name = "perf.txt";
        d.path = "";
        d.estado = "NAVEGACION";
        d.modified = false;
        d.message = fx.msg;
        d.cursorLine = cur.line;
        d.cursorCol = cur.col;
        d.totalLines = doc.lineCount();
        return d;
    };

    std::printf("\n== perf: desglose de buildScreen (300x80, viewport 24x80) ==\n");
    const double total = perf_time::bench_ns("buildScreen TOTAL", 2000, [&fx] {
        perf_time::g_sink += fx.frame().size();
    });

    auto bench = [&](const char* name, int frames, const std::function<void()>& fn) {
        perf_time::bench_with_total(name, frames, total, fn);
    };

    bench("beginFrame+endFrame", 20000, [&] {
        std::string out;
        r.beginFrame(out);
        r.endFrame(out);
        perf_time::g_sink += out.size();
    });
    bench("renderEditorContent (22 filas visibles)", 2000, [&] {
        std::string out;
        r.renderEditorContent(out, doc, cur, vp, std::nullopt, layout.content,
                              gutterW);
        perf_time::g_sink += out.size();
    });
    bench("statusBar (data+render)", 20000, [&] {
        std::string out;
        StatusBarData data = barData();
        r.renderStatusBar(out, layout.statusBar, data);
        perf_time::g_sink += out.size();
    });
    bench("moveCursorTo+columnOf", 20000, [&] {
        std::string out;
        int visualCol = utf8::columnOf(doc.lineAt(cur.line), cur.col);
        r.moveCursorTo(out, cur.line - vp.top + 1, gutterW + visualCol + 1 +
                                                     layout.content.col);
        perf_time::g_sink += out.size();
    });
    CHECK(perf_time::g_sink > 0);
}

// ---------------------------------------------------------------------------
// 3. El ciclo que le importa al usuario: tecla -> estado -> frame completo.
// ---------------------------------------------------------------------------
TEST(perf_ciclo_tecla_mas_frame) {
    std::printf("\n== perf: ciclo tecleo real (handleEvent + buildScreen) ==\n");
    RenderFixture fx(300);
    fx.ed.state_ = State::Interaccion;

    Event e;
    e.type = EventType::InsertChar;
    e.text = "a";
    int keys = 0;
    const double us = [&] {
        const auto s = std::chrono::steady_clock::now();
        while (keys < 2000) {
            fx.ed.handleEvent(e);
            perf_time::g_sink += fx.frame().size();
            ++keys;
        }
        const auto en = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(en - s)
                   .count() /
               1000.0 / keys;
    }();
    std::printf("%-46s %8.1f us por tecla (%d teclas)\n",
                "handleEvent+buildScreen", us, keys);
    CHECK_EQ(keys, 2000);
}

// ---------------------------------------------------------------------------
// 4. Bytes por EVENTO hacia la terminal: lo que el emulador debe re-procesar.
//    Compara el frame completo actual contra lo que costaria un render
//    diferencial (solo las filas que realmente cambiaron).
// ---------------------------------------------------------------------------
TEST(perf_bytes_por_evento_hacia_terminal) {
    std::printf("\n== perf: bytes por evento hacia la terminal ==\n");
    RenderFixture fx(300);
    fx.ed.state_ = State::Interaccion;

    // frame() construye el frame completo y deja el cache interno preparado.
    // No manipulamos rowCache_/statusCache_ directamente: el benchmark debe
    // depender de la API real del Renderer, no de su implementacion interna.
    const std::string base = fx.frame();
    // Nota: buildDiffFrame se mide aqui pero el ciclo real (perf_ciclo_tecla)
    // aun usa buildScreen; el diff es feature medida pero no integrada.

    Event e;
    e.type = EventType::InsertChar;
    e.text = "a";

    // Caso A: una tecla que solo cambia UNA fila.
    fx.ed.handleEvent(e);
    const std::string trasTecla = fx.frame();
    const std::size_t deltaTecla = fx.ed.renderer_
        .buildDiffFrame(fx.ed.active().document, fx.ed.active().cursor,
                        fx.ed.active().viewport, "perf.txt", false, fx.msg,
                        State::Navegacion, std::nullopt)
        .size();

    // Caso B: scroll de una linea (cursor al borde inferior -> viewport.top++),
    // cambian TODAS las filas visibles aunque el texto es el mismo desplazado.
    fx.ed.active().cursor.line += 100;
    fx.ed.active().viewport.top += 1;
    const std::string trasScroll = fx.frame();
    const std::size_t deltaScroll = fx.ed.renderer_
        .buildDiffFrame(fx.ed.active().document, fx.ed.active().cursor,
                        fx.ed.active().viewport, "perf.txt", false, fx.msg,
                        State::Navegacion, std::nullopt)
        .size();

    auto changedRows = [](const std::string& a, const std::string& b) {
        int rows = 0, rowStart = 0;
        for (int i = 0; i <= static_cast<int>(std::min(a.size(), b.size())); ++i) {
            if (i == static_cast<int>(a.size()) ||
                (i < static_cast<int>(a.size()) && a[i] == '\r')) {
                if (i >= static_cast<int>(b.size()) ||
                    b.substr(rowStart, i - rowStart) !=
                        a.substr(rowStart, i - rowStart)) {
                    ++rows;
                }
                rowStart = i + 2;
            }
        }
        return rows;
    };

    std::printf("%-46s %6zu bytes/frame\n", "frame completo (camino viejo)",
                base.size());
    std::printf("%-46s %6zu bytes (diff)\n",
                "tecla 'a': ANTES vs AHORA", deltaTecla);
    std::printf("%-46s %6d filas / %6zu -> %zu bytes\n",
                "scroll 1 linea: filas / antes -> ahora",
                changedRows(base, trasScroll), trasScroll.size(), deltaScroll);
    CHECK(!base.empty());
}
