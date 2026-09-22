// ===========================================================================
// P0 — RowLayout en suite (migra bench_RowLayout.cpp standalone).
//
// Separa 4 contratos (no mezclar cold con cache-hit):
//   cold  -> utf8::columnOf scan puro, gate kColumnOfCold (0 allocs)
//   hit   -> reutiliza kColumnOfCacheHit existente (no se redefine)
//   Full / Checkpoint query steady -> kRowLayoutFrame (0 allocs)
//   build 40 lineas -> kRowLayoutBuild (generoso por linea)
//   huge 540KB -> memoria informativa (memoryBytes, sin gate de allocs)
// Construccion fuera del Scoped medido; solo queries adentro.
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

#include "core/RowLayout.h"
#include "core/utf8.h"

namespace {

// Linea con tabs + wide + multibyte para ejercitar el algoritmo Unicode.
inline std::string makeWideTabLine(int cols) {
    const std::string pat = "int x(){ return 42; } // \xC3\xA9\xE2\x80\x94\t";
    std::string s;
    while ((int)s.size() < cols) s += pat;
    s.resize(cols);
    return s;
}

inline std::string makeHugeLine() {
    const std::string pat = "int x(){ return 42; } // \xC3\xA9\xE2\x80\x94\t";
    std::string huge;
    while (huge.size() < 540 * 1024) huge += pat;
    huge.resize(540 * 1024);
    return huge;
}

} // namespace

// --- cold: scan puro sin cache ni tablas ---
TEST(bench_perf_rowlayout_cold_perf) {
    perf_arch::reportVerbose("\n== perf_rowlayout_cold (columnOf tabs+wide, sin cache) ==\n");
    const int cols[] = {1000, 10000, 100000};
    for (int c : cols) {
        std::string line = makeWideTabLine(c);
        const int iters = c <= 10000 ? 200 : 20;
        char label[64];
        std::snprintf(label, sizeof(label), "columnOf cold %6d cols", c);
        long long total_ns = 0, total_a = 0, total_b = 0;
        for (int i = 0; i < iters; ++i) {
            int byte = (i * 7919) % (int)line.size();
            byte = utf8::alignStart(line, byte);
            alloc_stats::resetAll();
            auto s = std::chrono::steady_clock::now();
            {
                alloc_stats::Scoped sc(alloc_stats::kOther);
                volatile int col = utf8::columnOf(line, byte);
                perf_time::g_sink += (size_t)col;
            }
            auto e = std::chrono::steady_clock::now();
            total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
            auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
            total_a += st.allocs;
            total_b += st.bytesAllocated;
        }
        perf_arch::reportVerbose("%-48s %6d iters  %8.1f us/op  %6lld allocs/op  %8lld bytes/op\n",
            label, iters, (double)total_ns / iters / 1000.0, total_a / iters, total_b / iters);
        alloc_stats::Stats agg{(unsigned long long)total_a, 0, (unsigned long long)total_b, 0};
        perf_limits::checkAllocBudget(perf_limits::kColumnOfCold, agg, iters, __FILE__, __LINE__);
    }
    CHECK(perf_time::g_sink > 0);
}

// --- Full query steady: build fuera, solo columnAt/range adentro ---
TEST(bench_perf_rowlayout_full_checked) {
    perf_arch::reportVerbose("\n== perf_rowlayout_full (query steady, build fuera) ==\n");
    std::string line = makeWideTabLine(8000);
    rowlayout::RowLayoutFull rl(line);
    const int iters = 500;
    long long total_ns = 0, total_a = 0, total_b = 0;
    for (int i = 0; i < iters; ++i) {
        int byte = (i * 7919) % (int)line.size();
        byte = utf8::alignStart(line, byte);
        alloc_stats::resetAll();
        auto s = std::chrono::steady_clock::now();
        {
            alloc_stats::Scoped sc(alloc_stats::kOther);
            volatile int c = rl.columnAt(byte);
            auto v = rl.range(0, 80);
            auto e2 = rl.expandVisible(0, 80);
            perf_time::g_sink += (size_t)c + v.size() + e2.size();
        }
        auto e = std::chrono::steady_clock::now();
        total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        // expandVisible devuelve std::string -> 1 alloc esperable por diseno.
        // Para contrato 0-alloc medir solo columnAt+range (sin string).
        total_a += st.allocs;
        total_b += st.bytesAllocated;
    }
    // Gate estricto solo sobre path sin strings: re-medir columnAt+range.
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped sc(alloc_stats::kOther);
        for (int i = 0; i < iters; ++i) {
            int byte = (i * 7919) % (int)line.size();
            byte = utf8::alignStart(line, byte);
            volatile int c = rl.columnAt(byte);
            auto v = rl.range(0, 80);
            perf_time::g_sink += (size_t)c + v.size();
        }
    }
    auto st2 = alloc_stats::statsFor(alloc_stats::kGlobal);
    perf_arch::reportVerbose("%-48s %6d iters  %8.1f us/op (query c/allocs abajo)\n",
        "rowlayout Full query", iters, (double)total_ns / iters / 1000.0);
    perf_arch::reportVerbose("%-48s %6lld allocs/op  %8lld bytes/op (con expandVisible)\n",
        "  con expandVisible", total_a / iters, total_b / iters);
    perf_limits::checkAllocBudget(perf_limits::kRowLayoutFrame, st2, iters, __FILE__, __LINE__);
    CHECK(perf_time::g_sink > 0);
}

