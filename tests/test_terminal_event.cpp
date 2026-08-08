#include <unistd.h>
#include <fcntl.h>

#include "Event.h"
#include "Terminal.h"
#include "test_framework.h"

// ---------------------------------------------------------------------------
// Paso 9: traduccion de secuencias de escape de la terminal a eventos.
//
// Estas pruebas redirigen temporalmente STDIN a un pipe y le escriben
// la secuencia de bytes que emite la terminal, para verificar que
// readEvent() la traduce al Event correcto.
//
// v0.3: no hay campo shift. La seleccion se activa con Ctrl+S; los
// modificadores (Shift/Ctrl/Alt) que una terminal pueda anadir sobre las
// flechas se ignoran y solo importa el caracter final.
// ---------------------------------------------------------------------------

namespace {

struct PipedStdin {
    int restoreFd = -1;
    int pipeEnds[2];

    PipedStdin() {
        restoreFd = dup(STDIN_FILENO); // conservar el stdin real
        if (pipe(pipeEnds) == 0) {
            dup2(pipeEnds[0], STDIN_FILENO); // lector -> stdin
            close(pipeEnds[0]);
        }
    }

    void feed(const std::string& bytes) {
        // Escribir todo de una vez (los read() bloqueantes del parser
        // consumiran exactamente los bytes que necesiten).
        ssize_t w = write(pipeEnds[1], bytes.data(), bytes.size());
        (void)w;
    }

    ~PipedStdin() {
        close(pipeEnds[1]);
        // Cada caso usa un pipe nuevo para no arrastrar bytes.
        if (restoreFd >= 0) {
            dup2(restoreFd, STDIN_FILENO);
            close(restoreFd);
        }
    }
};

Event parse(const char* seq) {
    PipedStdin p;
    p.feed(seq);
    Terminal t;
    return t.readEvent();
}

} // namespace

TEST(terminal_plain_right_arrow) {
    Event e = parse("\x1b[C");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveRight));
}

TEST(terminal_plain_left_arrow) {
    Event e = parse("\x1b[D");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveLeft));
}

TEST(terminal_modified_right_arrow_ignored) {
    // "ESC [ 1;2C" = flecha derecha con modificador. El modificador (2=
    // shift... ) se ignora: se traduce al mismo movimiento.
    Event e = parse("\x1b[1;2C");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveRight));
}

TEST(terminal_modified_left_arrow_ignored) {
    Event e = parse("\x1b[1;2D");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveLeft));
}

TEST(terminal_modified_up_arrow_ignored) {
    Event e = parse("\x1b[1;2A");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveUp));
}

TEST(terminal_modified_down_arrow_ignored) {
    Event e = parse("\x1b[1;2B");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveDown));
}

TEST(terminal_modified_home_ignored) {
    Event e = parse("\x1b[1;2H");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveHome));
}

TEST(terminal_modified_end_ignored) {
    Event e = parse("\x1b[1;2F");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveEnd));
}

TEST(terminal_bare_modifier_sequence) {
    // "ESC [ 2 A" (modificador sin parametro "1;"). Con los modificadores
    // ignorados termina siendo una flecha arriba.
    Event e = parse("\x1b[2A");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveUp));
}

TEST(terminal_ctrl_modifier_ignored) {
    // Modificador ctrl (codigo 5): tambien se ignora, es una flecha derecha.
    Event e = parse("\x1b[1;5C");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveRight));
}

TEST(terminal_plain_home_via_bracket) {
    Event e = parse("\x1b[H");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveHome));
}

TEST(terminal_plain_end_via_bracket) {
    Event e = parse("\x1b[F");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveEnd));
}

TEST(terminal_delete_key) {
    Event e = parse("\x1b[3~");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::Delete));
}

TEST(terminal_home_via_tilde) {
    Event e = parse("\x1b[1~");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveHome));
}

TEST(terminal_ss3_with_params_unsupported) {
    // Prefijo SS3 (ESC O) con parametros no se soporta: None.
    Event e = parse("\x1bO1;2C");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
}

TEST(terminal_lone_escape) {
    Event e = parse("\x1b");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::Escape));
}

TEST(terminal_incomplete_sequence_times_out) {
    // ESC + '[' sin caracter final: se descarta (None) tras el timeout,
    // en lugar de bloquear para siempre esperando el ultimo byte.
    Event e = parse("\x1b[");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
}

TEST(terminal_escape_followed_by_char_ignored) {
    // ESC seguido de algo que no sea una secuencia de control conocida
    // no es un ESC suelto: la secuencia completa se descarta (None).
    Event e = parse("\x1bx");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
}