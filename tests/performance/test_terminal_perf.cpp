// ===========================================================================
// P5 — Terminal/Keymap decode sin allocs repetitivas.
// Solo lookups (control + sequence); strings construidos fuera del Scoped.
// readEvent bloqueante no se benchea.
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

#include "platform/tty/Keymap.h"

TEST(bench_perf_terminal_decode_checked) {
    perf_arch::reportVerbose("\n== perf_terminal_decode (Keymap lookup, 0 allocs) ==\n");
    Keymap km; // defaults fuera de medicion
    // Secuencias pre-construidas fuera del Scoped (evita costo artificial).
    const std::vector<std::string> seqs = {
        "[A", "[B", "[C", "[D", "[1;2C", "[1;5D", "3~", "OH", "OF", "[200~",
    };
    const std::vector<unsigned char> ctrls = {13, 127, 9, 27, 1, 5, 11, 14};
    const int iters = 2000;

    // --- sequences ---
    {
        long long total_ns = 0, total_a = 0, total_b = 0;
        for (int i = 0; i < iters; ++i) {
            const std::string& q = seqs[(size_t)i % seqs.size()];
            alloc_stats::resetAll();
            auto s = std::chrono::steady_clock::now();
            {
                alloc_stats::Scoped sc(alloc_stats::kOther);
                auto r = km.sequence(q);
                perf_time::g_sink += r.has_value() ? 1 : 0;
            }
            auto e = std::chrono::steady_clock::now();
            total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
            auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
            total_a += st.allocs;
            total_b += st.bytesAllocated;
        }
        perf_arch::reportVerbose("%-48s %6d iters  %8.1f us/op  %6lld allocs/op\n",
            "keymap sequence", iters, (double)total_ns / iters / 1000.0, total_a / iters);
        alloc_stats::Stats agg{(unsigned long long)total_a, 0, (unsigned long long)total_b, 0};
        perf_limits::checkAllocBudget(perf_limits::kTerminalDecode, agg, iters, __FILE__, __LINE__);
    }
    // --- controls ---
    {
        long long total_ns = 0, total_a = 0, total_b = 0;
        for (int i = 0; i < iters; ++i) {
            unsigned char q = ctrls[(size_t)i % ctrls.size()];
            alloc_stats::resetAll();
            auto s = std::chrono::steady_clock::now();
            {
                alloc_stats::Scoped sc(alloc_stats::kOther);
                auto r = km.control(q);
                perf_time::g_sink += r.has_value() ? 1 : 0;
            }
            auto e = std::chrono::steady_clock::now();
            total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
            auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
            total_a += st.allocs;
            total_b += st.bytesAllocated;
        }
        perf_arch::reportVerbose("%-48s %6d iters  %8.1f us/op  %6lld allocs/op\n",
            "keymap control", iters, (double)total_ns / iters / 1000.0, total_a / iters);
        alloc_stats::Stats agg{(unsigned long long)total_a, 0, (unsigned long long)total_b, 0};
        perf_limits::checkAllocBudget(perf_limits::kTerminalDecode, agg, iters, __FILE__, __LINE__);
    }
    CHECK(perf_time::g_sink > 0);
}
