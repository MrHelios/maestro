// Frontier tests (pasos 1-15): verifican el nuevo orden sin tocar la
// suite existente. Se corren con: make test-one FILTER='frontier*'
#include "test_framework.h"

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
#include "rendering/tty/Theme.h"
#include "rendering/tty/TtyScroll.h"
#include "app/Editor.h"

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

// 7: Theme -> Style en un solo lugar.
TEST(frontier_theme_style_mapping) {
    Theme dark = darkTheme();
    CHECK(!themeAnsiFor(dark, StyleRole::Selection).empty());
    CHECK(themeAnsiFor(dark, StyleRole::Default).empty());
    CHECK(themeAnsiFor(dark, StyleRole::GutterBlank).empty());
    CHECK(themeAnsiFor(dark, StyleRole::Selection) == dark.selection);
    CHECK(themeAnsiFor(dark, StyleRole::AccentNavegacion) == dark.accentNavegacion);
    CHECK(themeAnsiFor(dark, StyleRole::SyntaxKeyword) == dark.syntaxKeyword);
    Theme light = lightTheme();
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
    ed.processEventForTesting(r);
    CHECK(ed.getStateForTesting() == State::Navegacion);
    // El payload se aplicó: viewport ajustado a 30x100 (contenido 28).
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    // Payload inválido se ignora (conserva el tamaño anterior).
    InputEvent bad;
    bad.type = InputEventType::Resize;
    bad.resizeRows = 0;
    bad.resizeCols = -5;
    ed.processEventForTesting(bad);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
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
