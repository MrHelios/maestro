// ===========================================================================
// PERF UTF-8: micro-benchmarks de columnOf/truncate/range/isCellStart.
// Mide el costo de las utilidades de core/utf8.h que usa el renderer por
// tecla (columnOf para posicionar cursor, truncate/range para recortar
// lineas al ancho del viewport). Tamanos elegidos para representar lineas
// reales: 1k cols ~ linea corta, 4k ~ linea larga, 10k/40k ~ documento
// concatenado o linea extrema; los casos "mid" ejercitan busqueda en el
// medio del buffer, no solo al final.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include "test_framework.h"
#include "helpers/perf_time_utils.h"
#include "core/Cursor.h"
#include "core/utf8.h"

namespace {
using perf_time::g_sink;
using perf_time::bench_us;

std::string makeMixed(int cols) {
    std::string s;
    s.reserve(cols * 3);
    for (int i = 0; i < cols; ++i) {
        if (i % 4 == 0) s += "a";
        else if (i % 4 == 1) s += "\xC3\xA9";
        else if (i % 4 == 2) s += "\xE2\x80\x94";
        else s += "\xF0\x9F\x98\x80";
    }
    return s;
}
}

TEST(bench_perf_utf8_columnOf_checked) {
    std::printf("\n== perf: utf8::columnOf (v3 micro-opt) ==\n");
    std::string ascii10k(10000, 'a');
    std::string utf8_10k = makeMixed(4000);
    std::string utf8_100k = makeMixed(40000);
    std::string ascii4k(4000, 'a');
    std::string mixed1k = makeMixed(1000);
    bench_us("columnOf ASCII 10KB", 20000, [&]{ g_sink += utf8::columnOf(ascii10k, (int)ascii10k.size()); });
    bench_us("columnOf UTF-8 10KB ~4k cols", 20000, [&]{ g_sink += utf8::columnOf(utf8_10k, (int)utf8_10k.size()); });
    bench_us("columnOf UTF-8 100KB ~40k cols", 2000, [&]{ g_sink += utf8::columnOf(utf8_100k, (int)utf8_100k.size()); });
    bench_us("columnOf ascii 4k", 20000, [&]{ g_sink += utf8::columnOf(ascii4k, (int)ascii4k.size()); });
    bench_us("columnOf mixed 1k", 20000, [&]{ g_sink += utf8::columnOf(mixed1k, (int)mixed1k.size()); });
    bench_us("columnOf 100KB mid 50kB", 5000, [&]{ g_sink += utf8::columnOf(utf8_100k, 50000); });
    CHECK(g_sink > 0);
}
TEST(bench_perf_utf8_columnOf_breakdown_checked) {
    std::printf("\n== perf: utf8::columnOf breakdown ASCII%% vs UTF-8 / offset ==\n");
    std::string ascii10k(10000, 'a');
    std::string ascii100k(100000, 'a');
    std::string utf8_10k = makeMixed(4000);
    std::string utf8_100k = makeMixed(40000);
    bench_us("columnOf_ascii_10k_end", 20000, [&]{ g_sink += utf8::columnOf(ascii10k, (int)ascii10k.size()); });
    bench_us("columnOf_ascii_100k_end", 2000, [&]{ g_sink += utf8::columnOf(ascii100k, (int)ascii100k.size()); });
    bench_us("columnOf_utf8_10k_end", 20000, [&]{ g_sink += utf8::columnOf(utf8_10k, (int)utf8_10k.size()); });
    bench_us("columnOf_utf8_100k_end", 2000, [&]{ g_sink += utf8::columnOf(utf8_100k, (int)utf8_100k.size()); });
    bench_us("columnOf_ascii_10k_middle", 20000, [&]{ g_sink += utf8::columnOf(ascii10k, (int)ascii10k.size()/2); });
    bench_us("columnOf_utf8_10k_middle", 20000, [&]{ g_sink += utf8::columnOf(utf8_10k, (int)utf8_10k.size()/2); });
    bench_us("columnOf_ascii_100k_middle", 2000, [&]{ g_sink += utf8::columnOf(ascii100k, (int)ascii100k.size()/2); });
    bench_us("columnOf_utf8_100k_middle", 2000, [&]{ g_sink += utf8::columnOf(utf8_100k, (int)utf8_100k.size()/2); });
    auto benchOffsets = [&](const char* tag, const std::string& s){
        bench_us((std::string(tag)+" col 0").c_str(), 50000, [&]{ g_sink += utf8::columnOf(s, 0); });
        bench_us((std::string(tag)+" col 80").c_str(), 20000, [&]{ g_sink += utf8::columnOf(s, 80); });
        bench_us((std::string(tag)+" col 1000").c_str(), 20000, [&]{ g_sink += utf8::columnOf(s, 1000); });
        bench_us((std::string(tag)+" col size/2").c_str(), 5000, [&]{ g_sink += utf8::columnOf(s, (int)s.size()/2); });
        bench_us((std::string(tag)+" col size").c_str(), 2000, [&]{ g_sink += utf8::columnOf(s, (int)s.size()); });
    };
    std::printf(" -- offsets ascii10k --\n");
    benchOffsets("ascii10k", ascii10k);
    std::printf(" -- offsets utf8_10k --\n");
    benchOffsets("utf8_10k", utf8_10k);
    std::printf(" -- offsets ascii100k --\n");
    benchOffsets("ascii100k", ascii100k);
    std::printf(" -- offsets utf8_100k --\n");
    benchOffsets("utf8_100k", utf8_100k);
    CHECK(g_sink > 0);
}
TEST(bench_perf_utf8_truncate_checked) {
    std::printf("\n== perf: utf8::truncate ==\n");
    std::string ascii(4000, 'a');
    std::string mixed = makeMixed(1000);
    std::string longMixed = makeMixed(10000);
    bench_us("truncate ascii 4k -> 80 cols", 20000, [&]{ g_sink += utf8::truncate(ascii, 80).size(); });
    bench_us("truncate mixed 1k -> 80 cols", 20000, [&]{ g_sink += utf8::truncate(mixed, 80).size(); });
    bench_us("truncate mixed 10k -> 80 cols", 20000, [&]{ g_sink += utf8::truncate(longMixed, 80).size(); });
    bench_us("truncate mixed 1k -> 500 cols", 20000, [&]{ g_sink += utf8::truncate(mixed, 500).size(); });
    CHECK(g_sink > 0);
}

