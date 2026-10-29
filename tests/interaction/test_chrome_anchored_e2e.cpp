// Tests de integración mouse + chrome anclado (Editor real).
// Todos los tests llevan el prefijo `chrome_anchored_` para poder filtrarlos.

#include "test_framework.h"
#define private public
#include "app/Editor.h"
#undef private
#include "platform/InputEvent.h"
#include "platform/CellPos.h"

#include <string>
#include <vector>

namespace {

// Editor con N líneas y viewport de 20x80 (deja espacio para chrome + residuo)
void setupEditor(Editor& ed, int nLines, int top, int cursorLine) {
    std::vector<std::string> lines;
    for (int i = 0; i < nLines; ++i) lines.push_back("linea " + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 20;
    ed.active().viewport.width = 80;
    ed.active().viewport.top = top;
    ed.active().cursor.line = cursorLine;
    ed.active().cursor.col = 5;
}

InputEvent mouse(InputEventType type, CellPos cell) {
    InputEvent e;
    e.type = type;
    e.cell = cell;
    return e;
}

} // namespace

// Press en StatusBar (fila lógica = content.height) arma el gesto
TEST(chrome_anchored_e2e_press_statusbar_arms_gesture) {
    Editor ed;
    setupEditor(ed, 25, 0, 10);

    ed.handleEvent(mouse(InputEventType::MousePress, CellPos{10, 20}));

    CHECK(ed.mouseGestureActive_);
    CHECK(!ed.mouseDragStarted_);
}

// Press en MessageBar (fila lógica = content.height + 1) arma el gesto
TEST(chrome_anchored_e2e_press_messagebar_arms_gesture) {
    Editor ed;
    setupEditor(ed, 25, 0, 10);

    ed.handleEvent(mouse(InputEventType::MousePress, CellPos{15, 21}));

    CHECK(ed.mouseGestureActive_);
}

// Arrastre hacia el espacio residual (gapAsBottom -> fila content.height)
// mantiene el autoscroll hacia abajo
TEST(chrome_anchored_e2e_drag_into_gap_continues_autoscroll_down) {
    Editor ed;
    setupEditor(ed, 30, 5, 15);

    ed.handleEvent(mouse(InputEventType::MousePress, CellPos{10, 10}));
    ed.handleEvent(mouse(InputEventType::MouseDrag, CellPos{10, 20}));

    CHECK(ed.mouseAutoscrollActive());
    CHECK(ed.mouseAutoscrollDirection_ == Editor::MouseAutoscrollDirection::Down);
}

// Release en espacio residual (CellPos{} inválido) cierra el gesto sin
// dejar estado inconsistente y conserva la selección
TEST(chrome_anchored_e2e_release_in_gap_ends_gesture_cleanly) {
    Editor ed;
    setupEditor(ed, 25, 0, 10);

    ed.handleEvent(mouse(InputEventType::MousePress, CellPos{10, 5}));
    ed.handleEvent(mouse(InputEventType::MouseDrag, CellPos{12, 8}));
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Seleccion));

    ed.handleEvent(mouse(InputEventType::MouseRelease, CellPos{}));

    CHECK(!ed.mouseGestureActive_);
    CHECK(!ed.mouseAutoscrollActive());
    CHECK(ed.hasSelection());
}