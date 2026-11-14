#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "test_framework.h"
#include "rendering/Sink.h"
#include "platform/CellPos.h"
#include "platform/InputEvent.h"
#include "platform/IEventSource.h"
#include "platform/MouseEvent.h"
#include "platform/ResizeEvent.h"
#include "platform/tty/ITtyKeymap.h"
#include "platform/tty/TtyKeymap.h"
#include "platform/tty/Terminal.h"
#include "platform/tty/TtyMouse.h"
#include "platform/clipboard/ClipboardFactory.h"
#include "platform/gui/GuiSdlMapper.h"
#include "platform/InputEvent.h"
#include "platform/MouseEvent.h"
#include "filesystem/FileWatcherFactory.h"
#include "layout/ScreenToCursor.h"
#include "layout/Layout.h"
#include "rendering/tty/TtyTheme.h"
#include "rendering/tty/TtyScroll.h"
#include "rendering/tty/TtyRenderer.h"
#include "helpers/test_tty_renderer.h"
#include "helpers/spy_screen_renderer.h"

#include "app/Editor.h"


namespace {
// Sink capturador: retiene lo escrito para assertar el primer frame
struct CaptureSink : public Sink {
    std::string data;
    bool writeStdout(const std::string& s) override {
        data += s;
        return true;
    }
};
} // namespace

// CellPos es el tipo común 0-based.
TEST(frontier_cellpos_valid) {
    // posiciones validas
    CHECK((CellPos{10, 5}.valid()));
    CHECK((CellPos{0, 5}.valid()));
    CHECK((CellPos{10, 0}.valid()));
    CHECK((CellPos{0, 0}.valid())); // origen: celda válida, no "vacía"
    
    // posiciones no validas
    CHECK((!CellPos{-1, 5}.valid()));
    CHECK((!CellPos{10, -1}.valid()));
    CHECK((CellPos{3, 4} == CellPos{3, 4}));
    CHECK((CellPos{3, 4} != CellPos{3, 5}));
    
    // contrato default inválido
    CHECK((!CellPos{}.valid()));
    CHECK((!InputEvent{}.cell.valid()));
}

// fábricas MouseEvent -> InputEvent.
TEST(frontier_mouse_event_factories) {
    InputEvent p, d;
    
    p = makeMousePressEvent(CellPos{4, 2});
    CHECK_EQ(static_cast<int>(p.type), static_cast<int>(InputEventType::MousePress));
    CHECK_EQ(p.cell.col, 4);
    CHECK_EQ(p.cell.row, 2);
    
    d = makeMouseDragEvent(CellPos{6, 3});
    CHECK_EQ(static_cast<int>(d.type), static_cast<int>(InputEventType::MouseDrag));
    InputEvent r = makeMouseReleaseEvent(CellPos{6, 3});
    CHECK_EQ(static_cast<int>(r.type), static_cast<int>(InputEventType::MouseRelease));
}

// Terminal es un IEventSource.
TEST(frontier_terminal_is_event_source) {
    Terminal t;
    
    IEventSource* src = &t;
    CHECK(src != nullptr);
}

TEST(frontier_theme_style_mapping_dark) {
    TtyTheme dark = darkTheme();
    
    CHECK(themeAnsiFor(dark, StyleRole::StatusBase) == dark.statusBar);
    CHECK(themeAnsiFor(dark, StyleRole::Default).empty());
    CHECK(themeAnsiFor(dark, StyleRole::GutterBlank).empty());
    CHECK(themeAnsiFor(dark, StyleRole::Selection) == dark.selection);
    CHECK(themeAnsiFor(dark, StyleRole::AccentNavegacion) == dark.accentNavegacion);
    CHECK(themeAnsiFor(dark, StyleRole::SyntaxKeyword) == dark.syntaxKeyword);
    
    CHECK(!themeAnsiFor(dark, StyleRole::Selection).empty());
}

TEST(frontier_theme_style_mapping_light) {
    TtyTheme light = lightTheme();

    CHECK(themeAnsiFor(light, StyleRole::StatusBase) == light.statusBar);
    CHECK(themeAnsiFor(light, StyleRole::Default).empty());
    CHECK(themeAnsiFor(light, StyleRole::GutterBlank).empty());
    CHECK(themeAnsiFor(light, StyleRole::Selection) == light.selection);
    CHECK(themeAnsiFor(light, StyleRole::AccentNavegacion) == light.accentNavegacion);
    CHECK(themeAnsiFor(light, StyleRole::SyntaxKeyword) == light.syntaxKeyword);
    
    CHECK(!themeAnsiFor(light, StyleRole::Selection).empty());
}

// ScreenToCursor acepta CellPos 0-based.
TEST(frontier_screen_to_cursor_cellpos) {
    Document doc;
    Layout layout = computeLayout(24, 80);
    Viewport vp;
    
    doc.restore({"hello", "world"});
    vp.top = 0;
    vp.left = 0;
    vp.width = 80;
    vp.height = 22;
    
    // CellPos 0-based: (col=7, row=0) = primera celda de texto
    // (gutter=7, relCol=7 -> columna visual 0 -> byte 0 de la linea 0).
    auto p = screenToCursor(CellPos{7, 0}, layout, vp, doc);
    CHECK(p.has_value());
    if (p) {
        CHECK_EQ(p->line, 0);
        CHECK_EQ(p->col, 0);
    }
    
    // Fuera del contenido: nullopt.
    CHECK(!screenToCursor(CellPos{1000, 1000}, layout, vp, doc).has_value());
}

// Clipboard y FileWatcher ??
// factories devuelven interfaces sin conocer concretos.
TEST(frontier_factories_return_interfaces) {
    auto cb = makeSystemClipboard();
    
    CHECK(cb != nullptr);
    CHECK(cb->copy("hola"));
    
    auto pasted = cb->paste();
    CHECK(pasted.has_value());
    
    auto nullCb = makeNullClipboard();
    CHECK(nullCb != nullptr);
    
    auto w = makeFileWatcher();
    CHECK(w != nullptr);
    
    auto nullW = makeNullFileWatcher();
    CHECK(nullW != nullptr);
}

// decoder SGR -> CellPos + InputEvent.
TEST(frontier_mouse_decoder_cellpos) {
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

    // Terminal delega en el mismo decoder (sin duplicar tabla Cb).
    {
        InputEvent e;
        Terminal::parseMouseSgr("[<0;10;5M", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MousePress));
        CHECK_EQ(e.cell.col, 9);
        CHECK_EQ(e.cell.row, 4);
    }
}

// Editor común — Resize es un evento AUTÓNOMO: 
// el payload manda, sin provider ni backend. 
// Una GUI puede inyectarlo directamente.
TEST(frontier_editor_handles_resize_event) {
    Editor ed;
    InputEvent r;
    
    r.type = InputEventType::Resize;
    r.resizeRows = 30;
    r.resizeCols = 100;
    ed.handleEvent(r);
    CHECK(ed.getStateForTesting() == State::Navegacion);
    
    // El payload se aplicó: viewport ajustado a 30x100
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
}

// Payload inválido se ignora
TEST(frontier_editor_handles_resize_event_bad_input) {
    Editor ed;
    InputEvent bad;
    
    ed.resize(30, 100); // tamaño previo que el payload inválido debe conservar
    bad.type = InputEventType::Resize;
    bad.resizeRows = 0;
    bad.resizeCols = -5;
    ed.handleEvent(bad);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
}

// Sin resize() previo: el constructor ya deja geometría coherente
TEST(frontier_fase_c_initial_viewport_matches_fallback) {
    Editor ed;
    const Layout exp = computeLayout(24, 80);
    
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, exp.content.width);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, exp.content.height);
}

