// Tests de la GUI (chrome + contenido + modales + pintado SDL real).
//
// Dos capas:
//   - Pura (sin ventana): guichrome::statusLine/messageLine, buildFrame,
//     snapshots lastContentRows_/lastListLines_ (SaveAs en MessageBar,
//     abrir en la lista).
//   - Pintado SDL real (sección final, driver de video "dummy" headless +
//     renderer por software + SDL_RenderReadPixels): verifica que lo que se
//     pinta en píxeles reproduce el snapshot (fondos de selección/listas,
//     cursor opaco, MessageBar, sin cajas negras del blended). Sin SDL2
//     compilado o sin video dummy se skipean, no fallan.

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "test_framework.h"

#include "app/ChromePresentation.h"
#include "app/Editor.h"
#include "app/Message.h"
#include "base/utf8.h"
#include "filesystem/NullFileWatcher.h"
#include "helpers/FakeClipboard.h"
#include "helpers/test_render_utils.h"
#include "layout/Layout.h"
#include "platform/gui/GuiRunLoop.h"
#include "syntax/SyntaxCache.h"
#include "syntax/SyntaxLanguage.h"
#include "rendering/RenderUtil.h"
#include "rendering/Sink.h"
#include "rendering/Style.h"
#include "rendering/gui/GuiChrome.h"
#include "rendering/gui/GuiRenderer.h"
#include "rendering/tty/TtyChrome.h"
#include "rendering/tty/TtyEncoder.h"
#include "rendering/tty/TtyTheme.h"

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
    CHECK_EQ(gui.lastListSelected(), -1);
    CHECK(gui.lastChrome().statusBar.right.find('-') == std::string::npos);

    // selected-scroll >= viewport: fuera por abajo.
    gui.renderFileList(items, 19, 10, "/tmp", mbar, 40, 5, sink);
    CHECK(!gui.lastCursor().visible);
    CHECK(!gui.lastCursor().cell.valid());
    CHECK_EQ(gui.lastListSelected(), -1);
    CHECK(gui.lastChrome().statusBar.right.find('-') == std::string::npos);

    // selected fuera del rango de items.
    gui.renderFileList(items, 25, 10, "/tmp", mbar, 40, 5, sink);
    CHECK(!gui.lastCursor().visible);
    CHECK_EQ(gui.lastListSelected(), -1);
    CHECK(gui.lastChrome().statusBar.right.find('-') == std::string::npos);

    // En rango válido sigue visible y con right exacto.
    gui.renderFileList(items, 12, 10, "/tmp", mbar, 40, 5, sink);
    CHECK(gui.lastCursor().visible);
    CHECK_EQ(gui.lastListSelected(), 12);
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
    CHECK_EQ(gui.lastListSelected(), -1);
    CHECK(gui.lastChrome().statusBar.right.find('-') == std::string::npos);
    gui.renderBufferList(names, 1, 40, 5, sink);
    CHECK(gui.lastCursor().visible);
    CHECK_EQ(gui.lastListSelected(), 1);
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

// Paleta completa: cada familia se distingue del texto base y dark!=light
// donde importa (contenido, selección, listas, sintaxis).
TEST(gui_paleta_contenido_se_distingue) {
    for (bool dark : {true, false}) {
        const GuiColor base = guichrome::colorFor(StyleRole::Default, dark);
        CHECK(guichrome::colorFor(StyleRole::Gutter, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::SyntaxKeyword, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::SyntaxString, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::SyntaxNumber, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::SyntaxComment, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::SyntaxType, dark) != base);
        // Fondos destacados: distintos del fondo de contenido.
        const GuiColor bg = guichrome::background(dark);
        CHECK(guichrome::currentLineBackground(dark) != bg);
        CHECK(guichrome::selectionBackground(dark) != bg);
        CHECK(guichrome::bracketBackground() != bg);
        // styleFor implementa rowBgKindFor: Default siempre sobre el fondo
        // de contenido (el FrameBuilder nunca emite Default en la línea
        // actual: ahí usa el rol CurrentLine), sintaxis en línea actual
        // envuelta, Selection/Bracket/ListSelected con su fondo propio.
        auto sCur =
            guichrome::styleFor(StyleRole::SyntaxKeyword, true, dark);
        CHECK(sCur.bg == guichrome::currentLineBackground(dark));
        auto sPlain =
            guichrome::styleFor(StyleRole::Default, false, dark);
        CHECK(sPlain.bg == bg);
        CHECK(guichrome::styleFor(StyleRole::Default, true, dark).bg == bg);
        auto sSel =
            guichrome::styleFor(StyleRole::Selection, false, dark);
        CHECK(sSel.bg == guichrome::selectionBackground(dark));
        auto sBr =
            guichrome::styleFor(StyleRole::BracketMatch, false, dark);
        CHECK(sBr.bg == guichrome::bracketBackground());
        auto sList =
            guichrome::styleFor(StyleRole::ListSelected, false, dark);
        CHECK(sList.bg == guichrome::currentLineBackground(dark));
    }
    CHECK(guichrome::background(true) != guichrome::background(false));
    CHECK(guichrome::selectionBackground(true) !=
          guichrome::selectionBackground(false));
}

