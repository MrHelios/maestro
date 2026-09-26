// Frontier tests (pasos 1-15): verifican el nuevo orden sin tocar la
// suite existente. Se corren con: make test-one FILTER='frontier*'
#include "test_framework.h"

#include "platform/CellPos.h"
#include "platform/Event.h"
#include "platform/IEventSource.h"
#include "platform/MouseEvent.h"
#include "platform/ResizeEvent.h"
#include "platform/tty/ITtyKeymap.h"
#include "platform/tty/Keymap.h"
#include "platform/tty/TtyKeymap.h"
#include "platform/tty/Terminal.h"
#include "platform/tty/TtyMouse.h"
#include "platform/clipboard/ClipboardFactory.h"
#include "filesystem/FileWatcherFactory.h"
#include "layout/ScreenToCursor.h"
#include "rendering/Theme.h"
#include "rendering/tty/TtyScroll.h"
#include "app/Editor.h"

// 1: CellPos es el tipo común 1-based.
TEST(frontier_cellpos_valid) {
    CHECK((CellPos{10, 5}.valid()));
    CHECK((!CellPos{0, 5}.valid()));
    CHECK((!CellPos{10, 0}.valid()));
    CHECK((CellPos{3, 4} == CellPos{3, 4}));
    CHECK((CellPos{3, 4} != CellPos{3, 5}));
}

// 1+3: Event expone CellPos y payload Resize.
TEST(frontier_event_cellpos_roundtrip) {
    Event e;
    e.type = EventType::MousePress;
    e.setCellPos(CellPos{12, 7});
    CHECK_EQ(e.mouseCol, 12);
    CHECK_EQ(e.mouseRow, 7);
    CHECK((e.cellPos() == CellPos{12, 7}));

    Event r;
    r.type = EventType::Resize;
    r.resizeRows = 40;
    r.resizeCols = 120;
    CHECK_EQ(r.resizeRows, 40);
    CHECK_EQ(r.resizeCols, 120);
}

// 2: fábricas MouseEvent -> Event.
TEST(frontier_mouse_event_factories) {
    Event p = makeMousePressEvent(CellPos{4, 2});
    CHECK_EQ(static_cast<int>(p.type), static_cast<int>(EventType::MousePress));
    CHECK_EQ(p.mouseCol, 4);
    CHECK_EQ(p.mouseRow, 2);
    Event d = makeMouseDragEvent(CellPos{6, 3});
    CHECK_EQ(static_cast<int>(d.type), static_cast<int>(EventType::MouseDrag));
    Event r = makeMouseReleaseEvent(CellPos{6, 3});
    CHECK_EQ(static_cast<int>(r.type), static_cast<int>(EventType::MouseRelease));
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

// 8: ScreenToCursor acepta CellPos y equivale a (row,col).
TEST(frontier_screen_to_cursor_cellpos) {
    Document doc;
    doc.restore({"hello", "world"});
    Layout layout = computeLayout(24, 80);
    Viewport vp;
    vp.top = 0;
    vp.left = 0;
    vp.width = 80;
    vp.height = 22;
    auto a = screenToCursor(1, 4, layout, vp, doc);
    auto b = screenToCursor(CellPos{4, 1}, layout, vp, doc);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (a && b) {
        CHECK_EQ(a->line, b->line);
        CHECK_EQ(a->col, b->col);
    }
    // Fuera del contenido: nullopt en ambas formas.
    CHECK(!screenToCursor(1000, 1000, layout, vp, doc).has_value());
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

// 10: Keymap implementa ITtyKeymap (alias TtyKeymap, TTY-only).
TEST(frontier_ikeymap_interface) {
    TtyKeymap km;
    ITtyKeymap& iface = km;
    iface.bindControl(18, EventType::PageDown);
    auto t = iface.control(18);
    CHECK(t.has_value());
    if (t) CHECK_EQ(static_cast<int>(*t), static_cast<int>(EventType::PageDown));
    iface.bindSequence("[9~", EventType::PageUp);
    auto s = iface.sequence("[9~");
    CHECK(s.has_value());
    Terminal term;
    ITtyKeymap& ti = term.keymapIface();
    CHECK(ti.control(17).has_value());
}

// 11: decoder SGR -> CellPos + Event.
TEST(frontier_mouse_decoder_cellpos) {
    {
        Event e;
        CellPos p;
        decodeMouseSgr("[<0;10;5M", e, p);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MousePress));
        CHECK((p == CellPos{10, 5}));
        CHECK_EQ(e.mouseCol, 10);
        CHECK_EQ(e.mouseRow, 5);
    }
    {
        Event e;
        CellPos p;
        decodeMouseSgr("[<32;11;6M", e, p);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MouseDrag));
        CHECK((p == CellPos{11, 6}));
    }
    {
        Event e;
        CellPos p;
        decodeMouseSgr("[<3;11;6m", e, p);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MouseRelease));
        CHECK((p == CellPos{11, 6}));
    }
    {
        // Rueda con Shift (68 = 64|4) sigue siendo ScrollUp.
        Event e;
        CellPos p;
        decodeMouseSgr("[<68;10;5M", e, p);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::ScrollUp));
    }
    {
        Event e;
        CellPos p;
        decodeMouseSgr("[<64M", e, p);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::None));
    }
    {
        // Terminal delega en el mismo decoder (sin duplicar tabla Cb).
        Event e;
        Terminal::parseMouseSgr("[<0;10;5M", e);
        CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::MousePress));
        CHECK_EQ(e.mouseCol, 10);
        CHECK_EQ(e.mouseRow, 5);
    }
}

// 12: Editor común — Resize es un evento AUTÓNOMO: el payload manda,
// sin provider ni backend. Una GUI puede inyectarlo directamente.
TEST(frontier_editor_handles_resize_event) {
    Editor ed;
    Event r;
    r.type = EventType::Resize;
    r.resizeRows = 30;
    r.resizeCols = 100;
    ed.processEventForTesting(r);
    CHECK(ed.getStateForTesting() == State::Navegacion);
    // El payload se aplicó: viewport ajustado a 30x100 (contenido 28).
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    // Payload inválido se ignora (conserva el tamaño anterior).
    Event bad;
    bad.type = EventType::Resize;
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