// Sin resize() previo el primer render no se rompe: 
// frame completo, cursor en origen y loop sigue vivo.
TEST(frontier_fase_c_render_without_resize_does_not_break) {
    Editor ed;
    CaptureSink cap;
    const Layout exp = computeLayout(24, 80);
    
    ed.setRenderer(makeTtyTestRenderer());
    ed.setSink(cap);
    ed.renderFrame();
    
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, exp.content.width);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, exp.content.height);
    CHECK(!cap.data.empty());
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 0);
    CHECK(ed.getStateForTesting() == State::Navegacion);
    CHECK(ed.isRunning());
}

// Con resize() el viewport coincide con la geometría real 
// (no con el fallback)
// el dueño aplica el tamaño y el editor lo refleja + renderiza.
TEST(frontier_fase_c_resize_viewport_matches_real_size) {
    Editor ed;
    CaptureSink cap;
    const Layout exp = computeLayout(30, 100);
    
    ed.setRenderer(makeTtyTestRenderer());
    ed.setSink(cap);
    ed.resize(30, 100);
    
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, exp.content.width);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, exp.content.height);
    ed.renderFrame();
    CHECK(!cap.data.empty());
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 0);
    CHECK(ed.isRunning());
}

TEST(frontier_fase_c_resize_invalid_ignored) {
    Editor ed;
    
    ed.resize(30, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    
    ed.resize(0, -5);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    
    ed.resize(-1, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
}

// Invariant: handleEvent(Resize) delega en resize() — mismo resultado.
TEST(frontier_fase_c_resize_event_delegates) {
    Editor viaEvent, viaDirect;
    InputEvent r;
    
    r.type = InputEventType::Resize;
    r.resizeRows = 30;
    r.resizeCols = 100;
    viaEvent.handleEvent(r);

    viaDirect.resize(30, 100);
    
    CHECK_EQ(viaEvent.getActiveBufferForTesting().viewport.width,
             viaDirect.getActiveBufferForTesting().viewport.width);
    CHECK_EQ(viaEvent.getActiveBufferForTesting().viewport.height,
             viaDirect.getActiveBufferForTesting().viewport.height);
}


// secuencia de arranque del loop
// (getWindowSize -> resize -> primer renderFrame). 
// Replica TtyRunLoop::run() sin TTY
// el frame debe ser completo 
// (rebuild con clear-screen), con la geometría aplicada y el cursor en origen.
TEST(frontier_fase_c_first_render_after_resize) {
    Editor ed;
    CaptureSink cap;
    
    ed.setRenderer(makeTtyTestRenderer());
    ed.setSink(cap);
    ed.resize(30, 100);
    ed.renderFrame();
    
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    
    CHECK(!cap.data.empty());
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 0);
    CHECK(ed.getStateForTesting() == State::Navegacion);
    CHECK(ed.isRunning());
}

// Creamos un documento con varias linea, dejamos viewport en posicion (0,0)
// movemsos cursor a ultima linea
// verificamos que el viewport cambio via scroll
TEST(frontier_fase_c_first_render_scrolls_cursor_into_view) {
    Editor ed;
    CaptureSink cap;
    std::vector<std::string> lines;
    
    ed.setRenderer(makeTtyTestRenderer());
    ed.setSink(cap);
    ed.resize(30, 100); // contenido: 28 filas
    
    for (int i = 0; i < 60; ++i) 
        lines.push_back("l" + std::to_string(i));
    
    ed.getActiveBufferForTesting().document.restore(lines); // actualizamos el documento
    ed.getActiveBufferForTesting().cursor.line = 59;
    ed.getActiveBufferForTesting().cursor.col = 0;
    ed.getActiveBufferForTesting().viewport.top = 0; // el viewport esta al inicio
    ed.renderFrame();
    
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, 60 - 28); // el viewport hizo el scroll
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 59);
    CHECK(!cap.data.empty());
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    CHECK(ed.isRunning());
}

// El highlight real, sin hacks de private: el SpyScreenRenderer ve el
// BracketPair que el Editor entrega al render. Con el cursor sobre el '('
// de "main()" el par debe ser exactamente open=(0,8), close=(0,9). Esto
// distingue "el camino de brackets corre" de "el par se pinta".
TEST(frontier_fase_c_first_render_passes_bracket_pair_to_renderer) {
    Editor ed;
    CaptureSink cap;
    SpyScreenRenderer& spy = injectSpy(ed);
    
    ed.setSink(cap);
    ed.resize(30, 100);
    ed.getActiveBufferForTesting().document.restore({"int main() {", "  return 0;", "}"});
    ed.getActiveBufferForTesting().filename = "test.cpp";
    ed.getActiveBufferForTesting().cursor.line = 0;
    ed.getActiveBufferForTesting().cursor.col = 8; // '(' de "main()"
    
    ed.renderFrame();
    
    CHECK_EQ(spy.renderScreenDiffCount, 1);
    CHECK(spy.lastBracketPair.has_value());
    if (spy.lastBracketPair) {
        CHECK((spy.lastBracketPair->open == Position{0, 8}));
        CHECK((spy.lastBracketPair->close == Position{0, 9}));
    }
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 8);
    CHECK(ed.isRunning());
}

// el renderer solo hace dibujado completo si se cambia la dimensiones de la terminal
// pero un resize al mismo tamaño no lo fuerza
TEST(frontier_fase_c_resize_rebuild_only_on_geometry_change) {
    Editor ed;
    CaptureSink cap;
    
    ed.setRenderer(makeTtyTestRenderer());
    ed.setSink(cap);
    ed.renderFrame(); // fallback 24x80, caché frío
    cap.data.clear();
    
    ed.resize(30, 100);
    ed.renderFrame();
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    
    cap.data.clear();
    ed.resize(30, 100); // mismo tamaño: fast path
    ed.renderFrame();
    CHECK(cap.data.find("\x1b[2J") == std::string::npos);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    CHECK(ed.isRunning());
}

// tick() agrupado (clearExpired + autoscroll) en el camino de timeout. 
// Sin gesto de mouse armado debe ser no-op observable:
// no mueve cursor ni viewport, no crea selección, no detiene el loop.
// El camino armado sigue cubierto por mouse_tick_* (vía tickMouseAutoscroll).
TEST(frontier_fase_c_tick_without_gesture_is_noop) {
    Editor ed;
    const int lineBefore = ed.getActiveBufferForTesting().cursor.line;
    const int colBefore = ed.getActiveBufferForTesting().cursor.col;
    const int topBefore = ed.getActiveBufferForTesting().viewport.top;
    const auto t = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    
    ed.tick(t); // fake timeout
    
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, lineBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, colBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, topBefore);
    CHECK(!ed.hasSelection());
    CHECK(ed.isRunning());
    
    // El bombeo externo sin fuentes listas también es no-op.
    ed.pollExternalEvents();
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, lineBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, colBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, topBefore);
    CHECK(!ed.hasSelection());
    CHECK(ed.isRunning());
}

