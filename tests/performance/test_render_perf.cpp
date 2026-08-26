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

#define private public
#include "ui/Editor.h"
#undef private

#include "core/utf8.h"

namespace {

std::size_t g_sink = 0; // evita que el compilador elimine el trabajo medido

double nsPerFrame(const char* label, int frames, const std::function<void()>& fn) {
    fn(); // warmup (paginas, cache de strings, etc.)
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) fn();
    const auto end = std::chrono::steady_clock::now();
    const double ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    const double per = ns / frames;
    std::printf("%-46s %8.1f us/frame  (%d frames)\n", label, per / 1000.0, frames);
    return per;
}

struct RenderFixture {
    Editor ed;
    Message msg;

    explicit RenderFixture(int lines, int width = 80) {
        ed.active().document.restore(
            std::vector<std::string>(static_cast<size_t>(lines),
                                     std::string(static_cast<size_t>(width), 'x')));
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
        nsPerFrame("doc", frames[i], [&fx] {
            g_sink += fx.frame().size();
        });
    }
    CHECK(g_sink > 0);
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

    const int gutterW = std::min(std::max(3, 4), vp.width); // gutterWidth(300)=4
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
    const double total = nsPerFrame("buildScreen TOTAL", 2000, [&fx] {
        g_sink += fx.frame().size();
    });

    auto bench = [&](const char* name, int frames, const std::function<void()>& fn) {
        fn();
        const auto s = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i) fn();
        const auto e = std::chrono::steady_clock::now();
        const double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count());
        std::printf("%-46s %8.1f us  (%4.1f%% del total)\n", name, ns / frames / 1000.0,
                    100.0 * ns / frames / total);
    };

    bench("beginFrame+endFrame", 20000, [&] {
        std::string out;
        r.beginFrame(out);
        r.endFrame(out);
        g_sink += out.size();
    });
    bench("renderEditorContent (22 filas visibles)", 2000, [&] {
        std::string out;
        r.renderEditorContent(out, doc, cur, vp, std::nullopt, layout.content,
                              gutterW);
        g_sink += out.size();
    });
    bench("statusBar (data+render)", 20000, [&] {
        std::string out;
        StatusBarData data = barData();
        r.renderStatusBar(out, layout.statusBar, data);
        g_sink += out.size();
    });
    bench("moveCursorTo+columnOf", 20000, [&] {
        std::string out;
        int visualCol = utf8::columnOf(doc.lineAt(cur.line), cur.col);
        r.moveCursorTo(out, cur.line - vp.top + 1, gutterW + visualCol + 1 +
                                                     layout.content.col);
        g_sink += out.size();
    });
    CHECK(g_sink > 0);
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
            g_sink += fx.frame().size();
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

    // Frame base (pantalla ya dibujada) + cache del render diferencial.
    const std::string base = fx.frame();
    fx.ed.renderer_.lastEditorBody_ = fx.ed.renderer_.buildEditorBody(
        fx.ed.active().document, fx.ed.active().cursor,
        fx.ed.active().viewport, "perf.txt", false, fx.msg, State::Navegacion,
        std::nullopt);
    fx.ed.renderer_.hasLastEditorBody_ = true;
    fx.ed.renderer_.lastViewportW_ = 80;
    fx.ed.renderer_.lastViewportH_ = 24;

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