// Contenido: el snapshot sin SDL lleva sintaxis + selección + currentLine.
TEST(gui_contenido_snapshot_con_sintaxis_y_seleccion) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"int main() {", "  return 0;", "}"});
    Viewport vp = makeVp(5, 40);
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    ChromeRequest cr = makeChromeRequest(msg, State::Navegacion);
    // Selección sobre "return" de la fila 1; la fila 0 queda libre para
    // sintaxis C++ (.cpp). Priming como test_frame_pure: con el caché
    // interno frío el isValidThrough heredado salta el primer ensure, así
    // que se calienta un SyntaxCache externo (igual que el Editor le pasa
    // el del buffer activo en renderFrame).
    Selection sel{{1, 2}, {1, 8}};
    SyntaxCache warm;
    warm.setLanguage(SyntaxLanguage::Cpp);
    warm.ensureValid(doc, doc.lineCount());
    gui.setExternalSyntaxCache(&warm);
    gui.renderScreenDiff(doc, cur, vp, "t.cpp", false, cr, sink, sel,
                         std::nullopt, std::nullopt);
    gui.setExternalSyntaxCache(nullptr);
    CHECK(!gui.lastContentRows().empty());
    const auto& r0 = gui.lastContentRows()[0];
    CHECK(r0.isCurrentLine);
    CHECK(!r0.hasRole(StyleRole::Selection));
    // Sintaxis C++ en archivo .cpp: algún token coloreado en el frame.
    bool hasSyntax = r0.hasRole(StyleRole::SyntaxKeyword) ||
                     r0.hasRole(StyleRole::SyntaxType) ||
                     r0.hasRole(StyleRole::SyntaxPreprocessor);
    CHECK(hasSyntax);
    // El texto plano conserva el contenido.
    CHECK(contains(r0.plain(), "int"));
    // La fila seleccionada sí lleva Selection.
    CHECK(gui.lastContentRows()[1].hasRole(StyleRole::Selection));
}

// Highlight de búsqueda: viaja como Selection en el Frame (fusionado por
// FrameBuilder) y el snapshot GUI lo expone igual que la selección.
TEST(gui_contenido_highlight_busqueda_como_selection) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo hola"});
    Viewport vp = makeVp(5, 40);
    Cursor cur;
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    ChromeRequest cr = makeChromeRequest(msg, State::Navegacion);
    Selection hl{{0, 0}, {0, 4}};  // primer "hola"
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, cr, sink, std::nullopt,
                         hl, std::nullopt);
    CHECK(!gui.lastContentRows().empty());
    CHECK(gui.lastContentRows()[0].hasRole(StyleRole::Selection));
    // Sin highlight ni selección no hay rol Selection.
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, cr, sink, std::nullopt,
                         std::nullopt, std::nullopt);
    CHECK(!gui.lastContentRows()[0].hasRole(StyleRole::Selection));
}

// Bracket apareado: el snapshot expone BracketMatch para pintarlo destacado.
TEST(gui_contenido_bracket_match_en_snapshot) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"int f() { return 0; }"});
    Viewport vp = makeVp(5, 40);
    Cursor cur;
    cur.line = 0;
    cur.col = 8;  // sobre '{' aprox (el par lo resuelve el caller real)
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    ChromeRequest cr = makeChromeRequest(msg, State::Navegacion);
    BracketPair pair{{0, 8}, {0, 21}};
    gui.renderScreenDiff(doc, cur, vp, "t.cpp", false, cr, sink, std::nullopt,
                         std::nullopt, pair);
    CHECK(gui.lastContentRows()[0].hasRole(StyleRole::BracketMatch));
}

// Modales: BufferSelector deja líneas "  nombre" + relleno y cursor en lista.
TEST(gui_modal_buffer_selector_lineas_y_cursor) {
    GuiRenderer gui;
    NullTestSink sink;
    std::vector<std::string> names = {"a.txt", "b.txt"};
    gui.renderBufferList(names, 1, 40, 5, sink);
    CHECK_EQ(gui.lastChrome().statusBar.estado, "SELECCIONAR");
    // height=5 son filas de contenido (calculateLayout suma el chrome):
    // 2 items + 3 de relleno.
    CHECK_EQ(static_cast<int>(gui.lastListLines().size()), 5);
    CHECK_EQ(gui.lastListLines()[0].text, "  a.txt");
    CHECK(!gui.lastListLines()[0].filler);
    CHECK_EQ(gui.lastListLines()[1].text, "  b.txt");
    CHECK(!gui.lastListLines()[1].filler);
    CHECK_EQ(gui.lastListLines()[2].text, "  ~");
    CHECK(gui.lastListLines()[2].filler);
    CHECK_EQ(gui.lastListSelected(), 1);
    CHECK(gui.lastCursor().visible);
    CHECK_EQ(gui.lastCursor().cell.row, gui.lastLayout().content.row + 1);
}

