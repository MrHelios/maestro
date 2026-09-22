#pragma once
// ===========================================================================
// Gates duros de recursos (allocations/bytes) para tests de performance.
//
// Son independientes del hardware: si un cambio sube allocs/bytes por encima
// del límite, el test falla en cualquier máquina (Lenovo, otra PC o CI).
// El tiempo absoluto se reporta (informational); el relativo podrá gatearse
// después contra una operación de referencia.
//
// Regla de márgenes: límites intencionalmente más amplios que la baseline,
// salvo hot paths donde cero es parte del diseño (warm, bounded, cached).
// ---------------------------------------------------------------------------

#include <climits>
#include <string>

#include "test_framework.h"
#include "helpers/alloc_stats.h"

namespace perf_limits {

// Contrato de performance por área. Todo per-op (por iteración).
struct PerfLimits {
    const char* area;
    unsigned long long baselineAllocs;  // medido en baseline
    unsigned long long maxAllocs;       // hard gate
    unsigned long long baselineBytes;   // medido en baseline
    unsigned long long maxBytes;        // hard gate (ULLONG_MAX = sin gate)
    const char* margin;
    const char* reason;
};

// dispara dos CHECK (allocs y bytes, si aplica) con mensaje explícito.
// iters = iteraciones acumuladas en `st`.
inline void checkAllocBudget(const PerfLimits& lim,
                             const alloc_stats::Stats& st,
                             long long iters,
                             const char* file, int line) {
    const unsigned long long n = iters > 0 ? (unsigned long long)iters : 1;
    const unsigned long long a = st.allocs / n;
    ::testfw::report(a <= lim.maxAllocs,
        std::string(lim.area) + " allocs/op " + std::to_string(a) +
            " <= " + std::to_string(lim.maxAllocs),
        file, line);
    if (lim.maxBytes != ULLONG_MAX) {
        const unsigned long long b = st.bytesAllocated / n;
        ::testfw::report(b <= lim.maxBytes,
            std::string(lim.area) + " bytes/op " + std::to_string(b) +
                " <= " + std::to_string(lim.maxBytes),
            file, line);
    }
}

// Baseline: 0 allocs / 0 B (warm). Exacto: convergida no debe asignar.
constexpr PerfLimits kSyntaxWarm{
    "syntax warm", 0, 0, 0, 0, "exacto",
    "warm no debe asignar por diseno (cache convergida)"};

// Baseline: 3 allocs / 184 B por ensureValid incremental.
constexpr PerfLimits kSyntaxIncremental{
    "syntax incremental", 3, 4, 184, 256, "+33% allocs / +39% bytes",
    "ensureValid incremental es O(1): cota holgada sobre 3/184"};

// Baseline: 0 / 0. Bounded escanea solo el viewport, sin asignar.
constexpr PerfLimits kBracketBounded{
    "bracket bounded", 0, 0, 0, 0, "exacto",
    "bounded no debe asignar por diseno (scanning puro)"};

// Baseline: 0 / 0 (cached y afterEdit). Camino real con spans por referencia.
constexpr PerfLimits kBracketCached{
    "bracket cached", 0, 0, 0, 0, "exacto",
    "cached/afterEdit usan allSpans() por referencia: cero allocs"};

// Baseline: hit ~0,1us. El hit de cache visual no debe asignar.
constexpr PerfLimits kCursorCacheHit{
    "cursor cache hit", 0, 0, 0, 0, "exacto",
    "visualColumn cacheada no debe asignar por diseno"};

// Baseline: 164 allocations/frame (Cpp, viewport 24x80).
// Limit:    180 allocations/frame. Margin: +9.8%.
// Reason:   detect allocation regressions without coupling the test
//           to an exact STL/allocation layout.
constexpr PerfLimits kRenderViewport{
    "render viewport", 164, 180, 0, ULLONG_MAX, "+9.8% allocs",
    "detectar regresiones sin acoplarse al layout exacto de STL"};

} // namespace perf_limits
