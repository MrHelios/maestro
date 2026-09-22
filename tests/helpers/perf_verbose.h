#pragma once
// ===========================================================================
// Flag único de verbosidad para tests de performance.
//
//   Modo normal (por defecto): solo "[RUN] test" / "ok". Nunca tablas.
//   Modo verbose: MAESTRO_PERF_VERBOSE=1 ... -> tablas y métricas completas.
//
// Sin dependencias: puede incluirse desde alloc_stats.h, perf_time_utils.h,
// perf_arch.h y binarios standalone (bench_RowLayout).
// ---------------------------------------------------------------------------

#include <cstdlib>
#include <cstring>

namespace perf_verbose {

// true solo con MAESTRO_PERF_VERBOSE=1 (o "true").
// Se acepta PERF_VERBOSE como alias legacy.
inline bool enabled() {
    const char* e = std::getenv("MAESTRO_PERF_VERBOSE");
    if (e && (e[0] == '1' || std::strcmp(e, "true") == 0)) return true;
    const char* legacy = std::getenv("PERF_VERBOSE");
    return legacy && (legacy[0] == '1' || std::strcmp(legacy, "true") == 0);
}

} // namespace perf_verbose
