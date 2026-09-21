#pragma once
// Instrumentacion minima y jerarquica para validar hipotesis:
//   buildScreen -> renderEditorRow -> utf8::columnOf/range/truncate
// Hipotesis: no es columnOf individual sino multiplicacion de recorridos O(n)
// sobre la misma linea dentro de renderEditorRow.
//
// Contadores por frame con dos dimensiones:
//   calls + bytes (aprox. O(n))
//   - columnOf: input_bytes/scan_limit = limit (min(byteCol, line.size())).
//     Aproximacion al trabajo O(n); no es literal "bytes iterados" porque el
//     loop avanza con cellLen() (1-4 bytes) y tiene fast-path ASCII de 8 bytes.
//   - range/truncate/expandTabs: scanned_bytes = bytes examinados hasta el
//     limite (i / line.size()), mas cercano a iterados.
// Overhead nulo cuando disabled (early return inline).

#include <cstdint>
#include <string>
#include <time.h>

namespace instrument {

// Etiqueta para desglose por contexto dentro de renderEditorRow.
// Usada via thread_local currentTag: el caller setea tag, el callee (utf8::*)
// incrementa tanto el total como el bucket detallado con el scanned_bytes real.
enum class Utf8Tag : uint8_t {
    None = 0,
    RangeVisibleRaw,
    RangeSegments,
    RangeFilled,
    TruncateFilled,
    ColumnOfSelection,
    ColumnOfBracket,
    ColumnOfSyntax,
    ColumnOfFilled, // colCount(visible) / colCount(truncated) dentro de renderFilledRow
    ExpandTabs,
};

struct Count {
    uint64_t calls = 0;
    // Para columnOf: input_bytes/scan_limit (limit). Para range/truncate/
    // expandTabs: bytes efectivamente escaneados. Ver nota arriba.
    // Se mantiene el nombre scanned_bytes por compatibilidad con reportes
    // previos; semanticamente es "bytes de entrada considerados" ~ O(n).
    uint64_t scanned_bytes = 0;
    uint64_t nanos = 0; // tiempo acumulado en bucket
    // Alias mas preciso para columnOf
    uint64_t input_bytes() const { return scanned_bytes; }
};

struct FrameMetrics {
    // Jerarquia render
    uint64_t buildScreen = 0;
    uint64_t buildEditorBody = 0;
    uint64_t renderEditorContent = 0;
    uint64_t renderEditorRow = 0;
    uint64_t renderFilledRow = 0;
    uint64_t buildScreen_nanos = 0;
    uint64_t buildEditorBody_nanos = 0;
    uint64_t renderEditorContent_nanos = 0;
    uint64_t renderEditorRow_nanos = 0;
    uint64_t renderFilledRow_nanos = 0;

    // SyntaxCache
    uint64_t syntaxEnsureValid_calls = 0;
    uint64_t syntaxLinesRequested = 0; // sum of upTo
    uint64_t syntaxLinesParsed = 0;    // actual lines highlightadas
    uint64_t syntaxHighlighterCalls = 0;
    uint64_t syntaxEnsureValid_nanos = 0;

    // Totales utf8 (toda llamada, venga de donde venga)
    Count columnOf;
    Count range;
    Count truncate;
    Count expandTabs;

    // Desglose por contexto (dentro de renderEditorRow / renderFilledRow)
    Count range_visibleRaw;
    Count range_segments;
    Count range_filled;
    Count truncate_filled;
    Count columnOf_selection;
    Count columnOf_bracket;
    Count columnOf_syntax;
    Count columnOf_filled;
    Count expandTabs_visible;

