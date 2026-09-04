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

TEST(perf_utf8_columnOf) {
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
TEST(perf_utf8_truncate) {
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

TEST(perf_utf8_range) {
    std::printf("\n== perf: utf8::range ==\n");
    std::string mixed = makeMixed(1000);
    std::string longMixed = makeMixed(10000);
    bench_us("range mixed 1k [10,90)", 20000, [&]{ g_sink += utf8::range(mixed, 10, 90).size(); });
    bench_us("range mixed 10k [100,180)", 20000, [&]{ g_sink += utf8::range(longMixed, 100, 180).size(); });
    bench_us("range mixed 1k [0,80) viewport", 20000, [&]{ g_sink += utf8::range(mixed, 0, 80).size(); });
    CHECK(g_sink > 0);
}

TEST(perf_utf8_isCellStart) {
    std::printf("\n== perf: utf8::isCellStart scan ==\n");
    std::string mixed = makeMixed(1000);
    bench_us("isCellStart scan 1k cols (~2.5kB)", 50000, [&]{
        size_t c=0; for(int i=0;i<(int)mixed.size();++i) c+= utf8::isCellStart(mixed,i);
        g_sink+=c;
    });
    CHECK(g_sink > 0);
}
