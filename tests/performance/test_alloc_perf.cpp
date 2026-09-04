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
//   5-14. Nuevos:   Delete/Backspace, Undo/Redo, multibyte, paste bloque,
//                  movimiento horizontal, saltos grandes, handleEvent aislado,
//                  carga/restore+resize, multi-buffer y sesion realista mixta.
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

using perf_helpers::moveEvent;

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
// ---------------------------------------------------------------------------
// 5. Borrado: 500 Backspace hacia atras y 500 Delete hacia adelante en linea
//    larga 4k (ejercita camino UTF-8/bytes, cursor se desplaza).
// ---------------------------------------------------------------------------
TEST(alloc_delete_backspace_1000) {
    Editor ed;
    ed.active().document.restore(makeLines(300, 4000));
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    ed.active().cursor.line = 10;
    ed.active().cursor.col = 2000;
    ed.state_ = State::Interaccion;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kTyping);
        for (int i = 0; i < 500; ++i) ed.handleEvent(moveEvent(EventType::Backspace));
        for (int i = 0; i < 500; ++i) ed.handleEvent(moveEvent(EventType::Delete));
    }
    alloc_stats::report("Delete: 500 Backspace + 500 Delete en linea 4k");
    CHECK_EQ(ed.active().document.lineLength(10), 3000);
    CHECK_EQ(ed.active().cursor.col, 1500);
}

// ---------------------------------------------------------------------------
// 6. Undo/Redo: recorre historial (costo de reproducir edits).
// ---------------------------------------------------------------------------
TEST(alloc_undo_redo_500) {
    Editor ed;
    ed.active().document.restore(makeLines(300, 80));
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    ed.active().cursor.line = 10;
    ed.state_ = State::Interaccion;
    for (int i = 0; i < 500; ++i) ed.handleEvent(charEvent('a'));
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kHistoryCommit);
        for (int i = 0; i < 500; ++i) ed.handleEvent(moveEvent(EventType::Undo));
        for (int i = 0; i < 500; ++i) ed.handleEvent(moveEvent(EventType::Redo));
    }
    alloc_stats::report("Undo/Redo: 500 Undo + 500 Redo tras 500 inserts");
    CHECK_EQ(ed.active().cursor.col, 500);
    CHECK_EQ(ed.active().document.lineLength(10), 580);
}

// ---------------------------------------------------------------------------
// 7. Insercion multibyte: tildes y emoji (camino UTF-8, caso caro).
// ---------------------------------------------------------------------------
TEST(alloc_insert_multibyte_1000) {
    Editor ed;
    ed.active().document.restore(makeLines(300, 80));
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    ed.active().cursor.line = 10;
    ed.state_ = State::Interaccion;
    Event e_accent; e_accent.type = EventType::InsertChar; e_accent.text = "\xC3\xA9";
    Event e_emoji; e_emoji.type = EventType::InsertChar; e_emoji.text = "\xF0\x9F\x98\x80";
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kTyping);
        for (int i = 0; i < 500; ++i) ed.handleEvent(e_accent);
        for (int i = 0; i < 500; ++i) ed.handleEvent(e_emoji);
    }
    alloc_stats::report("Insert multibyte: 500 \xC3\xA9 + 500 emoji via handleEvent");
    CHECK_EQ(ed.active().document.lineLength(10), 3080);
}

TEST(alloc_paste_insertText_10k) {
    Buffer b;
    b.document.restore(makeLines(300, 80));
    b.cursor.line = 10;
    std::string big(10000, 'x');
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kDocInsert);
        b.document.insertText(10, 5, big);
    }
    alloc_stats::report("Paste: Document::insertText 10k de una vez");
    CHECK_EQ(b.document.lineLength(10), 10080);
}

