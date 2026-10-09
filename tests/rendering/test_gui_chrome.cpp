// Tests de la GUI (chrome + contenido + modales + pintado SDL real).
//
// Dos capas:
//   - Pura (sin ventana): guichrome::statusLine/messageLine, buildFrame,
//     snapshots lastContentRows_/lastListLines_ (SaveAs en MessageBar,
//     abrir en la lista).
//   - Pintado SDL real (sección final, driver de video "dummy" headless +
//     renderer por software + SDL_RenderReadPixels): verifica que lo que se
//     pinta en píxeles reproduce el snapshot (fondos de selección/listas,
//     cursor en bloque con glifo invertido, MessageBar, sin cajas negras
//     del blended). Sin SDL2
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
#include "layout/Gutter.h"
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
    // "int f() { return 0; }" mide 21 bytes: '{' en 8, '}' en 20. Cada
    // extremo del par identifica UN carácter ([col, col+1), ver BracketPair).
    BracketPair pair{{0, 8}, {0, 20}};
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
// El fondo pintado en cada celda es exactamente styleFor(rol, línea
// actual).bg, en una sola capa opaca (un fill por segmento, sin overlays
// con alfa ni segundas pasadas). El resolver del cursor Block recupera el
// fondo efectivo con la MISMA llamada y los mismos argumentos (ver
// blockGlyphInContent); si el pintor compusiera fondos en etapas, ambos
// divergirían. Sin fuente cada celda es fill sólido y la comparación es
// píxel a píxel exacta, rol por rol —incluido Bracket, que solo ocupa sus
// propias celdas y no se puede muestrear más allá del texto— en ambos
// temas. (ListSelected, misma tabla, en el test de modales.)
TEST(gui_sdl_fondos_igualan_stylefor) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;  // sin fuente a propósito: fills exactos, sin tinta.
    gui.setSdlRenderer(sdl.ren);
    CHECK(!gui.hasFont());
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"abcdef", "gh"});
    Viewport vp = makeVp(10, 40);
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    Selection sel{{0, 1}, {0, 4}};    // "bcd": fondo Selection
    BracketPair pair{{0, 0}, {0, 5}};  // 'a' y 'f': fondo Bracket
    // Línea 0 = actual; bloque aparcado en el relleno (celda 9, EOL),
    // fuera de las celdas muestreadas salvo la 9, que se afirma como fill.
    Cursor cur;
    cur.line = 0;
    cur.col = 100;  // clamp a EOL → celda 9 (espacio de relleno)
    for (bool firstTheme : {true, false}) {
        const bool dark = gui.isDarkTheme();
        gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             sel, std::nullopt, pair);
        CHECK(gui.lastCursor().visible);
        const int cw = gui.cellW(), ch = gui.cellH();
        auto bgAt = [&](int c, int r, GuiColor& out) {
            return readPixel(sdl.ren, c * cw + cw / 2, r * ch + ch / 2, out);
        };
        GuiColor got{};
        // Fila actual: gutter ('1'), 'a' bracket, 'b' selection,
        // 'e' línea actual, 'f' bracket, relleno línea actual.
        CHECK(bgAt(1, 0, got));
        CHECK(got == guichrome::styleFor(StyleRole::GutterCurrent, true,
                                         dark)
                         .bg);
        CHECK(bgAt(3, 0, got));
        CHECK(got ==
              guichrome::styleFor(StyleRole::BracketMatch, true, dark).bg);
        CHECK(bgAt(4, 0, got));
        CHECK(got == guichrome::styleFor(StyleRole::Selection, true, dark).bg);
        CHECK(bgAt(7, 0, got));
        CHECK(got == guichrome::styleFor(StyleRole::CurrentLine, true,
                                         dark)
                         .bg);
        CHECK(bgAt(8, 0, got));
        CHECK(got ==
              guichrome::styleFor(StyleRole::BracketMatch, true, dark).bg);
        // Celda 9: el cursor (fill sobre espacio de relleno, sin glifo).
        CHECK_EQ(gui.lastCursor().cell.col, 9);
        CHECK(bgAt(9, 0, got));
        CHECK(got == guichrome::cursorColor(dark));
        // Fila no actual: texto plano sobre fondo base.
        CHECK(bgAt(3, 1, got));
        CHECK(got ==
              guichrome::styleFor(StyleRole::Default, false, dark).bg);
        if (firstTheme) gui.toggleTheme();
    }
#endif
}

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

