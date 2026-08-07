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
// readEvent() la traduce al Event correcto (tipo + estado shift).
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
    CHECK(!e.shift);
}

TEST(terminal_plain_left_arrow) {
    Event e = parse("\x1b[D");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveLeft));
    CHECK(!e.shift);
}

TEST(terminal_shift_right_arrow) {
    Event e = parse("\x1b[1;2C");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveRight));
    CHECK(e.shift);
}

TEST(terminal_shift_left_arrow) {
    Event e = parse("\x1b[1;2D");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveLeft));
    CHECK(e.shift);
}

TEST(terminal_shift_up_arrow) {
    Event e = parse("\x1b[1;2A");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveUp));
    CHECK(e.shift);
}

TEST(terminal_shift_down_arrow) {
    Event e = parse("\x1b[1;2B");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveDown));
    CHECK(e.shift);
}

TEST(terminal_shift_home) {
    Event e = parse("\x1b[1;2H");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveHome));
    CHECK(e.shift);
}

TEST(terminal_shift_end) {
    Event e = parse("\x1b[1;2F");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveEnd));
    CHECK(e.shift);
}

TEST(terminal_shift_bare_modifier_sequence) {
    // Algunas terminales envian "ESC [ 2 A" (modificador sin el "1;"
    // del parametro). No es un formato estandar de flecha, pero no debe
    // romper nada (se ignora como None).
    Event e = parse("\x1b[2A");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
}

TEST(terminal_non_shift_modifier_ignored) {
    // Modificador ctrl (codigo 5) no es shift y v0.1 no lo maneja.
    Event e = parse("\x1b[1;5C");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
    CHECK(!e.shift);
}

TEST(terminal_plain_home_via_bracket) {
    Event e = parse("\x1b[H");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveHome));
    CHECK(!e.shift);
}

TEST(terminal_plain_end_via_bracket) {
    Event e = parse("\x1b[F");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveEnd));
    CHECK(!e.shift);
}

TEST(terminal_delete_key) {
    Event e = parse("\x1b[3~");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::Delete));
}

TEST(terminal_home_via_tilde) {
    Event e = parse("\x1b[1~");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MoveHome));
}

TEST(terminal_shift_right_via_ss3_prefix) {
    // Prefijo SS3 (ESCS O) con modificador: igual que bracket pero con 'O'.
    Event e = parse("\x1bO1;2C");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None)); // 'O' con params no se soporta
}