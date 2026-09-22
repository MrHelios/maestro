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

// Baseline highlight steady: tras warmup de bracketCache_ + N/2 fijo,
// 0 allocs/op medido en 1k/10k/25k (steady O(viewport)), tiempo ~constante.
// Contratos (dos, no uno):
//   1) Absoluto: allocs/op <= 2, bytes/op <= 512 (PerfLimits arriba).
//   2) Escalabilidad: allocs(10k) <= allocs(1k)+2 y allocs(25k) <= allocs(1k)+2.
// Si falla, es bug real (matcher escapa del viewport o reparse sin convergencia).
constexpr PerfLimits kBracketHighlightSteady{
    "bracket highlight steady", 0, 2, 0, 512, "warmup excluido; +2 allocs margen",
    "steady debe ser O(viewport): warmup deja bracketCache valido hasta top"};

// columnOf cache hit: O(1). Baseline hit ~0.08-0.17us vs
// full 1.5ms-176ms. Gate conservador: uncached/cached >= 50x para evitar
// fragilidad; fallar indica que el cache no evita el rescan 0..byteCol.
// Medido: 1 alloc / 64 B total por bloque (amortizado 0/op con 1000 iters,
// 64/op con iters=1 en 100xLeft). Margen +1 alloc / 128 B para overhead fijo.
constexpr double kColumnOfCacheMinSpeedup = 50.0;
constexpr PerfLimits kColumnOfCacheHit{
    "columnOf cache hit", 0, 1, 0, 128, "+1 alloc / 128 B overhead fijo",
    "visualColumn cacheado no debe rescanear O(N); 1 alloc inicial es overhead fijo"};

// columnOf cold/uncached con tabs+wide: scan puro O(N), no debe asignar.
// Separado de kColumnOfCacheHit: si falla el hit sabemos que es la cache,
// si falla este sabemos que es el algoritmo Unicode/tabs.
constexpr PerfLimits kColumnOfCold{
    "columnOf cold", 0, 0, 0, 0, "exacto",
    "columnOf hace scan sin asignar por diseno"};

// RowLayout query steady: objeto ya construido, columnAt/range/expandVisible
// no deben asignar (Full: tablas; Checkpoint: scan acotado K). La
// construccion se mide aparte (kRowLayoutBuild) para no mezclar costos.
constexpr PerfLimits kRowLayoutFrame{
    "rowlayout frame", 0, 0, 0, 0, "exacto",
    "query steady no debe asignar; build va en kRowLayoutBuild"};

// RowLayout build (construccion por linea, 40-lineas frame): Checkpoint
// ~2-4 allocs/linea por growth (Full ~3: byteToCol+colToByte+cells).
// Gate por linea construida, generoso (+5 margen STL) para no acoplarse
// al layout exacto de la STL. No confundir con el caso huge 540KB, que
// es solo memoria informativa (memoryBytes, sin gate de allocs).
constexpr PerfLimits kRowLayoutBuild{
    "rowlayout build", 3, 8, 0, ULLONG_MAX, "+5 allocs margen STL",
    "build acotado por linea; huge 540KB va aparte como memoria informativa"};

// Search steady: collectMatches repetido con mismo query/doc converge en
// allocations independientes de N (el vector resultado es ~constante si el
// query es raro). Cold es solo informativo (recorre N lineas).
constexpr PerfLimits kSearchSteady{
    "search steady", 0, 2, 0, 512, "+2 allocs / 512 B margen vector",
    "steady no debe crecer con N; cold es O(N) informativo"};

// Clipboard paste: costo proporcional al contenido pegado, no al documento.
// Se mide solo insertText/insertBlock; deleteRange va en test aparte para
// no esconder regresiones de deleteRange/UTF-8.
constexpr PerfLimits kClipboardPaste{
    "clipboard paste", 0, 4, 0, ULLONG_MAX, "4 allocs overhead split/insert",
    "paste escala con pegado, no con N del documento"};