// Límite entre segmentos bajo el bloque: el cursor en el primer carácter
// seleccionado cae dentro del segmento Selection (la frontera no se corre
// una celda). Lo fija el snapshot (rol exacto en la celda); los píxeles
// confirman que el glifo sigue legible sobre el bloque.
TEST(gui_sdl_cursor_block_en_borde_seleccion) {
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
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    Selection sel{{0, 1}, {0, 4}};  // "ola": frontera visual en col 1
    Cursor cur;
    cur.line = 0;
    cur.col = 1;  // 'o', primer seleccionado
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink, sel,
                         std::nullopt, std::nullopt);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
    const Layout lo = gui.lastLayout();
    const CellPos cell = gui.lastCursor().cell;
    CHECK_EQ(cell.col, gutterWidth(doc.lineCount(), vp.width) + 1);
    // La celda pertenece al segmento Selection (no al previo): la frontera
    // visual [1,4) se atribuye sin corrimiento.
    {
        const int relCol = cell.col - lo.content.col;
        const auto& row = gui.lastContentRows()[0];
        CHECK(row.hasRole(StyleRole::Selection));
        int acc = 0;
        bool found = false;
        for (const auto& seg : row.segs) {
            const int cols = colWidth(seg.text);
            if (relCol >= acc && relCol < acc + cols) {
                CHECK(seg.role == StyleRole::Selection);
                CHECK_EQ(seg.text.substr(
                             static_cast<size_t>(relCol - acc), 1),
                         "o");
                found = true;
                break;
            }
            acc += cols;
        }
        CHECK(found);
    }
    // ...y en píxeles el glifo es el correcto en su posición: misma máscara
    // de tinta que una referencia con el cursor aparcado (un vecino
    // erróneo también dejaría la celda no sólida, así que presencia de
    // tinta sola no basta). Tolerancia acotada por el jitter de 1px de
    // SDL_ttf en-cadena vs aislado (ver gutter_no_desplaza_glifo).
    const bool dark = gui.isDarkTheme();
    const int cw = gui.cellW(), ch = gui.cellH();
    const GuiColor cc = guichrome::cursorColor(dark);
    if (!gui.hasFont()) {
        // Degradado sin fuente: fill sólido (nada que invertir).
        for (int dx = 0; dx < cw; ++dx)
            for (int dy = 0; dy < ch; ++dy) {
                CHECK(readPixel(sdl.ren, cell.col * cw + dx,
                                cell.row * ch + dy, probe));
                CHECK(probe == cc);
            }
    } else {
        auto readColumn = [&](int c, int r, std::vector<GuiColor>& out) {
            out.clear();
            for (int dx = 0; dx < cw; ++dx)
                for (int dy = 0; dy < ch; ++dy) {
                    GuiColor g{};
                    if (!readPixel(sdl.ren, c * cw + dx, r * ch + dy, g))
                        return false;
                    out.push_back(g);
                }
            return true;
        };
        std::vector<GuiColor> curPx;
        CHECK(readColumn(cell.col, cell.row, curPx));
        // Referencia: mismos estilos (misma selección), cursor aparcado en
        // el relleno sin tocar la celda.
        Cursor park;
        park.line = 0;
        park.col = 100;  // clamp a EOL
        gui.renderScreenDiff(doc, park, vp, "t.txt", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             sel, std::nullopt, std::nullopt);
        CHECK(gui.lastCursor().visible);
        std::vector<GuiColor> refPx;
        CHECK(readColumn(cell.col, cell.row, refPx));
        const GuiColor selBg = guichrome::selectionBackground(dark);
        auto inkMask = [&](const std::vector<GuiColor>& v,
                           const GuiColor& bg) {
            std::vector<char> m;
            m.reserve(v.size());
            for (const GuiColor& g : v) m.push_back(g != bg);
            return m;
        };
        CHECK_EQ(refPx.size(), curPx.size());
        const auto rm = inkMask(refPx, selBg);
        const auto cm = inkMask(curPx, cc);
        int refInk = 0, mismatch = 0;
        for (size_t i = 0; i < rm.size(); ++i) {
            refInk += rm[i] ? 1 : 0;
            if (rm[i] != cm[i]) ++mismatch;
        }
        CHECK(refInk > 10);
        CHECK(mismatch <= 4);
    }
#endif
}

// Política del cursor en bloque (ver GuiChrome.h): fondo uniforme del
// cursor + glifo con el fondo EFECTIVO de la celda (inversión exacta), o
// fondo base como fallback cuando el efectivo no contrasta (bracket en
// dark). Sin fuente se deja solo el fill.
TEST(gui_cursor_glyph_politica_inversion_y_contraste) {
    // Las 4 familias de fondo de contenido + lista, en ambos temas: el fg
    // que decide cursorGlyphFg siempre contrasta con el cursor.
    const RowBgKind kinds[] = {RowBgKind::Content, RowBgKind::CurrentLine,
                               RowBgKind::Selection, RowBgKind::Bracket,
                               RowBgKind::List};
    for (bool dark : {true, false}) {
        const GuiColor cc = guichrome::cursorColor(dark);
        for (RowBgKind k : kinds) {
            const GuiColor cellBg = guichrome::guiBgFor(k, dark);
            const GuiColor fg = guichrome::cursorGlyphFg(cellBg, dark);
            CHECK(guichrome::cursorContrastRatio(fg, cc) >=
                  guichrome::kCursorGlyphMinContrast);
        }
    }
    // Inversión exacta donde hay contraste: el fg es el fondo efectivo.
    for (bool dark : {true, false}) {
        for (RowBgKind k :
             {RowBgKind::Content, RowBgKind::CurrentLine, RowBgKind::Selection,
              RowBgKind::List}) {
            const GuiColor cellBg = guichrome::guiBgFor(k, dark);
            CHECK(guichrome::cursorGlyphFg(cellBg, dark) == cellBg);
        }
    }
    // Caso que motivó el fallback: bracket amarillo sobre cursor claro en
    // dark no contrasta -> se usa el fondo base (contraste garantizado).
    {
        const GuiColor bracket = guichrome::guiBgFor(RowBgKind::Bracket, true);
        CHECK(guichrome::cursorContrastRatio(
                  bracket, guichrome::cursorColor(true)) <
              guichrome::kCursorGlyphMinContrast);
        CHECK(guichrome::cursorGlyphFg(bracket, true) ==
              guichrome::background(true));
        CHECK(guichrome::cursorContrastRatio(
                  guichrome::background(true),
                  guichrome::cursorColor(true)) >=
              guichrome::kCursorGlyphMinContrast);
    }
    // En light el bracket sí contrasta con el cursor oscuro: inversión
    // exacta también ahí.
    {
        const GuiColor bracket =
            guichrome::guiBgFor(RowBgKind::Bracket, false);
        CHECK(guichrome::cursorGlyphFg(bracket, false) == bracket);
    }
}