    void reset();
    std::string report() const;            // plano tipo ejemplo del enunciado
    std::string reportHierarchical() const; // arbol jerarquico
    std::string reportTimed() const;       // con nanos y porcentajes
    std::string reportTimedHierarchical() const;
};

// Estado global
extern bool enabled;
extern thread_local FrameMetrics current;
extern thread_local Utf8Tag currentTag;

inline void enable(bool v = true) { enabled = v; }
inline bool isEnabled() { return enabled; }

inline void resetFrame() {
    current.reset();
    currentTag = Utf8Tag::None;
}

inline FrameMetrics snapshotAndReset() {
    FrameMetrics out = current;
    resetFrame();
    return out;
}

// Tag RAII minimo overhead
struct ScopedTag {
    Utf8Tag prev;
    explicit ScopedTag(Utf8Tag t) : prev(currentTag) {
        if (enabled) currentTag = t;
    }
    ~ScopedTag() {
        if (enabled) currentTag = prev;
    }
    ScopedTag(const ScopedTag&) = delete;
    ScopedTag& operator=(const ScopedTag&) = delete;
};

// Helpers para jerarquia (llamados desde Renderer.cpp)
inline void onBuildScreen() { if (!enabled) return; current.buildScreen++; }
inline void onBuildEditorBody() { if (!enabled) return; current.buildEditorBody++; }
inline void onRenderEditorContent() { if (!enabled) return; current.renderEditorContent++; }
inline void onRenderEditorRow() { if (!enabled) return; current.renderEditorRow++; }
inline void onRenderFilledRow() { if (!enabled) return; current.renderFilledRow++; }

// Helpers genericos para utf8 (llamados desde utf8.h)
// Para columnOf: scanned = limit = min(byteCol, n) (input_bytes/scan_limit),
// aproximacion O(n) del trabajo; no equivale a bytes iterados literales por
// cellLen/fast-path. Para range/truncate/expandTabs si es bytes escaneados.
inline void recordColumnOf(uint64_t inputLimit, uint64_t ns = 0) {
    if (!enabled) return;
    current.columnOf.calls++;
    current.columnOf.scanned_bytes += inputLimit;
    current.columnOf.nanos += ns;
    switch (currentTag) {
        case Utf8Tag::ColumnOfSelection: current.columnOf_selection.calls++; current.columnOf_selection.scanned_bytes += inputLimit; current.columnOf_selection.nanos += ns; break;
        case Utf8Tag::ColumnOfBracket:   current.columnOf_bracket.calls++;   current.columnOf_bracket.scanned_bytes   += inputLimit; current.columnOf_bracket.nanos += ns; break;
        case Utf8Tag::ColumnOfSyntax:    current.columnOf_syntax.calls++;    current.columnOf_syntax.scanned_bytes    += inputLimit; current.columnOf_syntax.nanos += ns; break;
        case Utf8Tag::ColumnOfFilled:    current.columnOf_filled.calls++;    current.columnOf_filled.scanned_bytes    += inputLimit; current.columnOf_filled.nanos += ns; break;
        default: break;
    }
}
inline void recordRange(uint64_t scannedBytes, uint64_t ns = 0) {
    if (!enabled) return;
    current.range.calls++;
    current.range.scanned_bytes += scannedBytes;
    current.range.nanos += ns;
    switch (currentTag) {
        case Utf8Tag::RangeVisibleRaw: current.range_visibleRaw.calls++; current.range_visibleRaw.scanned_bytes += scannedBytes; current.range_visibleRaw.nanos += ns; break;
        case Utf8Tag::RangeSegments:   current.range_segments.calls++;   current.range_segments.scanned_bytes   += scannedBytes; current.range_segments.nanos += ns; break;
        case Utf8Tag::RangeFilled:     current.range_filled.calls++;     current.range_filled.scanned_bytes     += scannedBytes; current.range_filled.nanos += ns; break;
        default: break;
    }
}
inline void recordTruncate(uint64_t scannedBytes, uint64_t ns = 0) {
    if (!enabled) return;
    current.truncate.calls++;
    current.truncate.scanned_bytes += scannedBytes;
    current.truncate.nanos += ns;
    if (currentTag == Utf8Tag::TruncateFilled) {
        current.truncate_filled.calls++;
        current.truncate_filled.scanned_bytes += scannedBytes;
        current.truncate_filled.nanos += ns;
    }
}
inline void recordExpandTabs(uint64_t inputBytes, uint64_t ns = 0) {
    if (!enabled) return;
    current.expandTabs.calls++;
    current.expandTabs.scanned_bytes += inputBytes;
    current.expandTabs.nanos += ns;
    if (currentTag == Utf8Tag::ExpandTabs) {
        current.expandTabs_visible.calls++;
        current.expandTabs_visible.scanned_bytes += inputBytes;
        current.expandTabs_visible.nanos += ns;
    }
}

// Timing helpers para jerarquia
inline void addBuildScreenTime(uint64_t ns) { if (enabled) current.buildScreen_nanos += ns; }
inline void addBuildEditorBodyTime(uint64_t ns) { if (enabled) current.buildEditorBody_nanos += ns; }
inline void addRenderEditorContentTime(uint64_t ns) { if (enabled) current.renderEditorContent_nanos += ns; }
inline void addRenderEditorRowTime(uint64_t ns) { if (enabled) current.renderEditorRow_nanos += ns; }
inline void addRenderFilledRowTime(uint64_t ns) { if (enabled) current.renderFilledRow_nanos += ns; }

// SyntaxCache helpers
inline void recordSyntaxEnsureValid(int requestedUpTo, int parsedLines, uint64_t ns) {
    if (!enabled) return;
    current.syntaxEnsureValid_calls++;
    current.syntaxLinesRequested += (requestedUpTo < 0 ? 0 : requestedUpTo);
    current.syntaxLinesParsed += (parsedLines < 0 ? 0 : parsedLines);
    current.syntaxEnsureValid_nanos += ns;
}
inline void recordHighlighterCall(uint64_t ns = 0) {
    if (!enabled) return;
    current.syntaxHighlighterCalls++;
    // opcional: sumar ns si se mide por llamada
    (void)ns;
}

// Scoped timer para jerarquia (RAII) - usa clock_gettime para no arrastrar <chrono> (compila 17s -> 0.5s por TU)
inline uint64_t nowNanos() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
struct ScopedTimer {
    uint64_t t0 = 0;
    uint64_t* out = nullptr;
    bool active = false;
    explicit ScopedTimer(uint64_t* o) : out(o), active(enabled) { if(active) t0 = nowNanos(); }
    ~ScopedTimer() { if(active && out){ uint64_t t1 = nowNanos(); *out += (t1 - t0); } }
};

// Atajos para setear tag sin RAII cuando se quiere envolver una sola llamada
inline void setTag(Utf8Tag t) { if (enabled) currentTag = t; }
inline void clearTag() { if (enabled) currentTag = Utf8Tag::None; }

} // namespace instrument