// Modales: FileBrowser respeta scroll, marca carpetas con "/" y expone DTO.
TEST(gui_modal_file_browser_scroll_y_carpetas) {
    GuiRenderer gui;
    NullTestSink sink;
    std::vector<FileListItem> items;
    for (int i = 0; i < 6; ++i)
        items.push_back({{"f" + std::to_string(i)}, i == 1});
    MessageBarData mbar{std::string("ayuda"), MessageKind::Info};
    // height=5 son filas de contenido; scroll=2 muestra f2,f3,f4,f5 + relleno.
    gui.renderFileList(items, 3, 2, "/tmp", mbar, 40, 5, sink);
    CHECK_EQ(gui.lastChrome().statusBar.estado, "ABRIR ARCHIVO");
    CHECK_EQ(static_cast<int>(gui.lastListLines().size()), 5);
    CHECK_EQ(gui.lastListLines()[0].text, "  f2");
    CHECK_EQ(gui.lastListLines()[1].text, "  f3");
    CHECK_EQ(gui.lastListLines()[2].text, "  f4");
    CHECK_EQ(gui.lastListLines()[3].text, "  f5");
    CHECK_EQ(gui.lastListLines()[4].text, "  ~");
    CHECK(gui.lastListLines()[4].filler);
    CHECK_EQ(gui.lastListSelected(), 3);
    CHECK_EQ(gui.lastListScroll(), 2);
    CHECK(gui.lastCursor().visible);
    CHECK_EQ(gui.lastCursor().cell.row, gui.lastLayout().content.row + 1);
    // Carpeta con "/".
    gui.renderFileList(items, 1, 0, "/tmp", mbar, 40, 5, sink);
    CHECK_EQ(gui.lastListLines()[1].text, "  f1/");
}

// Modales: SaveAs comparte lista con abrir pero el cursor va al MessageBar.
TEST(gui_modal_saveas_comparte_lista_cursor_en_messagebar) {
    GuiRenderer gui;
    NullTestSink sink;
    std::vector<FileListItem> items = {{{"a.txt"}, false}, {{"d"}, true}};
    MessageBarData msg{
        std::string("Nombre del Archivo: nuevo.txt (Control+S para Guardar)"),
        MessageKind::Prompt};
    msg.cursor = 20 + 9;
    gui.renderSaveAsFileList(items, 0, 0, "/tmp", msg, 40, 10, sink);
    CHECK_EQ(gui.lastChrome().statusBar.estado, "GUARDAR COMO");
    CHECK(!gui.lastListLines().empty());
    CHECK_EQ(gui.lastListLines()[0].text, "  a.txt");
    CHECK_EQ(gui.lastListLines()[1].text, "  d/");
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Bar);
    CHECK(gui.lastCursor().cell ==
          chrome::messageBarCursorCell(gui.lastLayout().chrome, msg));
}

// Un archivo real llamado "~" produce el mismo texto "  ~" que el relleno,
// pero viaja con filler=false: es texto normal, no relleno tenue.
TEST(gui_modal_tilde_real_no_es_relleno) {
    GuiRenderer gui;
    NullTestSink sink;
    MessageBarData mbar{std::string("ayuda"), MessageKind::Info};
    std::vector<FileListItem> items = {{{"~"}, false}, {{"a.txt"}, false}};
    gui.renderFileList(items, 0, 0, "/tmp", mbar, 40, 10, sink);
    CHECK_EQ(static_cast<int>(gui.lastListLines().size()), 10);
    CHECK_EQ(gui.lastListLines()[0].text, "  ~");
    CHECK(!gui.lastListLines()[0].filler);  // archivo real, no relleno
    CHECK_EQ(gui.lastListLines()[1].text, "  a.txt");
    CHECK(!gui.lastListLines()[1].filler);
    CHECK_EQ(gui.lastListLines()[2].text, "  ~");
    CHECK(gui.lastListLines()[2].filler);  // relleno real

    std::vector<std::string> names = {"~"};
    gui.renderBufferList(names, 0, 40, 10, sink);
    CHECK_EQ(gui.lastListLines()[0].text, "  ~");
    CHECK(!gui.lastListLines()[0].filler);
    CHECK(gui.lastListLines()[1].filler);
}

// Mensajes con timeout en GUI: un mensaje de acción (ej. "Nada que
// deshacer.") llega al MessageBar vía lastChrome y expira tras
// kActionMessageTimeout (el loop GUI hace tick+renderFrame en idle, igual
// que el TTY en su rama timeout). Persistentes (ayuda de modo) no expiran.
TEST(gui_mensaje_accion_expira_en_gui) {
    auto clipboard = std::make_unique<FakeClipboard>();
    auto watcher = std::make_unique<NullFileWatcher>();
    Editor ed(std::move(clipboard), std::move(watcher));
    auto gui = std::make_unique<GuiRenderer>();
    GuiRenderer* guiPtr = gui.get();
    ed.setRenderer(std::move(gui));
    NullTestSink sink;
    ed.setSink(sink);

    // Dispara un mensaje de ACCIÓN con timeout (undo sin historial).
    InputEvent undo;
    undo.type = InputEventType::Undo;
    ed.handleEvent(undo);
    ed.renderFrame();
    CHECK(!guiPtr->lastChrome().message.text.empty());
    CHECK(contains(guiPtr->lastChrome().message.text, "Nada que deshacer"));

    // El timeout del Editor lo pide (el loop GUI lo usa para su Delay).
    const auto now = std::chrono::steady_clock::now();
    CHECK(ed.nextTimeoutMs(now) >= 0);

    // Tras el timeout + tick, el MessageBar queda vacío (fondo limpio).
    auto later = now + Editor::kActionMessageTimeout +
                 std::chrono::milliseconds(50);
    ed.tick(later);
    ed.renderFrame();
    CHECK(guiPtr->lastChrome().message.text.empty());
}

