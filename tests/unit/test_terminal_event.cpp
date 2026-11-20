#include <unistd.h>
#include <fcntl.h>

#include "test_framework.h"
#include "platform/InputEvent.h"
#include "platform/tty/Terminal.h"
#include "platform/tty/TtyMouse.h"

#define private public
#include "app/Editor.h"
#undef private

namespace {

struct PipedStdin {
    int restoreFd = -1;
    int pipeEnds[2];

    PipedStdin() {
        // conservar el stdin real
        restoreFd = dup(STDIN_FILENO); 
        if (pipe(pipeEnds) == 0) {
            // lector -> stdin
            dup2(pipeEnds[0], STDIN_FILENO); 
            close(pipeEnds[0]);
        }
    }
    
    // Escribir todo de una vez
    void feed(const std::string& bytes) {
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

InputEvent parse(const char* seq) {
    PipedStdin p;
    Terminal t;
    
    p.feed(seq);
    return t.readEvent();
}

} // namespace

TEST(terminal_event_plain_right_arrow) {
    InputEvent e = parse("\x1b[C");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MoveRight));
}

TEST(terminal_event_plain_left_arrow) {
    InputEvent e = parse("\x1b[D");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MoveLeft));
}

// Flecha derecha con modificadores.
// Los modificadores se ignoran: se traduce al mismo movimiento.
TEST(terminal_event_modified_right_arrow_ignored) {
    CHECK_EQ(static_cast<int>(parse("\x1b[1;2C").type),
             static_cast<int>(InputEventType::MoveRight)); // Shift
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;3C").type),
             static_cast<int>(InputEventType::MoveRight)); // Alt
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;5C").type),
             static_cast<int>(InputEventType::MoveRight)); // Ctrl
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;7C").type),
             static_cast<int>(InputEventType::MoveRight)); // Ctrl + Alt
}

// Flecha izquierda con modificadores.
// Los modificadores se ignoran: se traduce al mismo movimiento.
TEST(terminal_event_modified_left_arrow_ignored) {
    CHECK_EQ(static_cast<int>(parse("\x1b[1;2D").type),
             static_cast<int>(InputEventType::MoveLeft)); // Shift
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;3D").type),
             static_cast<int>(InputEventType::MoveLeft)); // Alt
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;5D").type),
             static_cast<int>(InputEventType::MoveLeft)); // Ctrl
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;7D").type),
             static_cast<int>(InputEventType::MoveLeft)); // Ctrl + Alt
}

// Flecha arriba con y sin modificadores.
// Los modificadores se ignoran: se traduce al mismo movimiento.
TEST(terminal_event_modified_up_arrow_ignored) {
    CHECK_EQ(static_cast<int>(parse("\x1b[A").type),
             static_cast<int>(InputEventType::MoveUp)); // Sin modificador
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;2A").type),
             static_cast<int>(InputEventType::MoveUp)); // Shift
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;3A").type),
             static_cast<int>(InputEventType::MoveUp)); // Alt
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;5A").type),
             static_cast<int>(InputEventType::MoveUp)); // Ctrl
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;7A").type),
             static_cast<int>(InputEventType::MoveUp)); // Ctrl + Alt
}

// Flecha abajo con y sin modificadores.
// Los modificadores se ignoran: se traduce al mismo movimiento.
TEST(terminal_event_modified_down_arrow_ignored) {
    CHECK_EQ(static_cast<int>(parse("\x1b[B").type),
             static_cast<int>(InputEventType::MoveDown)); // Sin modificador
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;2B").type),
             static_cast<int>(InputEventType::MoveDown)); // Shift
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;3B").type),
             static_cast<int>(InputEventType::MoveDown)); // Alt
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;5B").type),
             static_cast<int>(InputEventType::MoveDown)); // Ctrl
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;7B").type),
             static_cast<int>(InputEventType::MoveDown)); // Ctrl + Alt
}

// Home con y sin modificadores.
// Los modificadores se ignoran: se traduce al mismo movimiento.
TEST(terminal_event_modified_home_ignored) {
    CHECK_EQ(static_cast<int>(parse("\x1b[H").type),
             static_cast<int>(InputEventType::MoveHome)); // Sin modificador
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;2H").type),
             static_cast<int>(InputEventType::MoveHome)); // Shift
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;3H").type),
             static_cast<int>(InputEventType::MoveHome)); // Alt
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;5H").type),
             static_cast<int>(InputEventType::MoveHome)); // Ctrl
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;7H").type),
             static_cast<int>(InputEventType::MoveHome)); // Ctrl + Alt
}

