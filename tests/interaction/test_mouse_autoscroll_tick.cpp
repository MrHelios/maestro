#include "test_support.h"

// Autoscroll temporal de seleccion por mouse: con el puntero QUIETO en la
// zona de scroll, el viewport sigue avanzando un paso por intervalo
// (tickMouseAutoscroll, reloj inyectado: determinista, sin sleeps).
//
// Semantica ASIMETRICA (limites del reporte SGR): hacia abajo la zona es
// estrictamente fuera del contenido (statusbar); hacia arriba, como la
// terminal no reporta filas sobre el contenido, la primera fila visible
// cuenta como intencion. Al armar, lastStep = ahora (sin salto inmediato);
// cada tick como maximo +1 linea (sin deuda acumulada); en el limite
// sostiene el borde; al volver al interior o en release se apaga
// de inmediato.

namespace {
const auto kStep = Editor::kMouseAutoscrollInterval;

Event mousePressAt(int col, int row) {
    Event e;
    e.type = EventType::MousePress;
    e.mouseCol = col;
    e.mouseRow = row;
    return e;
}

Event mouseDragAt(int col, int row) {
    Event e;
    e.type = EventType::MouseDrag;
    e.mouseCol = col;
    e.mouseRow = row;
    return e;
}

Event mouseReleaseAt(int col, int row) {
    Event e;
    e.type = EventType::MouseRelease;
    e.mouseCol = col;
    e.mouseRow = row;
    return e;
}

std::chrono::steady_clock::time_point now() {
    return std::chrono::steady_clock::now();
}
} // namespace

TEST(mouse_tick_scrolls_down_periodically) {
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

    ed.handleEvent(mousePressAt(4, 2)); // linea 11, arma gesto
    CHECK_EQ(ed.active().cursor.line, 11);
    ed.handleEvent(mouseDragAt(4, 5)); // statusbar, fuera por abajo
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK_EQ(ed.active().viewport.top, 11); // paso del evento
    CHECK(ed.mouseAutoscrollActive());

    const auto t0 = now();
    // Antes del intervalo: no mueve.
    CHECK(!ed.tickMouseAutoscroll(t0 + kStep / 2));
    CHECK_EQ(ed.active().viewport.top, 11);
    // Un paso por intervalo, anchor fijo, position sigue al borde.
    CHECK(ed.tickMouseAutoscroll(t0 + kStep));
    CHECK_EQ(ed.active().viewport.top, 12);
    CHECK_EQ(ed.active().cursor.line, 15);
    CHECK(ed.tickMouseAutoscroll(t0 + kStep * 2));
    CHECK_EQ(ed.active().viewport.top, 13);
    CHECK_EQ(ed.active().cursor.line, 16);
    CHECK_EQ(ed.active().selection->anchor.line, 11);
    CHECK_EQ(ed.active().selection->position.line, 16);
    // Sin deuda acumulada: salto grande de reloj da un solo paso.
    CHECK(ed.tickMouseAutoscroll(t0 + kStep * 5));
    CHECK_EQ(ed.active().viewport.top, 14);
    CHECK_EQ(ed.active().cursor.line, 17);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

TEST(mouse_tick_scrolls_up_periodically) {
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

    ed.handleEvent(mousePressAt(4, 3)); // linea 12, arma gesto
    ed.handleEvent(mouseDragAt(4, 0));  // fuera por arriba
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK_EQ(ed.active().viewport.top, 9); // paso del evento
    CHECK_EQ(ed.active().cursor.line, 9);
    CHECK(ed.mouseAutoscrollActive());

    const auto t0 = now();
    CHECK(!ed.tickMouseAutoscroll(t0 + kStep / 2));
    CHECK_EQ(ed.active().viewport.top, 9);
    CHECK(ed.tickMouseAutoscroll(t0 + kStep));
    CHECK_EQ(ed.active().viewport.top, 8);
    CHECK_EQ(ed.active().cursor.line, 8);
    CHECK_EQ(ed.active().selection->anchor.line, 12);
    CHECK_EQ(ed.active().selection->position.line, 8);
}

TEST(mouse_tick_stops_when_back_inside) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i) lines.push_back("l" + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 4;
    ed.active().viewport.width = 20;
    ed.active().viewport.top = 10;
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 11;
    ed.active().cursor.col = 0;

    ed.handleEvent(mousePressAt(4, 2));
    ed.handleEvent(mouseDragAt(4, 5)); // fuera: arma Down
    CHECK(ed.mouseAutoscrollActive());
    // El mouse vuelve adentro: se apaga de inmediato, sin esperar al tick.
    ed.handleEvent(mouseDragAt(4, 2)); // relRow=1, dentro
    CHECK(!ed.mouseAutoscrollActive());
    const int topNow = ed.active().viewport.top;
    const auto t0 = now();
    CHECK(!ed.tickMouseAutoscroll(t0 + kStep * 10));
    CHECK_EQ(ed.active().viewport.top, topNow);
}