// ---------------------------------------------------------------------------
// Pintado SDL real (headless): driver "dummy" + renderer por software +
// SDL_RenderReadPixels. Prueban el código bajo #ifdef HAVE_SDL2 que los
// tests de snapshot no tocan (paintFrameContent/SegmentRow/ListContent/
// ChromeAndCursor). Sin SDL2 o sin video dummy se skipean.
// ---------------------------------------------------------------------------
#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

#include <cstdlib>
#endif

#ifdef HAVE_SDL2
namespace {

struct SdlDummy {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    bool ok = false;
    SdlDummy(int w = 800, int h = 600) {
        ::setenv("SDL_VIDEODRIVER", "dummy", 1);
        ::setenv("SDL_AUDIODRIVER", "dummy", 1);
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return;
        win = SDL_CreateWindow("maestro-test", 0, 0, w, h, SDL_WINDOW_HIDDEN);
        if (!win) return;
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) ren = SDL_CreateRenderer(win, -1, 0);  // cualquiera
        ok = (ren != nullptr);
    }
    ~SdlDummy() {
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
    }
};

bool readPixel(SDL_Renderer* r, int x, int y, GuiColor& out) {
    Uint32 v = 0;
    SDL_Rect rc{x, y, 1, 1};
    if (SDL_RenderReadPixels(r, &rc, SDL_PIXELFORMAT_ARGB8888, &v,
                             sizeof v) != 0)
        return false;
    out = GuiColor{static_cast<uint8_t>((v >> 16) & 0xFF),
                   static_cast<uint8_t>((v >> 8) & 0xFF),
                   static_cast<uint8_t>(v & 0xFF), 0xFF};
    return true;
}

// ¿Hay al menos un píxel de ese color en la franja de celdas dada?
bool stripHas(SDL_Renderer* r, int col0, int col1, int row, int cw, int ch,
              const GuiColor& want) {
    for (int c = col0; c < col1; ++c)
        for (int dx = 0; dx < cw; ++dx)
            for (int dy = 0; dy < ch; ++dy) {
                GuiColor got{};
                if (!readPixel(r, c * cw + dx, row * ch + dy, got)) return false;
                if (got == want) return true;
            }
    return false;
}

bool stripHasPureBlack(SDL_Renderer* r, int col0, int col1, int row, int cw,
                       int ch) {
    return stripHas(r, col0, col1, row, cw, ch, GuiColor{0, 0, 0, 0xFF});
}

}  // namespace
#endif

// Fondo de contenido y de línea actual en píxeles (independiente de fuente:
// los bg se rellenan siempre; el texto solo agrega textura encima).
TEST(gui_sdl_fondo_contenido_y_linea_actual) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola", "mundo"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;  // (0,0): línea actual = fila 0
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    const bool dark = gui.isDarkTheme();
    const int cw = gui.cellW(), ch = gui.cellH();
    const Layout lo = gui.lastLayout();
    // Col 20: más allá del texto ("hola" + gutter) y del cursor -> fondo puro.
    const int x = (lo.content.col + 20) * cw + cw / 2;
    CHECK(readPixel(sdl.ren, x, (lo.content.row + 0) * ch + ch / 2, probe));
    CHECK(probe == guichrome::currentLineBackground(dark));
    // Fila 2: fuera del documento ("~" solo a la izquierda) -> fondo base.
    CHECK(readPixel(sdl.ren, x, (lo.content.row + 2) * ch + ch / 2, probe));
    CHECK(probe == guichrome::background(dark));
#endif
}

// La selección se pinta con su fondo en píxeles, sin píxeles negros puros
// (ningún color legítimo lo es en ningún tema: el más oscuro es el fondo
// 0x12,0x13,0x14; el blended correcto solo mezcla). Es invariante anti
// "caja negra": si un camino de texturas dejara de mezclar alfa, este test
// lo caza (verificado por mutación sobre el fill de fondo, que también
// tumba este test).
TEST(gui_sdl_seleccion_pinta_fondo_sin_cajas_negras) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    Selection sel{{0, 0}, {0, 4}};  // "hola"
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink, sel,
                         std::nullopt, std::nullopt);
    const bool dark = gui.isDarkTheme();
    const int cw = gui.cellW(), ch = gui.cellH();
    const Layout lo = gui.lastLayout();
    const int row = lo.content.row + 0;
    // Toda la franja de contenido: el fondo de selección aparece (pinta el
    // camino de Selection sobre SDL real) y ningún píxel es negro puro
    // (ningún color legítimo lo es en ningún tema: el más oscuro es el
    // fondo 0x12,0x13,0x14; el blended correcto solo mezcla, nunca da 0).
    CHECK(stripHas(sdl.ren, lo.content.col, lo.content.col + lo.content.width,
                   row, cw, ch, guichrome::selectionBackground(dark)));
    CHECK(!stripHasPureBlack(sdl.ren, lo.content.col,
                             lo.content.col + lo.content.width, row, cw, ch));
#endif
}