// El camino de eventos solo expira mensajes: con autoscroll armado,
// clearExpiredMessages() no consulta el oráculo ni mueve nada (eso
// pertenece al timeout, vía tick()). Pinea la corrección del loop:
// eventos y wakeups externos ya no dan pasos extra ni dejan la pantalla
// desactualizada.
TEST(frontier_fase_c_event_path_does_not_autoscroll) {
    Editor ed;
    InputEvent press, drag;
    std::vector<std::string> lines;
    
    for (int i = 0; i < 20; ++i) 
        lines.push_back("l" + std::to_string(i));
    
    ed.getActiveBufferForTesting().document.restore(lines);
    ed.getActiveBufferForTesting().viewport.height = 4;
    ed.getActiveBufferForTesting().viewport.width = 20;
    ed.getActiveBufferForTesting().viewport.top = 10;
    ed.getActiveBufferForTesting().viewport.left = 0;
    ed.getActiveBufferForTesting().cursor.line = 11;
    ed.getActiveBufferForTesting().cursor.col = 0;
    ed.resize(6, 20);

    press.type = InputEventType::MousePress;
    press.cell = CellPos{7, 1}; // línea 11, arma gesto
    ed.handleEvent(press);

    drag.type = InputEventType::MouseDrag;
    drag.cell = CellPos{7, 4}; // chrome: fuera por abajo
    ed.handleEvent(drag);
    
    CHECK(ed.getStateForTesting() == State::Seleccion);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, 11); // paso del evento
    CHECK(ed.hasSelection());

    int oracleCalls = 0;
    ed.setMouseButtonPressedQuery([&] { ++oracleCalls; return true; });
    const int topBefore = ed.getActiveBufferForTesting().viewport.top;
    const int lineBefore = ed.getActiveBufferForTesting().cursor.line;
    const auto now = std::chrono::steady_clock::now();

    ed.clearExpiredMessages(now);
    CHECK_EQ(oracleCalls, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, topBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, lineBefore);
    CHECK(ed.hasSelection());
    CHECK(ed.getStateForTesting() == State::Seleccion);
    CHECK(ed.isRunning());

    // El timeout sí avanza: un paso, con consulta al oráculo.
    ed.tick(std::chrono::steady_clock::now() + std::chrono::seconds(10));
    CHECK(oracleCalls > 0);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, topBefore + 1);
}

// requestQuit con resultado: el botón de cierre GUI sabe si salió.
// Sin modificar sale; modificado bloquea salvo force.
TEST(frontier_fase_c_request_quit) {
    Editor clean, ed;
    InputEvent i, x;
    
    CHECK(clean.isRunning());
    CHECK(clean.requestQuit(false));
    CHECK(!clean.isRunning());

    i.type = InputEventType::InsertChar;
    i.text = "i";
    ed.handleEvent(i); // Navegacion -> Interaccion
    
    x.type = InputEventType::InsertChar;
    x.text = "X";
    ed.handleEvent(x); // modifica el buffer
    CHECK(ed.getActiveBufferForTesting().modified);
    CHECK(ed.isRunning());
    CHECK(!ed.requestQuit(false)); // bloqueado: aviso, sigue corriendo
    CHECK(ed.isRunning());
    
    CHECK(ed.requestQuit(true)); // forzado: sale
    CHECK(!ed.isRunning());
}

// Es un test puro: sin Editor, sin sink, solo funciones que reciben números 
// y devuelven decisiones o strings.
TEST(frontier_tty_scroll_op) {
    TtyScrollOp op = scrollOpFor(20, 3);
    CHECK(op.useRegion);
    CHECK_EQ(op.absDelta, 3);
    CHECK(op.down);
    
    TtyScrollOp up = scrollOpFor(20, -2);
    CHECK(up.useRegion);
    CHECK(!up.down);
    CHECK(!scrollOpFor(20, 0).useRegion);
    CHECK(!scrollOpFor(20, 20).useRegion);
    CHECK(!scrollOpFor(20, 25).useRegion);
    CHECK(!scrollOpFor(0, 3).useRegion);
    
    std::string prefix = scrollRegionPrefix(20, op);
    CHECK(prefix == "\x1b[1;20r\x1b[3S\x1b[r");
}

// Inyección tardía del renderer 
// crea dos editores con el mismo documento (misma lógica de armado vía makeDoc) pero con distinto 
// momento de inyección del renderer, y compara que el primer frame pintado sea byte a byte idéntico.
TEST(frontier_fase_c_late_setRenderer_matches_early_renderer_highlight) {
    auto makeDoc = [](Editor& ed) {
        ed.getActiveBufferForTesting().document.restore(
            {"int main() {", "  int x = 1;", "  return x;", "}"});
        ed.getActiveBufferForTesting().filename = "t.cpp";
        ed.resize(24, 80);
    };
    
    // editor 1

    Editor early;
    CaptureSink ref;
    
    early.setRenderer(makeTtyTestRenderer());
    makeDoc(early);
    early.setSink(ref);
    early.renderFrame();
    CHECK(!ref.data.empty());
    
    // editor 2

    Editor late;
    CaptureSink nullCap, got;
    
    makeDoc(late);
    late.setSink(nullCap);
    late.renderFrame();  // con Null: corre scroll/brackets pero no pinta
    CHECK(nullCap.data.empty());
    
    late.setRenderer(makeTtyTestRenderer());  // tardío (tests; main usa el ctor)
    late.setSink(got);
    late.renderFrame();
    CHECK_EQ(got.data, ref.data);
}

// división entera píxeles/celda -> CellPos 0-based.
TEST(frontier_guimouse_pixels_to_cell) {
    CHECK((guiCellFromPixels(0, 0, 9, 18) == CellPos{0, 0}));
    CHECK((guiCellFromPixels(8, 17, 9, 18) == CellPos{0, 0}));
    CHECK((guiCellFromPixels(9, 18, 9, 18) == CellPos{1, 1}));
    CHECK((guiCellFromPixels(20, 40, 9, 18) == CellPos{2, 2}));
    
    // Métrica inválida o píxel negativo -> inválida, nunca {0,0} espurio.
    CHECK(!guiCellFromPixels(10, 10, 0, 18).valid());
    CHECK(!guiCellFromPixels(10, 10, 9, 0).valid());
    CHECK(!guiCellFromPixels(-1, 5, 9, 18).valid());
    CHECK(!guiCellFromPixels(5, -1, 9, 18).valid());
}

// puntos <-> drawable:
// escala 1 es identidad
// escala 2 los puntos suben 2× (evento -> drawable) y bajan 2× (IME).
TEST(frontier_guimouse_scale_coord_hidpi) {
    CHECK_EQ(guiScaleCoord(100, 960, 960), 100);
    CHECK_EQ(guiScaleCoord(100, 960, 1920), 200);
    CHECK_EQ(guiScaleCoord(200, 1920, 960), 100);
    CHECK_EQ(guiScaleCoord(0, 960, 1920), 0);
    CHECK_EQ(guiScaleCoord(100, 200, 300), 150);  // escala fraccionaria 1.5×
    
    // Escala desconocida (<=0): identidad = comportamiento pre-HiDPI.
    CHECK_EQ(guiScaleCoord(100, 0, 1920), 100);
    CHECK_EQ(guiScaleCoord(100, 960, 0), 100);
    CHECK_EQ(guiScaleCoord(100, -5, 1920), 100);
    CHECK_EQ(guiScaleCoord(100, 960, -7), 100);
    
    // Cadena completa a 2× 
    // (ventana 960×600, drawable 1920×1200, celda 9×18): 
    // el punto (19,37) es drawable (38,74) = celda (4,4)
    const int px = guiScaleCoord(19, 960, 1920);
    const int py = guiScaleCoord(37, 600, 1200);
    CHECK_EQ(px, 38);
    CHECK_EQ(py, 74);
    
    const CellPos c = guiCellFromPixels(px, py, 9, 18);
    CHECK_EQ(c.col, 4);
    CHECK_EQ(c.row, 4);
}

