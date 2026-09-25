#include "test_support.h"

// BUG: cuando un gesto de seleccion por mouse (press + drag) llega al
// limite superior/inferior del viewport y el cursor sale de la zona de
// contenido del archivo, el scroll/seleccion se detiene: hay que mover el
// cursor de arriba para abajo para que siga marcando nuevas filas.
//
// Comportamiento deseado: si se esta en uno de los limites y el cursor se
// mueve fuera de la parte del contenido, el scroll sigue automaticamente
// bajando/subiendo (marcando filas) hasta que el mouse vuelva a la zona
// de contenido. Estos dos tests capturan ese comportamiento y hoy FALLAN:
// el drag fuera del contenido en el limite se ignora (sigue en
// Navegacion, sin rango, cursor quieto).

TEST(mouse_autoscroll_outside_top_at_limit) {
    // Limite superior: viewport ya en top==0, cursor fuera por arriba
    // (mouseRow=0, relRow=-1, fuera del contenido) debe seguir marcando
    // hacia la primera fila en vez de ignorarse.
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 8; ++i) lines.push_back("l" + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 4;
    ed.active().viewport.width = 20;
    ed.active().viewport.top = 0; // limite superior
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 2;
    ed.active().cursor.col = 0;

    Event press;
    press.type = EventType::MousePress;
    press.mouseCol = 4; // gutter=3: visual 0
    press.mouseRow = 3; // relRow=2 -> linea doc 2
    ed.handleEvent(press);
    CHECK_EQ(ed.active().cursor.line, 2);

    Event drag;
    drag.type = EventType::MouseDrag;
    drag.mouseCol = 4;
    drag.mouseRow = 0; // fuera del contenido, por arriba
    ed.handleEvent(drag);

    // Auto-scroll hacia arriba: entra en Seleccion y marca hasta el borde.
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.active().selection.has_value());
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().viewport.top, 0); // no puede subir mas
    CHECK_EQ(ed.active().cursor.line, 0);  // pero marca la primera fila
    if (ed.active().selection.has_value()) {
        CHECK_EQ(ed.active().selection->anchor.line, 2);
        CHECK_EQ(ed.active().selection->position.line, 0);
    }
}

TEST(mouse_autoscroll_outside_bottom_at_limit) {
    // Limite inferior: viewport ya en top==maxTop, cursor fuera por abajo
    // (mouseRow=h+1, primera fila de statusbar, fuera del contenido) debe
    // seguir marcando hacia la ultima fila en vez de ignorarse.
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 8; ++i) lines.push_back("l" + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 4;
    ed.active().viewport.width = 20;
    ed.active().viewport.top = 4; // maxTop: lineas 4..7 visibles
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 5;
    ed.active().cursor.col = 0;

    Event press;
    press.type = EventType::MousePress;
    press.mouseCol = 4; // gutter=3: visual 0
    press.mouseRow = 2; // relRow=1 -> linea doc 5
    ed.handleEvent(press);
    CHECK_EQ(ed.active().cursor.line, 5);

    Event drag;
    drag.type = EventType::MouseDrag;
    drag.mouseCol = 4;
    drag.mouseRow = 5; // h=4 -> relRow=4: statusbar, fuera del contenido
    ed.handleEvent(drag);

    // Auto-scroll hacia abajo: entra en Seleccion y marca hasta el borde.
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.active().selection.has_value());
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().viewport.top, 4); // no puede bajar mas
    CHECK_EQ(ed.active().cursor.line, 7);  // pero marca la ultima fila
    if (ed.active().selection.has_value()) {
        CHECK_EQ(ed.active().selection->anchor.line, 5);
        CHECK_EQ(ed.active().selection->position.line, 7);
    }
}