// El cursor en bloque se pinta opaco DESPUÉS del texto: el centro de su
// celda es exactamente el color del cursor (independiente de fuente).
TEST(gui_sdl_cursor_block_cubre_celda) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
    const int cw = gui.cellW(), ch = gui.cellH();
    const CellPos cell = gui.lastCursor().cell;
    CHECK(readPixel(sdl.ren, cell.col * cw + cw / 2, cell.row * ch + ch / 2,
                    probe));
    CHECK(probe == guichrome::cursorColor(gui.isDarkTheme()));
#endif
}

// BufferSelector sobre SDL real: la fila navegada lleva fondo ListSelected
// y el resto fondo base (muestreo a la derecha del texto -> sin glifos).
TEST(gui_sdl_lista_modal_destaca_seleccionada) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    std::vector<std::string> names = {"a.txt", "b.txt"};
    gui.renderBufferList(names, 1, 40, 10, sink);
    const bool dark = gui.isDarkTheme();
    const int cw = gui.cellW(), ch = gui.cellH();
    const Layout lo = gui.lastLayout();
    const int x = (lo.content.col + 30) * cw + cw / 2;  // más allá del texto
    CHECK(readPixel(sdl.ren, x, (lo.content.row + 1) * ch + ch / 2, probe));
    CHECK(probe == guichrome::currentLineBackground(dark));  // == ListSelected
    CHECK(readPixel(sdl.ren, x, (lo.content.row + 5) * ch + ch / 2, probe));
    CHECK(probe == guichrome::background(dark));  // relleno "~"
#endif
}

// SaveAs sobre SDL real: el cursor Bar de 2px cae en el input del MessageBar.
TEST(gui_sdl_saveas_cursor_bar_en_messagebar) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    std::vector<FileListItem> items = {{{"a.txt"}, false}};
    MessageBarData mbar{
        std::string("Nombre del Archivo: nuevo.txt (Control+S para Guardar)"),
        MessageKind::Prompt};
    mbar.cursor = 20 + 9;
    gui.renderSaveAsFileList(items, 0, 0, "/tmp", mbar, 40, 10, sink);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Bar);
    const int cw = gui.cellW(), ch = gui.cellH();
    const CellPos cell = gui.lastCursor().cell;
    const int px = cell.col * cw + (cw > 2 ? 1 : 0);  // dentro de los 2px
    CHECK(readPixel(sdl.ren, px, cell.row * ch + ch / 2, probe));
    CHECK(probe == guichrome::cursorColor(gui.isDarkTheme()));
#endif
}

// MessageBar sobre SDL real: con mensaje y fuente hay píxeles de texto;
// vacío (tras timeout) la fila queda en fondo limpio en todos los casos.
TEST(gui_sdl_mensaje_llena_y_vacia_messagebar) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    const bool dark = gui.isDarkTheme();
    const GuiColor mbg = guichrome::messageBackground(dark);
    std::vector<FileListItem> items = {{{"a.txt"}, false}};
    MessageBarData with{std::string("ayuda"), MessageKind::Info};
    gui.renderFileList(items, 0, 0, "/tmp", with, 40, 10, sink);
    const Layout lo = gui.lastLayout();
    const int cw = gui.cellW(), ch = gui.cellH();
    const int row = lo.chrome.row + 1;
    if (gui.hasFont()) {
        // Algún píxel distinto del fondo: el texto se pintó de verdad.
        bool anyText = false;
        for (int c = lo.chrome.col; c < lo.chrome.col + lo.chrome.width && !anyText; ++c)
            for (int dx = 0; dx < cw && !anyText; ++dx)
                for (int dy = 0; dy < ch && !anyText; ++dy) {
                    if (!readPixel(sdl.ren, c * cw + dx, row * ch + dy, probe)) break;
                    if (probe != mbg) anyText = true;
                }
        CHECK(anyText);
    }
    MessageBarData empty{std::string(""), MessageKind::Info};
    gui.renderFileList(items, 0, 0, "/tmp", empty, 40, 10, sink);
    const Layout lo2 = gui.lastLayout();
    const int row2 = lo2.chrome.row + 1;
    bool allBg = true;
    for (int c = lo2.chrome.col; c < lo2.chrome.col + lo2.chrome.width && allBg; ++c)
        for (int dx = 0; dx < cw && allBg; ++dx)
            for (int dy = 0; dy < ch && allBg; ++dy) {
                if (!readPixel(sdl.ren, c * cw + dx, row2 * ch + dy, probe)) {
                    allBg = false;
                    break;
                }
                if (probe != mbg) allBg = false;
            }
    CHECK(allBg);
#endif
}