// El glifo repintado usa el fondo REAL pintado en la celda (no una
// abstracción): por cada rol con fondo propio se mide el fondo pintado en
// una referencia aparcada (moda del píxel, >50%), se deriva el fg esperado
// con la política y se afirman fill + núcleo de ese color en el cursor.
// Cierra el triángulo con los otros dos tests: politica_* certifica la
// función contra familias abstractas, fondos_igualan_stylefor que lo
// pintado == styleFor(rol), y este que el resolver recupera ese mismo RGB
// (un fondo efectivo erróneo daría otro fg y el conteo caería a ~0).
// El snapshot fija (rol, carácter) por caso para que el test no sea
// autoconsistente ante un Frame degenerado.
TEST(gui_sdl_cursor_glifo_usa_fondo_real_pintado) {
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
    Viewport vp = makeVp(10, 40);
    Message msg{std::string(""), MessageKind::Info, std::nullopt};

    auto readColumn = [&](int c, int r, int cw, int ch,
                          std::vector<GuiColor>& out) {
        out.clear();
        for (int dx = 0; dx < cw; ++dx)
            for (int dy = 0; dy < ch; ++dy) {
                GuiColor g{};
                if (!readPixel(sdl.ren, c * cw + dx, r * ch + dy, g))
                    return false;
                out.push_back(g);
            }
        return true;
    };
    // Moda de color + fracción (celda ~190px: O(n²) despreciable).
    auto topColor = [&](const std::vector<GuiColor>& v) {
        std::vector<std::pair<GuiColor, int>> counts;
        for (const GuiColor& g : v) {
            bool found = false;
            for (auto& kv : counts)
                if (kv.first == g) {
                    ++kv.second;
                    found = true;
                    break;
                }
            if (!found) counts.push_back({g, 1});
        }
        std::pair<GuiColor, int> best{{}, -1};
        for (const auto& kv : counts)
            if (kv.second > best.second) best = kv;
        return std::pair<GuiColor, double>{
            best.first, best.second / static_cast<double>(v.size())};
    };

    struct Case {
        std::vector<std::string> lines;
        int curLine;
        int curCol;  // byte del glifo (ASCII: byte == columna visual)
        std::optional<Selection> sel;
        std::optional<BracketPair> pair;
        StyleRole role;  // rol esperado en la celda (premisa del snapshot)
        const char* glyph;
    };
    const Selection selBc{{0, 1}, {0, 3}};  // "bc"
    // Bracket sobre 'm' (trazo grueso: núcleo sólido amplio para contar;
    // el rol es posicional, el carácter es irrelevante).
    const BracketPair brParen{{0, 0}, {0, 1}};  // 'm' y 'w'
    // NOTA: no hay caso Default/content: la fila del cursor siempre es la
    // actual, así que un fondo Content bajo el bloque es inalcanzable en
    // contenido (el cursor nunca pisa gutter ni "~"; ver comentario en el
    // resolver). Las familias alcanzables son estas tres.
    const std::vector<Case> cases = {
        {{"ab"}, 0, 1, std::nullopt, std::nullopt, StyleRole::CurrentLine, "b"},
        {{"abcd"},
         0,
         1,
         selBc,
         std::nullopt,
         StyleRole::Selection,
         "b"},
        {{"mw"},
         0,
         0,
         std::nullopt,
         brParen,
         StyleRole::BracketMatch,
         "m"},
    };

    for (bool firstTheme : {true, false}) {
        const bool dark = gui.isDarkTheme();
        const GuiColor cc = guichrome::cursorColor(dark);
        const int cw = gui.cellW(), ch = gui.cellH();
        for (const Case& cs : cases) {
            Document doc;
            doc.restore(cs.lines);
            Cursor cur;
            cur.line = cs.curLine;
            cur.col = cs.curCol;
            gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                                 makeChromeRequest(msg, State::Navegacion),
                                 sink, cs.sel, std::nullopt, cs.pair);
            CHECK(gui.lastCursor().visible);
            CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
            const Layout lo = gui.lastLayout();
            const CellPos cell = gui.lastCursor().cell;
            const int gw = gutterWidth(doc.lineCount(), vp.width);
            CHECK_EQ(cell.col, lo.content.col + gw + cs.curCol);
            // Premisa del snapshot: rol y carácter exactos en la celda.
            {
                const int relCol = cell.col - lo.content.col;
                const auto& row =
                    gui.lastContentRows()[static_cast<size_t>(cell.row -
                                                              lo.content.row)];
                int acc = 0;
                bool found = false;
                for (const auto& seg : row.segs) {
                    const int cols = colWidth(seg.text);
                    if (relCol >= acc && relCol < acc + cols) {
                        CHECK(seg.role == cs.role);
                        CHECK_EQ(seg.text.substr(
                                     static_cast<size_t>(relCol - acc), 1),
                                 cs.glyph);
                        found = true;
                        break;
                    }
                    acc += cols;
                }
                CHECK(found);
            }
            std::vector<GuiColor> curPx;
            CHECK(readColumn(cell.col, cell.row, cw, ch, curPx));
            // Referencia aparcada: misma fila y estilos, sin cursor encima.
            Cursor park;
            park.line = cs.curLine;
            park.col = 100;  // clamp a EOL
            gui.renderScreenDiff(doc, park, vp, "t.txt", false,
                                 makeChromeRequest(msg, State::Navegacion),
                                 sink, cs.sel, std::nullopt, cs.pair);
            CHECK(gui.lastCursor().visible);
            std::vector<GuiColor> refPx;
            CHECK(readColumn(cell.col, cell.row, cw, ch, refPx));
            // Fondo real medido (moda >50%: el glifo es minoría).
            const auto [paintedBg, bgFrac] = topColor(refPx);
            CHECK(bgFrac > 0.5);
            const GuiColor expectFg =
                guichrome::cursorGlyphFg(paintedBg, dark);
            // Fill aplicado (moda = cursor) + núcleo del fg derivado del
            // fondo MEDIDO (no de la familia abstracta).
            const auto [curMode, curFrac] = topColor(curPx);
            CHECK(curMode == cc);
            CHECK(curFrac > 0.5);
            // Núcleo en el fg esperado. Los glifos de trazo fino ('l', '(')
            // apenas tienen píxeles de cobertura total a 16px, por eso los
            // casos usan letras de trazo grueso con núcleo amplio.
            int core = 0;
            for (const GuiColor& g : curPx)
                if (g == expectFg) ++core;
            CHECK(core >= 5);
        }
        if (firstTheme) gui.toggleTheme();
    }