TEST(bench_perf_utf8_range_checked) {
    std::printf("\n== perf: utf8::range ==\n");
    std::string mixed = makeMixed(1000);
    std::string longMixed = makeMixed(10000);
    bench_us("range mixed 1k [10,90)", 20000, [&]{ g_sink += utf8::range(mixed, 10, 90).size(); });
    bench_us("range mixed 10k [100,180)", 20000, [&]{ g_sink += utf8::range(longMixed, 100, 180).size(); });
    bench_us("range mixed 1k [0,80) viewport", 20000, [&]{ g_sink += utf8::range(mixed, 0, 80).size(); });
    CHECK(g_sink > 0);
}

TEST(bench_perf_utf8_isCellStart_checked) {
    std::printf("\n== perf: utf8::isCellStart scan ==\n");
    std::string mixed = makeMixed(1000);
    bench_us("isCellStart scan 1k cols (~2.5kB)", 50000, [&]{
        size_t c=0; for(int i=0;i<(int)mixed.size();++i) c+= utf8::isCellStart(mixed,i);
        g_sink+=c;
    });
    CHECK(g_sink > 0);
}
TEST(bench_perf_utf8_columnOf_cache_vs_full_checked) {
    std::printf("\n== perf: columnOf cache incremental vs full scan (Left repetido) ==\n");
    std::string ascii10k(10000, 'a');
    std::string utf8_10k = makeMixed(4000);
    auto runCase = [&](const char* tag, const std::string& line, int startByte){
        startByte = std::min(startByte, (int)line.size());
        startByte = utf8::alignStart(line, startByte);
        std::vector<int> steps;
        int b = startByte;
        for(int k=0;k<100 && b>0;++k){ int prev = utf8::cellStartBefore(line, b); if(prev==b) break; steps.push_back(prev); b=prev; }
        if(steps.empty()) return;
        bench_us((std::string(tag)+" full 100xLeft").c_str(), 500, [&]{
            long c=0; for(int v: steps) c += utf8::columnOf(line, v);
            g_sink += c;
        });
        bench_us((std::string(tag)+" cached 100xLeft").c_str(), 5000, [&]{
            Cursor cur; cur.col = startByte; cur.visualColumn(line);
            long c=0; for(int v: steps) { cur.col = v; c += cur.visualColumn(line); }
            g_sink += c;
        });
        bench_us((std::string(tag)+" full 1x col").c_str(), 5000, [&]{ g_sink += utf8::columnOf(line, startByte); });
        Cursor cur2; cur2.col = startByte; cur2.visualColumn(line);
        bench_us((std::string(tag)+" cached hit 1x col").c_str(), 50000, [&]{ g_sink += cur2.visualColumn(line); });
    };
    runCase("ascii10k", ascii10k, 5000);
    runCase("utf8_10k", utf8_10k, 5000);
    runCase("utf8_10k end", utf8_10k, (int)utf8_10k.size());
    std::printf(" -- idea viable: Left retrocede 1 celda via cellStartBefore O(1) max 3 bytes, Right via cellLen O(1); cache evita rescan 0..byteCol --\n");
    CHECK(g_sink > 0);
}