// --- Checkpoint query steady ---
TEST(bench_perf_rowlayout_checkpoint_checked) {
    perf_arch::reportVerbose("\n== perf_rowlayout_checkpoint (query steady, build fuera) ==\n");
    std::string line = makeWideTabLine(8000);
    rowlayout::RowLayoutCheckpoint rl(line);
    const int iters = 500;
    alloc_stats::resetAll();
    auto t0 = std::chrono::steady_clock::now();
    {
        alloc_stats::Scoped sc(alloc_stats::kOther);
        for (int i = 0; i < iters; ++i) {
            int byte = (i * 7919) % (int)line.size();
            byte = utf8::alignStart(line, byte);
            volatile int c = rl.columnAt(byte);
            auto v = rl.range(0, 80);
            perf_time::g_sink += (size_t)c + v.size();
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters / 1000.0;
    auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
    perf_arch::reportVerbose("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n",
        "rowlayout Ckpt query", iters, us, st.allocs / (unsigned long long)iters,
        st.bytesAllocated / (unsigned long long)iters);
    perf_limits::checkAllocBudget(perf_limits::kRowLayoutFrame, st, iters, __FILE__, __LINE__);
    CHECK(perf_time::g_sink > 0);
}

// --- build: construccion por linea + huge 540KB ---
TEST(bench_perf_rowlayout_build_checked) {
    perf_arch::reportVerbose("\n== perf_rowlayout_build (40 lineas + huge 540KB) ==\n");
    std::vector<std::string> lines;
    for (int i = 0; i < 40; ++i) lines.push_back(makeWideTabLine(2000 + (i * 137) % 3000));
    const int iters = 50;
    alloc_stats::resetAll();
    auto t0 = std::chrono::steady_clock::now();
    {
        alloc_stats::Scoped sc(alloc_stats::kOther);
        for (int i = 0; i < iters; ++i) {
            for (auto& l : lines) {
                rowlayout::RowLayoutCheckpoint rl(l);
                perf_time::g_sink += (size_t)rl.totalCols();
            }
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters / 1000.0;
    auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
    // por linea construida
    long long builds = (long long)iters * (long long)lines.size();
    perf_arch::reportVerbose("%-48s %6d iters  %8.1f us/frame  %6llu allocs/linea\n",
        "rowlayout Ckpt build 40lin", iters, us, st.allocs / (unsigned long long)builds);
    perf_limits::checkAllocBudget(perf_limits::kRowLayoutBuild, st, builds, __FILE__, __LINE__);

    // huge 540KB: memoria informativa, sin gate de allocs. Contrato
    // estructural: Checkpoint debe usar estrictamente menos memoria que Full.
    std::string huge = makeHugeLine();
    {
        rowlayout::RowLayoutFull f(huge);
        rowlayout::RowLayoutCheckpoint c(huge);
        perf_arch::reportVerbose("%-48s %8.1f KB / %8.1f KB chk=%d\n", "rowlayout huge mem Full/Ckpt",
            f.memoryBytes() / 1024.0, c.memoryBytes() / 1024.0, c.checkpointCount());
        CHECK(c.memoryBytes() < f.memoryBytes());
        CHECK(c.checkpointCount() >= 1);
    }
    CHECK(perf_time::g_sink > 0);
}