#endif
}

// El cursor en bloque invierte el glifo (paridad con el TTY): la celda se
// rellena con el color del cursor y el carácter se repinta encima con el
// fg de la política (fondo efectivo o fallback), así sigue legible. Sin
// fuente se deja solo el fill.
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
    Cursor cur;  // (0,0) -> sobre la 'h'
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
    const int cw = gui.cellW(), ch = gui.cellH();
    const CellPos cell = gui.lastCursor().cell;
    const GuiColor cc = guichrome::cursorColor(gui.isDarkTheme());
    // Toda la celda: el fondo del cursor está presente (no lo tapa el texto
    // original) y, con fuente, hay píxeles distintos (el glifo invertido).
    bool hasBg = false;
    bool hasOther = false;
    for (int dx = 0; dx < cw; ++dx)
        for (int dy = 0; dy < ch; ++dy) {
            CHECK(readPixel(sdl.ren, cell.col * cw + dx, cell.row * ch + dy,
                            probe));
            if (probe == cc)
                hasBg = true;
            else
                hasOther = true;
        }
    CHECK(hasBg);
    if (gui.hasFont()) {
        CHECK(hasOther);  // el glifo se repintó: la celda no es sólida
    } else {
        CHECK(!hasOther);  // degradado sin fuente: solo fill
    }
#endif
}

