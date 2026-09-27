#include "test_support.h"
#include <cctype>

// Extrae la ultima posicion de cursor "\x1b[{row};{col}H" del frame.
// (El frame termina con "\x1b[?25h", que no matchea por el '?'.)
inline int cursorScreenRow(const std::string& frame) {
    int row = -1;
    size_t p = 0;
    while ((p = frame.find("\x1b[", p)) != std::string::npos) {
        size_t q = p + 2;
        size_t start = q;
        while (q < frame.size() && std::isdigit(static_cast<unsigned char>(frame[q]))) ++q;
        if (q > start && q < frame.size() && frame[q] == ';') {
            size_t r = q + 1;
            size_t cstart = r;
            while (r < frame.size() && std::isdigit(static_cast<unsigned char>(frame[r]))) ++r;
            if (r > cstart && r < frame.size() && frame[r] == 'H')
                row = std::stoi(frame.substr(start, q - start));
            p = r;
        } else {
            p = q + 1;
        }
    }
    return row;
}

// Contrato visual explicito: scroll de rueda con el cursor fuera del
// viewport deja FrameCursor.visible==false y el TTY oculta el cursor
// (sin posicionarlo). Nunca debe caer en status bar / mensajes.
//
// Setup: viewport h=10, top=20 (lineas visibles 20..29), cursor en la ultima
// fila visible (linea 29). ScrollUp mueve el viewport a top=17 sin mover el
// cursor (suppressScrollToCursor_). Entonces la fila cruda seria
// 29-17+1=13, fuera del contenido (1..10): el contrato exige ocultarlo,
// no clamparlo al borde ni pintarlo en la barra.
TEST(scroll_wheel_up_bottom_cursor_stays_in_content) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("line " + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 80;
    ed.active().viewport.top = 20;
    ed.active().cursor.line = 29; // ultima fila visible (parte baja)
    ed.active().cursor.col = 0;

    // Pre-condicion: cursor dentro del contenido (fila 10 de 1..10).
    {
        Renderer r;
        std::string before = r.buildScreen(ed.active().document, ed.active().cursor,
                                           ed.active().viewport, "t", false, "",
                                           ed.state_, std::nullopt);
        CHECK_EQ(cursorScreenRow(before), 10);
    }

    press(ed, EventType::ScrollUp);
    CHECK_EQ(ed.active().viewport.top, 17);
    CHECK_EQ(ed.active().cursor.line, 29);

    // Contrato: cursor fuera del viewport => Frame invisible y TTY oculto.
    {
        FrameBuilder fb;
        Frame f = fb.buildFrame(ed.active().document, ed.active().cursor,
                                ed.active().viewport, "t", false, "",
                                ed.state_, std::nullopt);
        CHECK(!f.cursor.visible);
    }
    Renderer r;
    std::string screen = r.buildScreen(ed.active().document, ed.active().cursor,
                                       ed.active().viewport, "t", false, "",
                                       ed.state_, std::nullopt);
    // Sin posicionamiento de cursor: oculto, nunca en status bar/mensajes.
    CHECK_EQ(cursorScreenRow(screen), -1);
    CHECK(screen.find(" q") == std::string::npos);
    CHECK(screen.find("\x1b[?25h") == std::string::npos);
}

// Caso simetrico: cursor en la parte superior + ScrollDown deja al cursor
// fuera por arriba (raw <= 0). Contrato: ocultarlo, no clamparlo a la fila 1.
TEST(scroll_wheel_down_top_cursor_stays_in_content) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("line " + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 80;
    ed.active().viewport.top = 20;
    ed.active().cursor.line = 20; // primera fila visible (parte alta)
    ed.active().cursor.col = 0;

    {
        Renderer r;
        std::string before = r.buildScreen(ed.active().document, ed.active().cursor,
                                           ed.active().viewport, "t", false, "",
                                           ed.state_, std::nullopt);
        CHECK_EQ(cursorScreenRow(before), 1);
    }

    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.active().viewport.top, 23);
    CHECK_EQ(ed.active().cursor.line, 20);

    // Contrato: cursor fuera del viewport => Frame invisible y TTY oculto.
    {
        FrameBuilder fb;
        Frame f = fb.buildFrame(ed.active().document, ed.active().cursor,
                                ed.active().viewport, "t", false, "",
                                ed.state_, std::nullopt);
        CHECK(!f.cursor.visible);
    }
    Renderer r;
    std::string screen = r.buildScreen(ed.active().document, ed.active().cursor,
                                       ed.active().viewport, "t", false, "",
                                       ed.state_, std::nullopt);
    CHECK_EQ(cursorScreenRow(screen), -1);
    CHECK(screen.find(" q") == std::string::npos);
    CHECK(screen.find("\x1b[?25h") == std::string::npos);
}