// el rect del cursor vive en píxeles del drawable pero
// SDL_SetTextInputRect habla en puntos de ventana.
TEST(frontier_guiime_rect_to_window_units) {    
    const GuiImeRectPx id = guiImeRectToWindow({36, 72, 9, 18}, 960, 600,
                                               960, 600);
    CHECK_EQ(id.x, 36);
    CHECK_EQ(id.y, 72);
    CHECK_EQ(id.w, 9);
    CHECK_EQ(id.h, 18);
    
    // Celda (2,2) con celda escalada 18×36 a 2×: drawable (36,72,18,36) ->
    // ventana (18,36,9,18).
    const GuiImeRectPx half = guiImeRectToWindow({36, 72, 18, 36}, 960, 600,
                                                 1920, 1200);
    CHECK_EQ(half.x, 18);
    CHECK_EQ(half.y, 36);
    CHECK_EQ(half.w, 9);
    CHECK_EQ(half.h, 18);
    
    // Ida y vuelta por eje: ventana -> drawable -> ventana es identidad en
    // múltiplos exactos (el truncado <1px nunca cambia la celda).
    const int px = guiScaleCoord(19, 960, 1920);
    const int py = guiScaleCoord(37, 600, 1200);
    CHECK_EQ(guiScaleCoord(px, 1920, 960), 19);
    CHECK_EQ(guiScaleCoord(py, 1200, 600), 37);
    
    // Medidas inválidas: 
    // identidad por eje (comportamiento pre-HiDPI).
    // Los ejes son independientes: si solo falla el ancho, Y sigue bajando.
    const GuiImeRectPx inv = guiImeRectToWindow({36, 72, 18, 36}, 0, 600,
                                                1920, 1200);
    CHECK_EQ(inv.x, 36);
    CHECK_EQ(inv.y, 36);
    CHECK_EQ(inv.w, 18);
    CHECK_EQ(inv.h, 18);
    
    const GuiImeRectPx inv2 = guiImeRectToWindow({36, 72, 18, 36}, 960, 600,
                                                 0, 0);
    CHECK_EQ(inv2.x, 36);
    CHECK_EQ(inv2.y, 72);
    CHECK_EQ(inv2.w, 18);
    CHECK_EQ(inv2.h, 36);
}

// Las fábricas son el camino canónico CellPos -> InputEvent del loop.
TEST(frontier_guimouse_press_release_factories_carry_cell) {
    InputEvent p = makeMousePressEvent(CellPos{3, 4});
    CHECK_EQ(static_cast<int>(p.type), static_cast<int>(InputEventType::MousePress));
    CHECK((p.cell == CellPos{3, 4}));
    
    InputEvent d = makeMouseDragEvent(CellPos{5, 6});
    CHECK_EQ(static_cast<int>(d.type), static_cast<int>(InputEventType::MouseDrag));
    CHECK((d.cell == CellPos{5, 6}));
    
    InputEvent r = makeMouseReleaseEvent(CellPos{7, 8});
    CHECK_EQ(static_cast<int>(r.type), static_cast<int>(InputEventType::MouseRelease));
    CHECK((r.cell == CellPos{7, 8}));
}

// preciseY manda, si es cero, fallback al entero.
// flipped niega. Ambos cero -> 0.0.
TEST(frontier_guimouse_wheel_delta_precise_first) {
    CHECK_EQ(guiWheelDelta(1.0f, 1, false), 1.0);
    CHECK_EQ(guiWheelDelta(-1.0f, -1, false), -1.0);
    CHECK_EQ(guiWheelDelta(0.25f, 0, false), 0.25);
    CHECK_EQ(guiWheelDelta(0.0f, 1, false), 1.0);
    CHECK_EQ(guiWheelDelta(0.0f, -2, false), -2.0);
    CHECK_EQ(guiWheelDelta(0.0f, 0, false), 0.0);
    CHECK_EQ(guiWheelDelta(1.0f, 1, true), -1.0);
    CHECK_EQ(guiWheelDelta(0.25f, 0, true), -0.25);
    CHECK_EQ(guiWheelDelta(0.0f, 1, true), -1.0);
}

// la rueda clásica sale directo; 
// el táctil junta fracciones hasta 1.0
// opuestas se cancelan sin eventos fantasma.
TEST(frontier_guimouse_wheel_accumulates_fractions) {
    double acc = 0.0;
    CHECK_EQ(guiConsumeWheelDelta(acc, 1.0), 1);
    CHECK_EQ(acc, 0.0);
    CHECK_EQ(guiConsumeWheelDelta(acc, -1.0), -1);
    CHECK_EQ(acc, 0.0);
    CHECK_EQ(guiConsumeWheelDelta(acc, 3.0), 3);
    CHECK_EQ(acc, 0.0);
    
    // Fracciones exactas en binario (0.25): 3 no alcanzan, la 4ª emite una.
    CHECK_EQ(guiConsumeWheelDelta(acc, 0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, 0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, 0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, 0.25), 1);
    CHECK_EQ(acc, 0.0);
    
    // Cancelación: 0.5 - 0.5 = 0, ningún evento.
    CHECK_EQ(guiConsumeWheelDelta(acc, 0.5), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, -0.5), 0);
    CHECK_EQ(acc, 0.0);
    
    // Negativas acumulan igual: 4 × -0.25 = una unidad abajo.
    CHECK_EQ(guiConsumeWheelDelta(acc, -0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, -0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, -0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, -0.25), -1);
    CHECK_EQ(acc, 0.0);
}

// drawable 960x600, celda real 9x18 -> 106x33.
// Es el camino del loop tras SDL_GetRendererOutputSize.
TEST(frontier_guigrid_size_scale_1x) {
    const platform::WindowSize s = guiGridSize(960, 600, 9, 18);
    
    CHECK_EQ(s.cols, 106);
    CHECK_EQ(s.rows, 33);
    CHECK_EQ(s.cellW, 9);
    CHECK_EQ(s.cellH, 18);
    CHECK_EQ(s.pixelW, 960);
    CHECK_EQ(s.pixelH, 600);
}

// HiDPI 2x, división FÍSICA: drawable 1920x1200 / celda sin escalar 9x18
// = 213x66 (frente a 106x33 con drawable 960x600). Es lo que daría NO
// escalar la fuente: grilla duplicada con texto diminuto (bug P1).
// El contrato visual-estable escala la celda antes de dividir (ver
// guigrid_stable_visual_size_hidpi): este test solo congela la división.
TEST(frontier_guigrid_size_hidpi_2x_uses_drawable) {    
    const platform::WindowSize s = guiGridSize(1920, 1200, 9, 18);
    CHECK_EQ(s.cols, 213);
    CHECK_EQ(s.rows, 66);
    
    // Cadena completa evento->celda a 2x (paridad con guimouse_scale_coord):
    // punto (19,37) -> drawable (38,74) -> celda (4,4).
    const int px = guiScaleCoord(19, 960, 1920);
    const int py = guiScaleCoord(37, 600, 1200);
    const CellPos c = guiCellFromPixels(px, py, s.cellW, s.cellH);
    CHECK_EQ(c.col, 4);
    CHECK_EQ(c.row, 4);
    
    // Sin escalar (bug HiDPI: puntos / celda) daría (2,2).
    const CellPos wrong = guiCellFromPixels(19, 37, s.cellW, s.cellH);
    CHECK_EQ(wrong.col, 2);
    CHECK_EQ(wrong.row, 2);
}

// Sin fuente (celda <=0): fallback canónico, igual que GuiFont y el loop.
TEST(frontier_guigrid_size_fallback_and_clamp) {
    const platform::WindowSize f = guiGridSize(960, 600, 0, 0);
    CHECK_EQ(f.cellW, platform::kFallbackCellW);
    CHECK_EQ(f.cellH, platform::kFallbackCellH);
    CHECK_EQ(f.cols, 106);
    CHECK_EQ(f.rows, 33);
    
    const platform::WindowSize f2 = guiGridSize(960, 600, -3, -5);
    CHECK_EQ(f2.cellW, platform::kFallbackCellW);
    CHECK_EQ(f2.cellH, platform::kFallbackCellH);
    
    // Ventana minimizada / drawable diminuto: nunca 0x0 al Editor.
    const platform::WindowSize m = guiGridSize(1, 1, 9, 18);
    CHECK_EQ(m.cols, 1);
    CHECK_EQ(m.rows, 1);
    
    const platform::WindowSize z = guiGridSize(0, 0, 9, 18);
    CHECK_EQ(z.cols, 1);
    CHECK_EQ(z.rows, 1);
    
    // Fraccionaria 1.5x: 300px / 9 = 33 cols (truncado, sin redondeo).
    const platform::WindowSize fr = guiGridSize(300, 300, 9, 18);
    CHECK_EQ(fr.cols, 33);
    CHECK_EQ(fr.rows, 16);
}