// Celda vacía (cursor en EOL, sobre el relleno de línea actual): el bloque
// es solo fill sólido, sin glifo que invertir. La posición se deriva del
// documento (byte EOL, no una columna mágica) y se verifica cada eslabón:
// columna visual → celda (gutter + viewport) → segmento del snapshot
// (espacio de relleno CurrentLine) → píxeles.
TEST(gui_sdl_cursor_block_en_vacio_es_fill_solido) {
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
    doc.restore({"hola"});  // ASCII a propósito: byte == columna visual.
    Viewport vp = makeVp(10, 40);
    CHECK_EQ(vp.left, 0);
    const std::string& line = doc.lineAt(0);
    const int eolByte = static_cast<int>(line.size());
    const int eolVis = colWidth(line);
    CHECK_EQ(eolByte, 4);
    CHECK_EQ(eolVis, 4);
    Cursor cur;
    cur.line = 0;
    cur.col = eolByte;  // EOL: más allá del último glifo, sobre el relleno
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
    const Layout lo = gui.lastLayout();
    CHECK_EQ(lo.content.col, 0);
    CHECK_EQ(lo.content.row, 0);
    // Eslabón visual: el cursor EOL resuelve al ancho visible de la línea.
    CHECK_EQ(cur.visualColumn(doc), eolVis);
    // Eslabón celda: gutter + viewport (sin left) + EOL visual.
    const CellPos cell = gui.lastCursor().cell;
    CHECK_EQ(cell.row, lo.content.row);  // fila actual, primera visible
    const int gutterW = cell.col - lo.content.col - eolVis;
    CHECK_EQ(gutterW, gutterWidth(doc.lineCount(), vp.width));
    // Eslabón snapshot: la celda cae en un espacio del relleno
    // (CurrentLine), no sobre un glifo.
    {
        const int relCol = cell.col - lo.content.col;
        const auto& row = gui.lastContentRows()[0];
        CHECK(row.isCurrentLine);
        int acc = 0;
        bool found = false;
        for (const auto& seg : row.segs) {
            const int cols = colWidth(seg.text);
            if (relCol >= acc && relCol < acc + cols) {
                CHECK(seg.role == StyleRole::CurrentLine);
                CHECK_EQ(seg.text.substr(
                             static_cast<size_t>(relCol - acc), 1),
                         " ");
                found = true;
                break;
            }
            acc += cols;
        }
        CHECK(found);
    }
    // ...y en píxeles es fill sólido (nada que invertir).
    const int cw = gui.cellW(), ch = gui.cellH();
    const GuiColor cc = guichrome::cursorColor(gui.isDarkTheme());
    for (int dx = 0; dx < cw; ++dx)
        for (int dy = 0; dy < ch; ++dy) {
            CHECK(readPixel(sdl.ren, cell.col * cw + dx, cell.row * ch + dy,
                            probe));
            CHECK(probe == cc);
        }
    // Más allá de EOL colapsa a la misma celda vacía (clamp a EOL).
    {
        Cursor beyond;
        beyond.line = 0;
        beyond.col = 100;
        gui.renderScreenDiff(doc, beyond, vp, "t.txt", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             std::nullopt, std::nullopt, std::nullopt);
        CHECK(gui.lastCursor().visible);
        CHECK(gui.lastCursor().cell == cell);
    }
#endif
}

// Tabulación bajo el bloque: el Frame expande tabs antes de segmentar,
// así que el cursor sobre el byte del tab cae al inicio de su expansión
// (espacios → solo fill) y pasada la expansión vuelve a haber glifo en su
// celda exacta. El interior de la expansión es inalcanzable para el cursor
// (ver cursor_visual_siempre_en_inicio_de_celda).
TEST(gui_sdl_cursor_block_en_tab_sin_desplazar) {
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
    doc.restore({"a\tb"});  // visual: a@0, tab→1..3, b@4
    Viewport vp = makeVp(10, 40);
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    const int cw = gui.cellW(), ch = gui.cellH();
    const GuiColor cc = guichrome::cursorColor(gui.isDarkTheme());
    const int gw = gutterWidth(doc.lineCount(), vp.width);
    CHECK_EQ(gw, 3);

    // Sobre el byte del tab: visual = inicio de la expansión (1), celda de
    // espacio → fill sólido.
    {
        Cursor cur;
        cur.line = 0;
        cur.col = 1;
        gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             std::nullopt, std::nullopt, std::nullopt);
        CHECK(gui.lastCursor().visible);
        CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
        CHECK_EQ(cur.visualColumn(doc), 1);
        const CellPos cell = gui.lastCursor().cell;
        CHECK_EQ(cell.col, gw + 1);
        for (int dx = 0; dx < cw; ++dx)
            for (int dy = 0; dy < ch; ++dy) {
                CHECK(readPixel(sdl.ren, cell.col * cw + dx,
                                cell.row * ch + dy, probe));
                CHECK(probe == cc);
            }
    }
    // Pasada la expansión (byte de 'b'): glifo legible en su celda exacta.
    {
        Cursor cur;
        cur.line = 0;
        cur.col = 2;
        gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             std::nullopt, std::nullopt, std::nullopt);
        CHECK(gui.lastCursor().visible);
        CHECK_EQ(cur.visualColumn(doc), 4);
        const CellPos cell = gui.lastCursor().cell;
        CHECK_EQ(cell.col, gw + 4);
        // El snapshot confirma 'b' (no un resto de la expansión).
        {
            const int relCol = cell.col - gui.lastLayout().content.col;
            const auto& row = gui.lastContentRows()[0];
            int acc = 0;
            bool found = false;
            for (const auto& seg : row.segs) {
                const int cols = colWidth(seg.text);
                if (relCol >= acc && relCol < acc + cols) {
                    CHECK_EQ(seg.text.substr(
                                 static_cast<size_t>(relCol - acc), 1),
                             "b");
                    found = true;
                    break;
                }
                acc += cols;
            }
            CHECK(found);
        }
        bool hasBg = false, hasOther = false;
        for (int dx = 0; dx < cw; ++dx)
            for (int dy = 0; dy < ch; ++dy) {
                CHECK(readPixel(sdl.ren, cell.col * cw + dx,
                                cell.row * ch + dy, probe));
                if (probe == cc)
                    hasBg = true;
                else
                    hasOther = true;
            }
        CHECK(hasBg);
        if (gui.hasFont()) {
            CHECK(hasOther);
        } else {
            CHECK(!hasOther);
        }
    }
#endif
}

