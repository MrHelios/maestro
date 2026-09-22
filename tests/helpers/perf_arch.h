#pragma once
// ===========================================================================
// Arquitectura de performance (solo convención + helpers, sin TESTs nuevos).
//
//   bench_*         → medir solamente (sin CHECK en el cuerpo)
//   bench_*_checked → medir + CHECK (incluye CHECK directo o vía helper)
//   report_*        → imprimir sólo verbose (sin medición propia)
//   statsFor()      → obtener métricas (alias de alloc_stats::statsFor)
//   checkBudget()   → CHECK de presupuesto (wrapper de CHECK)
//   reportVerbose() → tabla sólo verbose (gated por MAESTRO_PERF_VERBOSE=1)
//
// Este header NO modifica ningún test existente: solo declara la
// convención para futuros tests y provee los helpers que hoy faltan.
// Los TESTs existentes se renombran a bench_*/bench_*_checked sin tocar
// sus cuerpos (ver informe de migración).
// ---------------------------------------------------------------------------

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "test_framework.h"
#include "helpers/alloc_stats.h"
#include "helpers/perf_verbose.h"

namespace perf_arch {

// Verbose explícito: solo con MAESTRO_PERF_VERBOSE=1.
inline bool verbose() { return perf_verbose::enabled(); }

// printf gated: usar en report_* y en futuras tablas.
inline void reportVerbose(const char* fmt, ...) {
    if (!verbose()) return;
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
}

// CHECK de presupuesto: mismo comportamiento que CHECK, pero deja
// explícito en el código que es un presupuesto de performance.
#define PERF_CHECK_BUDGET(cond) CHECK(cond)

// Wrapper funcional para presupuestos ya calculados (p.ej. allocs/op).
inline void checkBudget(bool ok, const char* cond, const char* file, int line) {
    ::testfw::report(ok, cond, file, line);
}

// Alias de arquitectura: obtener métricas.
// (Hoy delega en alloc_stats::statsFor; si cambia el backend, solo se
// toca aquí.)
inline const alloc_stats::Stats& statsFor(int scope) {
    return alloc_stats::statsFor(scope);
}

} // namespace perf_arch
