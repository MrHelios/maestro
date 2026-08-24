#include <unistd.h>
#include <fcntl.h>

#include "terminal/Event.h"
#include "terminal/Terminal.h"
#include "test_framework.h"

// ---------------------------------------------------------------------------
// Paso 9: traduccion de secuencias de escape de la terminal a eventos.
//
// Estas pruebas redirigen temporalmente STDIN a un pipe y le escriben
// la secuencia de bytes que emite la terminal, para verificar que
// readEvent() la traduce al Event correcto.
//
// v0.5: no hay campo shift. La seleccion se activa con la letra 's' dentro
// del modo Navegacion (no con un modificador); los modificadores
// (Shift/Ctrl/Alt) que una terminal pueda anadir sobre las flechas se
// ignoran y solo importa el caracter final.
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

TEST(terminal_remapped_modified_arrow_via_keymap) {
    // Integración Terminal+Keymap: remapear "[1;2D" (la clave real que
    // Terminal pasa a Keymap) debe cambiar el evento end-to-end. Sin
    // remapeo, "[1;2D" cae por fallback a MoveLeft; con binding explícito
    // debe dar el evento remapeado.
    PipedStdin p;
    p.feed("\x1b[1;2D");
    Terminal t;
    t.keymap().bindSequence("[1;2D", EventType::PageDown);
    Event e = t.readEvent();
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::PageDown));
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

TEST(terminal_pageup_pagedown) {
    Event up = parse("\x1b[5~");
    CHECK_EQ(static_cast<int>(up.type), static_cast<int>(EventType::PageUp));
    Event down = parse("\x1b[6~");
    CHECK_EQ(static_cast<int>(down.type), static_cast<int>(EventType::PageDown));
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

// ---------------------------------------------------------------------------
// Terminal -> UTF-8: traduccion de entrada multibyte
// ---------------------------------------------------------------------------
// readEvent() debe agrupar los bytes de continuacion de un caracter UTF-8
// y emitirlos juntos en Event.text, nunca un byte suelto.
// ---------------------------------------------------------------------------

TEST(terminal_utf8_ascii_single_byte) {
    Event e = parse("a");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::InsertChar));
    CHECK_EQ(e.text, "a");
    CHECK_EQ(e.text.size(), size_t{1});
}

TEST(terminal_utf8_two_bytes) {
    // "é" = 0xC3 0xA9.
    Event e = parse("\xC3\xA9");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::InsertChar));
    CHECK_EQ(e.text, "\xC3\xA9");
    CHECK_EQ(e.text.size(), size_t{2});
}

TEST(terminal_utf8_three_bytes) {
    // "—" (em dash) = 0xE2 0x80 0x94.
    Event e = parse("\xE2\x80\x94");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::InsertChar));
    CHECK_EQ(e.text, "\xE2\x80\x94");
    CHECK_EQ(e.text.size(), size_t{3});
}

TEST(terminal_utf8_four_bytes) {
    // "😀" = 0xF0 0x9F 0x98 0x80.
    Event e = parse("\xF0\x9F\x98\x80");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::InsertChar));
    CHECK_EQ(e.text, "\xF0\x9F\x98\x80");
    CHECK_EQ(e.text.size(), size_t{4});
}

TEST(terminal_utf8_consecutive_sequences) {
    // Dos caracteres UTF-8 consecutivos producen dos eventos, cada uno
    // con su caracter completo.
    {
        PipedStdin p;
        p.feed(std::string("\xC3\xA9") + "\xE2\x80\x94"); // "é—"
        Terminal t;
        Event e1 = t.readEvent();
        Event e2 = t.readEvent();
        CHECK_EQ(e1.text, "\xC3\xA9");
        CHECK_EQ(e2.text, "\xE2\x80\x94");
        CHECK_EQ(static_cast<int>(e1.type), static_cast<int>(EventType::InsertChar));
        CHECK_EQ(static_cast<int>(e2.type), static_cast<int>(EventType::InsertChar));
    }
}

TEST(terminal_utf8_consecutive_same_char) {
    // Tres emojis seguidos: cada readEvent() entrega uno completo.
    {
        PipedStdin p;
        p.feed(std::string("\xF0\x9F\x98\x80") + "\xF0\x9F\x98\x80"); // "😀😀"
        Terminal t;
        Event e1 = t.readEvent();
        Event e2 = t.readEvent();
        CHECK_EQ(e1.text, "\xF0\x9F\x98\x80");
        CHECK_EQ(e2.text, "\xF0\x9F\x98\x80");
    }
}

TEST(terminal_utf8_mixed_with_escape_key) {
    // UTF-8 seguido de una secuencia de escape: el UTF-8 sale entero y la
    // flecha se traduce a su evento. El parser no mezcla bytes.
    {
        PipedStdin p;
        p.feed(std::string("\xC3\xA9") + "\x1b[C"); // "é" + flecha derecha
        Terminal t;
        Event e1 = t.readEvent();
        Event e2 = t.readEvent();
        CHECK_EQ(e1.text, "\xC3\xA9");
        CHECK_EQ(static_cast<int>(e2.type), static_cast<int>(EventType::MoveRight));
    }
}

TEST(terminal_escape_key_then_utf8) {
    // Secuencia de escape seguida de UTF-8: la flecha primero, el UTF-8
    // despues, sin bytes residuales.
    {
        PipedStdin p;
        p.feed(std::string("\x1b[C") + "\xE2\x80\x94"); // flecha + "—"
        Terminal t;
        Event e1 = t.readEvent();
        Event e2 = t.readEvent();
        CHECK_EQ(static_cast<int>(e1.type), static_cast<int>(EventType::MoveRight));
        CHECK_EQ(e2.text, "\xE2\x80\x94");
        CHECK_EQ(static_cast<int>(e2.type), static_cast<int>(EventType::InsertChar));
    }
}

TEST(terminal_utf8_mixed_with_plain_keys) {
    // ASCII entre caracteres UTF-8 y una flecha al final.
    {
        PipedStdin p;
        p.feed(std::string("a\xC3\xA9") + "\x1b[D"); // "aé" + flecha izquierda
        Terminal t;
        Event a = t.readEvent();
        Event e = t.readEvent();
        Event arrow = t.readEvent();
        CHECK_EQ(a.text, "a");
        CHECK_EQ(e.text, "\xC3\xA9");
        CHECK_EQ(static_cast<int>(arrow.type), static_cast<int>(EventType::MoveLeft));
    }
}