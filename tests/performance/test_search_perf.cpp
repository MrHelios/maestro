// ===========================================================================
// P2 — Search: dos metricas explicitas.
//   cold  -> full-scan O(N), solo informativo (*_perf, sin gate).
//   steady-> query raro repetido, allocs independientes de N (*_checked).
// Tiempo se reporta; gate solo sobre allocs/bytes.
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

namespace {

inline std::vector<std::string> makeSearchLines(int n) {
    std::vector<std::string> out;
    out.reserve(n > 0 ? (size_t)n : 1);
    if (n <= 0) { out.emplace_back(""); return out; }
    for (int i = 0; i < n; ++i) {
        std::string l = "int var" + std::to_string(i % 1000) + " = " + std::to_string(i) + "; // ";
        while ((int)l.size() < 80) l += "x";
        l.resize(80);
        out.push_back(l);
    }
    return out;
}

} // namespace

// Cold: recorre N lineas, O(N). Informativo, sin gate duro.
TEST(bench_perf_search_cold_perf) {
    perf_arch::reportVerbose("\n== perf_search_cold (full-scan, informativo) ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeSearchLines(n);
        Editor ed;
        ed.active().document.restore(lines);
        const std::string q = "var500"; // ~N/1000 matches
        int iters = n == 1000 ? 20 : 5;
        char label[64];
        std::snprintf(label, sizeof(label), "search cold %5d", n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kOther);
            for (int i = 0; i < iters; ++i) {
                auto m = ed.collectMatches(q);
                perf_time::g_sink += m.size() + 1;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters / 1000.0;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        perf_arch::reportVerbose("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n",
            label, iters, us, st.allocs / (unsigned long long)iters,
            st.bytesAllocated / (unsigned long long)iters);
    }
    CHECK(perf_time::g_sink > 0);
}

// Steady: query raro (0 matches), vector vacio -> allocs ~constantes con N.
TEST(bench_perf_search_steady_checked) {
    perf_arch::reportVerbose("\n== perf_search_steady (0 matches, gateado) ==\n");
    const int sizes[] = {1000, 10000, 25000};
    unsigned long long baseAllocs = 0;
    for (int si = 0; si < 3; ++si) {
        int n = sizes[si];
        auto lines = makeSearchLines(n);
        Editor ed;
        ed.active().document.restore(lines);
        const std::string q = "zzz_no_match_123";
        int iters = 100;
        char label[64];
        std::snprintf(label, sizeof(label), "search steady %5d", n);
        long long total_ns = 0, total_a = 0, total_b = 0;
        for (int i = 0; i < iters; ++i) {
            alloc_stats::resetAll();
            auto s = std::chrono::steady_clock::now();
            {
                alloc_stats::Scoped sc(alloc_stats::kOther);
                auto m = ed.collectMatches(q);
                perf_time::g_sink += m.size() + 1;
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
        perf_limits::checkAllocBudget(perf_limits::kSearchSteady, agg, iters, __FILE__, __LINE__);
        unsigned long long perOp = (unsigned long long)total_a / (unsigned long long)iters;
        if (si == 0) baseAllocs = perOp;
        // Escalabilidad: no crecer con N (margen +2 como kBracketHighlightSteady).
        ::testfw::report(perOp <= baseAllocs + 2,
            std::string("search steady escalabilidad N=") + std::to_string(n) +
                " allocs/op " + std::to_string(perOp) + " <= base+2=" + std::to_string(baseAllocs + 2),
            __FILE__, __LINE__);
    }
    CHECK(perf_time::g_sink > 0);
}