// sin precondiciones; píxeles negativos -> 0 
// (desconocido, igual que Editor::resize)
// nunca negativos guardados. La grilla se clampea a 1x1.
TEST(frontier_guigrid_size_negative_pixels_normalized) {
    const platform::WindowSize s = guiGridSize(-100, -50, 9, 18);
    CHECK_EQ(s.pixelW, 0);
    CHECK_EQ(s.pixelH, 0);
    CHECK_EQ(s.cols, 1);
    CHECK_EQ(s.rows, 1);
    
    // Mixto: un eje negativo no contamina al otro.
    const platform::WindowSize m = guiGridSize(-100, 600, 9, 18);
    CHECK_EQ(m.pixelW, 0);
    CHECK_EQ(m.pixelH, 600);
    CHECK_EQ(m.cols, 1);
    CHECK_EQ(m.rows, 33);
    
    // Negativos en todo: celdas al fallback, píxeles a 0, grilla 1x1.
    const platform::WindowSize a = guiGridSize(-100, -50, -9, -18);
    CHECK_EQ(a.pixelW, 0);
    CHECK_EQ(a.pixelH, 0);
    CHECK_EQ(a.cellW, platform::kFallbackCellW);
    CHECK_EQ(a.cellH, platform::kFallbackCellH);
    CHECK_EQ(a.cols, 1);
    CHECK_EQ(a.rows, 1);
}

// escalado con redondeo entre espacios.
TEST(frontier_guiscale_size_font_and_fallback) {
    // Fuente: 16 a 1x queda 16; a 2x (600pts->1200px) pasa a 32.
    CHECK_EQ(guiScaledFontPixels(16, 600, 600), 16);
    CHECK_EQ(guiScaledFontPixels(16, 600, 1200), 32);
    CHECK_EQ(guiScaledFontPixels(16, 960, 960), 16);
    
    // Fraccionaria 1.5x: 16 -> 24 (redondeo, no truncado).
    CHECK_EQ(guiScaledFontPixels(16, 200, 300), 24);
    
    // Escala desconocida (<=0): identidad = comportamiento pre-HiDPI.
    CHECK_EQ(guiScaledFontPixels(16, 0, 1200), 16);
    CHECK_EQ(guiScaledFontPixels(16, 600, 0), 16);
    CHECK_EQ(guiScaledFontPixels(16, -5, 1200), 16);
    
    // Fallback por eje: 9x18 a 2x -> 18x36 (misma proporción que la fuente).
    CHECK_EQ(guiScaleSize(9, 960, 1920), 18);
    CHECK_EQ(guiScaleSize(18, 600, 1200), 36);
    CHECK_EQ(guiScaleSize(9, 960, 960), 9);
    CHECK_EQ(guiScaleSize(18, 600, 600), 18);
    
    // Base inválida o escala desconocida: identidad.
    CHECK_EQ(guiScaleSize(0, 960, 1920), 0);
    CHECK_EQ(guiScaleSize(9, 0, 1920), 9);
    CHECK_EQ(guiScaleSize(9, 960, 0), 9);
}

// Contrato visual-estable: a 2x la celda SE ESCALA antes de dividir
// y la grilla se mantiene (~106x33) en vez de duplicarse (213x66).
// Ventana 960x600 pts, drawable 1920x1200 px, base 16px/9x18:
TEST(frontier_guigrid_stable_visual_size_hidpi) {    
    const int fontPx = guiScaledFontPixels(16, 600, 1200);
    CHECK_EQ(fontPx, 32);
    
    const int cw = guiScaleSize(9, 960, 1920);
    const int ch = guiScaleSize(18, 600, 1200);
    CHECK_EQ(cw, 18);
    CHECK_EQ(ch, 36);
    
    const platform::WindowSize s2 = guiGridSize(1920, 1200, cw, ch);
    const platform::WindowSize s1 = guiGridSize(960, 600, 9, 18);
    CHECK_EQ(s2.cols, s1.cols);
    CHECK_EQ(s2.rows, s1.rows);
    CHECK_EQ(s2.cols, 106);
    CHECK_EQ(s2.rows, 33);
    
    // El mouse sigue la misma celda escalada: punto (19,37) -> drawable
    // (38,74) -> celda (2,2) con celda 18x36 (era (4,4) con 9x18 sin
    // escalar: el índice acompaña al tamaño visual, no al físico).
    const int px = guiScaleCoord(19, 960, 1920);
    const int py = guiScaleCoord(37, 600, 1200);
    const CellPos c = guiCellFromPixels(px, py, cw, ch);
    CHECK_EQ(c.col, 2);
    CHECK_EQ(c.row, 2);
}

// CONTRATO DE UNIDADES CLI (puro y sin SDL): `--font-size N` son píxeles
// lógicos (== píxeles del drawable a escala 1; no puntos tipográficos de
// 1/72in) y la talla que llega a TTF_OpenFont es guiScaledFontPixels(N,
// winH, outH) (eje vertical, redondeo). Fija que el mismo N da la misma
// altura visual en cualquier monitor: el loop escala tanto al abrir (init)
// como en cada resize.
TEST(frontier_gui_contrato_fontsize_pixeles_logicos) {
    // A escala 1 la base llega intacta a TTF_OpenFont.
    CHECK_EQ(guiScaledFontPixels(16, 600, 600), 16);
    
    // A escala 2 la talla duplica (misma altura visual, misma grilla).
    CHECK_EQ(guiScaledFontPixels(16, 600, 1200), 32);
    
    // Base custom del CLI (--font-size 18) escala igual.
    CHECK_EQ(guiScaledFontPixels(18, 600, 1200), 36);
    
    // Escala fraccionaria 1.5× con redondeo, no truncado (16 -> 24).
    CHECK_EQ(guiScaledFontPixels(16, 600, 900), 24);
    
    // Ventana/salida degenerada (minimizada, 0): identidad segura.
    CHECK_EQ(guiScaledFontPixels(16, 0, 1200), 16);
    CHECK_EQ(guiScaledFontPixels(16, 600, 0), 16);
}

// CONTRATO DE UNIDADES CLI (puro y sin SDL): `--geometry WxH` son
// coordenadas de ventana (puntos, no píxeles físicos). La misma ventana
// lógica en dos escalas da la misma grilla visual: si la geometría fueran
// píxeles físicos, a 2× la ventana lógica sería la mitad y la grilla
// cambiaría (53×16 en vez de 106×33).
TEST(frontier_gui_contrato_geometry_coords_ventana) {
    // Ventana lógica 1200×800 (p. ej. --geometry 1200x800) a escala 1.
    const int winW = 1200, winH = 800;
    const int cw1 = guiScaleSize(9, winW, winW);
    const int ch1 = guiScaleSize(18, winH, winH);
    const platform::WindowSize g1 = guiGridSize(winW, winH, cw1, ch1);
    CHECK_EQ(g1.cols, 133);
    CHECK_EQ(g1.rows, 44);
    
    // Misma ventana lógica a escala 2 (drawable 2400×1600): misma grilla.
    const int outW = 2400, outH = 1600;
    const int cw2 = guiScaleSize(9, winW, outW);
    const int ch2 = guiScaleSize(18, winH, outH);
    const platform::WindowSize g2 = guiGridSize(outW, outH, cw2, ch2);
    CHECK_EQ(g2.cols, g1.cols);
    CHECK_EQ(g2.rows, g1.rows);
    
    // Y la talla de fuente de esa geometría escala en la misma razón.
    CHECK_EQ(guiScaledFontPixels(16, winH, outH), 32);
}

