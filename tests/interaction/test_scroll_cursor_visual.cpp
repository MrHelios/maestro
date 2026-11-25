#include "test_support.h"
#include "rendering/tty/TtyRenderer.h"
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
        TtyRenderer r;
        std::string before = r.buildScreen(ed.active().document, ed.active().cursor,
                                           ed.active().viewport, "t", false,
                                           makeChromeRequest("", ed.state_), std::nullopt);
        CHECK_EQ(cursorScreenRow(before), 10);
    }

    press(ed, InputEventType::ScrollUp);
    CHECK_EQ(ed.active().viewport.top, 17);
    CHECK_EQ(ed.active().cursor.line, 29);

    // Contrato: cursor fuera del viewport => Frame invisible y TTY oculto.
    {
        FrameBuilder fb;
        Frame f = fb.buildFrame(ed.active().document, ed.active().cursor,
                                ed.active().viewport, "t", false,
                                makeChromeRequest("", ed.state_), std::nullopt);
        CHECK(!f.cursor.visible);
    }
    TtyRenderer r;
    std::string screen = r.buildScreen(ed.active().document, ed.active().cursor,
                                       ed.active().viewport, "t", false,
                                       makeChromeRequest("", ed.state_), std::nullopt);
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
        TtyRenderer r;
        std::string before = r.buildScreen(ed.active().document, ed.active().cursor,
                                           ed.active().viewport, "t", false,
                                           makeChromeRequest("", ed.state_), std::nullopt);
        CHECK_EQ(cursorScreenRow(before), 1);
    }

    press(ed, InputEventType::ScrollDown);
    CHECK_EQ(ed.active().viewport.top, 23);
    CHECK_EQ(ed.active().cursor.line, 20);

    // Contrato: cursor fuera del viewport => Frame invisible y TTY oculto.
    {
        FrameBuilder fb;
        Frame f = fb.buildFrame(ed.active().document, ed.active().cursor,
                                ed.active().viewport, "t", false,
                                makeChromeRequest("", ed.state_), std::nullopt);
        CHECK(!f.cursor.visible);
    }
    TtyRenderer r;
    std::string screen = r.buildScreen(ed.active().document, ed.active().cursor,
                                       ed.active().viewport, "t", false,
                                       makeChromeRequest("", ed.state_), std::nullopt);
    CHECK_EQ(cursorScreenRow(screen), -1);
    CHECK(screen.find(" q") == std::string::npos);
    CHECK(screen.find("\x1b[?25h") == std::string::npos);
}

// Soft-wrap: cursor debajo del viewport al navegar hacia abajo. Con 5 filas de
// viewport y 3 líneas de 3 filas visuales cada una (textWidth=12), el cursor en
// la primera fila de la línea 2 está en la fila visual 6. target = 6 - 5 + 1 =
// 2; target cae dentro del rango visual de L0 (filas 0-2), así que empezamos
// en L0 con topOffset=2 para mostrar contexto previo.
// Mostraríamos: L0 fila 2, L1 filas 3-5, L2 fila 6 (cursor en última fila visible).
TEST(softwrap_autoscroll_below_viewport_picks_nearest_line) {
    Editor ed;
    ed.active().filename = "notes.md";
    // textWidth = 19 - gutter(7) = 12; cada línea de 30 chars -> 3 filas.
    std::vector<std::string> lines;
    for (int i = 0; i < 3; ++i)
        lines.push_back(std::string(30, static_cast<char>('a' + i)));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 5;
    ed.active().viewport.width = 19;
    ed.active().viewport.top = 0;
    NullSink null;
    ed.setSink(null);
    // Línea 2 ocupa filas visuales 6,7,8; cursor en su primera fila (6).
    ed.active().cursor.line = 2;
    ed.active().cursor.col = 0;

    ed.renderFrame();

    // target = 2 cae dentro de L0 (filas 0-2): empezar en L0 con offset 2.
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().viewport.topOffset, 2);
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 0);
}

