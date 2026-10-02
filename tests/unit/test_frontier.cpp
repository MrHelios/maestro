// Frontier tests (pasos 1-15): verifican el nuevo orden sin tocar la
// suite existente. Se corren con: make test-one FILTER='frontier*'
#include "test_framework.h"
#include "rendering/Sink.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

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
#include "app/Editor.h"

namespace {
// Sink capturador: retiene lo escrito para assertar el primer frame
// (secuencia de arranque del TtyRunLoop: resize + renderFrame).
// Los tests que afirman bytes ANSI inyectan el backend real explícito vía
// makeTtyTestRenderer() (el default del Editor es Null neutro).
struct CaptureSink : public Sink {
    std::string data;
    bool writeStdout(const std::string& s) override {
        data += s;
        return true;
    }
};
} // namespace

// 1: CellPos es el tipo común 0-based.
TEST(frontier_cellpos_valid) {
    CHECK((CellPos{10, 5}.valid()));
    CHECK((CellPos{0, 5}.valid()));
    CHECK((CellPos{10, 0}.valid()));
    CHECK((CellPos{0, 0}.valid())); // origen: celda válida, no "vacía"
    CHECK((!CellPos{-1, 5}.valid()));
    CHECK((!CellPos{10, -1}.valid()));
    CHECK((CellPos{3, 4} == CellPos{3, 4}));
    CHECK((CellPos{3, 4} != CellPos{3, 5}));
    // Contrato default inválido: un evento que no es de mouse no debe
    // cargar una "celda válida" espuria en {0,0}.
    CHECK((!CellPos{}.valid()));
    CHECK((!InputEvent{}.cell.valid()));
    CHECK((makeMousePressEvent(CellPos{0, 0}).cell.valid()));
}

// 1+3: InputEvent expone CellPos y payload Resize.
TEST(frontier_event_cellpos_roundtrip) {
    InputEvent e;
    e.type = InputEventType::MousePress;
    e.cell = CellPos{12, 7};
    CHECK_EQ(e.cell.col, 12);
    CHECK_EQ(e.cell.row, 7);

    InputEvent r;
    r.type = InputEventType::Resize;
    r.resizeRows = 40;
    r.resizeCols = 120;
    CHECK_EQ(r.resizeRows, 40);
    CHECK_EQ(r.resizeCols, 120);
}

// 2: fábricas MouseEvent -> InputEvent.
TEST(frontier_mouse_event_factories) {
    InputEvent p = makeMousePressEvent(CellPos{4, 2});
    CHECK_EQ(static_cast<int>(p.type), static_cast<int>(InputEventType::MousePress));
    CHECK_EQ(p.cell.col, 4);
    CHECK_EQ(p.cell.row, 2);
    InputEvent d = makeMouseDragEvent(CellPos{6, 3});
    CHECK_EQ(static_cast<int>(d.type), static_cast<int>(InputEventType::MouseDrag));
    InputEvent r = makeMouseReleaseEvent(CellPos{6, 3});
    CHECK_EQ(static_cast<int>(r.type), static_cast<int>(InputEventType::MouseRelease));
}

// 4: Terminal es un IEventSource.
TEST(frontier_terminal_is_event_source) {
    Terminal t;
    IEventSource* src = &t;
    CHECK(src != nullptr);
}

// 7: TtyTheme -> Style en un solo lugar.
TEST(frontier_theme_style_mapping) {
    TtyTheme dark = darkTheme();
    CHECK(!themeAnsiFor(dark, StyleRole::Selection).empty());
    CHECK(themeAnsiFor(dark, StyleRole::Default).empty());
    CHECK(themeAnsiFor(dark, StyleRole::GutterBlank).empty());
    CHECK(themeAnsiFor(dark, StyleRole::Selection) == dark.selection);
    CHECK(themeAnsiFor(dark, StyleRole::AccentNavegacion) == dark.accentNavegacion);
    CHECK(themeAnsiFor(dark, StyleRole::SyntaxKeyword) == dark.syntaxKeyword);
    TtyTheme light = lightTheme();
    CHECK(themeAnsiFor(light, StyleRole::StatusBase) == light.statusBar);
}