// el drawable efectivo ante medidas degeneradas 
// (minimizado o driver que reporta un eje en 0). 
// El fallback es POR EJE: un eje válido nunca se descarta porque el otro falle
TEST(frontier_gui_drawable_fallback_por_eje) {
    // Medidas sanas: passthrough intacto.
    GuiDrawableSize ok = guiDrawableOrWindow(960, 600, 1920, 1200);
    CHECK_EQ(ok.w, 1920);
    CHECK_EQ(ok.h, 1200);
    
    // Un eje degenerado: el otro se conserva (no cae a la ventana).
    GuiDrawableSize mixW = guiDrawableOrWindow(960, 600, 0, 1200);
    CHECK_EQ(mixW.w, 960);
    CHECK_EQ(mixW.h, 1200);
    
    GuiDrawableSize mixH = guiDrawableOrWindow(960, 600, 1920, 0);
    CHECK_EQ(mixH.w, 1920);
    CHECK_EQ(mixH.h, 600);
    
    // Todo degenerado (minimizada sin medidas): 960×600, escala 1:1.
    GuiDrawableSize all = guiDrawableOrWindow(0, 0, 0, 0);
    CHECK_EQ(all.w, 960);
    CHECK_EQ(all.h, 600);
    
    // Negativos: igual que 0 (desconocido, nunca se guarda tal cual).
    GuiDrawableSize neg = guiDrawableOrWindow(-10, 600, -5, 1200);
    CHECK_EQ(neg.w, 960);
    CHECK_EQ(neg.h, 1200);
    
    // La cadena del loop sobre el fallback nunca da 0×0 al Editor: grilla
    // mínima 1×1 y fuente en identidad.
    const platform::WindowSize g = guiGridSize(all.w, all.h, 9, 18);
    CHECK_EQ(g.cols, 106);
    CHECK_EQ(g.rows, 33);
    CHECK_EQ(guiScaledFontPixels(16, 0, all.h), 16);
}

// cambio de monitor ida y vuelta (1× -> 2× -> 1×) con la MISMA ventana lógica. 
// Replica la cadena del loop (fallback drawable + fuente + grilla + mouse) en cada etapa: 
// la talla sigue a la escala
// (16->32->16), la grilla no se mueve y el mismo clic lógico cae en la
// misma celda en las tres etapas.
TEST(frontier_gui_hidpi_transicion_monitor_ida_y_vuelta) {
    const int winW = 960, winH = 600;
    // Etapa 1×: ventana == drawable.
    GuiDrawableSize d1 = guiDrawableOrWindow(winW, winH, 960, 600);
    
    const int f1 = guiScaledFontPixels(16, winH, d1.h);
    CHECK_EQ(f1, 16);
    
    const platform::WindowSize g1 = guiGridSize(d1.w, d1.h, 9, 18);
    CHECK_EQ(g1.cols, 106);
    CHECK_EQ(g1.rows, 33);
    
    // Etapa 2× (DISPLAY_CHANGED sin resize en puntos): drawable duplica.
    GuiDrawableSize d2 = guiDrawableOrWindow(winW, winH, 1920, 1200);
    
    const int f2 = guiScaledFontPixels(16, winH, d2.h);
    CHECK_EQ(f2, 32);
    
    const int cw2 = guiScaleSize(9, winW, d2.w);
    const int ch2 = guiScaleSize(18, winH, d2.h);
    const platform::WindowSize g2 = guiGridSize(d2.w, d2.h, cw2, ch2);
    CHECK_EQ(g2.cols, g1.cols);
    CHECK_EQ(g2.rows, g1.rows);
    
    // Vuelta a 1×: la talla baja sola (ensureFontPixels recarga por cambio)
    // y la grilla sigue estable.
    GuiDrawableSize d3 = guiDrawableOrWindow(winW, winH, 960, 600);
    const int f3 = guiScaledFontPixels(16, winH, d3.h);
    CHECK_EQ(f3, 16);
    
    const platform::WindowSize g3 = guiGridSize(d3.w, d3.h, 9, 18);
    CHECK_EQ(g3.cols, g1.cols);
    CHECK_EQ(g3.rows, g1.rows);
    
    // Mismo clic lógico (19,37) pts en las tres etapas -> misma celda.
    const CellPos c1 = guiCellFromPixels(guiScaleCoord(19, winW, d1.w),
                                         guiScaleCoord(37, winH, d1.h), 9, 18);
    const CellPos c2 = guiCellFromPixels(guiScaleCoord(19, winW, d2.w),
                                         guiScaleCoord(37, winH, d2.h), cw2,
                                         ch2);
    const CellPos c3 = guiCellFromPixels(guiScaleCoord(19, winW, d3.w),
                                         guiScaleCoord(37, winH, d3.h), 9, 18);
    CHECK(c2 == c1);
    CHECK(c3 == c1);
}

// escala NO uniforme (X a 2×, Y a 1.5×; fuera de lo que SDL
// produce, donde el factor es único). Cada eje usa su propia razón en
// coords, fallback y grilla; la fuente sigue el eje VERTICAL (la talla es
// altura) y el raster escala uniforme con ella.
TEST(frontier_gui_hidpi_escala_no_uniforme_por_eje) {
    const int winW = 960, winH = 600;
    const int outW = 1920, outH = 900;
    CHECK_EQ(guiScaleCoord(100, winW, outW), 200);
    CHECK_EQ(guiScaleCoord(100, winH, outH), 150);
    CHECK_EQ(guiScaledFontPixels(16, winH, outH), 24);
    CHECK_EQ(guiScaleSize(9, winW, outW), 18);
    CHECK_EQ(guiScaleSize(18, winH, outH), 27);
    
    const platform::WindowSize g = guiGridSize(outW, outH, 18, 27);
    CHECK_EQ(g.cols, 106);
    CHECK_EQ(g.rows, 33);
}

// --- Headless extra (sin ventana): bordes exactos + valores grandes ---
// División PURA en un solo espacio (no es prueba HiDPI: acá píxel y celda ya
// están en el mismo espacio; el caso HiDPI real —puntos vs drawable— vive en
// gui_hidpi_integracion_mismo_espacio_headless). El borde pertenece a la celda
// siguiente y los valores grandes no desbordan.
TEST(frontier_guimouse_pixels_bordes_y_grandes_headless) {
    CHECK((guiCellFromPixels(9, 18, 9, 18) == CellPos{1, 1}));
    CHECK((guiCellFromPixels(8, 17, 9, 18) == CellPos{0, 0}));
    CHECK((guiCellFromPixels(18, 36, 9, 18) == CellPos{2, 2}));
    CHECK((guiCellFromPixels(17, 35, 9, 18) == CellPos{1, 1}));
    
    // Origen siempre (0,0) con cualquier celda válida.
    CHECK((guiCellFromPixels(0, 0, 1, 1) == CellPos{0, 0}));
    CHECK((guiCellFromPixels(0, 0, 18, 36) == CellPos{0, 0}));
    
    // Valores grandes (4K): 3840/9=426, 2160/18=120, sin desborde.
    // es división pura con celda sin escalar, NO el camino HiDPI
    // real (a 2× la celda se escala a 18×36 y la grilla queda 213×60; ver
    // gui_hidpi_integracion_mismo_espacio_headless).
    const CellPos big = guiCellFromPixels(3840, 2160, 9, 18);
    CHECK_EQ(big.col, 426);
    CHECK_EQ(big.row, 120);
    CHECK(big.valid());
    
    // Celda 1x1: identidad píxel->celda.
    CHECK((guiCellFromPixels(123, 456, 1, 1) == CellPos{123, 456}));
    
    // Negativos parciales: un eje inválido invalida toda la celda.
    CHECK(!guiCellFromPixels(-1000000, 5, 9, 18).valid());
    CHECK(!guiCellFromPixels(5, -1000000, 9, 18).valid());
}

