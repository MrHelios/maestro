// ===========================================================================
// P4 — Syntax worst-case guardrail (el mas importante de P4).
// Operacion real: edit -> markDirty -> {ensureValid + bracket viewport} medido.
// Si ViewportSpanSource es viewport-only, el costo es ~constante con N.
// No se usa t(25k)/t(1k) como gate duro; gate sobre allocs/bytes +
// escalabilidad allocs(25k) <= allocs(1k)+2. Tiempo informativo.
// Guardrail visible: la salud se lee en la tabla con MAESTRO_PERF_VERBOSE=1
// (p.ej. 5.5 -> 5.8 -> 5.8 us plano con N). Un 5 -> 50 -> 5000 us expone la
// regresion de inmediato aunque CI siga verde: no convertirlo en gate duro.
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

#include "core/Document.h"
#include "core/BracketMatcher.h"
#include "syntax/SyntaxCache.h"
#include "syntax/SyntaxLanguage.h"

namespace {

inline std::vector<std::string> makeBaseLines(int n) {
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

// Mide solo ensureValid+bracket-viewport; edit+markDirty fuera del Scoped.
inline void benchWorstCase(const std::vector<std::string>& baseLines, const char* label,
                           unsigned long long& outAllocsPerOp, double& outUsPerOp) {
    int n = (int)baseLines.size();
    Document doc;
    doc.restore(baseLines);
    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, n);

    const int vpTop = n / 2;
    const int vpH = 24;
    const int vpBottom = std::min(n, vpTop + vpH);
    Position cur{vpTop + vpH / 2, 2};
    const std::string editA = "int edited_worst = 1; // aaaa";
    const std::string editB = "int edited_worst = 2; // bbbb";
    int iters = n <= 1000 ? 50 : 20;

    long long total_ns = 0, total_a = 0, total_b = 0;
    bool toggle = false;
    for (int i = 0; i < iters; ++i) {
        // --- edit + invalidation FUERA de medicion ---
        std::string next = toggle ? editA : editB;
        toggle = !toggle;
        {
            int line = vpTop + 1;
            int len = doc.lineLength(line);
            doc.deleteRange(line, 0, line, len);
            doc.insertText(line, 0, next);
            cache.markDirty(line);
        }
        // --- solo highlight+bracket viewport medido ---
        alloc_stats::resetAll();
        auto s = std::chrono::steady_clock::now();
        {
            alloc_stats::Scoped sc(alloc_stats::kOther);
            cache.ensureValid(doc, n);
            BracketSpanSource src;
            src.ensure = [&cache, &doc](int line) {
                if (line >= 0) cache.ensureValid(doc, line + 1);
            };
            src.spans = [&cache](int line) -> const std::vector<SyntaxSpan>& {
                return cache.spansFor(line);
            };
            auto p = findMatchingBracketBounded(doc, cur, SyntaxLanguage::Cpp, src, vpTop, vpBottom);
            perf_time::g_sink += (p ? 1 : 0) + cache.allBefore().size();
        }
        auto e = std::chrono::steady_clock::now();
        total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        total_a += st.allocs;
        total_b += st.bytesAllocated;
    }
    outUsPerOp = (double)total_ns / iters / 1000.0;
    outAllocsPerOp = (unsigned long long)total_a / (unsigned long long)iters;
    char lab[64];
    std::snprintf(lab, sizeof(lab), "%s", label);
    perf_arch::reportVerbose("%-48s %6d iters  %8.1f us/op  %6lld allocs/op  %8lld bytes/op\n",
        lab, iters, outUsPerOp, total_a / iters, total_b / iters);
    alloc_stats::Stats agg{(unsigned long long)total_a, 0, (unsigned long long)total_b, 0};
    perf_limits::checkAllocBudget(perf_limits::kSyntaxWorstCase, agg, iters, __FILE__, __LINE__);
}

} // namespace

TEST(bench_perf_syntax_unclosed_checked) {
    perf_arch::reportVerbose("\n== perf_syntax_unclosed (/* sin cerrar, viewport-only) ==\n");
    const int sizes[] = {1000, 10000, 25000};
    unsigned long long base = 0;
    for (int si = 0; si < 3; ++si) {
        int n = sizes[si];
        auto lines = makeBaseLines(n);
        if (n > 2) {
            lines[0] = "/* block comment sin cerrar que contamina todo lo que sigue";
            lines[0].resize(80, 'x');
        }
        char label[64];
        std::snprintf(label, sizeof(label), "unclosed /* %5d", n);
        unsigned long long a = 0;
        double us = 0;
        benchWorstCase(lines, label, a, us);
        if (si == 0) base = a;
        ::testfw::report(a <= base + 2,
            std::string("syntax worstcase escalabilidad N=") + std::to_string(n) +
                " allocs/op " + std::to_string(a) + " <= base+2=" + std::to_string(base + 2),
            __FILE__, __LINE__);
    }
    CHECK(perf_time::g_sink > 0);
}

TEST(bench_perf_syntax_rawstring_checked) {
    perf_arch::reportVerbose("\n== perf_syntax_rawstring (R\"()\" multiline, viewport-only) ==\n");
    const int sizes[] = {1000, 10000, 25000};
    unsigned long long base = 0;
    for (int si = 0; si < 3; ++si) {
        int n = sizes[si];
        auto lines = makeBaseLines(n);
        if (n > 10) {
            lines[1] = "std::string s = R\"RAW(";
            lines[2] = "linea dentro de raw string con { brackets [ falsos";
            lines[3] = "otra linea raw con /* comentario falso";
            for (int i = 4; i < n - 1; ++i) {
                if (i % 500 == 0) lines[i] = "medio raw } ] ) todavia dentro";
            }
            lines[n - 1] = ")RAW\"; // fin raw";
        }
        char label[64];
        std::snprintf(label, sizeof(label), "rawstring R() %5d", n);
        unsigned long long a = 0;
        double us = 0;
        benchWorstCase(lines, label, a, us);
        if (si == 0) base = a;
        ::testfw::report(a <= base + 2,
            std::string("syntax rawstring escalabilidad N=") + std::to_string(n) +
                " allocs/op " + std::to_string(a) + " <= base+2=" + std::to_string(base + 2),
            __FILE__, __LINE__);
    }
    CHECK(perf_time::g_sink > 0);
}