TEST(mouse_tick_stops_on_release) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i) lines.push_back("l" + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 4;
    ed.active().viewport.width = 20;
    ed.active().viewport.top = 10;
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 11;
    ed.active().cursor.col = 0;

    ed.handleEvent(mousePressAt(4, 2));
    ed.handleEvent(mouseDragAt(4, 5)); // fuera: arma Down
    CHECK(ed.mouseAutoscrollActive());
    ed.handleEvent(mouseReleaseAt(4, 5)); // release fuera tambien apaga
    CHECK(!ed.mouseAutoscrollActive());
    const int topNow = ed.active().viewport.top;
    const auto t0 = now();
    CHECK(!ed.tickMouseAutoscroll(t0 + kStep * 10));
    CHECK_EQ(ed.active().viewport.top, topNow);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

TEST(mouse_tick_holds_edge_at_limit) {
    // En top==maxTop el tick no puede scrollear pero sostiene el borde
    // (cursor en ultima linea) hasta que el mouse vuelva.
    Editor ed;
    ed.active().document.restore(
        {"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7"});
    ed.active().viewport.height = 4;
    ed.active().viewport.width = 20;
    ed.active().viewport.top = 4; // maxTop
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 5;
    ed.active().cursor.col = 0;

    ed.handleEvent(mousePressAt(4, 2)); // linea 5
    ed.handleEvent(mouseDragAt(4, 5));  // statusbar: clamp a linea 7
    CHECK_EQ(ed.active().viewport.top, 4);
    CHECK_EQ(ed.active().cursor.line, 7);
    CHECK(ed.mouseAutoscrollActive());

    const auto t0 = now();
    CHECK(ed.tickMouseAutoscroll(t0 + kStep));
    CHECK_EQ(ed.active().viewport.top, 4); // sin scroll: ya en el limite
    CHECK_EQ(ed.active().cursor.line, 7);  // sostiene la ultima fila
    CHECK(ed.mouseAutoscrollActive());     // sigue activo hasta volver
    CHECK_EQ(ed.active().selection->anchor.line, 5);
    CHECK_EQ(ed.active().selection->position.line, 7);
}

TEST(mouse_tick_small_file_clamps_by_side) {
    // count <= h (maxTop==0 para ambos extremos): el lado del drag decide,
    // primera linea por arriba y ultima por abajo.
    Editor ed;
    ed.active().document.restore({"a", "b", "c"});
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 20;
    ed.active().viewport.top = 0;
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 1;
    ed.active().cursor.col = 0;

    ed.handleEvent(mousePressAt(4, 2)); // linea 1
    ed.handleEvent(mouseDragAt(4, 0));  // fuera por arriba
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().cursor.line, 0); // primera linea
    CHECK(ed.mouseAutoscrollActive());
    const auto t0 = now();
    CHECK(ed.tickMouseAutoscroll(t0 + kStep));
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().cursor.line, 0);

    ed.handleEvent(mouseDragAt(4, 11)); // h=10: relRow=10, fuera por abajo
    CHECK_EQ(ed.active().cursor.line, 2); // ultima linea
    CHECK(ed.mouseAutoscrollActive());
    CHECK(ed.tickMouseAutoscroll(t0 + kStep * 2));
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK_EQ(ed.active().cursor.line, 2);
}

TEST(mouse_tick_without_gesture_does_nothing) {
    Editor ed;
    ed.active().document.restore({"a", "b"});
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 20;
    const auto t0 = now();
    CHECK(!ed.mouseAutoscrollActive());
    CHECK(!ed.tickMouseAutoscroll(t0 + kStep * 10));
    CHECK_EQ(ed.active().viewport.top, 0);
}

TEST(mouse_tick_scrolls_up_from_top_edge_row) {
    // relRow=0 ES contenido (primera fila visible), no fuera: este test fija
    // la DECISION sustituta (el fuera hacia arriba no es reportable por SGR,
    // asi que la primera fila cuenta como intencion de scroll-up). Si el
    // armado fuera "estrictamente fuera", por arriba el autoscroll jamas se
    // armaria en una terminal real aunque haya contenido por revelar.
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i) lines.push_back("l" + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 4;
    ed.active().viewport.width = 20;
    ed.active().viewport.top = 10; // margen para seguir subiendo
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 12;
    ed.active().cursor.col = 0;

    ed.handleEvent(mousePressAt(4, 3)); // linea 12, arma gesto
    ed.handleEvent(mouseDragAt(4, 1));  // primera fila visible (tope real)
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK_EQ(ed.active().viewport.top, 9); // paso del evento
    CHECK_EQ(ed.active().cursor.line, 9);
    // Primera fila (contenido) tratada como intencion por DECISION sustituta.
    CHECK(ed.mouseAutoscrollActive());

    const auto t0 = now();
    CHECK(ed.tickMouseAutoscroll(t0 + kStep));
    CHECK_EQ(ed.active().viewport.top, 8);
    CHECK_EQ(ed.active().cursor.line, 8);
    CHECK_EQ(ed.active().selection->anchor.line, 12);
    CHECK_EQ(ed.active().selection->position.line, 8);
}

TEST(mouse_tick_last_row_does_not_arm) {
    // Asimetria: la ultima fila visible es seleccion exacta hacia abajo
    // (el fuera real es la statusbar). El drag no arma el autoscroll y el
    // tick no mueve aunque pase el intervalo.
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i) lines.push_back("l" + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 4;
    ed.active().viewport.width = 20;
    ed.active().viewport.top = 10; // margen para seguir bajando
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 10;
    ed.active().cursor.col = 0;

    ed.handleEvent(mousePressAt(4, 1)); // linea 10, arma gesto
    ed.handleEvent(mouseDragAt(4, 4));  // ultima fila visible: exacta
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK_EQ(ed.active().viewport.top, 10); // sin scroll
    CHECK_EQ(ed.active().cursor.line, 13);
    CHECK(!ed.mouseAutoscrollActive());

    const auto t0 = now();
    CHECK(!ed.tickMouseAutoscroll(t0 + kStep * 10));
    CHECK_EQ(ed.active().viewport.top, 10);
    CHECK_EQ(ed.active().cursor.line, 13);
}