// 8: ScreenToCursor acepta CellPos 0-based.
TEST(frontier_screen_to_cursor_cellpos) {
    Document doc;
    doc.restore({"hello", "world"});
    Layout layout = computeLayout(24, 80);
    Viewport vp;
    vp.top = 0;
    vp.left = 0;
    vp.width = 80;
    vp.height = 22;
    // CellPos 0-based: (col=3, row=0) = primera celda de texto
    // (gutter=3, relCol=3 -> columna visual 0 -> byte 0 de la linea 0).
    auto p = screenToCursor(CellPos{3, 0}, layout, vp, doc);
    CHECK(p.has_value());
    if (p) {
        CHECK_EQ(p->line, 0);
        CHECK_EQ(p->col, 0);
    }
    // Fuera del contenido: nullopt.
    CHECK(!screenToCursor(CellPos{1000, 1000}, layout, vp, doc).has_value());
}

// 9: factories devuelven interfaces sin conocer concretos.
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

// 10: TtyKeymap implementa ITtyKeymap (TTY-only).
TEST(frontier_ikeymap_interface) {
    TtyKeymap km;
    ITtyKeymap& iface = km;
    iface.bindControl(18, InputEventType::PageDown);
    auto t = iface.control(18);
    CHECK(t.has_value());
    if (t) CHECK_EQ(static_cast<int>(*t), static_cast<int>(InputEventType::PageDown));
    iface.bindSequence("[9~", InputEventType::PageUp);
    auto s = iface.sequence("[9~");
    CHECK(s.has_value());
    Terminal term;
    ITtyKeymap& ti = term.keymapIface();
    CHECK(ti.control(17).has_value());
}

// 11: decoder SGR -> CellPos + InputEvent.
TEST(frontier_mouse_decoder_cellpos) {
    {
        InputEvent e;
        CellPos p;
        decodeMouseSgr("[<0;10;5M", e, p);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MousePress));
        CHECK((p == CellPos{9, 4}));
        CHECK_EQ(e.cell.col, 9);
        CHECK_EQ(e.cell.row, 4);
    }
    {
        InputEvent e;
        CellPos p;
        decodeMouseSgr("[<32;11;6M", e, p);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MouseDrag));
        CHECK((p == CellPos{10, 5}));
    }
    {
        InputEvent e;
        CellPos p;
        decodeMouseSgr("[<3;11;6m", e, p);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MouseRelease));
        CHECK((p == CellPos{10, 5}));
    }
    {
        // Rueda con Shift (68 = 64|4) sigue siendo ScrollUp.
        InputEvent e;
        CellPos p;
        decodeMouseSgr("[<68;10;5M", e, p);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::ScrollUp));
    }
    {
        InputEvent e;
        CellPos p;
        decodeMouseSgr("[<64M", e, p);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::None));
    }
    {
        // Terminal delega en el mismo decoder (sin duplicar tabla Cb).
        InputEvent e;
        Terminal::parseMouseSgr("[<0;10;5M", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::MousePress));
        CHECK_EQ(e.cell.col, 9);
        CHECK_EQ(e.cell.row, 4);
    }
}

// 12: Editor común — Resize es un evento AUTÓNOMO: el payload manda,
// sin provider ni backend. Una GUI puede inyectarlo directamente.
TEST(frontier_editor_handles_resize_event) {
    Editor ed;
    InputEvent r;
    r.type = InputEventType::Resize;
    r.resizeRows = 30;
    r.resizeCols = 100;
    ed.handleEvent(r);
    CHECK(ed.getStateForTesting() == State::Navegacion);
    // El payload se aplicó: viewport ajustado a 30x100 (contenido 28).
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    // Payload inválido se ignora (conserva el tamaño anterior).
    InputEvent bad;
    bad.type = InputEventType::Resize;
    bad.resizeRows = 0;
    bad.resizeCols = -5;
    ed.handleEvent(bad);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
}

// Invariant: resize() público — el composition root es dueño del tamaño.
// resize() válido ajusta viewports; inválido (<=0) se ignora.
TEST(fase_c_initial_viewport_matches_fallback) {
    // Sin resize() previo: el constructor ya deja geometría coherente
    // (fallback 24x80 -> contenido de computeLayout, no el default crudo
    // del struct Viewport). Es la garantía en código del tamaño inicial.
    Editor ed;
    const Layout exp = computeLayout(24, 80);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, exp.content.width);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, exp.content.height);
}

