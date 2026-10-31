#pragma once

#include <cstdarg>
#include <cstdio>

// Log de debug para el redimensionado de ventana (arriba/abajo/lados).
// Escribe en /tmp/maestro_resize.log (modo append, se crea si no existe).
// Borrar el archivo antes de cada prueba para tener un log limpio.
inline void resizeLog(const char* fmt, ...) {
    FILE* f = std::fopen("/tmp/maestro_resize.log", "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(f, fmt, args);
    va_end(args);
    std::fputc('\n', f);
    std::fclose(f);
}
