#include <chrono>
#include <string>
#include <vector>
#include <cstdio>

#include "test_framework.h"
#include "helpers/perf_arch.h"
#include "core/Document.h"
#include "alloc_stats.h"

static std::string makeLine(size_t n) { return std::string(n, 'x'); }
static std::vector<std::string> makeBlock(size_t lines, size_t width = 80) {
    std::vector<std::string> b;
    b.reserve(lines);
    for (size_t i = 0; i < lines; ++i) b.emplace_back(width, 'y');
    return b;
}

TEST(bench_perf_insertBlock_matrix) {
    struct Case {
        const char* name;
        size_t lineSize;
        size_t blockLines;
        int iters;
    };
    const Case cases[] = {
        {"small       100B x   1",        100,    1, 500},
        {"line_4k     4KB x   1",        4000,    1, 500},
        {"block_10    100B x  10",        100,   10, 200},
        {"mixed       4KB x  10",        4000,   10, 200},
        {"block_1k    100B x 1k",        100, 1000,  50},
        {"large      100KB x 1k",     100000, 1000,  20},
    };

    perf_arch::reportVerbose("\n== perf_insertBlock_matrix: time + allocs por col (0/mid/end) ==\n");
    perf_arch::reportVerbose("%-22s %8s %6s %12s %12s %10s %12s\n",
                "case", "col", "iters", "ns/op", "allocs/op", "bytes/op", "total allocs");

    for (auto& tc : cases) {
        auto block = makeBlock(tc.blockLines);
        size_t cols[3] = {0, tc.lineSize / 2, tc.lineSize};
        const char* colNames[3] = {"0", "mid", "end"};
        for (int ci = 0; ci < 3; ++ci) {
            size_t col = cols[ci];
            alloc_stats::resetAll();
            auto t0 = std::chrono::steady_clock::now();
            {
                alloc_stats::Scoped scope(alloc_stats::kDocInsert);
                for (int iter = 0; iter < tc.iters; ++iter) {
                    Document d;
                    d.restore({makeLine(tc.lineSize)});
                    d.insertBlock(0, static_cast<int>(col), block);
                    (void)d.lineCount();
                }
            }
            auto t1 = std::chrono::steady_clock::now();
            long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            long long nsPerOp = ns / tc.iters;
            auto s = alloc_stats::statsFor(alloc_stats::kDocInsert);
            long long allocsPerOp = s.allocs / (unsigned long long)tc.iters;
            long long bytesPerOp = s.bytesAllocated / (unsigned long long)tc.iters;
            perf_arch::reportVerbose("%-22s %8s %6d %12lld %12lld %10lld %12llu\n",
                        tc.name, colNames[ci], tc.iters, nsPerOp, allocsPerOp, bytesPerOp, s.allocs);
            alloc_stats::resetAll();
        }
    }
    perf_arch::reportVerbose("\n-- Mide ciclo completo construccion/restauracion + insertBlock (allocs en misma region) --\n");
    alloc_stats::report("perf_insertBlock_matrix aggregate");
}