// El "~" real seleccionado se pinta con el fg de texto (ListSelected), no
// con el verde tenue del relleno: con el sentinel por texto este test
// fallaba (el archivo se pintaba como Marker).
TEST(gui_sdl_tilde_real_se_pinta_como_texto) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    if (!gui.hasFont()) SKIP("sin fuente");
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    MessageBarData mbar{std::string("ayuda"), MessageKind::Info};
    std::vector<FileListItem> items = {{{"~"}, false}, {{"a.txt"}, false}};
    gui.renderFileList(items, 0, 0, "/tmp", mbar, 40, 10, sink);
    const bool dark = gui.isDarkTheme();
    const int cw = gui.cellW(), ch = gui.cellH();
    const Layout lo = gui.lastLayout();
    const int row = lo.content.row + 0;  // "~" real, seleccionado
    // Los glifos finos (~) rara vez tienen píxeles de cobertura total (un ~
    // blanco sobre fondo 0x3A llega a 0xF6, no a 0xFF): no se exige el fg
    // exacto sino su vecindad, más la ausencia del verde marker. Se salta
    // la primera celda: ahí pinta el cursor en bloque (opaco, color propio).
    bool hasTextTone = false;
    bool hasMarkerGreen = false;
    for (int c = lo.content.col + 1; c < lo.content.col + lo.content.width; ++c)
        for (int dx = 0; dx < cw; ++dx)
            for (int dy = 0; dy < ch; ++dy) {
                GuiColor g{};
                if (!readPixel(sdl.ren, c * cw + dx, row * ch + dy, g)) continue;
                const int r = g.r, gg = g.g, b = g.b;
                if (dark) {
                    if (r >= 0xC0 && gg >= 0xC0 && b >= 0xC0) hasTextTone = true;
                    if (gg > r + 0x18 && gg > b + 0x18 && gg > 0x50)
                        hasMarkerGreen = true;
                } else {
                    // Tema claro: texto casi negro sobre fondo claro (el
                    // marker gris 0x6E nunca baja de 0x50 ni siquiera en su
                    // núcleo, así que no hay falso positivo).
                    if (r <= 0x50 && gg <= 0x50 && b <= 0x50) hasTextTone = true;
                }
            }
    CHECK(hasTextTone);
    if (dark) CHECK(!hasMarkerGreen);
#endif
}

// Paridad de política de fondos TTY↔GUI.
//
// La política vive una sola vez en rowBgKindFor (Style.h). La GUI la
// consume directo (styleFor) y el TTY la implementa en ramas congeladas
// byte a byte (sus códigos combinan fg+bg y no se pueden reexpresar).
// Este test congela que ambas deciden lo mismo para cada rol de contenido
// × línea actual × tema:
//   - lado GUI: styleFor cableado a rowBgKindFor (si alguien reintroduce
//     lógica propia en GuiChrome, cae).
//   - lado TTY: los bytes de encodeRow contienen el código ANSI de fondo
//     de la familia (tabla local rol→código que fija las ramas del encoder;
//     GutterCurrent lleva su código combinado propio, que incluye el mismo
//     fondo de línea actual).
// Solo roles de contenido (los que emite FrameBuilder en filas); los de
// chrome (Status*/Accent*/Msg*) nunca aparecen en StyledRow.segs.
TEST(tty_gui_bg_policy_parity) {
    const StyleRole roles[] = {
        StyleRole::Default,           StyleRole::Gutter,
        StyleRole::GutterCurrent,     StyleRole::GutterBlank,
        StyleRole::Marker,            StyleRole::CurrentLine,
        StyleRole::Selection,         StyleRole::BracketMatch,
        StyleRole::ListSelected,      StyleRole::SyntaxKeyword,
        StyleRole::SyntaxType,        StyleRole::SyntaxPreprocessor,
        StyleRole::SyntaxString,      StyleRole::SyntaxCharacter,
        StyleRole::SyntaxNumber,      StyleRole::SyntaxComment,
    };
    for (bool dark : {true, false}) {
        TtyEncoder enc;
        enc.setTheme(dark ? darkTheme() : lightTheme());
        const TtyTheme& T = enc.theme();
        const std::string bgCodes[] = {T.currentLine, T.selection,
                                       T.bracketMatch, T.listSelected,
                                       T.gutterCurrent};
        for (StyleRole role : roles)
            for (bool cur : {false, true}) {
                // Lado GUI: cableado a la política compartida.
                CHECK(guichrome::styleFor(role, cur, dark).bg ==
                      guichrome::guiBgFor(rowBgKindFor(role, cur), dark));
                // Lado TTY: bytes observables de una fila de un segmento.
                StyledRow row;
                row.isCurrentLine = cur;
                row.arena = "x";
                row.segs.push_back(
                    FrameSegment{std::string_view(row.arena), role});
                const std::string bytes = enc.encodeRow(row);
                std::string want;
                switch (role) {
                    case StyleRole::Selection:
                        want = T.selection;
                        break;
                    case StyleRole::BracketMatch:
                        want = T.bracketMatch;
                        break;
                    case StyleRole::ListSelected:
                        want = T.listSelected;
                        break;
                    case StyleRole::CurrentLine:
                        want = T.currentLine;
                        break;
                    case StyleRole::GutterCurrent:
                        want = T.gutterCurrent;
                        break;
                    default:
                        if (cur && isSyntaxRole(role)) want = T.currentLine;
                        break;
                }
                if (want.empty()) {
                    for (const std::string& bg : bgCodes)
                        CHECK(bytes.find(bg) == std::string::npos);
                } else {
                    CHECK(bytes.find(want) != std::string::npos);
                }
            }
    }
}