// Bracket con fondo amarillo en dark: el fondo efectivo no contrasta con
// el cursor claro y la política usa el fallback (fondo base). El glifo
// sigue legible: la celda conserva fondo de cursor y no es sólida.
TEST(gui_sdl_cursor_block_en_bracket_usa_fallback_legible) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    if (!gui.isDarkTheme()) SKIP("caso dark: bracket vs cursor claro");
    if (!gui.hasFont()) SKIP("sin fuente");
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"int f() { return 0; }"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 0;
    cur.col = 8;  // sobre '{'
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    // Misma cadena de 21 bytes: '{' en 8, '}' en 20. Cada extremo es UN
    // carácter ([col, col+1); un EOL/fuera de rango se descarta en silencio,
    // así que el índice de cierre debe ser exacto para que el par viva.
    BracketPair pair{{0, 8}, {0, 20}};
    gui.renderScreenDiff(doc, cur, vp, "t.cpp", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, pair);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
    // Ambos lados del par llevan BracketMatch (gutter 3 + byte).
    {
        const auto& row = gui.lastContentRows()[0];
        CHECK(row.hasRole(StyleRole::BracketMatch));
        for (int byte : {8, 20}) {
            const int relCol = gutterWidth(doc.lineCount(), vp.width) + byte;
            int acc = 0;
            bool found = false;
            for (const auto& seg : row.segs) {
                const int cols = colWidth(seg.text);
                if (relCol >= acc && relCol < acc + cols) {
                    CHECK(seg.role == StyleRole::BracketMatch);
                    found = true;
                    break;
                }
                acc += cols;
            }
            CHECK(found);
        }
    }
    const int cw = gui.cellW(), ch = gui.cellH();
    const CellPos cell = gui.lastCursor().cell;
    const GuiColor cc = guichrome::cursorColor(true);
    bool hasBg = false;
    bool hasOther = false;
    for (int dx = 0; dx < cw; ++dx)
        for (int dy = 0; dy < ch; ++dy) {
            CHECK(readPixel(sdl.ren, cell.col * cw + dx, cell.row * ch + dy,
                            probe));
            if (probe == cc)
                hasBg = true;
            else
                hasOther = true;
        }
    CHECK(hasBg);
    CHECK(hasOther);  // fallback legible: el '{' se repintó, no es sólida
#endif
}