// --- Headless extra: grilla grande (4K) + drawable diminuto ---
// División PURA (mismo espacio en ambos lados). El camino HiDPI real con
// celda escalada vive en gui_hidpi_integracion_mismo_espacio_headless.
TEST(frontier_guigrid_grande_y_minimo_headless) {
    const platform::WindowSize k4 = guiGridSize(3840, 2160, 9, 18);
    CHECK_EQ(k4.cols, 426);
    CHECK_EQ(k4.rows, 120);
    CHECK_EQ(k4.cellW, 9);
    CHECK_EQ(k4.cellH, 18);
    
    // 1px de drawable: clamp a 1x1, nunca 0x0 al Editor::resize.
    const platform::WindowSize tiny = guiGridSize(1, 1, 18, 36);
    CHECK_EQ(tiny.cols, 1);
    CHECK_EQ(tiny.rows, 1);
    
    // Celda gigante (ventana menor que una celda): también 1x1.
    const platform::WindowSize huge = guiGridSize(100, 100, 1000, 1000);
    CHECK_EQ(huge.cols, 1);
    CHECK_EQ(huge.rows, 1);
}

// --- Headless extra: celda inválida -> Editor no arma gesto ---
// El píxel fuera de métrica da CellPos inválida (nunca {0,0} espurio) y el
// Editor la trata como fuera de contenido: cancela selección si la había y
// no arma gesto (el drag posterior sin press se ignora).
TEST(frontier_guimouse_celda_invalida_no_arma_gesto_headless) {
    InputEvent pressInv = makeMousePressEvent(CellPos{});
    
    CHECK(!pressInv.cell.valid());
    CHECK_EQ(static_cast<int>(pressInv.type),
             static_cast<int>(InputEventType::MousePress));
    // Editor: press inválido desde Navegación no mueve el cursor ni arma.
    {
        Editor ed;
        ed.getActiveBufferForTesting().document.restore({"hola", "mundo"});
        InputEvent press = makeMousePressEvent(CellPos{});
        ed.handleEvent(press);
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::Navegacion));
        CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
        CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 0);
        // Drag sin press previo: ignorado (sin selección, sin crash).
        InputEvent drag = makeMouseDragEvent(CellPos{5, 0});
        ed.handleEvent(drag);
        CHECK(!ed.hasSelection());
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::Navegacion));
    }
    // Editor: press inválido desde Selección cancela a Navegación (igual que
    // click en el chrome), sin dejar gesto armado.
    {
        Editor ed;
        ed.getActiveBufferForTesting().document.restore({"hola mundo"});
        InputEvent s;
        s.type = InputEventType::InsertChar;
        s.text = "s";
        ed.handleEvent(s);
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::Seleccion));
        ed.handleEvent(makeMousePressEvent(CellPos{}));
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::Navegacion));
        CHECK(!ed.hasSelection());
        ed.handleEvent(makeMouseDragEvent(CellPos{5, 0}));
        CHECK(!ed.hasSelection());
    }
}

// integración HiDPI en el MISMO espacio, sin SDL ni ventana.
// Replica EXACTAMENTE la cadena del loop (GuiRunLoop::currentSize +
// mouseCell) solo con helpers puros: la grilla divide píxeles REALES del
// drawable por celda FÍSICA escalada, y el evento en PUNTOS de ventana sube
// a drawable antes de dividirse por esa misma celda. Si un lado quedara sin
// escalar, los tests puros de división seguirían pasando pero el clic real
// caería en otra celda (el bug que este test caza: ambas variantes rotas dan
// una celda distinta de la estable).
TEST(frontier_gui_hidpi_integracion_mismo_espacio_headless) {
    // Marco 1×: ventana 960×600 pts == drawable 960×600 px, celda 9×18.
    const int winW = 960, winH = 600;
    const int out1W = 960, out1H = 600;
    const int cw1 = guiScaleSize(9, winW, out1W);
    const int ch1 = guiScaleSize(18, winH, out1H);
    CHECK_EQ(cw1, 9);
    CHECK_EQ(ch1, 18);
    
    const platform::WindowSize g1 = guiGridSize(out1W, out1H, cw1, ch1);
    CHECK_EQ(g1.cols, 106);
    CHECK_EQ(g1.rows, 33);
    
    // Marco 2×: misma ventana lógica, drawable 1920×1200, celda 18×36.
    const int out2W = 1920, out2H = 1200;
    const int cw2 = guiScaleSize(9, winW, out2W);
    const int ch2 = guiScaleSize(18, winH, out2H);
    CHECK_EQ(cw2, 18);
    CHECK_EQ(ch2, 36);
    
    const platform::WindowSize g2 = guiGridSize(out2W, out2H, cw2, ch2);
    // La grilla usa el DRAWABLE: con puntos de ventana daría 53×16.
    const platform::WindowSize wrongGrid = guiGridSize(winW, winH, cw2, ch2);
    CHECK_EQ(wrongGrid.cols, 53);
    CHECK_EQ(wrongGrid.rows, 16);
    CHECK(g2.cols == g1.cols);
    CHECK(g2.rows == g1.rows);
    
    // Mismo clic lógico (19,37) pts en ambos marcos -> misma celda visual.
    const CellPos c1 = guiCellFromPixels(
        guiScaleCoord(19, winW, out1W), guiScaleCoord(37, winH, out1H), cw1,ch1);
    const CellPos c2 = guiCellFromPixels(
        guiScaleCoord(19, winW, out2W), guiScaleCoord(37, winH, out2H), cw2,ch2);
    CHECK_EQ(c1.col, 2);
    CHECK_EQ(c1.row, 2);
    CHECK(c2 == c1);
    
    // Sin escalar el evento (puntos / celda física): celda chica (1,1).
    const CellPos noEventScale = guiCellFromPixels(19, 37, cw2, ch2);
    CHECK_EQ(noEventScale.col, 1);
    CHECK_EQ(noEventScale.row, 1);
    CHECK(!(noEventScale == c1));
    
    // Sin escalar la celda (drawable / celda base): celda grande (4,4).
    const int px2 = guiScaleCoord(19, winW, out2W);
    const int py2 = guiScaleCoord(37, winH, out2H);
    const CellPos noCellScale = guiCellFromPixels(px2, py2, 9, 18);
    CHECK_EQ(noCellScale.col, 4);
    CHECK_EQ(noCellScale.row, 4);
    CHECK(!(noCellScale == c1));
    
    // 4K real: ventana 1920×1080 pts, drawable 3840×2160, celda 18×36 ->
    // 213×60, igual que 1× (1920×1080 / 9×18). Sin escalar daría 426×120.
    const int w4 = 1920, h4 = 1080, o4W = 3840, o4H = 2160;
    const int cw4 = guiScaleSize(9, w4, o4W);
    const int ch4 = guiScaleSize(18, h4, o4H);
    CHECK_EQ(cw4, 18);
    CHECK_EQ(ch4, 36);
    
    const platform::WindowSize g4 = guiGridSize(o4W, o4H, cw4, ch4);
    const platform::WindowSize g4base = guiGridSize(w4, h4, 9, 18);
    CHECK_EQ(g4.cols, g4base.cols);
    CHECK_EQ(g4.rows, g4base.rows);
    CHECK_EQ(g4.cols, 213);
    CHECK_EQ(g4.rows, 60);
}