// La línea actual cubre TODA la fila visible aunque esté vacía o el texto
// sea corto: el FrameBuilder incluye fillers CurrentLine hasta el ancho
// visible (igual que el TTY envuelve toda la fila) y paintFrameContent los
// pinta. Sin esos fillers, el fondo quedaría cortado tras el último
// segmento. Se muestrea más allá de todo texto para observar el filler,
// no los glifos.
TEST(gui_sdl_linea_actual_vacia_cubre_fila) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola", "", "mundo"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 1;  // línea actual vacía
    cur.col = 0;
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    const bool dark = gui.isDarkTheme();
    const int cw = gui.cellW(), ch = gui.cellH();
    const Layout lo = gui.lastLayout();
    const int x = (lo.content.col + 35) * cw + cw / 2;
    // Fila 1 (actual, vacía): fondo de línea actual hasta el borde.
    CHECK(readPixel(sdl.ren, x, (lo.content.row + 1) * ch + ch / 2, probe));
    CHECK(probe == guichrome::currentLineBackground(dark));
    // Fila 0 (no actual, corta): fondo de contenido, sin restos de la
    // línea actual (las filas no actuales no llevan padding).
    CHECK(readPixel(sdl.ren, x, (lo.content.row + 0) * ch + ch / 2, probe));
    CHECK(probe == guichrome::background(dark));
#endif
}

// El idle de GuiRunLoop expira los mensajes con timeout: es el mismo camino
// que run() ejecuta en cada vuelta sin eventos (idleDelayMs + idleStep),
// pero ejercido acá sin SDL ni ventana, con tiempos controlados. El test
// anterior maneja tick/renderFrame a mano; este cubre la composición que
// realmente usa el loop.
TEST(gui_runloop_idle_expira_mensaje) {
    auto clipboard = std::make_unique<FakeClipboard>();
    auto watcher = std::make_unique<NullFileWatcher>();
    Editor ed(std::move(clipboard), std::move(watcher));
    auto gui = std::make_unique<GuiRenderer>();
    GuiRenderer* guiPtr = gui.get();
    ed.setRenderer(std::move(gui));
    NullTestSink sink;
    ed.setSink(sink);
    GuiRunLoop loop(ed);

    // Mensaje de ACCIÓN con timeout (undo sin historial).
    InputEvent undo;
    undo.type = InputEventType::Undo;
    ed.handleEvent(undo);
    ed.renderFrame();
    CHECK(contains(guiPtr->lastChrome().message.text, "Nada que deshacer"));

    // El delay del idle sale del Editor (fórmula pura, sin SDL).
    const auto now = std::chrono::steady_clock::now();
    const int d = loop.idleDelayMs(now);
    CHECK(d >= 0);
    CHECK(d <= 30);

    // El idle del loop tras el timeout: tick + renderFrame dejan el
    // MessageBar vacío.
    auto later = now + Editor::kActionMessageTimeout +
                 std::chrono::milliseconds(50);
    loop.idleStep(later);
    CHECK(guiPtr->lastChrome().message.text.empty());
}

// Cursor GUI: Bloque (navegación) fijo, Barra (inserción) parpadea 530ms.
// El blink sigue al shape (Bar), no al State nominal: cubre Interacción y
// los 4 prompts con input (que ya llegan como Bar desde cursorShapeFor).
TEST(gui_cursor_block_fijo_bar_parpadea) {
    using clk = std::chrono::steady_clock;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp();
    Cursor cur;
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    const auto t0 = clk::now();

    // Bloque (navegación): siempre visible, en toda la fase.
    {
        GuiRenderer gui;
        gui.setBlinkNow(t0);
        gui.renderScreenDiff(doc, cur, vp, "t", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             std::nullopt, std::nullopt, std::nullopt);
        CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
        for (int ms : {0, 200, 529, 530, 800, 1059, 1060, 2000}) {
            gui.setBlinkNow(t0 + std::chrono::milliseconds(ms));
            CHECK(gui.blinkPhaseOn());
            CHECK(gui.cursorShown());
        }
    }
    // Barra (inserción): 530ms ON / 530ms OFF (periodo 1060ms).
    {
        GuiRenderer gui;
        gui.setBlinkNow(t0);
        gui.renderScreenDiff(doc, cur, vp, "t", false,
                             makeChromeRequest(msg, State::Interaccion), sink,
                             std::nullopt, std::nullopt, std::nullopt);
        CHECK(gui.lastCursor().shape == FrameCursorShape::Bar);
        auto onOff = [&](int ms) {
            gui.setBlinkNow(t0 + std::chrono::milliseconds(ms));
            return gui.blinkPhaseOn();
        };
        CHECK(onOff(0));
        CHECK(onOff(200));
        CHECK(onOff(529));
        CHECK(!onOff(530));
        CHECK(!onOff(800));
        CHECK(!onOff(1059));
        CHECK(onOff(1060));
        CHECK(!onOff(1590));
        // cursorShown = visible lógico AND fase.
        gui.setBlinkNow(t0 + std::chrono::milliseconds(800));
        CHECK(gui.lastCursor().visible);
        CHECK(!gui.cursorShown());
    }
}

