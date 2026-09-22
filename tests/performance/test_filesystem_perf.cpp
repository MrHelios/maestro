// ===========================================================================
// P1 — File I/O en 2 familias (no mezclar costos):
//   A) Document puro: restore con datos sinteticos controlados (sin FS).
//   B) filesystem real: loadFromFile 10MB + saveToFile 25k.
// Asi se distingue parsing/Document de syscall/FS.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "test_framework.h"
#include "helpers/perf_arch.h"
#include "helpers/perf_limits.h"
#include "helpers/perf_time_utils.h"
#include "helpers/alloc_stats.h"

#include "core/Document.h"

namespace {

inline std::vector<std::string> makeCtrlLines(int n, int width = 80) {
    std::vector<std::string> out;
    out.reserve(n > 0 ? (size_t)n : 1);
    if (n <= 0) { out.emplace_back(""); return out; }
    for (int i = 0; i < n; ++i) {
        std::string l = "int v" + std::to_string(i) + " = " + std::to_string(i) + "; // ";
        while ((int)l.size() < width) l += "x";
        l.resize(width);
        out.push_back(l);
    }
    return out;
}

// Crea archivo real de ~10MB una sola vez, fuera del Scoped medido.
inline std::string makeBigFile(int targetBytes = 10 * 1024 * 1024) {
    std::string path = testfw::tmpPath();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    std::string line(400, 'x');
    line.replace(0, 10, "int v=0; //");
    int written = 0;
    while (written < targetBytes) {
        f << line << "\n";
        written += (int)line.size() + 1;
    }
    f.close();
    return path;
}

} // namespace

// Familia A: Document puro, sin tocar FS dentro del Scoped.
TEST(bench_perf_file_restore_puro_checked) {
    perf_arch::reportVerbose("\n== perf_file_restore_puro (sin FS, 1k/10k/25k) ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCtrlLines(n);
        int iters = n == 1000 ? 20 : n == 10000 ? 5 : 3;
        char label[64];
        std::snprintf(label, sizeof(label), "restore puro %5d", n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kOther);
            for (int i = 0; i < iters; ++i) {
                Document d;
                d.restore(lines);
                perf_time::g_sink += d.lineCount();
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters / 1000.0;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        perf_arch::reportVerbose("%-48s %6d iters  %8.1f us/op  %6llu allocs/op\n",
            label, iters, us, st.allocs / (unsigned long long)iters);
        perf_limits::checkDocumentLoadBudget(n, st, iters, __FILE__, __LINE__);
    }
    CHECK(perf_time::g_sink > 0);
}

// Familia B: loadFromFile real 10MB.
TEST(bench_perf_file_load10M_checked) {
    perf_arch::reportVerbose("\n== perf_file_load10M (filesystem real) ==\n");
    std::string path = makeBigFile();
    // contar lineas una vez fuera de medicion para el gate N-lineal
    int n = 0;
    {
        Document tmp;
        tmp.loadFromFile(path);
        n = tmp.lineCount();
        perf_time::g_sink += n;
    }
    const int iters = 3;
    auto t0 = std::chrono::steady_clock::now();
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped s(alloc_stats::kOther);
        for (int i = 0; i < iters; ++i) {
            Document d;
            auto r = d.loadFromFile(path);
            perf_time::g_sink += d.lineCount() + (r == LoadResult::Success ? 1 : 0);
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters / 1e6;
    auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
    perf_arch::reportVerbose("%-48s %6d iters  %8.1f ms/op  %6llu allocs/op  N=%d\n",
        "loadFromFile 10MB", iters, ms, st.allocs / (unsigned long long)iters, n);
    perf_limits::checkFileLoadBudget(n, st, iters, __FILE__, __LINE__);
    std::remove(path.c_str());
    CHECK(perf_time::g_sink > 0);
}

// Familia B: saveToFile 25k.
TEST(bench_perf_file_save25k_checked) {
    perf_arch::reportVerbose("\n== perf_file_save25k (filesystem real) ==\n");
    auto lines = makeCtrlLines(25000);
    Document d;
    d.restore(lines);
    std::string path = testfw::tmpPath();
    const int iters = 3;
    auto t0 = std::chrono::steady_clock::now();
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped s(alloc_stats::kOther);
        for (int i = 0; i < iters; ++i) {
            bool ok = d.saveToFile(path);
            perf_time::g_sink += ok ? 1 : 0;
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters / 1e6;
    auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
    perf_arch::reportVerbose("%-48s %6d iters  %8.1f ms/op  %6llu allocs/op\n",
        "saveToFile 25k", iters, ms, st.allocs / (unsigned long long)iters);
    perf_limits::checkAllocBudget(perf_limits::kFileSave, st, iters, __FILE__, __LINE__);
    std::remove(path.c_str());
    CHECK(perf_time::g_sink > 0);
}