// End con y sin modificadores.
// Los modificadores se ignoran: se traduce al mismo movimiento.
TEST(terminal_event_modified_end_ignored) {
    CHECK_EQ(static_cast<int>(parse("\x1b[F").type),
             static_cast<int>(InputEventType::MoveEnd)); // Sin modificador
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;2F").type),
             static_cast<int>(InputEventType::MoveEnd)); // Shift
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;3F").type),
             static_cast<int>(InputEventType::MoveEnd)); // Alt
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;5F").type),
             static_cast<int>(InputEventType::MoveEnd)); // Ctrl
             
    CHECK_EQ(static_cast<int>(parse("\x1b[1;7F").type),
             static_cast<int>(InputEventType::MoveEnd)); // Ctrl + Alt
}

// "[1;2D" cae por fallback a MoveLeft
// con binding explícito debe dar el evento remapeado cae a PageDown
TEST(terminal_event_remapped_modified_arrow_via_keymap) {
    PipedStdin p;
    Terminal t;
    InputEvent e;
    
    p.feed("\x1b[1;2D");
    t.keymapIface().bindSequence("[1;2D", InputEventType::PageDown);
    e = t.readEvent();
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::PageDown));
}

TEST(terminal_event_delete_key) {
    InputEvent e = parse("\x1b[3~");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::Delete));
}

// PageUp con y sin modificadores.
// Los modificadores se ignoran: se traduce al mismo movimiento.
// @falta agregar que maneje modificadores PageUp
TEST(terminal_event_modified_pageup_ignored) {
    CHECK_EQ(static_cast<int>(parse("\x1b[5~").type),
             static_cast<int>(InputEventType::PageUp)); // Sin modificador
             
    /*
    CHECK_EQ(static_cast<int>(parse("\x1b[5;2~").type),
             static_cast<int>(InputEventType::PageUp)); // Shift
    CHECK_EQ(static_cast<int>(parse("\x1b[5;3~").type),
             static_cast<int>(InputEventType::PageUp)); // Alt
    CHECK_EQ(static_cast<int>(parse("\x1b[5;5~").type),
             static_cast<int>(InputEventType::PageUp)); // Ctrl
    CHECK_EQ(static_cast<int>(parse("\x1b[5;7~").type),
             static_cast<int>(InputEventType::PageUp)); // Ctrl + Alt
     */
}

// PageDown con y sin modificadores.
// Los modificadores se ignoran: se traduce al mismo movimiento.
// @falta agregar que maneje modificadores PageDown
TEST(terminal_event_modified_pagedown_ignored) {
    CHECK_EQ(static_cast<int>(parse("\x1b[6~").type),
             static_cast<int>(InputEventType::PageDown)); // Sin modificador
             
    /*
    CHECK_EQ(static_cast<int>(parse("\x1b[6;2~").type),
             static_cast<int>(InputEventType::PageDown)); // Shift
    CHECK_EQ(static_cast<int>(parse("\x1b[6;3~").type),
             static_cast<int>(InputEventType::PageDown)); // Alt
    CHECK_EQ(static_cast<int>(parse("\x1b[6;5~").type),
             static_cast<int>(InputEventType::PageDown)); // Ctrl
    CHECK_EQ(static_cast<int>(parse("\x1b[6;7~").type),
             static_cast<int>(InputEventType::PageDown)); // Ctrl + Alt
    */
}

// No hay soporte para SS3
// actualmente usamos CSI
TEST(terminal_event_ss3_with_params_unsupported) {
    InputEvent e = parse("\x1bO1;2C");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
}

TEST(terminal_event_lone_escape) {
    InputEvent e = parse("\x1b");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::Escape));
}

// ESC + '[' sin caracter final: se descarta (None)
TEST(terminal_event_incomplete_sequence_times_out) {
    InputEvent e = parse("\x1b[");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
}

// ESC seguido de algo que no sea una secuencia de control conocida
TEST(terminal_event_escape_followed_by_char_ignored) {
    InputEvent e = parse("\x1bx");
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
}