// Render diff: buildDiffFrame steady tras primar cache debe ser O(viewport).
constexpr PerfLimits kRenderDiff{
    "render diff", 164, 200, 0, ULLONG_MAX, "+22% sobre viewport",
    "diff no debe escalar con N; margen holgado sobre kRenderViewport"};

// File save real (saveToFile 25k):
// saveToFile no debe introducir allocations proporcionales al documento.
// El costo temporal/bytes escritos escala con el contenido.
// Sin gate de bytes a proposito: quedaria acoplado al tamano exacto de linea.
constexpr PerfLimits kFileSave{
    "file save", 0, 16, 0, ULLONG_MAX, "16 allocs overhead ofstream",
    "sin allocs proporcionales al documento; tiempo/bytes escalan con contenido"};
// Syntax worst-case (/* sin cerrar, raw string multiline): tras edicion,
// solo ensureValid+bracket-viewport medido. Debe ser ~constante con N si
// ViewportSpanSource es viewport-only. Absoluto generoso + escalabilidad.
constexpr PerfLimits kSyntaxWorstCase{
    "syntax worstcase", 3, 6, 184, 1024, "+100% sobre incremental",
    "worst-case no debe reintroducir O(N) por tecla"};

// Terminal/Keymap decode: lookup control/sequence no debe asignar en steady
// (el std::string de la secuencia se construye fuera del Scoped medido).
constexpr PerfLimits kTerminalDecode{
    "terminal decode", 0, 0, 0, 0, "exacto",
    "decode es lookup puro sin allocs repetitivas"};

// Document load (restore vector<string>): 1 alloc/línea, ~120 B/línea.
// Gate N-lineal con overhead fijo: allocs <= 1.10*N + 10, bytes <= 135*N + 1024.
// Más robusto que umbral absoluto por N; separa de restore 3k (2 allocs/línea).
inline void checkDocumentLoadBudget(int n, const alloc_stats::Stats& st,
                                    long long iters,
                                    const char* file, int line) {
    const unsigned long long div = iters > 0 ? (unsigned long long)iters : 1;
    const unsigned long long a = st.allocs / div;
    const unsigned long long b = st.bytesAllocated / div;
    const unsigned long long maxA = (unsigned long long)(1.10 * n + 10 + 0.5);
    const unsigned long long maxB = (unsigned long long)(135ULL * (unsigned long long)n + 1024);
    ::testfw::report(a <= maxA,
        std::string("document load allocs/N ") + std::to_string(a) +
            " <= 1.10*" + std::to_string(n) + "+10=" + std::to_string(maxA) +
            " (" + std::to_string(a) + "/" + std::to_string(n) + ")",
        file, line);
    ::testfw::report(b <= maxB,
        std::string("document load bytes/N ") + std::to_string(b) +
            " <= 135*" + std::to_string(n) + "+1024=" + std::to_string(maxB),
        file, line);
}

// File load real (loadFromFile 10MB, lineas anchas 400B): como restore pero
// con lineas mas grandes + 2 buffers de 256KB. Cota propia: allocs igual
// que restore, bytes 600*N+1MB (cubre 400B/linea + chunks).
inline void checkFileLoadBudget(int n, const alloc_stats::Stats& st,
                                long long iters,
                                const char* file, int line) {
    const unsigned long long div = iters > 0 ? (unsigned long long)iters : 1;
    const unsigned long long a = st.allocs / div;
    const unsigned long long b = st.bytesAllocated / div;
    const unsigned long long maxA = (unsigned long long)(1.10 * n + 10 + 0.5);
    const unsigned long long maxB = 600ULL * (unsigned long long)n + 1024ULL * 1024ULL;
    ::testfw::report(a <= maxA,
        std::string("file load allocs/N ") + std::to_string(a) +
            " <= 1.10*" + std::to_string(n) + "+10=" + std::to_string(maxA),
        file, line);
    ::testfw::report(b <= maxB,
        std::string("file load bytes/N ") + std::to_string(b) +
            " <= 600*" + std::to_string(n) + "+1MB=" + std::to_string(maxB),
        file, line);
}

} // namespace perf_limits
