// ===========================================================================
// P3 — Clipboard separado + RenderDiff/StatusBar a escala.
//   paste 1MB (solo insert) vs deleteRange 1MB (solo delete, informativo).
//   Un Scoped por operacion: no mezclar costos.
//   diff 25k + scroll + resize con gate kRenderDiff; statusbar informativo.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "test_framework.h"
#include "helpers/perf_arch.h"
#include "helpers/perf_limits.h"
#include "helpers/perf_time_utils.h"
#include "helpers/alloc_stats.h"

#define private public
#include "app/Editor.h"
#undef private

#include "document/Document.h"

namespace {

inline std::vector<std::string> makeLines80(int n) {
    std::vector<std::string> out;
    out.reserve(n > 0 ? (size_t)n : 1);
    if (n <= 0) { out.emplace_back(""); return out; }
    for (int i = 0; i < n; ++i) {
        std::string l = "int v" + std::to_string(i) + " = " + std::to_string(i) + "; // ";
        while ((int)l.size() < 80) l += "x";
        l.resize(80);
        out.push_back(l);
    }
    return out;
}

} // namespace

// Solo paste/insert 1MB. Delete/revert fuera del Scoped.
TEST(bench_perf_clipboard_paste1M_checked) {
    perf_arch::reportVerbose("\n== perf_clipboard_paste1M (solo insert) ==\n");
    std::string big(1024 * 1024, 'x'); // 1MB
    auto lines = makeLines80(1000);
    const int iters = 5;
    long long total_ns = 0, total_a = 0, total_b = 0;
    for (int i = 0; i < iters; ++i) {
        Document d;
        d.restore(lines);
        int len = d.lineLength(500);
        alloc_stats::resetAll();
        auto s = std::chrono::steady_clock::now();
        {
            alloc_stats::Scoped sc(alloc_stats::kOther);
            d.insertText(500, len / 2, big);
            perf_time::g_sink += d.lineLength(500);
        }
        auto e = std::chrono::steady_clock::now();
        total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        total_a += st.allocs;
        total_b += st.bytesAllocated;
        // revert fuera de medicion
        d.deleteRange(500, len / 2, 500, len / 2 + (int)big.size());
    }
    perf_arch::reportVerbose("%-48s %6d iters  %8.1f ms/op  %6lld allocs/op\n",
        "paste 1MB insert", iters, (double)total_ns / iters / 1e6, total_a / iters);
    alloc_stats::Stats agg{(unsigned long long)total_a, 0, (unsigned long long)total_b, 0};
    perf_limits::checkAllocBudget(perf_limits::kClipboardPaste, agg, iters, __FILE__, __LINE__);
    CHECK(perf_time::g_sink > 0);
}

// Solo deleteRange 1MB. Informativo: no esconder regresiones UTF-8/limites.
TEST(bench_perf_document_deleteRange1M_perf) {
    perf_arch::reportVerbose("\n== perf_deleteRange1M (solo delete, informativo) ==\n");
    std::string big(1024 * 1024, 'x');
    auto lines = makeLines80(1000);
    const int iters = 5;
    for (int i = 0; i < iters; ++i) {
        Document d;
        d.restore(lines);
        int len = d.lineLength(500);
        d.insertText(500, len / 2, big); // setup fuera
        alloc_stats::resetAll();
        auto s = std::chrono::steady_clock::now();
        {
            alloc_stats::Scoped sc(alloc_stats::kOther);
            bool ok = d.deleteRange(500, len / 2, 500, len / 2 + (int)big.size());
            perf_time::g_sink += ok ? 1 : 0;
        }
        auto e = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / 1000.0;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        perf_arch::reportVerbose("%-48s %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n",
            "deleteRange 1MB", us, st.allocs, st.bytesAllocated);
    }
    CHECK(perf_time::g_sink > 0);
}