// ---------------------------------------------------------------------------
// Terminal -> UTF-8: traduccion de entrada multibyte
// ---------------------------------------------------------------------------

TEST(terminal_event_utf8_ascii_single_byte) {
    InputEvent e = parse("a");
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::InsertChar));
    CHECK_EQ(e.text, "a");
    CHECK_EQ(e.text.size(), size_t{1});
}

// "é" = 0xC3 0xA9.
TEST(terminal_event_utf8_two_bytes) {
    InputEvent e = parse("\xC3\xA9");
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::InsertChar));
    CHECK_EQ(e.text, "\xC3\xA9");
    CHECK_EQ(e.text.size(), size_t{2});
}

// "—" (em dash) = 0xE2 0x80 0x94.
TEST(terminal_event_utf8_three_bytes) {
    InputEvent e = parse("\xE2\x80\x94");
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::InsertChar));
    CHECK_EQ(e.text, "\xE2\x80\x94");
    CHECK_EQ(e.text.size(), size_t{3});
}

// "😀" = 0xF0 0x9F 0x98 0x80
TEST(terminal_event_utf8_four_bytes) {
    InputEvent e = parse("\xF0\x9F\x98\x80");
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::InsertChar));
    CHECK_EQ(e.text, "\xF0\x9F\x98\x80");
    CHECK_EQ(e.text.size(), size_t{4});
}

// Dos caracteres UTF-8 consecutivos producen dos eventos, 
// cada uno con su caracter completo.
TEST(terminal_event_utf8_consecutive_sequences) {
    PipedStdin p;
    Terminal t;
    
    // "é—"
    p.feed(std::string("\xC3\xA9") + "\xE2\x80\x94"); 
    
    InputEvent e1 = t.readEvent();
    InputEvent e2 = t.readEvent();
    
    CHECK_EQ(e1.text, "\xC3\xA9");
    CHECK_EQ(e2.text, "\xE2\x80\x94");
    CHECK_EQ(static_cast<int>(e1.type), static_cast<int>(InputEventType::InsertChar));
    CHECK_EQ(static_cast<int>(e2.type), static_cast<int>(InputEventType::InsertChar));
}

// Dos emojis seguidos: cada readEvent() entrega uno completo.
TEST(terminal_event_utf8_consecutive_same_char) {
    PipedStdin p;
    Terminal t;
    
    // "😀😀"
    p.feed(std::string("\xF0\x9F\x98\x80") + "\xF0\x9F\x98\x80"); 
    
    InputEvent e1 = t.readEvent();
    InputEvent e2 = t.readEvent();
    
    CHECK_EQ(e1.text, "\xF0\x9F\x98\x80");
    CHECK_EQ(e2.text, "\xF0\x9F\x98\x80");
}

// UTF-8 seguido de una secuencia de escape: 
// el UTF-8 sale entero y la flecha se traduce a su evento. 
// El parser no mezcla bytes.
TEST(terminal_event_utf8_mixed_with_escape_key) {
    PipedStdin p;
    Terminal t;
    
    p.feed(std::string("\xC3\xA9") + "\x1b[C"); // "é" + flecha derecha
    
    InputEvent e1 = t.readEvent();
    InputEvent e2 = t.readEvent();
    
    CHECK_EQ(e1.text, "\xC3\xA9");
    CHECK_EQ(static_cast<int>(e2.type), static_cast<int>(InputEventType::MoveRight));
}

// Secuencia de escape seguida de UTF-8: 
// la flecha primero, el UTF-8
// despues, sin bytes residuales.
TEST(terminal_event_escape_key_then_utf8) {
    PipedStdin p;
    Terminal t;
    
    p.feed(std::string("\x1b[C") + "\xE2\x80\x94"); // flecha + "—"
    
    InputEvent e1 = t.readEvent();
    InputEvent e2 = t.readEvent();
    
    CHECK_EQ(static_cast<int>(e1.type), static_cast<int>(InputEventType::MoveRight));
    CHECK_EQ(e2.text, "\xE2\x80\x94");
    CHECK_EQ(static_cast<int>(e2.type), static_cast<int>(InputEventType::InsertChar));
}

