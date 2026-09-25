#include "test_support.h"

// BUG (ahora con fix): soltar el boton FUERA de la ventana del terminal no
// entrega ningun evento de release (los modos 1000+1002+1006 no garantizan
// el release fuera de ventana). El release entregado DENTRO del terminal si
// frena (ver mouse_tick_stops_on_release); para el perdido, el tick consulta
// el oraculo fisico (setMouseButtonHeldOracle) y desarma en ambas
// direcciones. El tercer test fija que con boton abajo el scroll continua
// (el hold-quiet legitimo no debe cortarse).

namespace {
const auto kStep = Editor::kMouseAutoscrollInterval;

Event pressAt(int col, int row) {
    Event e;
    e.type = EventType::MousePress;
    e.mouseCol = col;
    e.mouseRow = row;
    return e;
}

Event dragAt(int col, int row) {
    Event e;
    e.type = EventType::MouseDrag;
    e.mouseCol = col;
    e.mouseRow = row;
    return e;
}

std::chrono::steady_clock::time_point now() {
    return std::chrono::steady_clock::now();
}
} // namespace

TEST(mouse_tick_stops_when_released_outside_down) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i) lines.push_back("l" + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 4;
    ed.active().viewport.width = 20;
    ed.active().viewport.top = 10; // maxTop=16
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 11;
    ed.active().cursor.col = 0;

    ed.handleEvent(pressAt(4, 2)); // linea 11, arma gesto
    ed.handleEvent(dragAt(4, 5));  // statusbar: arma Down, top 10->11
    CHECK_EQ(ed.active().viewport.top, 11);
    CHECK(ed.mouseAutoscrollActive());

    // Suelta fisica fuera de la ventana: jamas llega un evento de release.
    ed.setMouseButtonHeldOracle([] { return false; });
    const auto t0 = now();
    CHECK(!ed.tickMouseAutoscroll(t0 + kStep)); // sin paso
    CHECK(!ed.mouseAutoscrollActive());         // desarmado
    CHECK_EQ(ed.active().viewport.top, 11);     // quieto
}

TEST(mouse_tick_stops_when_released_outside_up) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i) lines.push_back("l" + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 4;
    ed.active().viewport.width = 20;
    ed.active().viewport.top = 10;
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 12;
    ed.active().cursor.col = 0;

    ed.handleEvent(pressAt(4, 3)); // linea 12, arma gesto
    ed.handleEvent(dragAt(4, 1));  // primera fila: arma Up, top 10->9
    CHECK_EQ(ed.active().viewport.top, 9);
    CHECK(ed.mouseAutoscrollActive());

    // Suelta fisica fuera de la ventana: jamas llega un evento de release.
    ed.setMouseButtonHeldOracle([] { return false; });
    const auto t0 = now();
    CHECK(!ed.tickMouseAutoscroll(t0 + kStep)); // sin paso
    CHECK(!ed.mouseAutoscrollActive());         // desarmado
    CHECK_EQ(ed.active().viewport.top, 9);      // quieto
}

TEST(mouse_tick_keeps_scrolling_while_held) {
    // Con boton fisicamente abajo el tick sigue avanzando (el hold-quiet
    // legitimo fuera del area no debe cortarse por el oraculo).
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i) lines.push_back("l" + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 4;
    ed.active().viewport.width = 20;
    ed.active().viewport.top = 10; // maxTop=16
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 11;
    ed.active().cursor.col = 0;

    ed.handleEvent(pressAt(4, 2)); // linea 11, arma gesto
    ed.handleEvent(dragAt(4, 5));  // statusbar: arma Down, top 10->11
    CHECK_EQ(ed.active().viewport.top, 11);

    ed.setMouseButtonHeldOracle([] { return true; });
    const auto t0 = now();
    CHECK(ed.tickMouseAutoscroll(t0 + kStep));
    CHECK_EQ(ed.active().viewport.top, 12);
    CHECK(ed.mouseAutoscrollActive());
}