// Carácter ancho (2 columnas) bajo el bloque, con fondo que fuerza el
// fallback de contraste (bracket amarillo en dark): el resolver lo recorre
// con las mismas primitivas de celdas que el pintor (sin doble contabilidad
// por segmento). Dos capas:
//   a) Sin fuente (determinista en todos los entornos): el fill ancho-doble
//      cubre exactamente las 2 celdas del carácter, sin tocar las vecinas.
//   b) Con fuente que avance el carácter 2 celdas (si la fuente solo tiene
//      .notdef de 1 celda se skipea: sin avance doble no hay nada que
//      probar del pintado): el glifo se repinta en su posición con el fg
//      del fallback, celda por celda, sin derrame.
TEST(gui_sdl_cursor_block_en_ancho_doble_legible) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    // "a漢b": 漢 = U+6F22, 2 cols visuales (bytes E6 BC A2).
    doc.restore({std::string("a\xe6\xbc\xa2") + "b"});
    Viewport vp = makeVp(10, 40);
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    // Cada extremo identifica UN carácter ([col, col+1)): el open cubre el
    // 漢 (bytes [1,2) → visual [1,3): columnOf redondea por celda
    // consumiendo el carácter entero) y el close cubre 'b' (byte 4,
    // a propósito: las assertions de la vecina derecha dependen de su
    // fondo amarillo). El fondo amarillo no contrasta con el cursor claro
    // en dark, así el fg del glifo es el fallback y el amarillo original
    // no debe aparecer en el span.
    BracketPair pair{{0, 1}, {0, 4}};
    const GuiColor bracketBg = guichrome::guiBgFor(RowBgKind::Bracket, true);
    const GuiColor cc = guichrome::cursorColor(true);

    // a) Sin fuente: solo fills, píxel a píxel exactos. El renderer sin
    // fuente pinta fondos pero no texto: el span del 漢 debe quedar
    // sólido en color cursor en ambas celdas y las vecinas intactas.
    {
        GuiRenderer gui;  // a propósito sin initForRenderer: sin fuente.
        if (gui.isDarkTheme() != true) SKIP("caso dark");
        gui.setSdlRenderer(sdl.ren);
        CHECK(!gui.hasFont());
        const int cw = gui.cellW(), ch = gui.cellH();
        auto solidAt = [&](int c, int r, const GuiColor& want) {
            for (int dx = 0; dx < cw; ++dx)
                for (int dy = 0; dy < ch; ++dy) {
                    GuiColor g{};
                    if (!readPixel(sdl.ren, c * cw + dx, r * ch + dy, g))
                        return false;
                    if (g != want) return false;
                }
            return true;
        };
        Cursor cur;
        cur.line = 0;
        cur.col = 1;  // byte lead del 漢
        gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             std::nullopt, std::nullopt, pair);
        CHECK(gui.lastCursor().visible);
        CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
        CHECK(gui.lastContentRows()[0].hasRole(StyleRole::BracketMatch));
        const CellPos cell = gui.lastCursor().cell;
        // Fill ancho-doble: ambas celdas sólidas en cursor...
        CHECK(solidAt(cell.col, cell.row, cc));
        CHECK(solidAt(cell.col + 1, cell.row, cc));
        // ...y las vecinas conservan su fondo ('a' línea actual, 'b'
        // bracket): ni el fill ni nada se desplazó una celda.
        const GuiColor curBg =
            guichrome::currentLineBackground(true);
        CHECK(solidAt(cell.col - 1, cell.row, curBg));
        CHECK(solidAt(cell.col + 2, cell.row, bracketBg));
    }

    // b) Con fuente de avance doble real: el glifo se repinta en posición.
    // Se elige un carácter ancho (2 cols según utf8) que la fuente avance
    // 2 celdas; si ninguno (p. ej. fuente sin CJK, que sustituye por
    // .notdef de 1 celda y desalinea la fila misma de la grilla) se
    // skipea: sin avance doble no hay nada que probar del pintado acá.
    {
        GuiRenderer gui;
        gui.initForRenderer(sdl.ren);
        if (!gui.isDarkTheme()) SKIP("caso dark: bracket vs cursor claro");
        if (!gui.hasFont()) SKIP("sin fuente");
        const char* kWideCandidates[] = {
            u8"\u6F22",  // 漢 CJK
            u8"\u1100",  // ᄀ jamo hangul
            u8"\u2B1B",  // ⬛
            u8"\u2B50",  // ⭐
            u8"\u3002",  // 。
            u8"\uFF21",  // Ａ fullwidth
        };
        const int cw = gui.cellW(), ch = gui.cellH();
        std::string wide;
        for (const char* cand : kWideCandidates) {
            const std::string line = std::string("a") + cand + "b";
            if (gui.textWidthPx(line) ==
                gui.textWidthPx("a") + 2 * cw + gui.textWidthPx("b")) {
                wide = cand;
                break;
            }
        }
        if (wide.empty()) SKIP("fuente sin avance doble para anchos");
        // Premisa del resolver: el elegido es ancho-doble (3 bytes UTF-8).
        CHECK_EQ(static_cast<int>(wide.size()), 3);
        CHECK_EQ(utf8::cellWidth(wide, 0, 3), 2);
        doc.restore({std::string("a") + wide + "b"});
        BracketPair pairW{{0, 1}, {0, 1 + static_cast<int>(wide.size())}};

        auto readColumn = [&](int c, int r, std::vector<GuiColor>& out) {
            out.clear();
            for (int dx = 0; dx < cw; ++dx)
                for (int dy = 0; dy < ch; ++dy) {
                    GuiColor g{};
                    if (!readPixel(sdl.ren, c * cw + dx, r * ch + dy, g))
                        return false;
                    out.push_back(g);
                }
            return true;
        };

        // 1) Cursor sobre el lead del carácter ancho.
        Cursor cur;
        cur.line = 0;
        cur.col = 1;
        gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             std::nullopt, std::nullopt, pairW);
        CHECK(gui.lastCursor().visible);
        CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
        const CellPos cell = gui.lastCursor().cell;
        std::vector<GuiColor> curA, curB, curL, curN;
        CHECK(readColumn(cell.col, cell.row, curA));
        CHECK(readColumn(cell.col + 1, cell.row, curB));
        CHECK(readColumn(cell.col - 1, cell.row, curL));
        CHECK(readColumn(cell.col + 2, cell.row, curN));

        // 2) Referencia: misma fila y estilos, cursor aparcado en el
        // relleno (no toca el span ni las vecinas).
        Cursor park;
        park.line = 0;
        park.col = 100;  // clamp a EOL
        gui.renderScreenDiff(doc, park, vp, "t.txt", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             std::nullopt, std::nullopt, pairW);
        CHECK(gui.lastCursor().visible);
        std::vector<GuiColor> refA, refB, refL, refN;
        CHECK(readColumn(cell.col, cell.row, refA));
        CHECK(readColumn(cell.col + 1, cell.row, refB));
        CHECK(readColumn(cell.col - 1, cell.row, refL));
        CHECK(readColumn(cell.col + 2, cell.row, refN));

        // Sin derrame ni desplazamiento fuera del span: vecinas idénticas.
        CHECK(refL == curL);
        CHECK(refN == curN);
        // Premisa no vacua: la referencia tenía amarillo en el span.
        {
            bool anyYellow = false;
            for (const auto& v : {refA, refB})
                for (const GuiColor& g : v)
                    if (g == bracketBg) anyYellow = true;
            CHECK(anyYellow);
        }
        // Glifo en posición, celda por celda: fondo de cursor + tinta del
        // fallback en ambas (un glifo desplazado una celda dejaría una
        // sólida) y cero amarillo original (cobertura total del fill).
        for (const auto& v : {curA, curB}) {
            bool anyBg = false, anyInk = false, anyYellow = false;
            for (const GuiColor& g : v) {
                if (g == cc)
                    anyBg = true;
                else
                    anyInk = true;
                if (g == bracketBg) anyYellow = true;
            }
            CHECK(anyBg);
            CHECK(anyInk);
            CHECK(!anyYellow);
        }
    }
#endif
}