// ASCII entre caracteres UTF-8 y una flecha al final.
TEST(terminal_event_utf8_mixed_with_plain_keys) {
    PipedStdin p;
    Terminal t;
    
    p.feed(std::string("a\xC3\xA9") + "\x1b[D"); // "aé" + flecha izquierda
    
    InputEvent a = t.readEvent();
    InputEvent e = t.readEvent();
    InputEvent arrow = t.readEvent();
    
    CHECK_EQ(a.text, "a");
    CHECK_EQ(e.text, "\xC3\xA9");
    CHECK_EQ(static_cast<int>(arrow.type), static_cast<int>(InputEventType::MoveLeft));
}

// ScrollUp con y sin modificadores.
TEST(terminal_event_scroll_up_modifiers) {
    CHECK_EQ(static_cast<int>(parse("\x1b[<64;12;5M").type),
             static_cast<int>(InputEventType::ScrollUp)); // Sin modificador
             
    CHECK_EQ(static_cast<int>(parse("\x1b[<68;12;5M").type),
             static_cast<int>(InputEventType::ScrollUp)); // Shift
             
    CHECK_EQ(static_cast<int>(parse("\x1b[<72;12;5M").type),
             static_cast<int>(InputEventType::ScrollUp)); // Alt
             
    CHECK_EQ(static_cast<int>(parse("\x1b[<80;12;5M").type),
             static_cast<int>(InputEventType::ScrollUp)); // Ctrl
             
    CHECK_EQ(static_cast<int>(parse("\x1b[<88;12;5M").type),
             static_cast<int>(InputEventType::ScrollUp)); // Ctrl + Alt
}

// ScrollDown con y sin modificadores.
TEST(terminal_event_scroll_down_modifiers) {
    CHECK_EQ(static_cast<int>(parse("\x1b[<65;12;5M").type),
             static_cast<int>(InputEventType::ScrollDown)); // Sin modificador
             
    CHECK_EQ(static_cast<int>(parse("\x1b[<69;12;5M").type),
             static_cast<int>(InputEventType::ScrollDown)); // Shift
             
    CHECK_EQ(static_cast<int>(parse("\x1b[<73;12;5M").type),
             static_cast<int>(InputEventType::ScrollDown)); // Alt
             
    CHECK_EQ(static_cast<int>(parse("\x1b[<81;12;5M").type),
             static_cast<int>(InputEventType::ScrollDown)); // Ctrl
             
    CHECK_EQ(static_cast<int>(parse("\x1b[<89;12;5M").type),
             static_cast<int>(InputEventType::ScrollDown)); // Ctrl + Alt
}

TEST(terminal_event_scroll_then_char) {
    PipedStdin p;
    Terminal t;
    
    p.feed(std::string("\x1b[<64;12;5M") + "a");
    
    InputEvent s = t.readEvent();
    InputEvent c = t.readEvent();
    
    CHECK_EQ(static_cast<int>(s.type), static_cast<int>(InputEventType::ScrollUp));
    CHECK_EQ(static_cast<int>(c.type), static_cast<int>(InputEventType::InsertChar));
    CHECK_EQ(c.text, "a");
}

TEST(terminal_event_char_scroll_char) {
    PipedStdin p;
    Terminal t;
    
    p.feed(std::string("a\x1b[<64;12;5M") + "b");
    
    InputEvent a = t.readEvent();
    InputEvent s = t.readEvent();
    InputEvent b = t.readEvent();
    
    CHECK_EQ(static_cast<int>(a.type), static_cast<int>(InputEventType::InsertChar));
    CHECK_EQ(a.text, "a");
    CHECK_EQ(static_cast<int>(s.type), static_cast<int>(InputEventType::ScrollUp));
    CHECK_EQ(static_cast<int>(b.type), static_cast<int>(InputEventType::InsertChar));
    CHECK_EQ(b.text, "b");
}

// ---------------------------------------------------------------------------
// SGR
// ---------------------------------------------------------------------------

// ScrollUp con y sin modificadores.
TEST(terminal_event_parse_mouse_sgr_direct_up) {
    InputEvent e;

    Terminal::parseMouseSgr("[<64;10;5M", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollUp));

    Terminal::parseMouseSgr("[<68;10;5M", e); // Shift
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollUp));

    Terminal::parseMouseSgr("[<72;10;5M", e); // Alt
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollUp));

    Terminal::parseMouseSgr("[<80;10;5M", e); // Ctrl
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollUp));

    Terminal::parseMouseSgr("[<88;10;5M", e); // Ctrl + Alt
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollUp));
}