// Soft-wrap: scroll fino dentro de una línea lógica más alta que el viewport.
// 1 línea de 30 chars, textWidth=12 -> 3 filas visuales. Viewport de 2 filas.
// Cursor en la fila visual 2 (última fila de la línea). topOffset debe ajustar
// para que el cursor quede visible (topOffset=1, primera fila visible = 1).
TEST(softwrap_topOffset_scrolls_inside_long_line) {
    Editor ed;
    ed.active().filename = "notes.md";
    ed.active().document.restore({std::string(30, 'a')});
    ed.active().viewport.height = 2;   // 2 filas visibles
    ed.active().viewport.width = 19;   // textWidth = 12 -> 3 filas por línea
    ed.active().viewport.top = 0;
    ed.active().viewport.topOffset = 0;
    // Línea 0 ocupa filas visuales 0,1,2. Cursor en fila visual 2 (col >= 24).
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 25;

    NullSink null;
    ed.setSink(null);
    ed.renderFrame();

    // topOffset ajustado para mostrar la fila del cursor en la última fila visible.
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().viewport.topOffset, 1);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 25);
}

// Soft-wrap: applyScroll puede desplazar topOffset dentro de una línea larga.
// 1 línea de 30 chars, textWidth=12 -> 3 filas visuales. Viewport de 2 filas.
// ScrollDown (delta=+1) desde top=0,topOffset=0 -> top=0,topOffset=1.
TEST(softwrap_applyScroll_moves_topOffset_inside_long_line) {
    Editor ed;
    ed.active().filename = "notes.md";
    NullSink null;
    ed.setSink(null);
    ed.active().document.restore({std::string(30, 'a')});
    ed.active().viewport.height = 2;   // 2 filas visibles
    ed.active().viewport.width = 19;   // textWidth = 12 -> 3 filas por línea
    ed.active().viewport.top = 0;
    ed.active().viewport.topOffset = 0;

    ed.applyScroll(1);

    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().viewport.topOffset, 1);

    // ScrollDown de nuevo desde topOffset=1: maxFirstRow = 3 - 2 = 1, así que
    // no se puede avanzar más (ya se muestra la última fila de la línea).
    ed.applyScroll(1);
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().viewport.topOffset, 1);

    // Tercer ScrollDown: mismo resultado, ya en el límite.
    ed.applyScroll(1);
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().viewport.topOffset, 1);
}

// ScrollUp restaura topOffset hacia 0.
TEST(softwrap_applyScroll_restore_topOffset) {
    Editor ed;
    ed.active().filename = "notes.md";
    NullSink null;
    ed.setSink(null);
    ed.active().document.restore({std::string(30, 'a')});
    ed.active().viewport.height = 2;
    ed.active().viewport.width = 19;
    ed.active().viewport.top = 0;
    ed.active().viewport.topOffset = 2;  // última fila de la línea

    ed.applyScroll(-1);
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().viewport.topOffset, 1);

    ed.applyScroll(-1);
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().viewport.topOffset, 0);

    // ScrollUp desde topOffset=0: ya no se puede más.
    ed.applyScroll(-1);
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().viewport.topOffset, 0);
}

// Soft-wrap: topOffset se valida tras un resize que reduce las filas de la
// línea. 1 línea de 30 chars, textWidth=12 -> 3 filas visuales. Viewport de
// 2 filas. topOffset=1 (muestra filas 1-2). Al ampliar el ancho, la línea
// ocupa 1 fila visual y topOffset debe clamp a 0.
TEST(softwrap_topOffset_clamped_on_resize) {
    Editor ed;
    ed.active().filename = "notes.md";
    NullSink null;
    ed.setSink(null);
    ed.active().document.restore({std::string(30, 'a')});
    ed.active().viewport.height = 2;
    ed.active().viewport.width = 19;   // textWidth=12 -> 3 filas
    ed.active().viewport.top = 0;
    ed.active().viewport.topOffset = 1;
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 15;      // fila visual 1

    ed.resize(2, 40);                 // textWidth=33 >= 30 -> 1 fila visual

    ed.renderFrame();

    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().viewport.topOffset, 0);  // clamp: solo 1 fila
}