// límites de la grilla en el MISMO espacio, sin SDL ni ventana.
// Con drawable 3840×2160 y celda 9×18 la grilla es 426×120 y ocupa
// 426*9=3834 px horizontales (6 px sobran a la derecha): el borde exacto
// ENTRE celdas pertenece a la siguiente, pero el borde EXTERIOR queda fuera
// (inválida, nunca clamp a la última columna). La variante acotada es la que
// separa ambas condiciones; la pura seguiría dando col==cols "válida".
TEST(frontier_gui_bordes_grilla_limites_headless) {
    const int cw = 9, ch = 18;
    const platform::WindowSize g = guiGridSize(3840, 2160, cw, ch);
    CHECK_EQ(g.cols, 426);
    CHECK_EQ(g.rows, 120);
    
    const int gridW = 426 * 9;    // 3834 px ocupados
    const int gridH = 120 * 18;   // 2160 px ocupados (exacto en vertical)
    CHECK_EQ(gridW, 3834);
    CHECK_EQ(gridH, 2160);
    
    // x == cellW: primera celda de la segunda columna (borde inter -> sigue).
    CHECK((guiCellFromPixelsBounded(cw, 0, cw, ch, g.cols, g.rows) ==
           CellPos{1, 0}));
    CHECK((guiCellFromPixelsBounded(0, ch, cw, ch, g.cols, g.rows) ==
           CellPos{0, 1}));
    // x == gridWidth-1: último píxel de la última columna (dentro).
    
    const CellPos last =
        guiCellFromPixelsBounded(gridW - 1, gridH - 1, cw, ch, g.cols, g.rows);
    CHECK(last.valid());
    CHECK_EQ(last.col, 425);
    CHECK_EQ(last.row, 119);
    // x == gridWidth: fuera de la grilla (no clamp a la última columna).
    CHECK(!guiCellFromPixelsBounded(gridW, 0, cw, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, gridH, cw, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(gridW, gridH, cw, ch, g.cols, g.rows)
               .valid());
    // Más allá del drawable también es inválida (franja sobrante incluida).
    CHECK(!guiCellFromPixelsBounded(3839, 0, cw, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(3840, 2160, cw, ch, g.cols, g.rows)
               .valid());
    // La pura, en cambio, daría col==cols "válida": es lo que la acotada evita.
    const CellPos pure =
        guiCellFromPixels(gridW, 0, cw, ch);
    CHECK(pure.valid());
    CHECK_EQ(pure.col, 426);
}

// el exterior no se reconvierte en clic válido por el clamp.
// La acotada devuelve inválida donde la pura devolvería la última columna;
// el Editor ya trata la inválida como fuera de contenido (cancela selección,
// no arma gesto: ver guimouse_celda_invalida_no_arma_gesto_headless).
TEST(frontier_gui_borde_exterior_no_clampea_a_ultima_headless) {
    const int cw = 9, ch = 18;
    const platform::WindowSize g = guiGridSize(3840, 2160, cw, ch);
    const int gridW = 426 * 9;
    
    // Dentro: ambas vías coinciden en la última columna.
    CHECK((guiCellFromPixelsBounded(gridW - 1, 0, cw, ch, g.cols, g.rows) == CellPos{425, 0}));
    CHECK((guiCellFromPixels(gridW - 1, 0, cw, ch) == CellPos{425, 0}));
    
    // Fuera: solo la acotada es inválida; la pura "ve" col 426.
    CHECK(!guiCellFromPixelsBounded(gridW, 0, cw, ch, g.cols, g.rows).valid());
    CHECK(guiCellFromPixels(gridW, 0, cw, ch).valid());
    
    // Grilla 1×1 (drawable de 1 px, celda 9×18): la única celda cubre los
    // píxeles 0..8; el borde exterior está en 9 px, no en 1 px.
    const platform::WindowSize tiny = guiGridSize(1, 1, cw, ch);
    CHECK_EQ(tiny.cols, 1);
    CHECK_EQ(tiny.rows, 1);
    CHECK((guiCellFromPixelsBounded(0, 0, cw, ch, tiny.cols, tiny.rows) == CellPos{0, 0}));
    CHECK((guiCellFromPixelsBounded(8, 17, cw, ch, tiny.cols, tiny.rows) == CellPos{0, 0}));
    CHECK(!guiCellFromPixelsBounded(9, 0, cw, ch, tiny.cols, tiny.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, 18, cw, ch, tiny.cols, tiny.rows).valid());
               
    // Con celda de 1 px la grilla 1×1 sí mide 1 px: (1,0) ya es exterior.
    const platform::WindowSize one = guiGridSize(1, 1, 1, 1);
    CHECK_EQ(one.cols, 1);
    CHECK_EQ(one.rows, 1);
    CHECK((guiCellFromPixelsBounded(0, 0, 1, 1, one.cols, one.rows) == CellPos{0, 0}));
    CHECK(!guiCellFromPixelsBounded(1, 0, 1, 1, one.cols, one.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, 1, 1, 1, one.cols, one.rows).valid());
    
    // x<0 o y<0: inválida en ambas vías, nunca {0,0} espurio.
    CHECK(!guiCellFromPixelsBounded(-1, 0, cw, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, -1, cw, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(-1, -1, cw, ch, g.cols, g.rows).valid());
    
    // Grilla/celda degenerada: inválida (el clamp de guiGridSize a 1×1 no
    // convierte un píxel en clic: la validez la decide la acotada).
    CHECK(!guiCellFromPixelsBounded(0, 0, 0, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, 0, cw, 0, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, 0, cw, ch, 0, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, 0, cw, ch, g.cols, 0).valid());
}

// sin desbordes ante coordenadas muy grandes y sin validar inválidos.
// Se compara por ÍNDICES (col >= cols), nunca por anchos (cols*cellW), así
// que ni INT_MAX desborda: la división int es segura y el clamp a 1×1 jamás
// fabrica un {0,0} a partir de un píxel fuera de rango.
TEST(frontier_gui_sin_desborde_coordenadas_grandes_headless) {
    const int cw = 9, ch = 18;
    
    // División pura con valores grandes: no hay overflow ni crash.
    const CellPos big = guiCellFromPixels(1000000, 2000000, cw, ch);
    CHECK_EQ(big.col, 1000000 / 9);
    CHECK_EQ(big.row, 2000000 / 18);
    CHECK(big.valid());
    
    // Acotada con la grilla 426×120: esos mismos píxeles quedan fuera.
    const platform::WindowSize g = guiGridSize(3840, 2160, cw, ch);
    CHECK(!guiCellFromPixelsBounded(1000000, 2000000, cw, ch, g.cols, g.rows).valid());
    
    // INT_MAX con celda 1×1: índice exacto, sin overflow (col == INT_MAX).
    const CellPos maxCell = guiCellFromPixels(2147483647, 2147483647, 1, 1);
    CHECK_EQ(maxCell.col, 2147483647);
    CHECK_EQ(maxCell.row, 2147483647);
    
    // ...y con grilla finita queda fuera, no clamp a la última.
    CHECK(!guiCellFromPixelsBounded(2147483647, 0, 1, 1, 426, 120).valid());
    CHECK(!guiCellFromPixelsBounded(0, 2147483647, 1, 1, 426, 120).valid());
    
    // Grilla gigante sin multiplicar: guiGridSize no computa cols*cellW.
    const platform::WindowSize huge = guiGridSize(2147483647, 2147483647, 1, 1);
    CHECK_EQ(huge.cols, 2147483647);
    CHECK_EQ(huge.rows, 2147483647);
    
    // Escala con valores grandes: long long intermedio, resultado exacto.
    CHECK_EQ(guiScaleCoord(1000000, 960, 1920), 2000000);
    CHECK_EQ(guiScaleCoord(2000000, 1920, 960), 1000000);
}