// Diff a escala: 25k + scroll + resize, gate kRenderDiff.
TEST(bench_perf_render_diff_25k_checked) {
    perf_arch::reportVerbose("\n== perf_render_diff_25k (diff + scroll + resize) ==\n");
    const int n = 25000;
    auto lines = makeLines80(n);
    Editor ed;
    ed.active().document.restore(lines);
    ed.active().viewport.height = 24;
    ed.active().viewport.width = 80;
    ed.active().viewport.top = n / 2;
    ed.active().cursor.line = n / 2;
    ed.state_ = State::Interaccion;
    Message msg;
    // primar cache con frame completo (fuera de medicion)
    {
        std::string base = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor,
            ed.active().viewport, "bench.cpp", false, msg, ed.state_, std::nullopt);
        perf_time::g_sink += base.size();
    }
    const int iters = 50;
    auto t0 = std::chrono::steady_clock::now();
    alloc_stats::resetAll();
    long long frames = 0;
    {
        alloc_stats::Scoped s(alloc_stats::kRenderFrame);
        for (int i = 0; i < iters; ++i) {
            // tecla que cambia 1 fila (1 frame por tecla, 2 frames por iter)
            Event e;
            e.type = EventType::InsertChar;
            e.text = "a";
            ed.handleEvent(e);
            auto out = ed.renderer_.buildDiffFrame(ed.active().document, ed.active().cursor,
                ed.active().viewport, "bench.cpp", false, msg, ed.state_, std::nullopt);
            perf_time::g_sink += out.size();
            ++frames;
            Event b;
            b.type = EventType::Backspace;
            ed.handleEvent(b);
            auto out2 = ed.renderer_.buildDiffFrame(ed.active().document, ed.active().cursor,
                ed.active().viewport, "bench.cpp", false, msg, ed.state_, std::nullopt);
            perf_time::g_sink += out2.size();
            ++frames;
            // scroll 1 linea cada 10 iters (todas las filas cambian, 1 frame extra)
            if (i % 10 == 9) {
                ed.active().viewport.top += 1;
                ed.active().cursor.line += 1;
                auto outs = ed.renderer_.buildDiffFrame(ed.active().document, ed.active().cursor,
                    ed.active().viewport, "bench.cpp", false, msg, ed.state_, std::nullopt);
                perf_time::g_sink += outs.size();
                ++frames;
            }
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
    // Todo normalizado por frame (no por iter): 1 iter = 2 frames + scroll extra.
    double usPerFrame = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / (double)frames / 1000.0;
    unsigned long long allocsPerFrame = st.allocs / (unsigned long long)frames;
    unsigned long long bytesPerFrame = st.bytesAllocated / (unsigned long long)frames;
    // viewport resize 80 -> 120 (fuera del gate principal, reportado)
    ed.active().viewport.width = 120;
    {
        auto outr = ed.renderer_.buildDiffFrame(ed.active().document, ed.active().cursor,
            ed.active().viewport, "bench.cpp", false, msg, ed.state_, std::nullopt);
        perf_arch::reportVerbose("%-48s %6zu bytes tras resize 120\n", "diff resize", outr.size());
        perf_time::g_sink += outr.size();
    }
    perf_arch::reportVerbose("%-48s %6lld frames  %8.1f us/frame  %6llu allocs/frame  %8llu bytes/frame\n",
        "diff 25k+scroll", frames, usPerFrame, allocsPerFrame, bytesPerFrame);
    // kRenderDiff es por frame: chequear el promedio por frame ya normalizado.
    alloc_stats::Stats perFrame{allocsPerFrame, 0, bytesPerFrame, 0};
    perf_limits::checkAllocBudget(perf_limits::kRenderDiff, perFrame, 1, __FILE__, __LINE__);
    CHECK(perf_time::g_sink > 0);
}

// StatusBar a 25k: informativo (PT: baseline primero, gate despues).
TEST(bench_perf_statusbar_25k_perf) {
    perf_arch::reportVerbose("\n== perf_statusbar_25k (informativo) ==\n");
    const int n = 25000;
    auto lines = makeLines80(n);
    Editor ed;
    ed.active().document.restore(lines);
    ed.active().viewport.height = 24;
    ed.active().viewport.width = 80;
    ed.active().cursor.line = n / 2;
    Renderer& r = ed.renderer_;
    Layout layout = computeLayout(ed.active().viewport.height + kStatusBarRows,
                                  ed.active().viewport.width);
    StatusBarData d;
    d.name = "bench.cpp";
    d.estado = "NAVEGACION";
    d.cursorLine = ed.active().cursor.line;
    d.cursorCol = 0;
    d.totalLines = n;
    const int iters = 2000;
    auto t0 = std::chrono::steady_clock::now();
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped s(alloc_stats::kOther);
        for (int i = 0; i < iters; ++i) {
            std::string out;
            r.renderStatusBar(out, layout.statusBar, d);
            perf_time::g_sink += out.size();
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters / 1000.0;
    auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
    perf_arch::reportVerbose("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n",
        "statusbar 25k", iters, us, st.allocs / (unsigned long long)iters,
        st.bytesAllocated / (unsigned long long)iters);
    CHECK(perf_time::g_sink > 0);
}