TEST(alloc_paste_100x100_history) {
    // Simula paste fragmentado en 100 operaciones incluyendo costo de History.
    Editor ed;
    ed.active().document.restore(makeLines(300, 80));
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    ed.active().cursor.line = 10;
    ed.active().cursor.col = 5;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kHistoryCommit);
        for (int i = 0; i < 100; ++i) {
            HistoryEntry e = ed.active().beginHistoryEntry();
            Position s{ed.active().cursor.line, ed.active().cursor.col};
            Position en = ed.active().document.insertText(s.line, s.col, std::string(100, 'y'));
            e.edits.push_back({EditType::Insert, s, en, std::string(100, 'y')});
            ed.active().cursor.line = en.line; ed.active().cursor.col = en.col;
            ed.active().commitHistoryEntry(std::move(e));
        }
    }
    alloc_stats::report("Paste: 100x insert 100 chars + History (fragmentado)");
    CHECK_EQ(ed.active().document.lineLength(10), 10080);
}

// ---------------------------------------------------------------------------
// 9. Movimiento horizontal: MoveLeft/MoveRight donde columnOf entra en juego.
// ---------------------------------------------------------------------------
TEST(alloc_movimiento_horizontal_2000) {
    Editor ed;
    ed.active().document.restore(makeLines(10, 4000));
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    ed.state_ = State::Navegacion;
    ed.active().cursor.line = 5;
    ed.active().cursor.col = 2000;
    // viewport con scroll horizontal
    ed.active().viewport.width = 80;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kCursorMove);
        for (int i = 0; i < 1000; ++i) { press(ed, EventType::MoveLeft); press(ed, EventType::MoveRight); }
    }
    alloc_stats::report("Mov horizontal: 1000 pares Left/Right en linea 4k");
    CHECK_EQ(ed.active().cursor.col, 2000);
}

// ---------------------------------------------------------------------------
// 10. Saltos grandes: PageUp/PageDown, Home/End, GoTo linea.
// ---------------------------------------------------------------------------
TEST(alloc_saltos_grandes) {
    Editor ed;
    ed.active().document.restore(makeLines(3000, 80));
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    ed.state_ = State::Navegacion;
    ed.active().cursor.line = 1500;
    ed.active().viewport.top = 1490;
    ed.active().viewport.height = 24;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kCursorMove);
        for (int i = 0; i < 200; ++i) { press(ed, EventType::PageUp); press(ed, EventType::PageDown); }
        for (int i = 0; i < 100; ++i) { press(ed, EventType::MoveHome); press(ed, EventType::MoveEnd); }
    }
    alloc_stats::report("Saltos: 200 PageUp/Down + 100 Home/End");
    CHECK_EQ(ed.active().cursor.line, 1500);
    CHECK_EQ(ed.active().cursor.col, 80);
    // Simula saltos directos de cursor+viewport, sin pasar por comando g:
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kCursorMove);
        for (int i = 0; i < 200; ++i) {
            ed.active().cursor.line = (i*13)%3000;
            ed.active().viewport.top = std::max(0, ed.active().cursor.line - 12);
        }
    }
    alloc_stats::report("Saltos directos: 200 GoTo via asignacion cursor/viewport");
    CHECK_EQ(ed.active().cursor.line, 2587);
}

// ---------------------------------------------------------------------------
// 11. Costo de handleEvent para navegacion horizontal.
// ---------------------------------------------------------------------------
TEST(alloc_handleEvent_moveLeft_1000) {
    Editor ed;
    ed.active().document.restore(makeLines(300, 80));
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    ed.state_ = State::Navegacion;
    ed.active().cursor.col = 500;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kCursorMove);
        for (int i = 0; i < 1000; ++i) press(ed, EventType::MoveLeft);
    }
    alloc_stats::report("handleEvent: 1000 MoveLeft");
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(alloc_document_directo) {
    Buffer b;
    b.document.restore(makeLines(300, 80));
    b.cursor.line = 10; b.cursor.col = 10;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kDocInsert);
        for (int i = 0; i < 1000; ++i) b.document.insertText(10, 10, "a");
    }
    alloc_stats::report("Document: 1000 insertText (referencia directa)");
    CHECK(b.document.lineLength(10) == 1080);
}

