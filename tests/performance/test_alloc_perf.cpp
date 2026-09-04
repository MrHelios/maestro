// ===========================================================================
// INSTRUMENTACION TEMPORAL: contador de allocations por operacion.
//
// No verifica comportamiento (eso ya lo cubren unit/ e interaction/): mide
// cuantas allocations y bytes provoca cada operacion del editor para saber
// QUE optimizar (arena/pool/reserve) y que NO hace falta tocar.
//
// Escenarios:
//   1. Tecleo:      1000 x InsertChar via handleEvent completo.
//   2. Desglose:    los mismos 1000 inserts hechos a mano contra
//                   Document + historial, con sub-scopes anidados
//                   (DocInsert / EditPushBack / commitHistoryEntry).
//   3. Scroll:      MoveUp/MoveDown oscilando en los bordes del viewport.
//   4. Render:      buildScreen repetido en el borde superior e inferior.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "test_framework.h"
#include "helpers/perf_helpers.h"
#include "alloc_stats.h"

#define private public
#include "ui/Editor.h"
#undef private

namespace {

using perf_helpers::makeLines;

Event charEvent(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

Event moveEvent(EventType t) {
    Event e;
    e.type = t;
    return e;
}

void press(Editor& ed, EventType type) { ed.handleEvent(moveEvent(type)); }

} // namespace

// ---------------------------------------------------------------------------
// 1. La pregunta original: que allocation ocurre al tipear 1000 caracteres?
// ---------------------------------------------------------------------------
TEST(alloc_fase_teclado_1000_caracteres) {
    Editor ed;
    ed.active().document.restore(makeLines(300, 80));
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    ed.active().cursor.line = 10;
    ed.state_ = State::Interaccion;

    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kTyping);
        for (int i = 0; i < 1000; ++i) {
            ed.handleEvent(charEvent('a'));
        }
    }
    CHECK_EQ(ed.active().cursor.col, 1000); // sanity: se tecleo de verdad
    alloc_stats::report("Tecleo: 1000 x InsertChar (handleEvent completo)");
}

// ---------------------------------------------------------------------------
// 2. Desglose del mismo trabajo, pieza por pieza (scopes anidados):
//    HistoryCommit incluye todo; DocInsert/EditPushBack son subconjuntos;
//    la diferencia es beginHistoryEntry/commitHistoryEntry/cursor.
// ---------------------------------------------------------------------------
TEST(alloc_desglose_insert_char) {
    Buffer b;
    b.document.restore(makeLines(300, 80));
    b.cursor.line = 10;

    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kHistoryCommit);
        for (int i = 0; i < 1000; ++i) {
            HistoryEntry e = b.beginHistoryEntry();
            const Position start{b.cursor.line, b.cursor.col};
            Position end;
            {
                alloc_stats::Scoped doc(alloc_stats::kDocInsert);
                end = b.document.insertText(start.line, start.col, "a");
            }
            {
                alloc_stats::Scoped push(alloc_stats::kEditPushBack);
                e.edits.push_back({EditType::Insert, start, end, "a"});
            }
            b.cursor.line = end.line;
            b.cursor.col = end.col;
            b.commitHistoryEntry(std::move(e));
        }
    }
    CHECK_EQ(b.document.lineLength(10), 1080);
    alloc_stats::report("Desglose: insert directo + historial (anidado)");
}

// ---------------------------------------------------------------------------
// 3. Scroll con teclado pegado a los bordes del viewport: aca es donde el
//    usuario reporta CPU alta. Oscilar en el borde fuerza scrollToCursor
//    en cada render posterior, pero este scope mide SOLO el movimiento.
// ---------------------------------------------------------------------------
TEST(alloc_scroll_bordes_viewport) {
    Editor ed;
    ed.active().document.restore(makeLines(300, 80));
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    ed.state_ = State::Navegacion;

    // Borde superior: cursor arranca en linea 3 y oscila 0<->1.
    ed.active().cursor.line = 3;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kCursorMove);
        for (int i = 0; i < 1000; ++i) {
            press(ed, EventType::MoveUp);
            press(ed, EventType::MoveDown);
        }
    }
    CHECK_EQ(ed.active().cursor.line, 3);
    alloc_stats::report("Scroll teclado: 1000 pares Up/Down en borde superior");

    // Borde inferior: cursor en la ultima linea, Down clampea.
    ed.active().cursor.line = 298;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kCursorMove);
        for (int i = 0; i < 1000; ++i) {
            press(ed, EventType::MoveDown);
            press(ed, EventType::MoveUp);
        }
    }
    CHECK_EQ(ed.active().cursor.line, 298);
    alloc_stats::report("Scroll teclado: 1000 pares Down/Up en borde inferior");
}

// ---------------------------------------------------------------------------
// 4. El otro sospechoso de CPU: renderFrame completo tras CADA tecla/movimiento.
//    Se mide buildScreen (la parte pura) en ambos bordes del viewport.
// ---------------------------------------------------------------------------
TEST(alloc_render_frame_bordes) {
    Editor ed;
    ed.active().document.restore(makeLines(300, 80));
    ed.state_ = State::Navegacion;

    Renderer& r = ed.renderer_;
    Message msg;

    // Borde superior: viewport.top = 0.
    ed.active().viewport.top = 0;
    ed.active().cursor.line = 5;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kRenderFrame);
        for (int i = 0; i < 200; ++i) {
            std::string out = r.buildScreen(
                ed.active().document, ed.active().cursor,
                ed.active().viewport, "perf.txt", false, msg,
                State::Navegacion, std::nullopt);
            if (out.empty()) CHECK(false);
        }
    }
    alloc_stats::report("Render: 200 x buildScreen borde superior");

    // Borde inferior: viewport clamped al EOF (top maximo).
    const int h = ed.active().viewport.height;
    ed.active().viewport.top = 300 - h;
    ed.active().cursor.line = 299;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kRenderFrame);
        for (int i = 0; i < 200; ++i) {
            std::string out = r.buildScreen(
                ed.active().document, ed.active().cursor,
                ed.active().viewport, "perf.txt", false, msg,
                State::Navegacion, std::nullopt);
            if (out.empty()) CHECK(false);
        }
    }
    alloc_stats::report("Render: 200 x buildScreen borde inferior");
}