// Sin resize() previo el primer render no se rompe: frame completo,
// cursor en origen y loop sigue vivo.
TEST(fase_c_render_without_resize_does_not_break) {
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink cap;
    ed.setSink(cap);
    ed.renderFrame();
    const Layout exp = computeLayout(24, 80);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, exp.content.width);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, exp.content.height);
    CHECK(!cap.data.empty());
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 0);
    CHECK(ed.getStateForTesting() == State::Navegacion);
    CHECK(ed.isRunning());
}

// Con resize() el viewport coincide con la geometría real (no con el
// fallback): el dueño aplica el tamaño y el editor lo refleja + renderiza.
TEST(fase_c_resize_viewport_matches_real_size) {
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink cap;
    ed.setSink(cap);
    ed.resize(30, 100);
    const Layout exp = computeLayout(30, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, exp.content.width);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, exp.content.height);
    ed.renderFrame();
    CHECK(!cap.data.empty());
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 0);
    CHECK(ed.isRunning());
}

TEST(fase_c_resize_public_applies_viewport) {
    Editor ed;
    ed.resize(30, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    CHECK(ed.getStateForTesting() == State::Navegacion);
}

TEST(fase_c_resize_invalid_ignored) {
    Editor ed;
    ed.resize(30, 100);
    ed.resize(0, -5);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    ed.resize(-1, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
}

// Invariant: handleEvent(Resize) delega en resize() — mismo resultado.
TEST(fase_c_resize_event_delegates) {
    Editor viaEvent;
    InputEvent r;
    r.type = InputEventType::Resize;
    r.resizeRows = 30;
    r.resizeCols = 100;
    viaEvent.handleEvent(r);
    Editor viaDirect;
    viaDirect.resize(30, 100);
    CHECK_EQ(viaEvent.getActiveBufferForTesting().viewport.width,
             viaDirect.getActiveBufferForTesting().viewport.width);
    CHECK_EQ(viaEvent.getActiveBufferForTesting().viewport.height,
             viaDirect.getActiveBufferForTesting().viewport.height);
}

// Riesgo residual 1: secuencia de arranque del loop
// (getWindowSize -> resize -> primer renderFrame). Replica
// TtyRunLoop::run() sin TTY: el frame debe ser completo (rebuild con
// clear-screen), con la geometría aplicada y el cursor en origen.
TEST(fase_c_first_render_after_resize) {
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink cap;
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

// Gap del primer render: con cursor en (0,0) el scroll es no-op y un
// render sin scrollToCursor pasaría igual. Con el cursor fuera de pantalla
// el primer renderFrame debe traerlo al viewport (misma cuenta que el
// código viejo: scrollToCursor con ancho de gutter).
TEST(fase_c_first_render_scrolls_cursor_into_view) {
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink cap;
    ed.setSink(cap);
    ed.resize(30, 100); // contenido: 28 filas
    std::vector<std::string> lines;
    for (int i = 0; i < 60; ++i) lines.push_back("l" + std::to_string(i));
    ed.getActiveBufferForTesting().document.restore(lines);
    ed.getActiveBufferForTesting().cursor.line = 59;
    ed.getActiveBufferForTesting().cursor.col = 0;
    ed.getActiveBufferForTesting().viewport.top = 0;
    ed.renderFrame();
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, 60 - 28);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 59);
    CHECK(!cap.data.empty());
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    CHECK(ed.isRunning());
}

// Camino de brackets en el primer render: con archivo .cpp y cursor junto
// a un bracket, renderFrame debe ejecutar el highlight sin romper el frame
// (el par exacto no es observable por fachada pública; esto cubre que el
// camino nuevo — inexistente en el código viejo — no regresa el render).
TEST(fase_c_first_render_with_bracket_highlight) {
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink cap;
    ed.setSink(cap);
    ed.resize(30, 100);
    ed.getActiveBufferForTesting().document.restore({"int main() {", "  return 0;", "}"});
    ed.getActiveBufferForTesting().filename = "test.cpp";
    ed.getActiveBufferForTesting().cursor.line = 0;
    ed.getActiveBufferForTesting().cursor.col = 8; // '(' de "main()"
    ed.renderFrame();
    CHECK(!cap.data.empty());
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 8);
    CHECK(ed.isRunning());
}