// ---------------------------------------------------------------------------
// 12. Carga inicial y resize: restore + buildScreen tras cambio de viewport.
// ---------------------------------------------------------------------------
TEST(alloc_carga_y_resize) {
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kOther);
        Document d; d.restore(makeLines(3000, 80));
        if (d.lineCount() != 3000) CHECK(false);
    }
    alloc_stats::report("Carga inicial: Document::restore 3000x80");
    Editor ed;
    ed.active().document.restore(makeLines(300, 80));
    ed.state_ = State::Navegacion;
    Renderer& r = ed.renderer_;
    Message msg;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kRenderFrame);
        ed.active().viewport.width = 120; ed.active().viewport.height = 40;
        for (int i = 0; i < 200; ++i) {
            std::string out = r.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "perf.txt", false, msg, State::Navegacion, std::nullopt);
            if (out.empty()) CHECK(false);
            ed.active().viewport.width = 80 + (i%40);
        }
    }
    alloc_stats::report("Resize: 200 buildScreen con viewport 80..120");
}

// ---------------------------------------------------------------------------
// 13. Multi-buffer: crear y cambiar de buffer activo.
// ---------------------------------------------------------------------------
TEST(alloc_multibuffer) {
    Editor ed;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kOther);
        for (int i = 0; i < 20; ++i) {
            ed.handleEvent(moveEvent(EventType::Prefix));
            Event ev; ev.type = EventType::InsertChar; ev.text = "n";
            ed.handleEvent(ev);
            ed.active().document.restore(makeLines(10, 40));
        }
    }
    alloc_stats::report("Multi-buffer: 20 newBuffer + restore");
    CHECK_EQ(ed.buffers.count(), 21);
    // switch rapido entre buffers existentes - verificar que alterna
    Buffer* before = &ed.buffers.active();
    ed.handleEvent(moveEvent(EventType::Prefix));
    Event evb; evb.type = EventType::InsertChar; evb.text = "b";
    ed.handleEvent(evb);
    CHECK(&ed.buffers.active() != before);
    bool switchedEveryTime = true;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kOther);
        for (int i = 0; i < 99; ++i) {
            Buffer* beforeLoop = &ed.buffers.active();
            ed.handleEvent(moveEvent(EventType::Prefix));
            Event ev; ev.type = EventType::InsertChar; ev.text = "b";
            ed.handleEvent(ev);
            if (&ed.buffers.active() == beforeLoop) switchedEveryTime = false;
        }
    }
    alloc_stats::report("Multi-buffer: 100 switch (Prefix+b)");
    CHECK(switchedEveryTime);
    CHECK_EQ(ed.buffers.count(), 21);
}

// ---------------------------------------------------------------------------
// 14. Sesion realista mixta: tipear, mover, borrar, undo/redo, scroll.
// ---------------------------------------------------------------------------
TEST(alloc_sesion_realista_mixta) {
    Editor ed;
    ed.active().document.restore(makeLines(300, 80));
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    ed.active().cursor.line = 10;
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped scope(alloc_stats::kTyping);
        for (int cycle = 0; cycle < 100; ++cycle) {
            ed.state_ = State::Interaccion;
            for (int i = 0; i < 10; ++i) ed.handleEvent(charEvent('a'));
            ed.state_ = State::Navegacion;
            for (int i = 0; i < 5; ++i) press(ed, EventType::MoveLeft);
            ed.state_ = State::Interaccion;
            for (int i = 0; i < 2; ++i) ed.handleEvent(moveEvent(EventType::Backspace));
            ed.handleEvent(moveEvent(EventType::Undo));
            ed.handleEvent(moveEvent(EventType::Redo));
            ed.state_ = State::Navegacion;
            press(ed, EventType::MoveDown);
            press(ed, EventType::MoveUp);
            if (cycle % 10 == 0) { press(ed, EventType::PageDown); press(ed, EventType::PageUp); }
        }
    }
    alloc_stats::report("Sesion realista: 100 ciclos mixtos (type/move/del/undo/scroll)");
    CHECK(ed.active().cursor.line >= 0);
    CHECK(ed.active().document.lineLength(10) > 80);
}

