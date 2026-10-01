#pragma once

#include <string>

#include "rendering/Sink.h"

// ---------------------------------------------------------------------------
// Sinks del backend TTY. Objetos sin estado global: el dueño los posee y los
// inyecta por referencia (Renderer::render*(..., Sink&)). No hay flag de
// test ni métodos estáticos: los tests que solo usan build*() no necesitan
// ninguna preparación, y los que verifican escritura usan NullSink (vive en
// rendering/Sink.h, zona común) o un TtySink real sobre el fd que capturan.
// ---------------------------------------------------------------------------
class TtySink : public Sink {
public:
    bool writeStdout(const std::string& s) override;

private:
    // Helper interno (reintenta EINTR). No es parte de la interfaz: el fd
    // es detalle de este backend, no de la abstracción Sink.
    static bool writeAll(int fd, const std::string& s);
};
