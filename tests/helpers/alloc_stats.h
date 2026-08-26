#pragma once

// ---------------------------------------------------------------------------
// Contador TEMPORAL de allocations (instrumentacion de diagnostico).
//
// Reemplaza operator new/delete GLOBALES del binario de tests y contabiliza
// allocations/frees por "scope". Los scopes se apilan (alloc_stats::Scoped)
// y la atribucion es INCLUSIVA: una allocation dentro de Typing > DocInsert
// suma a los dos. Fuera de todo scope suma a kOther. kGlobal lleva el total
// real (sin doble conteo).
//
// Uso tipico en un TEST:
//
//   alloc_stats::resetAll();          // descarta el ruido previo
//   { alloc_stats::Scoped s(alloc_stats::kTyping);
//     ... operacion a medir ...; }
//   alloc_stats::report("titulo");    // imprime la tabla
//
// Limitaciones conocidas (aceptables para diagnostico):
//   - single-thread: sin atomics.
//   - aligned new (> max_align_t) no pasa por el contador.
//   - bytes via malloc_usable_size (glibc): aproxima, no exacto.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(__GLIBC__)
#include <malloc.h>
#define MAESTRO_HAS_USABLE_SIZE 1
#endif

namespace alloc_stats {

enum Scope {
    kOther = 0,      // fuera de todo scope (ruido del framework, etc.)
    kTyping,         // ruta completa de tecleo: handleEvent(InsertChar)
    kDocInsert,      // Document::insertText solo
    kEditPushBack,   // edits.push_back(Edit)
    kHistoryCommit,  // beginHistoryEntry + commitHistoryEntry
    kCursorMove,     // moveUp/moveDown (scroll por teclado)
    kRenderFrame,    // buildScreen (frame ANSI completo, sin escribir terminal)
    kScopeCount,
    kGlobal = kScopeCount,  // total real, fila extra de la tabla
};
constexpr int kTableSize = kScopeCount + 1;

struct Stats {
    unsigned long long allocs = 0;
    unsigned long long frees = 0;
    unsigned long long bytesAllocated = 0;
    unsigned long long bytesFreed = 0;
};

namespace detail {

inline Stats& statAt(int i) {
    static Stats table[kTableSize];  // zero-init en carga: seguro pre-main
    return table[i];
}

constexpr int kMaxDepth = 32;

inline int& depthRef() {
    static int depth = 0;
    return depth;
}

inline int* stackRef() {
    static int stack[kMaxDepth];
    return stack;
}

inline std::size_t usableSize(void* p) {
#if MAESTRO_HAS_USABLE_SIZE
    return ::malloc_usable_size(p);
#else
    (void)p;
    return 0;
#endif
}

inline void recordAlloc(std::size_t sz) {
    const int d = depthRef();
    const int* st = stackRef();
    const unsigned long long bytes = static_cast<unsigned long long>(sz);
    if (d == 0) {
        Stats& o = statAt(kOther);
        o.allocs++;
        o.bytesAllocated += bytes;
    } else {
        for (int i = 0; i < d; ++i) {
            Stats& s = statAt(st[i]);
            s.allocs++;
            s.bytesAllocated += bytes;
        }
    }
    Stats& g = statAt(kGlobal);
    g.allocs++;
    g.bytesAllocated += bytes;
}

inline void recordFree(std::size_t sz) {
    const int d = depthRef();
    const int* st = stackRef();
    const unsigned long long bytes = static_cast<unsigned long long>(sz);
    if (d == 0) {
        Stats& o = statAt(kOther);
        o.frees++;
        o.bytesFreed += bytes;
    } else {
        for (int i = 0; i < d; ++i) {
            Stats& s = statAt(st[i]);
            s.frees++;
            s.bytesFreed += bytes;
        }
    }
    Stats& g = statAt(kGlobal);
    g.frees++;
    g.bytesFreed += bytes;
}

inline void pushScope(int scope) {
    int& d = depthRef();
    if (d < kMaxDepth) stackRef()[d] = scope;
    ++d;
}

inline void popScope() {
    int& d = depthRef();
    if (d > 0) --d;
}

} // namespace detail

// Guard RAII: mide todo lo allocated adentro (inclusivo con scopes anidados).
struct Scoped {
    explicit Scoped(int scope) { detail::pushScope(scope); }
    ~Scoped() { detail::popScope(); }
    Scoped(const Scoped&) = delete;
    Scoped& operator=(const Scoped&) = delete;
};

inline const Stats& statsFor(int scope) { return detail::statAt(scope); }

// Descarta lo acumulado hasta ahora. Llamar JUSTO ANTES de la fase a medir.
inline void resetAll() {
    for (int i = 0; i < kTableSize; ++i) detail::statAt(i) = Stats{};
}

inline void report(const char* title) {
    std::printf("\n== allocs: %s ==\n", title);
    std::printf("%-16s %10s %10s %16s %16s\n",
                "scope", "allocs", "frees", "+bytes", "-bytes");
    for (int i = 0; i < kTableSize; ++i) {
        const Stats& s = detail::statAt(i);
        if (i != kGlobal && i != kOther && s.allocs == 0 && s.frees == 0) continue;
        const char* name = (i == kGlobal) ? "GLOBAL"
                         : (i == kOther)  ? "(other)"
                                          : nullptr;
        static const char* kNames[kScopeCount] = {
            "(other)", "Typing", "DocInsert", "EditPushBack",
            "HistoryCommit", "CursorMove", "RenderFrame",
        };
        if (!name) name = kNames[i];
        std::printf("%-16s %10llu %10llu %16llu %16llu\n",
                    name, s.allocs, s.frees, s.bytesAllocated, s.bytesFreed);
    }
}

} // namespace alloc_stats

// --- Overrides globales de new/delete (afectan TODO el binario de tests) ---

void* operator new(std::size_t sz) {
    void* p = std::malloc(sz ? sz : 1);
    if (!p) throw std::bad_alloc();
    alloc_stats::detail::recordAlloc(alloc_stats::detail::usableSize(p));
    return p;
}

void* operator new[](std::size_t sz) { return ::operator new(sz); }

void* operator new(std::size_t sz, const std::nothrow_t&) noexcept {
    void* p = std::malloc(sz ? sz : 1);
    if (p) alloc_stats::detail::recordAlloc(alloc_stats::detail::usableSize(p));
    return p;
}

void* operator new[](std::size_t sz, const std::nothrow_t&) noexcept {
    return ::operator new(sz, std::nothrow);
}

void operator delete(void* p) noexcept {
    if (!p) return;
    alloc_stats::detail::recordFree(alloc_stats::detail::usableSize(p));
    std::free(p);
}

void operator delete[](void* p) noexcept { ::operator delete(p); }

void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }

void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

void operator delete(void* p, const std::nothrow_t&) noexcept {
    ::operator delete(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept {
    ::operator delete(p);
}