// La garantía que reemplazó a invalidateCache: TtyDiff detecta el cambio
// de geometría solo (rebuild con clear-screen); un resize al mismo tamaño
// conserva el fast path (sin "\x1b[2J").
TEST(fase_c_resize_rebuild_only_on_geometry_change) {
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink cap;
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

// Riesgo residual 2: tick() agrupado (clearExpired + autoscroll) en el
// camino de timeout. Sin gesto de mouse armado debe ser no-op observable:
// no mueve cursor ni viewport, no crea selección, no detiene el loop.
// El camino armado sigue cubierto por mouse_tick_* (vía tickMouseAutoscroll).
TEST(fase_c_tick_without_gesture_is_noop) {
    Editor ed;
    const int lineBefore = ed.getActiveBufferForTesting().cursor.line;
    const int colBefore = ed.getActiveBufferForTesting().cursor.col;
    const int topBefore = ed.getActiveBufferForTesting().viewport.top;
    const auto t = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    ed.tick(t);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, lineBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, colBefore);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.top, topBefore);
    CHECK(!ed.hasSelection());
    CHECK(ed.isRunning());
    // El bombeo externo sin fuentes listas también es no-op.
    ed.pollExternalEvents();
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, lineBefore);
    CHECK(ed.isRunning());
}

// El camino de eventos solo expira mensajes: con autoscroll armado,
// clearExpiredMessages() no consulta el oráculo ni mueve nada (eso
// pertenece al timeout, vía tick()). Pinea la corrección del loop:
// eventos y wakeups externos ya no dan pasos extra ni dejan la pantalla
// desactualizada.
TEST(fase_c_event_path_does_not_autoscroll) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i) lines.push_back("l" + std::to_string(i));
    ed.getActiveBufferForTesting().document.restore(lines);
    ed.getActiveBufferForTesting().viewport.height = 4;
    ed.getActiveBufferForTesting().viewport.width = 20;
    ed.getActiveBufferForTesting().viewport.top = 10;
    ed.getActiveBufferForTesting().viewport.left = 0;
    ed.getActiveBufferForTesting().cursor.line = 11;
    ed.getActiveBufferForTesting().cursor.col = 0;

    InputEvent press;
    press.type = InputEventType::MousePress;
    press.cell = CellPos{3, 1}; // línea 11, arma gesto
    ed.handleEvent(press);
    InputEvent drag;
    drag.type = InputEventType::MouseDrag;
    drag.cell = CellPos{3, 4}; // statusbar: fuera por abajo
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
TEST(fase_c_request_quit) {
    Editor clean;
    CHECK(clean.requestQuit(false));
    CHECK(!clean.isRunning());

    Editor ed;
    InputEvent i;
    i.type = InputEventType::InsertChar;
    i.text = "i";
    ed.handleEvent(i); // Navegacion -> Interaccion
    InputEvent x;
    x.type = InputEventType::InsertChar;
    x.text = "X";
    ed.handleEvent(x); // modifica el buffer
    CHECK(ed.getActiveBufferForTesting().modified);
    CHECK(!ed.requestQuit(false)); // bloqueado: aviso, sigue corriendo
    CHECK(ed.isRunning());
    CHECK(ed.requestQuit(true)); // forzado: sale
    CHECK(!ed.isRunning());
}

// 15: TtyScroll decide región vs rebuild.
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

// Inyección tardía del renderer (camino de main.cpp: Editor() + setRenderer
// con el backend real). El enganche del SyntaxCache externo ocurre en cada
// renderFrame (set/unset alrededor del render), no en el ctor: el primer
// frame tras setRenderer debe pintar highlight idéntico al de un Editor que
// tuvo el backend desde el inicio. Sin esto, producción (main) podría
// pintar sin highlight y ningún test lo cazaría.
TEST(fase_c_late_setRenderer_matches_early_renderer_highlight) {
    auto makeDoc = [](Editor& ed) {
        ed.getActiveBufferForTesting().document.restore(
            {"int main() {", "  int x = 1;", "  return x;", "}"});
        ed.getActiveBufferForTesting().filename = "t.cpp";
        ed.resize(24, 80);
    };

    Editor early;
    early.setRenderer(makeTtyTestRenderer());
    makeDoc(early);
    CaptureSink ref;
    early.setSink(ref);
    early.renderFrame();
    CHECK(!ref.data.empty());

    Editor late;
    makeDoc(late);
    CaptureSink nullCap;
    late.setSink(nullCap);
    late.renderFrame();  // con Null: corre scroll/brackets pero no pinta
    CHECK(nullCap.data.empty());
    late.setRenderer(makeTtyTestRenderer());  // tardío, como main
    CaptureSink got;
    late.setSink(got);
    late.renderFrame();
    CHECK_EQ(got.data, ref.data);
}