// ScrollDown con y sin modificadores.
TEST(terminal_event_parse_mouse_sgr_direct_down) {
    InputEvent e;

    Terminal::parseMouseSgr("[<65;10;5M", e);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollDown));

    Terminal::parseMouseSgr("[<69;10;5M", e); // Shift
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollDown));

    Terminal::parseMouseSgr("[<73;10;5M", e); // Alt
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollDown));

    Terminal::parseMouseSgr("[<81;10;5M", e); // Ctrl
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollDown));

    Terminal::parseMouseSgr("[<89;10;5M", e); // Ctrl + Alt
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollDown));
}

TEST(terminal_event_parse_mouse_sgr_direct_release_ignored) {
    InputEvent e;
    Terminal::parseMouseSgr("[<64;10;5m", e);
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
}

// left press genera MousePress (ya no None).
// Decoder convierte SGR 1-based a CellPos 0-based: (10,5) -> (9,4)
TEST(terminal_event_parse_mouse_sgr_direct_click_ignored) {
    InputEvent e;
    Terminal::parseMouseSgr("[<0;10;5M", e);
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MousePress));
    CHECK_EQ(e.cell.col, 9);
    CHECK_EQ(e.cell.row, 4);
}

TEST(terminal_event_parse_mouse_sgr_direct_unknown) {
    InputEvent e;
    Terminal::parseMouseSgr("[<99;10;5M", e);
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
}

TEST(terminal_event_parse_mouse_sgr_direct_malformed) {
    InputEvent e;
    Terminal::parseMouseSgr("[<64M", e);
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
}

TEST(terminal_event_parse_mouse_sgr_direct_down_with_ctrl) {
    InputEvent e;
    Terminal::parseMouseSgr("[<81;10;5M", e);
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollDown));
}

// End llega al final exclusivo de la línea en la última fila visual.
TEST(terminal_event_end_key_reaches_line_end) {
    Editor ed;
    PipedStdin p;
    Terminal t;
    InputEvent e;
    
    ed.active().filename = "notes";
    ed.active().document.restore({std::string(30, 'a')});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    
    // Inyectar secuencia ESC [ F (End) via pipe al Terminal parser.
    p.feed("\x1b[F");

    e = t.readEvent();
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MoveEnd));
    ed.handleEvent(e);

    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 30);
    CHECK_EQ(ed.active().cursor.col, ed.active().document.lineLength(0));
}

// Terminal es un IEventSource.
TEST(terminal_event_is_event_source) {
    Terminal t;
    
    IEventSource* src = &t;
    CHECK(src != nullptr);
}

// decoder SGR -> CellPos + InputEvent.
TEST(terminal_event_mouse_decoder_cellpos) {
    struct Case {
        const char* seq;
        InputEventType type;
        bool checkPos;   // los casos sin coordenadas validas no chequean posicion
        CellPos pos;
    };

    const Case cases[] = {
        {"[<0;10;5M",  InputEventType::MousePress,   true,  {9, 4}},
        {"[<32;11;6M", InputEventType::MouseDrag,    true,  {10, 5}},
        {"[<3;11;6m",  InputEventType::MouseRelease, true,  {10, 5}},
        // Rueda con Shift (68 = 64|4) sigue siendo ScrollUp.
        {"[<68;10;5M", InputEventType::ScrollUp,     false, {}},
        // Rueda hacia abajo.
        {"[<65;10;5M", InputEventType::ScrollDown,   false, {}},
        // Secuencia incompleta -> None.
        {"[<64M",      InputEventType::None,         false, {}},
    };

    for (const Case& c : cases) {
        InputEvent e;
        CellPos p;
        decodeMouseSgr(c.seq, e, p);
        
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(c.type));
        if (c.checkPos) {
            CHECK((p == c.pos));
            CHECK_EQ(e.cell.col, c.pos.col);
            CHECK_EQ(e.cell.row, c.pos.row);
        }
    }

    // Terminal delega en el mismo decoder
    InputEvent e;
    Terminal::parseMouseSgr("[<0;10;5M", e);
    
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MousePress));
    CHECK_EQ(e.cell.col, 9);
    CHECK_EQ(e.cell.row, 4);
}
