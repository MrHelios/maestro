// Tests del chrome GUI (StatusBar + MessageBar + cursor en prompts).
//
// La GUI pinta en SDL (sin bytes afirmables en tests sin ventana): estos
// tests verifican la capa PURA que la GUI comparte con TTY:
//   - guichrome::statusLine/messageLine (ancho + contenido, sin ANSI)
//   - GuiRenderer::buildFrame (mismo Frame que TTY: cursor en MessageBar
//     para los 4 prompts con input, dentro del input via msg.cursor)
//   - render* sin SDL son no-op seguros y dejan lastChrome/lastCursor
//     coherentes (SaveAs en MessageBar, abrir en la lista).

#include <string>
#include <vector>

#include "test_framework.h"

#include "app/ChromePresentation.h"
#include "app/Message.h"
#include "base/utf8.h"
#include "helpers/test_render_utils.h"
#include "layout/Layout.h"
#include "rendering/RenderUtil.h"
#include "rendering/Sink.h"
#include "rendering/gui/GuiChrome.h"
#include "rendering/gui/GuiRenderer.h"
#include "rendering/tty/TtyChrome.h"

namespace {

int colWidth(const std::string& s) {
    return utf8::columnOf(s, static_cast<int>(s.size()));
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

class NullTestSink : public Sink {
public:
    bool writeStdout(const std::string&) override { return true; }
};

Viewport makeVp(int h = 5, int w = 40) {
    Viewport vp;
    vp.top = 0;
    vp.left = 0;
    vp.height = h;
    vp.width = w;
    return vp;
}

}  // namespace

TEST(gui_statusline_ancho_exacto_y_contenido) {
    StatusBarData st;
    st.name = "archivo.txt";
    st.path = "/home/usuario";
    st.estado = "NAVEGACION";
    st.totalLines = 1;
    for (int w : {10, 20, 40, 80}) {
        const std::string line = guichrome::statusLine(st, w);
        CHECK_EQ(colWidth(line), w);
    }
    const std::string line = guichrome::statusLine(st, 80);
    CHECK(contains(line, "archivo.txt"));
    CHECK(contains(line, "NAVEGACION"));
    CHECK(contains(line, "0% (1,1)"));
}

TEST(gui_statusline_nunca_excede_con_todo_largo) {
    StatusBarData st;
    st.name = std::string(80, 'n');
    st.path = "/" + std::string(80, 'p');
    st.estado = std::string(40, 'e');
    st.right = std::string(40, 'r');
    st.totalLines = 1000;
    for (int w = 1; w <= 100; ++w) {
        const std::string line = guichrome::statusLine(st, w);
        CHECK(colWidth(line) <= w);
        CHECK_EQ(colWidth(line), w);
    }
}

TEST(gui_messageline_trunca_a_width) {
    MessageBarData msg{std::string("Nombre del Archivo: nuevo.txt (Control+S para Guardar)"),
                       MessageKind::Prompt};
    for (int w : {1, 10, 40, 80}) {
        const std::string line = guichrome::messageLine(msg, w);
        CHECK(colWidth(line) <= w);
    }
    CHECK(contains(guichrome::messageLine(msg, 80), "nuevo.txt"));
    // Angosto: se trunca por la derecha, nunca excede.
    CHECK(!contains(guichrome::messageLine(msg, 20), "Guardar)"));
}

// Contrato de ancho asimétrico (deliberado, ver CONTRATO DE ANCHO en
// GuiChrome.h): statusLine EXACTO width incluso con contenido corto
// (rellena con fill); messageLine A LO SUMO width (texto corto => línea
// corta, sin fill: el fondo ya lo pintó el renderer).
TEST(gui_chrome_contrato_ancho_asimetrico) {
    StatusBarData st;
    st.name = "a";
    st.estado = "E";
    st.totalLines = 1;
    for (int w = 1; w <= 80; ++w) {
        CHECK_EQ(colWidth(guichrome::statusLine(st, w)), w);
    }
    // Texto corto: messageLine NO rellena (solo agrandaría la textura).
    MessageBarData short_{std::string("hi"), MessageKind::Info};
    CHECK_EQ(guichrome::messageLine(short_, 80), " hi   ");
    CHECK(colWidth(guichrome::messageLine(short_, 80)) < 80);
    // Vacío: solo pads.
    MessageBarData empty{std::string(""), MessageKind::Info};
    CHECK_EQ(guichrome::messageLine(empty, 80), "    ");
    // Nunca excede, en ningún ancho.
    for (int w = 1; w <= 80; ++w) {
        CHECK(colWidth(guichrome::messageLine(short_, w)) <= w);
    }
}

TEST(gui_colores_distinguen_temas_y_roles) {
    CHECK(guichrome::background(true) != guichrome::background(false));
    CHECK(guichrome::statusBackground(true) != guichrome::statusBackground(false));
    CHECK(guichrome::colorFor(StyleRole::StatusBase, true) !=
          guichrome::colorFor(StyleRole::AccentNavegacion, true));
    CHECK(guichrome::messageColor(MessageKind::Prompt, true) ==
          guichrome::colorFor(StyleRole::MsgPrompt, true));
}

TEST(gui_frame_busqueda_cursor_en_messagebar_dentro_del_input) {
    GuiRenderer gui;
    Document doc;
    doc.restore({"hola mundo hola"});
    Viewport vp = makeVp();
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    // "Find: ho (1/2)" con cursor tras "ho" (offset 7), no al final (12).
    Message prompt{std::string("Find: ho (1/2)"), MessageKind::Prompt, std::nullopt,
                   7, 6};
    ChromeRequest cr = makeChromeRequest(prompt, State::Busqueda);
    Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
    CHECK(f.cursor.visible);
    CHECK(f.cursor.shape == FrameCursorShape::Bar);
    Layout layout = f.layout;
    // Fila del MessageBar (segunda del chrome).
    CHECK_EQ(f.cursor.cell.row, layout.chrome.row + 1);
    const CellPos expect = chrome::messageBarCursorCell(layout.chrome, cr.message);
    CHECK(expect.valid());
    CHECK(f.cursor.cell == expect);
    // No apunta al final del texto visible.
    const int padL = 1;
    const int endCol = layout.chrome.col + padL +
                       colWidth(utf8::truncate(cr.message.text, layout.chrome.width - 4));
    CHECK(f.cursor.cell.col != endCol);
}

TEST(gui_frame_irafila_y_renombrar_cursor_en_messagebar) {
    GuiRenderer gui;
    Document doc;
    doc.restore({"a", "b", "c"});
    Viewport vp = makeVp();
    Cursor cur;
    {
        Message prompt{std::string("ir a fila: 12"), MessageKind::Prompt, std::nullopt,
                       13, 11};
        ChromeRequest cr = makeChromeRequest(prompt, State::IrAFila);
        Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
        CHECK(f.cursor.visible);
        CHECK(f.cursor.shape == FrameCursorShape::Bar);
        CHECK_EQ(f.cursor.cell.row, f.layout.chrome.row + 1);
        CHECK(f.cursor.cell == chrome::messageBarCursorCell(f.layout.chrome, cr.message));
    }
    {
        Message prompt{std::string("Nombre del archivo: a.txt"), MessageKind::Prompt,
                       std::nullopt, 21, 20};
        ChromeRequest cr = makeChromeRequest(prompt, State::Renombrar);
        Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
        CHECK(f.cursor.visible);
        CHECK(f.cursor.shape == FrameCursorShape::Bar);
        CHECK_EQ(f.cursor.cell.row, f.layout.chrome.row + 1);
        CHECK(f.cursor.cell == chrome::messageBarCursorCell(f.layout.chrome, cr.message));
    }
}

TEST(gui_frame_navegacion_cursor_en_contenido_no_en_messagebar) {
    GuiRenderer gui;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp();
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    ChromeRequest cr = makeChromeRequest(msg, State::Navegacion);
    Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
    CHECK(f.cursor.visible);
    CHECK(f.cursor.shape == FrameCursorShape::Block);
    // En contenido: fila dentro del area de contenido, no del chrome.
    CHECK(f.cursor.cell.row >= f.layout.content.row);
    CHECK(f.cursor.cell.row < f.layout.content.row + f.layout.content.height);
}

TEST(gui_render_sin_sdl_no_crashea_y_guarda_chrome) {
    GuiRenderer gui;  // sin setSdlRenderer: no-op seguro.
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp();
    Cursor cur;
    Message prompt{std::string("Find: ho"), MessageKind::Prompt, std::nullopt, 8, 6};
    ChromeRequest cr = makeChromeRequest(prompt, State::Busqueda);
    gui.renderScreenDiff(doc, cur, vp, "t", false, cr, sink, std::nullopt,
                         std::nullopt, std::nullopt);
    CHECK_EQ(gui.lastChrome().message.text, "Find: ho");
    CHECK(gui.lastCursor().visible);
    CHECK_EQ(gui.lastCursor().cell.row, gui.lastLayout().chrome.row + 1);

    std::vector<std::string> names = {"a.txt", "b.txt"};
    gui.renderBufferList(names, 1, 40, 5, sink);
    CHECK_EQ(gui.lastChrome().statusBar.estado, "SELECCIONAR");
    CHECK(gui.lastCursor().visible);

    std::vector<FileListItem> items = {{"a.txt", false}, {"dir", true}};
    MessageBarData mbar{std::string("ayuda"), MessageKind::Info};
    gui.renderFileList(items, 0, 0, "/tmp", mbar, 40, 5, sink);
    CHECK_EQ(gui.lastChrome().statusBar.estado, "ABRIR ARCHIVO");
    // Abrir: cursor en la lista (contenido), no en el MessageBar.
    CHECK_EQ(gui.lastCursor().cell.row, gui.lastLayout().content.row);
}

TEST(gui_saveas_cursor_en_messagebar_no_en_lista) {
    GuiRenderer gui;
    NullTestSink sink;
    std::vector<FileListItem> items = {{"a.txt", false}, {"b.txt", false}};
    MessageBarData msg{std::string("Nombre del Archivo: nuevo.txt (Control+S para Guardar)"),
                       MessageKind::Prompt};
    msg.cursor = 20 + 9;  // final del nombre, antes del sufijo.
    for (const auto& c : {0, 1}) {
        gui.renderSaveAsFileList(items, c, 0, "/tmp", msg, 40, 10, sink);
        CHECK_EQ(gui.lastChrome().statusBar.estado, "GUARDAR COMO");
        CHECK(gui.lastCursor().visible);
        CHECK(gui.lastCursor().shape == FrameCursorShape::Bar);
        const CellPos expect =
            chrome::messageBarCursorCell(gui.lastLayout().chrome, msg);
        CHECK(gui.lastCursor().cell == expect);
        // Nunca en la fila de la lista.
        CHECK(gui.lastCursor().cell.row != gui.lastLayout().content.row + c ||
              gui.lastCursor().cell.col != gui.lastLayout().content.col);
    }
}

TEST(gui_filelist_args_invalidos_ocultan_cursor_y_no_dan_right_negativo) {
    GuiRenderer gui;
    NullTestSink sink;
    // 20 items: height=5 -> viewport de 5 filas, scroll=10 muestra [10..14].
    std::vector<FileListItem> items;
    for (int i = 0; i < 20; ++i) items.push_back({{"f" + std::to_string(i)}, false});
    MessageBarData mbar{std::string("ayuda"), MessageKind::Info};

    // selected < scroll: por encima del contenido.
    gui.renderFileList(items, 5, 10, "/tmp", mbar, 40, 5, sink);
    CHECK(!gui.lastCursor().visible);
    CHECK(!gui.lastCursor().cell.valid());
    CHECK(gui.lastChrome().statusBar.right.find('-') == std::string::npos);

    // selected-scroll >= viewport: fuera por abajo.
    gui.renderFileList(items, 19, 10, "/tmp", mbar, 40, 5, sink);
    CHECK(!gui.lastCursor().visible);
    CHECK(!gui.lastCursor().cell.valid());
    CHECK(gui.lastChrome().statusBar.right.find('-') == std::string::npos);

    // selected fuera del rango de items.
    gui.renderFileList(items, 25, 10, "/tmp", mbar, 40, 5, sink);
    CHECK(!gui.lastCursor().visible);
    CHECK(gui.lastChrome().statusBar.right.find('-') == std::string::npos);

    // En rango válido sigue visible y con right exacto.
    gui.renderFileList(items, 12, 10, "/tmp", mbar, 40, 5, sink);
    CHECK(gui.lastCursor().visible);
    CHECK_EQ(gui.lastCursor().cell.row, gui.lastLayout().content.row + 2);
    CHECK_EQ(gui.lastChrome().statusBar.right, "3/20");
}

TEST(gui_bufferlist_args_invalidos_ocultan_cursor) {
    GuiRenderer gui;
    NullTestSink sink;
    std::vector<std::string> names = {"a.txt", "b.txt"};
    gui.renderBufferList(names, 7, 40, 5, sink);
    CHECK(!gui.lastCursor().visible);
    CHECK(!gui.lastCursor().cell.valid());
    CHECK(gui.lastChrome().statusBar.right.find('-') == std::string::npos);
    gui.renderBufferList(names, 1, 40, 5, sink);
    CHECK(gui.lastCursor().visible);
    CHECK_EQ(gui.lastChrome().statusBar.right, "2/2");
}

// La política de layout vive una sola vez (ChromeLayout.h): la línea plana
// de la GUI debe ser byte a byte igual al StatusBar TTY sin ANSI. Si alguien
// reintroduce lógica propia en algún backend, este test lo caza.
TEST(gui_statusline_paridad_con_tty_sin_ansi) {
    TtyChrome tty;
    std::vector<StatusBarData> cases;
    {
        StatusBarData d;
        d.name = "archivo.txt";
        d.path = "/home/usuario";
        d.estado = "NAVEGACION";
        d.totalLines = 1;
        cases.push_back(d);
    }
    {
        StatusBarData d;
        d.name = std::string(80, 'n');
        d.path = "/" + std::string(80, 'p') + "/cola.txt";
        d.estado = std::string(40, 'e');
        d.right = std::string(40, 'r');
        d.modified = true;
        d.totalLines = 1000;
        d.cursorLine = 256;
        d.cursorCol = 512;
        cases.push_back(d);
    }
    {
        StatusBarData d;
        d.name = "";
        d.path = "";
        d.estado = "SELECCIONAR";
        d.modified = true;
        d.totalLines = 5;
        d.cursorLine = 2;
        cases.push_back(d);
    }
    {
        StatusBarData d;
        d.name = "áéí.txt";
        d.path = "/casa/niño";
        d.estado = "NAVEGACION";
        d.right = "2/5";
        cases.push_back(d);
    }
    for (const auto& st : cases) {
        for (int w = 1; w <= 100; ++w) {
            const std::string plain =
                testutil::stripAnsi(tty.renderStatusBar(w, st));
            CHECK_EQ(plain, guichrome::statusLine(st, w));
        }
    }
}

// MessageBar angosto + input largo: el cursor va más allá de lo visible y
// debe quedar clampeado a lo pintado (misma política de truncado que
// messageLine), nunca fuera de la barra.
TEST(gui_messagebar_truncado_cursor_clampeado_a_visible) {
    GuiRenderer gui;
    Document doc;
    doc.restore({"hola"});
    // width=20 -> avail = 20-1-3 = 16; el input ("Nombre del Archivo: " +
    // "nuevo.txt" = 29 cols) lo excede y el sufijo decorativo también.
    Viewport vp = makeVp(5, 20);
    Cursor cur;
    const std::string text =
        "Nombre del Archivo: nuevo.txt (Control+S para Guardar)";
    Message prompt{text, MessageKind::Prompt, std::nullopt, 20 + 9, 20};
    ChromeRequest cr = makeChromeRequest(prompt, State::Renombrar);
    Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
    CHECK(f.cursor.visible);
    CHECK_EQ(f.cursor.cell.row, f.layout.chrome.row + 1);
    CHECK_EQ(f.layout.chrome.width, 20);

    // Misma celda que la política compartida (no un cálculo propio).
    CHECK(f.cursor.cell ==
          chrome::messageBarCursorCell(f.layout.chrome, cr.message));
    // Clampeado al final de lo visible (padL + avail), no al final del
    // texto completo (padL + 29) ni fuera de la barra.
    CHECK_EQ(f.cursor.cell.col, f.layout.chrome.col + 1 + 16);
    CHECK(f.cursor.cell.col <= f.layout.chrome.col + 20 - 1);

    // messageLine trunca con la misma política: el cursor cae justo al
    // final del texto visible, nunca más allá.
    const std::string line = guichrome::messageLine(cr.message, 20);
    CHECK_EQ(line, " " + utf8::truncate(text, 16) + "   ");
    CHECK_EQ(f.cursor.cell.col - f.layout.chrome.col - 1,
             colWidth(utf8::truncate(text, 16)));
}

// Mismo caso pero UTF-8: el offset del cursor viene en bytes y debe
// traducirse a columnas (un cálculo por bytes daría otra celda).
TEST(gui_messagebar_truncado_utf8_cursor_en_columnas_no_bytes) {
    GuiRenderer gui;
    Document doc;
    doc.restore({"hola"});
    // "Nombre: "(8) + á(2B/1C) + é(2B/1C) + 漢(3B/2C) + 字(3B/2C) +
    // ".txt"(4B/4C) = 22 bytes, 18 columnas. width=16 -> avail=12, visible
    // "Nombre: áé漢" (15 bytes, 12 cols): 漢 entra justo, 字 ya no.
    const std::string text = "Nombre: áé漢字.txt";
    CHECK_EQ(static_cast<int>(text.size()), 22);
    Viewport vp = makeVp(5, 16);
    Cursor cur;
    Message prompt{text, MessageKind::Prompt, std::nullopt,
                   static_cast<int>(text.size()), 8};
    ChromeRequest cr = makeChromeRequest(prompt, State::Renombrar);
    Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
    CHECK(f.cursor.visible);
    CHECK_EQ(f.cursor.cell.row, f.layout.chrome.row + 1);
    CHECK(f.cursor.cell ==
          chrome::messageBarCursorCell(f.layout.chrome, cr.message));
    // 18 cols -> clampeado a avail=12: col = chrome.col+1+12. Por bytes
    // (22) daría chrome.col+1+22, clampeado a la última celda (col+15).
    CHECK_EQ(f.cursor.cell.col, f.layout.chrome.col + 13);

    const std::string line = guichrome::messageLine(cr.message, 16);
    CHECK(testutil::validUtf8(line));  // sin partir carácter multibyte
    CHECK(contains(line, "漢"));
    CHECK(!contains(line, "字"));
}
