#pragma once

#include <string>

// ---------------------------------------------------------------------------
// Sink: frontera de escritura para Renderer::render*.
//
// Es una interfaz pura de UN método: recibe el frame ya codificado y lo
// entrega sin saber dónde termina. Sin fd, sin errno, sin write(2), sin
// STDOUT_FILENO, sin ANSI:
//   - TtySink  (rendering/tty/): escribe a la salida estándar real.
//   - NullSink (rendering/tty/): descarta la escritura (tests).
//
// REGLA: ningún Sink tiene estado global. Son objetos con lifetime
// explícito: el dueño (main, tests) los posee e inyecta por referencia.
// ---------------------------------------------------------------------------
class Sink {
public:
    virtual ~Sink() = default;

    // Entrega el buffer de salida. Devuelve false si la escritura falló
    // (el Renderer invalida su cache diferencial en ese caso).
    virtual bool writeStdout(const std::string& s) = 0;
};