// Soft-wrap: topOffset se valida tras una edición que reduce las filas de la
// línea. 1 línea de 30 chars, textWidth=12 -> 3 filas visuales. topOffset=2
// (última fila). Se acorta la línea a 5 chars -> 1 fila visual. topOffset
// debe clamp a 0.
TEST(softwrap_topOffset_clamped_on_edit_reducing_rows) {
    Editor ed;
    ed.active().filename = "notes.md";
    NullSink null;
    ed.setSink(null);
    ed.active().document.restore({std::string(30, 'a')});
    ed.active().viewport.height = 2;
    ed.active().viewport.width = 19;   // textWidth=12 -> 3 filas
    ed.active().viewport.top = 0;
    ed.active().viewport.topOffset = 2;  // última fila de la línea
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;

    // Editar: acortar la línea a 5 caracteres (1 fila visual).
    ed.active().document.restore({std::string(5, 'b')});

    ed.renderFrame();

    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().viewport.topOffset, 0);  // clamp: solo 1 fila
}

// Soft-wrap P2: salto a línea larga debajo del viewport. Cuando la búsqueda
// termina con l == cursor.line (la línea del cursor se convierte en el nuevo
// top), se debe calcular topOffset para mostrar el cursor, no dejarlo en 0.
// Línea 0: 1 fila, Línea 1: 1 fila, Línea 2: 10 filas (120 chars, textWidth=12).
// Viewport 2 filas. Cursor en fila visual 5 de la línea 2 (fila 7 total).
TEST(softwrap_autoscroll_jump_to_long_line_calculates_offset) {
    Editor ed;
    ed.active().filename = "notes.md";
    NullSink null;
    ed.setSink(null);
    std::vector<std::string> lines;
    lines.push_back("a");                              // 1 fila visual (fila 0)
    lines.push_back("b");                              // 1 fila visual (fila 1)
    lines.push_back(std::string(120, 'c'));            // 10 filas visuales (filas 2-11)
    ed.active().document.restore(lines);
    ed.active().viewport.height = 2;
    ed.active().viewport.width = 19;   // textWidth = 12
    ed.active().viewport.top = 0;
    ed.active().viewport.topOffset = 0;
    // Línea 2, columna 65 -> fila visual 5 de esa línea (fila 7 total).
    ed.active().cursor.line = 2;
    ed.active().cursor.col = 65;

    ed.renderFrame();

    // top=0 deja al cursor en la fila 7, fuera del viewport [0,2).
    // top=2, topOffset=4 -> topRow=6, cursor en fila relativa 1 (visible).
    CHECK_EQ(ed.active().viewport.top, 2);
    CHECK_EQ(ed.active().viewport.topOffset, 4);
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 65);
}

// Soft-wrap P2 mejorado: cuando el cursor está debajo del viewport en una
// línea corta, pero el target cae dentro de una línea anterior larga, se
// debe empezar en esa línea anterior con el topOffset adecuado para mostrar
// contexto previo antes del cursor.
// L0: 1 fila (fila 0)
// L1: 10 filas (filas 1-10)
// L2: 1 fila (fila 11, cursor)
// Viewport: 5 filas. target = 11 - 5 + 1 = 7, que cae dentro de L1.
// Resultado: top = 1, topOffset = 6 → filas 7,8,9,10 de L1 + fila 11 de L2.
TEST(softwrap_autoscroll_shows_context_from_previous_long_line) {
    Editor ed;
    ed.active().filename = "notes.md";
    NullSink null;
    ed.setSink(null);
    std::vector<std::string> lines;
    lines.push_back("a");                              // 1 fila visual (fila 0)
    lines.push_back(std::string(120, 'b'));            // 10 filas visuales (filas 1-10)
    lines.push_back("c");                              // 1 fila visual (fila 11)
    ed.active().document.restore(lines);
    ed.active().viewport.height = 5;
    ed.active().viewport.width = 19;   // textWidth = 12
    ed.active().viewport.top = 0;
    ed.active().viewport.topOffset = 0;
    // Línea 2, col 0 → fila visual 11 (primera fila de L2).
    ed.active().cursor.line = 2;
    ed.active().cursor.col = 0;

    ed.renderFrame();

    // target = 7 cae dentro de L1 (filas 1-10): empezar en L1 con offset 6.
    CHECK_EQ(ed.active().viewport.top, 1);
    CHECK_EQ(ed.active().viewport.topOffset, 6);
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 0);
}