TEST(bench_perf_insertBlock_breakdown) {
    auto bench = [](const char* label, size_t lineSize, const std::vector<std::string>& block, size_t col, int iters) {
        std::string target = makeLine(lineSize);

        // A: solo construcción de newLines (setup fuera, mide solo substr+concat)
        alloc_stats::resetAll();
        auto tA0 = std::chrono::steady_clock::now();
        {
            alloc_stats::Scoped sa(alloc_stats::kDocInsert);
            for (int i = 0; i < iters; ++i) {
                std::string right = target.substr(col);
                std::string left = target.substr(0, col);
                std::vector<std::string> newLines;
                newLines.reserve(block.size());
                if (block.size() == 1) {
                    newLines.push_back(left + block[0] + right);
                } else {
                    newLines.push_back(left + block.front());
                    for (size_t k = 1; k + 1 < block.size(); ++k) newLines.push_back(block[k]);
                    newLines.push_back(block.back() + right);
                }
                volatile size_t sink = newLines.size(); (void)sink;
            }
        }
        auto tA1 = std::chrono::steady_clock::now();
        long long nsA = std::chrono::duration_cast<std::chrono::nanoseconds>(tA1 - tA0).count() / iters;
        auto sA = alloc_stats::statsFor(alloc_stats::kDocInsert);
        long long allocA = sA.allocs / iters;

        for (int docLines : {1, 300}) {
            // Setup fuera de región medida
            std::vector<std::string> baseLines;
            baseLines.reserve(docLines + block.size() + 10);
            for (int i = 0; i < docLines; ++i) baseLines.emplace_back(80, 'x');
            std::string right = target.substr(col);
            std::string left = target.substr(0, col);
            std::vector<std::string> newLinesB;
            newLinesB.reserve(block.size());
            if (block.size() == 1) newLinesB.push_back(left + block[0] + right);
            else {
                newLinesB.push_back(left + block.front());
                for (size_t k = 1; k + 1 < block.size(); ++k) newLinesB.push_back(block[k]);
                newLinesB.push_back(block.back() + right);
            }
            int line = docLines / 2;
            // B in-place con capacidad reservada, sin copia por iter
            std::vector<std::string> lines = baseLines;
            lines.reserve(docLines + block.size() + 10);
            std::string saved = lines[line];
            alloc_stats::resetAll();
            auto tB0 = std::chrono::steady_clock::now();
            {
                alloc_stats::Scoped sb(alloc_stats::kDocInsert);
                for (int i = 0; i < iters; ++i) {
                    lines.erase(lines.begin() + line);
                    lines.insert(lines.begin() + line, newLinesB.begin(), newLinesB.end());
                    // revert in-place para próxima iter sin re-alloc/copy
                    lines.erase(lines.begin() + line, lines.begin() + line + newLinesB.size());
                    lines.insert(lines.begin() + line, saved);
                }
            }
            auto tB1 = std::chrono::steady_clock::now();
            long long nsBpair = std::chrono::duration_cast<std::chrono::nanoseconds>(tB1 - tB0).count() / iters;
            auto sB = alloc_stats::statsFor(alloc_stats::kDocInsert);
            long long allocBpair = sB.allocs / iters;
            long long allocB = allocBpair / 2;

            // C: ciclo completo Document (incluye restore) - setup distinto, se reporta separado
            // Para comparabilidad, también preparamos baseDoc fuera pero medimos con restore dentro
            alloc_stats::resetAll();
            auto tC0 = std::chrono::steady_clock::now();
            {
                alloc_stats::Scoped sc(alloc_stats::kDocInsert);
                for (int i = 0; i < iters; ++i) {
                    Document d;
                    if (docLines == 1) d.restore({target});
                    else {
                        std::vector<std::string> many;
                        many.reserve(docLines);
                        for (int k = 0; k < docLines; ++k) many.push_back(std::string(80,'x'));
                        many[docLines/2] = target;
                        d.restore(many);
                    }
                    d.insertBlock(docLines/2, (int)col, block);
                }
            }
            auto tC1 = std::chrono::steady_clock::now();
            long long nsC = std::chrono::duration_cast<std::chrono::nanoseconds>(tC1 - tC0).count() / iters;
            auto sC = alloc_stats::statsFor(alloc_stats::kDocInsert);
            long long allocC = sC.allocs / iters;

            perf_arch::reportVerbose("%-22s col=%-3s docLines=%3d iters=%4d | A %6lld ns %3lld alloc | B(inplace) %6lld ns %3lld alloc/pair->%3lld/op | C(full+restore) %6lld ns %3lld alloc\n",
                        label, (col==0?"0":col==lineSize/2?"mid":"end"), docLines, iters, nsA, allocA, nsBpair, allocBpair, allocB, nsC, allocC);
            alloc_stats::resetAll();
        }
    };

    perf_arch::reportVerbose("\n== perf_insertBlock_breakdown: A puro, B in-place sin copia, C incluye restore ==\n");
    perf_arch::reportVerbose("B mide erase+insert+revert in-place (cap reservada), A+B no es comparable directo a C (C incluye restore)\n");
    bench("block_10 100B x10 ", 100, makeBlock(10), 100, 500);
    bench("mixed 4KB x10 mid", 4000, makeBlock(10), 2000, 500);
    bench("mixed 4KB x10 end", 4000, makeBlock(10), 4000, 500);
    bench("block_1k 100B x1k", 100, makeBlock(1000), 50, 50);
}