// El ancla del blink se resetea al mover el cursor o cambiar de forma:
// tras moverse, la barra vuelve a ON aunque la fase anterior estuviera OFF.
TEST(gui_cursor_blink_resetea_al_mover) {
    using clk = std::chrono::steady_clock;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola", "mundo"});
    Viewport vp = makeVp();
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    const auto t0 = clk::now();
    GuiRenderer gui;
    Cursor c0;
    c0.line = 0;
    c0.col = 0;
    gui.setBlinkNow(t0);
    gui.renderScreenDiff(doc, c0, vp, "t", false,
                         makeChromeRequest(msg, State::Interaccion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    // Fase OFF a los 800ms.
    gui.setBlinkNow(t0 + std::chrono::milliseconds(800));
    CHECK(!gui.blinkPhaseOn());
    // Mueve el cursor con el mismo "ahora": el ancla se resetea -> ON.
    Cursor c1;
    c1.line = 0;
    c1.col = 1;
    gui.renderScreenDiff(doc, c1, vp, "t", false,
                         makeChromeRequest(msg, State::Interaccion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(gui.blinkPhaseOn());
    CHECK(gui.cursorShown());
    // Y vuelve a apagarse 530ms después del movimiento.
    gui.setBlinkNow(t0 + std::chrono::milliseconds(800 + 530));
    CHECK(!gui.blinkPhaseOn());
}

// Rects del cursor + ancla IME: celda * tamaño de celda; la barra se pinta
// de 2px pero el IME ancla la celda completa; el IME sigue al cursor
// lógico aunque el blink esté OFF.
TEST(gui_cursor_rects_e_ime_en_celda) {
    using clk = std::chrono::steady_clock;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp();
    Cursor cur;
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    const auto t0 = clk::now();
    const int cw = 9, ch = 18;  // fallbacks de GuiFont sin fuente

    // Bloque: rect de celda completa.
    {
        GuiRenderer gui;
        gui.setBlinkNow(t0);
        gui.renderScreenDiff(doc, cur, vp, "t", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             std::nullopt, std::nullopt, std::nullopt);
        const CellPos cell = gui.lastCursor().cell;
        CHECK(cell.valid());
        const GuiPixelRect pr = gui.cursorPixelRect();
        CHECK(pr.valid);
        CHECK_EQ(pr.x, cell.col * cw);
        CHECK_EQ(pr.y, cell.row * ch);
        CHECK_EQ(pr.w, cw);
        CHECK_EQ(pr.h, ch);
        const GuiPixelRect ime = gui.imeRectPx();
        CHECK(ime.valid);
        CHECK_EQ(ime.x, cell.col * cw);
        CHECK_EQ(ime.y, cell.row * ch);
        CHECK_EQ(ime.w, cw);
        CHECK_EQ(ime.h, ch);
    }
    // Barra: pintado fino de 2px, IME en celda completa, válido en OFF.
    {
        GuiRenderer gui;
        gui.setBlinkNow(t0);
        gui.renderScreenDiff(doc, cur, vp, "t", false,
                             makeChromeRequest(msg, State::Interaccion), sink,
                             std::nullopt, std::nullopt, std::nullopt);
        const CellPos cell = gui.lastCursor().cell;
        const GuiPixelRect pr = gui.cursorPixelRect();
        CHECK(pr.valid);
        CHECK_EQ(pr.w, 2);
        CHECK_EQ(pr.h, ch);
        // Fase OFF: no se pinta, pero la geometría lógica sigue válida y el
        // IME sigue anclado.
        gui.setBlinkNow(t0 + std::chrono::milliseconds(800));
        CHECK(!gui.cursorShown());
        const GuiPixelRect off = gui.cursorPixelRect();
        CHECK(off.valid);
        CHECK_EQ(off.x, cell.col * cw);
        CHECK_EQ(off.y, cell.row * ch);
        CHECK_EQ(off.w, 2);
        CHECK_EQ(off.h, ch);
        const GuiPixelRect ime = gui.imeRectPx();
        CHECK(ime.valid);
        CHECK_EQ(ime.x, cell.col * cw);
        CHECK_EQ(ime.y, cell.row * ch);
        CHECK_EQ(ime.w, cw);
        CHECK_EQ(ime.h, ch);
    }
}

// El idle del loop inyecta el tiempo de blink: la barra (Interacción)
// se apaga sola tras 530ms sin eventos, el bloque nunca.
TEST(gui_runloop_idle_anima_blink_bar) {
    using clk = std::chrono::steady_clock;
    auto clipboard = std::make_unique<FakeClipboard>();
    auto watcher = std::make_unique<NullFileWatcher>();
    Editor ed(std::move(clipboard), std::move(watcher));
    auto gui = std::make_unique<GuiRenderer>();
    GuiRenderer* guiPtr = gui.get();
    ed.setRenderer(std::move(gui));
    NullTestSink sink;
    ed.setSink(sink);
    GuiRunLoop loop(ed);
    loop.setGuiRenderer(guiPtr);

    // Navegación -> Interacción (barra en contenido).
    InputEvent ie;
    ie.type = InputEventType::InsertChar;
    ie.text = "i";
    ed.handleEvent(ie);
    const auto t0 = clk::now();
    loop.idleStep(t0);
    CHECK(guiPtr->lastCursor().shape == FrameCursorShape::Bar);
    CHECK(guiPtr->cursorShown());
    // 800ms después sin eventos: fase OFF (el loop repinta cada ≤30ms en
    // producción, acá se ejercita el mismo idleStep con tiempo controlado).
    loop.idleStep(t0 + std::chrono::milliseconds(800));
    CHECK(guiPtr->lastCursor().visible);
    CHECK(!guiPtr->blinkPhaseOn());
    CHECK(!guiPtr->cursorShown());
    // El ancla IME sigue válida en OFF (la candidata no pierde posición).
    CHECK(guiPtr->imeRectPx().valid);
}