// Gutter + cursor en letra no inicial: el glifo repintado es el de la
// celda del cursor, sin doble conteo del gutter. relCol y segmentos viven
// en el mismo marco con origen en content.col (gutter incluido en ambos);
// restar el gutter además del origen —o saltearlo en el recorrido—
// desplazaría el glifo una celda. Se caza comparando la máscara de tinta
// contra una referencia con el cursor aparcado: mismo conjunto de píxeles
// de tinta en las mismas coordenadas (los colores se invierten, la forma
// no se mueve).
TEST(gui_sdl_cursor_block_gutter_no_desplaza_glifo) {
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
    Document doc;
    doc.restore({"abcdef"});  // ASCII: byte == columna visual
    Viewport vp = makeVp(10, 40);
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    const bool dark = gui.isDarkTheme();
    // Fila actual sin sintaxis/selección: rol CurrentLine en todo el texto.
    const GuiColor cellBg = guichrome::currentLineBackground(dark);
    const GuiColor cc = guichrome::cursorColor(dark);
    const int cw = gui.cellW(), ch = gui.cellH();

    auto readColumn = [&](int c, int r, std::vector<GuiColor>& out) {
        out.clear();
        for (int dx = 0; dx < cw; ++dx)
            for (int dy = 0; dy < ch; ++dy) {
                GuiColor g{};
                if (!readPixel(sdl.ren, c * cw + dx, r * ch + dy, g))
                    return false;
                out.push_back(g);
            }
        return true;
    };
    auto inkMask = [&](const std::vector<GuiColor>& v, const GuiColor& bg) {
        std::vector<char> m;
        m.reserve(v.size());
        for (const GuiColor& g : v) m.push_back(g != bg);
        return m;
    };

    // Cursor sobre 'd' (4ª letra, byte 3): celda = gutter + 3.
    Cursor cur;
    cur.line = 0;
    cur.col = 3;
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
    CHECK(gui.lastContentRows()[0].isCurrentLine);
    const CellPos cell = gui.lastCursor().cell;
    CHECK_EQ(cell.col, gutterWidth(doc.lineCount(), vp.width) + 3);
    std::vector<GuiColor> curPx;
    CHECK(readColumn(cell.col, cell.row, curPx));

    // Referencia: misma fila y estilos, cursor aparcado en el relleno.
    Cursor park;
    park.line = 0;
    park.col = 100;  // clamp a EOL
    gui.renderScreenDiff(doc, park, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(gui.lastCursor().visible);
    std::vector<GuiColor> refPx;
    CHECK(readColumn(cell.col, cell.row, refPx));

    // Misma máscara de tinta en las mismas coordenadas: el glifo repintado
    // es 'd' en su posición. Comparación con tolerancia acotada (no
    // igualdad exacta): SDL_ttf puede rasterizar 1px distinto el mismo
    // glifo en-cadena (textura de la fila, con kerning/redondeo de avances
    // acumulados) que aislado (textura del cursor) —observado: 1px de 190—.
    // Un desplazamiento real por doble conteo del gutter movería la máscara
    // una celda entera (decenas de píxeles) o la dejaría sólida, muy por
    // encima de la cota.
    {
        CHECK_EQ(refPx.size(), curPx.size());
        const auto rm = inkMask(refPx, cellBg);
        const auto cm = inkMask(curPx, cc);
        int refInk = 0, mismatch = 0;
        for (size_t i = 0; i < rm.size(); ++i) {
            refInk += rm[i] ? 1 : 0;
            if (rm[i] != cm[i]) ++mismatch;
        }
        CHECK(refInk > 10);   // premisa no vacua: la 'd' tiene tinta real
        CHECK(mismatch <= 4);  // 1px de jitter SDL_ttf sí; una celda no
    }
    // No vacua en fondo tampoco: la celda no es sólida en ningún render.
    {
        bool refBg = false, curBg = false;
        for (const GuiColor& g : refPx)
            if (g == cellBg) refBg = true;
        for (const GuiColor& g : curPx)
            if (g == cc) curBg = true;
        CHECK(refBg);
        CHECK(curBg);
    }
#endif
}

// Cursor en lista modal: cae en el espacio líder ("  nombre", col 0 del
// contenido), así el bloque es fill sólido. Cubre la rama de modales del
// resolver de punta a punta (DTO + píxeles): el cursor nunca va sobre una
// letra en estas pantallas (col fija del contenido).
TEST(gui_sdl_lista_cursor_en_espacio_es_fill_solido) {
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
    gui.renderBufferList({"a.txt", "b.txt"}, 1, 40, 10, sink);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
    const Layout lo = gui.lastLayout();
    const CellPos cell = gui.lastCursor().cell;
    CHECK_EQ(cell.col, lo.content.col);  // espacio líder, nunca sobre letra
    CHECK_EQ(cell.row, lo.content.row + 1);
    CHECK_EQ(gui.lastListSelected(), 1);
    CHECK_EQ(gui.lastListLines()[1].text, "  b.txt");
    CHECK(!gui.lastListLines()[1].filler);
    const int cw = gui.cellW(), ch = gui.cellH();
    const GuiColor cc = guichrome::cursorColor(gui.isDarkTheme());
    for (int dx = 0; dx < cw; ++dx)
        for (int dy = 0; dy < ch; ++dy) {
            CHECK(readPixel(sdl.ren, cell.col * cw + dx, cell.row * ch + dy,
                            probe));
            CHECK(probe == cc);
        }
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
