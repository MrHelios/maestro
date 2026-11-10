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
// (secuencia de arranque del TtyRunLoop: resize + renderFrame).
// Los tests que afirman bytes ANSI inyectan el backend real explícito vía makeTtyTestRenderer()
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