TEST(bench_perf_insertBlock_shift_inplace) {
    auto bench = [&](int docLines, int insertLine, size_t blockLines, int iters) {
        auto block = makeBlock(blockLines, 80);
        // Setup fuera de la medición.
        std::vector<std::string> lines;
        lines.reserve(docLines + blockLines + 10);
        for (int i = 0; i < docLines; ++i) lines.emplace_back(80, 'x');
        std::string saved = lines[insertLine];
        std::vector<std::string> newLines = block;
        int Npost = docLines - insertLine - 1;
        if (Npost < 0) Npost = 0;
        alloc_stats::resetAll();
        auto t0 = std::chrono::steady_clock::now();
        {
            alloc_stats::Scoped sb(alloc_stats::kDocInsert);
            for (int it = 0; it < iters; ++it) {
                // Operación equivalente al algoritmo anterior.
                lines.erase(lines.begin() + insertLine);
                lines.insert(lines.begin() + insertLine, newLines.begin(), newLines.end());
                // Restauración in-place para repetir el benchmark.
                lines.erase(lines.begin() + insertLine, lines.begin() + insertLine + newLines.size());
                lines.insert(lines.begin() + insertLine, saved);
                volatile size_t s = lines.size(); (void)s;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        long long nsPair = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters;
        auto st = alloc_stats::statsFor(alloc_stats::kDocInsert);
        long long allocsPair = st.allocs / static_cast<unsigned long long>(iters);
        long long bytesPair = st.bytesAllocated / static_cast<unsigned long long>(iters);
        perf_arch::reportVerbose("inplace doc=%4d line=%4d Npost=%4d block=%3zu iters=%4d => cycle %7lld ns %3lld alloc %8lld bytes\n",
                    docLines, insertLine, Npost, blockLines, iters, nsPair, allocsPair, bytesPair);
        alloc_stats::resetAll();
        if ((int)lines.size() != docLines) std::printf("  ERROR size %zu != %d\n", lines.size(), docLines);
    };
    perf_arch::reportVerbose("\n== perf_insertBlock_shift_inplace ==\n");
    perf_arch::reportVerbose("(mide desplazamiento del vector y coste de copiar/mover strings, con capacidad suficiente; no representa realloc de lines_)\n");
    // doc fijo 1000, block 1 línea
    bench(1000, 0, 1, 4000);
    bench(1000, 500, 1, 4000);
    bench(1000, 999, 1, 4000);
    // matriz original pedida pero con block 10
    bench(10, 0, 10, 2000);
    bench(100, 0, 10, 2000);
    bench(100, 50, 10, 2000);
    bench(100, 90, 10, 2000);
    bench(300, 0, 10, 2000);
    bench(300, 150, 10, 2000);
    bench(300, 290, 10, 2000);
    bench(1000, 500, 10, 1000);
    perf_arch::reportVerbose("--- block sweep doc300 line150 ---\n");
    bench(300, 150, 1, 2000);
    bench(300, 150, 10, 2000);
    bench(300, 150, 300, 500);
}

TEST(bench_perf_insertBlock_old_vs_new_vector) {
    auto bench = [](int docLines, int line, size_t blockLines, int iters) {
        auto block = makeBlock(blockLines);
        std::vector<std::string> base;
        base.reserve(docLines + blockLines + 10);
        for (int i = 0; i < docLines; ++i) base.emplace_back(80, 'x');
        std::string target = base[line];
        std::string right = target.substr(40);
        std::string left = target.substr(0, 40);
        std::vector<std::string> newLines;
        newLines.reserve(block.size());
        newLines.push_back(left + block.front());
        for (size_t i = 1; i + 1 < block.size(); ++i) newLines.push_back(block[i]);
        newLines.push_back(block.back() + right);
        auto old = base;
        old.reserve(docLines + blockLines + 10);
        alloc_stats::resetAll();
        auto t0 = std::chrono::steady_clock::now();
        {
            alloc_stats::Scoped s(alloc_stats::kDocInsert);
            for (int i = 0; i < iters; ++i) {
                old.erase(old.begin() + line);
                old.insert(old.begin() + line, newLines.begin(), newLines.end());
                old.erase(old.begin() + line, old.begin() + line + newLines.size());
                old.insert(old.begin() + line, target);
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        long long oldNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters;
        auto oldStats = alloc_stats::statsFor(alloc_stats::kDocInsert);
        auto modern = base;
        modern.reserve(docLines + blockLines + 10);
        alloc_stats::resetAll();
        auto t2 = std::chrono::steady_clock::now();
        {
            alloc_stats::Scoped s(alloc_stats::kDocInsert);
            for (int i = 0; i < iters; ++i) {
                modern.insert(modern.begin() + line + 1, newLines.begin() + 1, newLines.end());
                modern[line] = newLines[0];
                modern.erase(modern.begin() + line, modern.begin() + line + newLines.size());
                modern.insert(modern.begin() + line, target);
            }
        }
        auto t3 = std::chrono::steady_clock::now();
        long long newNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count() / iters;
        auto newStats = alloc_stats::statsFor(alloc_stats::kDocInsert);
        perf_arch::reportVerbose("doc=%4d line=%4d block=%4zu iters=%5d | OLD %8lld ns %4llu alloc | NEW %8lld ns %4llu alloc\n",
                    docLines, line, blockLines, iters, oldNs, oldStats.allocs / (unsigned long long)iters, newNs, newStats.allocs / (unsigned long long)iters);
    };
    perf_arch::reportVerbose("\n== perf_insertBlock_old_vs_new_vector: OLD erase+insert vs NEW insert+assign ==\n");
    bench(10, 0, 1, 5000);
    bench(10, 0, 10, 3000);
    bench(100, 50, 10, 3000);
    bench(300, 150, 10, 3000);
    bench(1000, 500, 10, 1000);
    bench(300, 150, 300, 300);
}

TEST(bench_perf_insertBlock_single_vs_multi_alloc_detail) {
    auto block1 = makeBlock(1);
    auto block10 = makeBlock(10);

    alloc_stats::resetAll();
    {
        alloc_stats::Scoped s(alloc_stats::kDocInsert);
        for (int i = 0; i < 200; ++i) {
            Document d; d.restore({makeLine(4000)});
            d.insertBlock(0, 2000, block1);
        }
    }
    alloc_stats::report("insertBlock 4KB line, 1-line block @mid x200");

    alloc_stats::resetAll();
    {
        alloc_stats::Scoped s(alloc_stats::kDocInsert);
        for (int i = 0; i < 200; ++i) {
            Document d; d.restore({makeLine(4000)});
            d.insertBlock(0, 2000, block10);
        }
    }
    alloc_stats::report("insertBlock 4KB line, 10-line block @mid x200");
}
