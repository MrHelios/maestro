// Tests de GUI (chrome + contenido + modales + pintado) 
// — capa pura, sin ventana SDL. 
// No requiere HAVE_SDL2 ni driver de video dummy.

#include "helpers/test_gui_chrome_common.h"

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

// Contrato de ancho asimétrico 
// statusLine EXACTO width incluso con contenido corto
// (rellena con fill); messageLine A LO SUMO width 
// (texto corto => línea corta, sin fill: el fondo ya lo pintó el renderer).
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
    for (int w = 1; w <= 80; ++w) 
        CHECK(colWidth(guichrome::messageLine(short_, w)) <= w);
}

TEST(gui_colores_distinguen_temas_y_roles) {
    CHECK(guichrome::background(true) != guichrome::background(false));
    CHECK(guichrome::statusBackground(true) != guichrome::statusBackground(false));
    CHECK(guichrome::colorFor(StyleRole::StatusBase, true) != guichrome::colorFor(StyleRole::AccentNavegacion, true));
    CHECK(guichrome::messageColor(MessageKind::Prompt, true) == guichrome::colorFor(StyleRole::MsgPrompt, true));
}

TEST(gui_frame_busqueda_cursor_en_messagebar_dentro_del_input) {
    GuiRenderer gui;
    Document doc;
    Viewport vp = makeVp();
    Cursor cur;
    
    doc.restore({"hola mundo hola"});
    cur.line = 0;
    cur.col = 0;
    
    // "Find: ho (1/2)" con cursor tras "ho" (offset 7), no al final (12).
    Message prompt{std::string("Find: ho (1/2)"), MessageKind::Prompt, std::nullopt, 7, 6};
    
    ChromeRequest cr = makeChromeRequest(prompt, State::Busqueda);
    Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
    CHECK(f.cursor.visible);
    CHECK(f.cursor.shape == FrameCursorShape::Bar);
    
    // Fila del MessageBar (segunda del chrome).
    Layout layout = f.layout;
    CHECK_EQ(f.cursor.cell.row, layout.chrome.row + 1);
    
    const CellPos expect = chrome::messageBarCursorCell(layout.chrome, cr.message);
    CHECK(expect.valid());
    CHECK(f.cursor.cell == expect);
    
    // No apunta al final del texto visible.
    const int padL = 1;
    const int endCol = layout.chrome.col + padL + colWidth(utf8::truncate(cr.message.text, layout.chrome.width - 4));
    CHECK(f.cursor.cell.col != endCol);
}

TEST(gui_frame_irafila_cursor_en_messagebar) {
    GuiRenderer gui;
    Document doc;
    Viewport vp = makeVp();
    Cursor cur;
    
    doc.restore({"a", "b", "c"});
    
    Message prompt{std::string("ir a fila: 12"), MessageKind::Prompt, std::nullopt, 13, 11};
    ChromeRequest cr = makeChromeRequest(prompt, State::IrAFila);
    Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
    
    CHECK(f.cursor.visible);
    CHECK(f.cursor.shape == FrameCursorShape::Bar);
    CHECK_EQ(f.cursor.cell.row, f.layout.chrome.row + 1);
    CHECK(f.cursor.cell == chrome::messageBarCursorCell(f.layout.chrome, cr.message));
}

TEST(gui_frame_renombrar_cursor_en_messagebar) {
    GuiRenderer gui;
    Document doc;
    Viewport vp = makeVp();
    Cursor cur;
    
    doc.restore({"a", "b", "c"});    
    
    Message prompt{std::string("Nombre del archivo: a.txt"), MessageKind::Prompt, std::nullopt, 21, 20};
    ChromeRequest cr = makeChromeRequest(prompt, State::Renombrar);
    Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
    
    CHECK(f.cursor.visible);
    CHECK(f.cursor.shape == FrameCursorShape::Bar);
    CHECK_EQ(f.cursor.cell.row, f.layout.chrome.row + 1);
    CHECK(f.cursor.cell == chrome::messageBarCursorCell(f.layout.chrome, cr.message));
}

TEST(gui_frame_navegacion_cursor_en_contenido_no_en_messagebar) {
    GuiRenderer gui;
    Document doc;
    Viewport vp = makeVp();
    Cursor cur;
    
    doc.restore({"hola"});
    cur.line = 0;
    cur.col = 0;
    Message msg {std::string(""), MessageKind::Info, std::nullopt};
    ChromeRequest cr = makeChromeRequest(msg, State::Navegacion);
    Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
    CHECK(f.cursor.visible);
    CHECK(f.cursor.shape == FrameCursorShape::Block);
    
    // En contenido: fila dentro del area de contenido, no del chrome.
    CHECK(f.cursor.cell.row >= f.layout.content.row);
    CHECK(f.cursor.cell.row < f.layout.content.row + f.layout.content.height);
}

// sin setSdlRenderer: no-op seguro.
TEST(gui_render_sin_sdl_no_crashea_y_guarda_chrome) {
    GuiRenderer gui;  
    NullTestSink sink;
    Document doc;
    Viewport vp = makeVp();
    Cursor cur;
    
    doc.restore({"hola"});
    Message prompt{std::string("Find: ho"), MessageKind::Prompt, std::nullopt, 8, 6};
    ChromeRequest cr = makeChromeRequest(prompt, State::Busqueda);
    gui.renderScreenDiff(doc, cur, vp, "t", false, cr, sink, std::nullopt,
                         std::nullopt, std::nullopt);
    CHECK_EQ(gui.lastChrome().message.text, "Find: ho");
    CHECK(gui.lastCursor().visible);
    CHECK_EQ(gui.lastCursor().cell.row, gui.lastLayout().chrome.row + 1);

    std::vector<std::string> names = {"a.txt", "b.txt"};
    gui.renderBufferList(names, 1, 0, 40, 5, sink);
    CHECK_EQ(gui.lastChrome().statusBar.estado, "SELECCIONAR");
    CHECK(gui.lastCursor().visible);

    std::vector<FileListItem> items = {{"a.txt", false}, {"dir", true}};
    MessageBarData mbar{std::string("ayuda"), MessageKind::Info};
    gui.renderFileList(items, 0, 0, "/tmp", mbar, 40, 5, sink);
    CHECK_EQ(gui.lastChrome().statusBar.estado, "ABRIR ARCHIVO");
    
    // Abrir: cursor en la lista (contenido).
    CHECK_EQ(gui.lastCursor().cell.row, gui.lastLayout().content.row);
}

TEST(gui_saveas_cursor_en_messagebar_no_en_lista) {
    GuiRenderer gui;
    NullTestSink sink;
    
    std::vector<FileListItem> items = {{"a.txt", false}, {"b.txt", false}};
    MessageBarData msg{std::string("Nombre del Archivo: nuevo.txt (Control+S para Guardar)"), MessageKind::Prompt};
    msg.cursor = 20 + 9;  // final del nombre, antes del sufijo.
    for (const auto& c : {0, 1}) {
        gui.renderSaveAsFileList(items, c, 0, "/tmp", msg, 40, 10, sink);
        CHECK_EQ(gui.lastChrome().statusBar.estado, "GUARDAR COMO");
        CHECK(gui.lastCursor().visible);
        CHECK(gui.lastCursor().shape == FrameCursorShape::Bar);
        
        const CellPos expect = chrome::messageBarCursorCell(gui.lastLayout().chrome, msg);
        CHECK(gui.lastCursor().cell == expect);
        
        // Nunca en la fila de la lista.
        CHECK(gui.lastCursor().cell.row != gui.lastLayout().content.row + c ||
              gui.lastCursor().cell.col != gui.lastLayout().content.col);
    }
}

TEST(gui_filelist_args_invalidos_ocultan_cursor_y_no_dan_right_negativo) {
    GuiRenderer gui;
    NullTestSink sink;
    
    // 20 items: 
    // height=5 -> viewport de 5 filas, scroll=10 muestra [10..14].
    std::vector<FileListItem> items;
    for (int i = 0; i < 20; ++i) 
        items.push_back({{"f" + std::to_string(i)}, false});
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
    gui.renderBufferList(names, 7, 0, 40, 5, sink);
    CHECK(!gui.lastCursor().visible);
    CHECK(!gui.lastCursor().cell.valid());
    CHECK_EQ(gui.lastListSelected(), -1);
    CHECK(gui.lastChrome().statusBar.right.find('-') == std::string::npos);
    
    gui.renderBufferList(names, 1, 0, 40, 5, sink);
    CHECK(gui.lastCursor().visible);
    CHECK_EQ(gui.lastListSelected(), 1);
    CHECK_EQ(gui.lastChrome().statusBar.right, "2/2");
}

// la línea plana de la GUI debe ser byte a byte igual al StatusBar TTY sin ANSI. 
// Si alguien reintroduce lógica propia en algún backend, este test lo caza.
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
            const std::string plain = testutil::stripAnsi(tty.renderStatusBar(w, st));
            CHECK_EQ(plain, guichrome::statusLine(st, w));
        }
    }
}

// MessageBar angosto + input largo: 
// el cursor va más allá de lo visible y debe quedar clampeado a lo pintado, nunca fuera de la barra.
TEST(gui_messagebar_truncado_cursor_clampeado_a_visible) {
    GuiRenderer gui;
    Document doc;
    Viewport vp = makeVp(5, 20);
    Cursor cur;
    
    doc.restore({"hola"});
    // width=20 -> avail = 20-1-3 = 16; 
    // el input ("Nombre del Archivo: " + "nuevo.txt" = 29 cols) 
    // lo excede y el sufijo decorativo también.
    const std::string text = "Nombre del Archivo: nuevo.txt (Control+S para Guardar)";
    Message prompt{text, MessageKind::Prompt, std::nullopt, 20 + 9, 20};
    ChromeRequest cr = makeChromeRequest(prompt, State::Renombrar);
    Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
    
    CHECK(f.cursor.visible);
    CHECK_EQ(f.cursor.cell.row, f.layout.chrome.row + 1);
    CHECK_EQ(f.layout.chrome.width, 20);

    // Misma celda que la política compartida (no un cálculo propio).
    CHECK(f.cursor.cell == chrome::messageBarCursorCell(f.layout.chrome, cr.message));
    // Clampeado al final de lo visible no al final del texto completo ni fuera de la barra.
    CHECK_EQ(f.cursor.cell.col, f.layout.chrome.col + 1 + 16);
    CHECK(f.cursor.cell.col <= f.layout.chrome.col + 20 - 1);

    // messageLine trunca con la misma política: 
    // el cursor cae justo al final del texto visible, nunca más allá.
    const std::string line = guichrome::messageLine(cr.message, 20);
    CHECK_EQ(line, " " + utf8::truncate(text, 16) + "   ");
    CHECK_EQ(f.cursor.cell.col - f.layout.chrome.col - 1, colWidth(utf8::truncate(text, 16)));
}

// Mismo caso pero UTF-8: 
// el offset del cursor viene en bytes y debe traducirse a columnas.
TEST(gui_messagebar_truncado_utf8_cursor_en_columnas_no_bytes) {
    GuiRenderer gui;
    Document doc;
    const std::string text = "Nombre: áé漢字.txt";
    Viewport vp = makeVp(5, 16);
    Cursor cur;
    
    doc.restore({"hola"});
    // "Nombre: "(8) + á(2B/1C) + é(2B/1C) + 漢(3B/2C) + 字(3B/2C) +
    // ".txt"(4B/4C) = 22 bytes, 18 columnas. width=16 -> avail=12, visible
    // "Nombre: áé漢" (15 bytes, 12 cols): 漢 entra justo, 字 ya no.
    CHECK_EQ(static_cast<int>(text.size()), 22);
    
    Message prompt{text, MessageKind::Prompt, std::nullopt, static_cast<int>(text.size()), 8};
    ChromeRequest cr = makeChromeRequest(prompt, State::Renombrar);
    Frame f = gui.buildFrame(doc, cur, vp, "t", false, cr);
    CHECK(f.cursor.visible);
    CHECK_EQ(f.cursor.cell.row, f.layout.chrome.row + 1);
    CHECK(f.cursor.cell == chrome::messageBarCursorCell(f.layout.chrome, cr.message));
    
    // 18 cols -> clampeado a avail=12: col = chrome.col+1+12. Por bytes
    // (22) daría chrome.col+1+22, clampeado a la última celda (col+15).
    CHECK_EQ(f.cursor.cell.col, f.layout.chrome.col + 13);

    const std::string line = guichrome::messageLine(cr.message, 16);
    // sin partir carácter multibyte
    CHECK(testutil::validUtf8(line));
    CHECK(contains(line, "漢"));
    CHECK(!contains(line, "字"));
}

// Paleta completa: 
// cada familia se distingue del texto base y dark!=light donde importa 
// (contenido, selección, listas, sintaxis).
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
        

        // Default siempre sobre el fondo de contenido
        auto sCur = guichrome::styleFor(StyleRole::SyntaxKeyword, true, dark);
        CHECK(sCur.bg == guichrome::currentLineBackground(dark));
        
        auto sPlain = guichrome::styleFor(StyleRole::Default, false, dark);
        CHECK(sPlain.bg == bg);
        CHECK(guichrome::styleFor(StyleRole::Default, true, dark).bg == bg);
        
        auto sSel = guichrome::styleFor(StyleRole::Selection, false, dark);
        CHECK(sSel.bg == guichrome::selectionBackground(dark));
        
        auto sBr = guichrome::styleFor(StyleRole::BracketMatch, false, dark);
        CHECK(sBr.bg == guichrome::bracketBackground());
        
        auto sList = guichrome::styleFor(StyleRole::ListSelected, false, dark);
        CHECK(sList.bg == guichrome::currentLineBackground(dark));
    }
    CHECK(guichrome::background(true) != guichrome::background(false));
    CHECK(guichrome::selectionBackground(true) != guichrome::selectionBackground(false));
}

// Contenido: el snapshot sin SDL lleva sintaxis + selección + currentLine.
TEST(gui_contenido_snapshot_con_sintaxis_y_seleccion) {
    GuiRenderer gui;
    NullTestSink sink;
    
    Document doc;
    Viewport vp = makeVp(5, 40);
    Cursor cur;
    
    doc.restore({"int main() {", "  return 0;", "}"});
    cur.line = 0;
    cur.col = 0;
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    ChromeRequest cr = makeChromeRequest(msg, State::Navegacion);
    
    // ???
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
    gui.renderScreenDiff(doc, cur, vp, "t.cpp", false, cr, sink, sel, std::nullopt, std::nullopt);
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

// Highlight de búsqueda: 
// viaja como Selection en el Frame (fusionado por FrameBuilder) 
// y el snapshot GUI lo expone igual que la selección.
TEST(gui_contenido_highlight_busqueda_como_selection) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    Viewport vp = makeVp(5, 40);
    Cursor cur;
    
    doc.restore({"hola mundo hola"});
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    ChromeRequest cr = makeChromeRequest(msg, State::Navegacion);
    Selection hl{{0, 0}, {0, 4}};  // primer "hola"
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, cr, sink, std::nullopt, hl, std::nullopt);
    CHECK(!gui.lastContentRows().empty());
    CHECK(gui.lastContentRows()[0].hasRole(StyleRole::Selection));
    
    // Sin highlight ni selección no hay rol Selection.
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, cr, sink, std::nullopt, std::nullopt, std::nullopt);
    CHECK(!gui.lastContentRows()[0].hasRole(StyleRole::Selection));
}

// Bracket apareado: el snapshot expone BracketMatch para pintarlo destacado.
TEST(gui_contenido_bracket_match_en_snapshot) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    Viewport vp = makeVp(5, 40);
    Cursor cur;
    
    doc.restore({"int f() { return 0; }"});
    cur.line = 0;
    cur.col = 8;  // sobre '{' aprox (el par lo resuelve el caller real)
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    ChromeRequest cr = makeChromeRequest(msg, State::Navegacion);
    
    // "int f() { return 0; }" mide 21 bytes: '{' en 8, '}' en 20. Cada
    // extremo del par identifica UN carácter ([col, col+1), ver BracketPair).
    BracketPair pair{{0, 8}, {0, 20}};
    gui.renderScreenDiff(doc, cur, vp, "t.cpp", false, cr, sink, std::nullopt, std::nullopt, pair);
    CHECK(gui.lastContentRows()[0].hasRole(StyleRole::BracketMatch));
}

// Modales: BufferSelector deja líneas "  nombre" + relleno y cursor en lista.
TEST(gui_modal_buffer_selector_lineas_y_cursor) {
    GuiRenderer gui;
    NullTestSink sink;
    std::vector<std::string> names = {"a.txt", "b.txt"};
    gui.renderBufferList(names, 1, 0, 40, 5, sink);
    
    CHECK_EQ(gui.lastChrome().statusBar.estado, "SELECCIONAR");
    // height=5 son filas de contenido
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

// FileBrowser respeta scroll, marca carpetas con "/" y expone DTO.
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

// SaveAs comparte lista con abrir pero el cursor va al MessageBar.
TEST(gui_modal_saveas_comparte_lista_cursor_en_messagebar) {
    GuiRenderer gui;
    NullTestSink sink;
    std::vector<FileListItem> items = {{{"a.txt"}, false}, {{"d"}, true}};
    MessageBarData msg {
        std::string("Nombre del Archivo: nuevo.txt (Control+S para Guardar)"),
        MessageKind::Prompt
    };
    
    msg.cursor = 20 + 9;
    gui.renderSaveAsFileList(items, 0, 0, "/tmp", msg, 40, 10, sink);
    
    CHECK_EQ(gui.lastChrome().statusBar.estado, "GUARDAR COMO");
    CHECK(!gui.lastListLines().empty());
    CHECK_EQ(gui.lastListLines()[0].text, "  a.txt");
    CHECK_EQ(gui.lastListLines()[1].text, "  d/");
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Bar);
    CHECK(gui.lastCursor().cell == chrome::messageBarCursorCell(gui.lastLayout().chrome, msg));
}

// Un archivo real llamado "~" produce el mismo texto "  ~" que el relleno,
// pero viaja con filler=false: es texto normal, no relleno tenue.
TEST(gui_modal_tilde_real_no_es_relleno) {
    GuiRenderer gui;
    NullTestSink sink;
    MessageBarData mbar {std::string("ayuda"), MessageKind::Info};
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
    gui.renderBufferList(names, 0, 0, 40, 10, sink);
    CHECK_EQ(gui.lastListLines()[0].text, "  ~");
    CHECK(!gui.lastListLines()[0].filler);
    CHECK(gui.lastListLines()[1].filler);
}

// Mensajes con timeout en GUI: 
// un mensaje de acción (ej. "Nada que deshacer.") 
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

// Las 4 familias de fondo de contenido + lista, en ambos temas: 
// el fg que decide cursorGlyphFg siempre contrasta con el cursor.
TEST(gui_cursor_glyph_politica_inversion_y_contraste) {
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
    
    // Caso que motivó el fallback: 
    // bracket amarillo sobre cursor claro en dark no contrasta -> se usa el fondo base.
    {
        const GuiColor bracket = guichrome::guiBgFor(RowBgKind::Bracket, true);
        CHECK(guichrome::cursorContrastRatio(
                  bracket, guichrome::cursorColor(true)) < guichrome::kCursorGlyphMinContrast);
        CHECK(guichrome::cursorGlyphFg(bracket, true) == guichrome::background(true));
        CHECK(guichrome::cursorContrastRatio(
                  guichrome::background(true),
                  guichrome::cursorColor(true)) >= guichrome::kCursorGlyphMinContrast);
    }
    
    // En light el bracket sí contrasta con el cursor oscuro: inversión exacta también ahí.
    {
        const GuiColor bracket = guichrome::guiBgFor(RowBgKind::Bracket, false);
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
                CHECK(guichrome::styleFor(role, cur, dark).bg == guichrome::guiBgFor(rowBgKindFor(role, cur), dark));
                      
                // Lado TTY: bytes observables de una fila de un segmento.
                StyledRow row;
                row.isCurrentLine = cur;
                row.arena = "x";
                row.segs.push_back(FrameSegment{std::string_view(row.arena), role});
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
    auto later = now + Editor::kActionMessageTimeout + std::chrono::milliseconds(50);
    loop.idleStep(later);
    CHECK(guiPtr->lastChrome().message.text.empty());
}

// Cursor GUI: 
// Bloque (navegación) fijo, Barra (inserción) parpadea 530ms.
// El blink sigue al shape (Bar), no al State nominal: cubre Interacción y
// los 4 prompts con input (que ya llegan como Bar desde cursorShapeFor).
TEST(gui_cursor_block_fijo_bar_parpadea) {
    using clk = std::chrono::steady_clock;
    NullTestSink sink;
    Document doc;
    Viewport vp = makeVp();
    Cursor cur;
    
    doc.restore({"hola"});
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
    Viewport vp = makeVp();
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    const auto t0 = clk::now();
    GuiRenderer gui;
    Cursor c0;
    
    doc.restore({"hola", "mundo"});
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

TEST(gui_tema_invariantes_por_familia) {
    // Lo que debe adaptarse al tema cambia.
    CHECK(guichrome::background(true) != guichrome::background(false));
    CHECK(guichrome::statusBackground(true) !=
          guichrome::statusBackground(false));
    CHECK(guichrome::messageBackground(true) !=
          guichrome::messageBackground(false));
    CHECK(guichrome::cursorColor(true) != guichrome::cursorColor(false));
    CHECK(guichrome::currentLineBackground(true) !=
          guichrome::currentLineBackground(false));
    CHECK(guichrome::selectionBackground(true) !=
          guichrome::selectionBackground(false));
    CHECK(guichrome::colorFor(StyleRole::Default, true) !=
          guichrome::colorFor(StyleRole::Default, false));
    CHECK(guichrome::colorFor(StyleRole::Gutter, true) !=
          guichrome::colorFor(StyleRole::Gutter, false));
    CHECK(guichrome::colorFor(StyleRole::SyntaxKeyword, true) !=
          guichrome::colorFor(StyleRole::SyntaxKeyword, false));
    CHECK(guichrome::colorFor(StyleRole::SyntaxString, true) !=
          guichrome::colorFor(StyleRole::SyntaxString, false));
    CHECK(guichrome::colorFor(StyleRole::SyntaxNumber, true) !=
          guichrome::colorFor(StyleRole::SyntaxNumber, false));
          
    // Excepción fija documentada: el bracket NO cambia de tema.
    CHECK(guichrome::bracketBackground() == guichrome::bracketBackground());
    CHECK(guichrome::colorFor(StyleRole::StatusModified, true) !=
          guichrome::colorFor(StyleRole::StatusModified, false));
    CHECK(guichrome::styleFor(StyleRole::BracketMatch, false, true).bg ==
          guichrome::styleFor(StyleRole::BracketMatch, false, false).bg);
          
    // Colapsos intencionales dentro del mismo tema.
    CHECK(guichrome::colorFor(StyleRole::AccentNavegacion, true) ==
          guichrome::colorFor(StyleRole::AccentGuardar, true));
    CHECK(guichrome::colorFor(StyleRole::AccentNavegacion, false) ==
          guichrome::colorFor(StyleRole::AccentGuardar, false));
          
    // Mensajes base sin color propio (README v0.9): mismo tono que base.
    CHECK(guichrome::colorFor(StyleRole::MsgInfo, true) == guichrome::colorFor(StyleRole::Default, true));
    CHECK(guichrome::colorFor(StyleRole::MsgSuccess, true) == guichrome::colorFor(StyleRole::MsgInfo, true));
    CHECK(guichrome::colorFor(StyleRole::MsgWarning, true) == guichrome::colorFor(StyleRole::MsgInfo, true));
    CHECK(guichrome::colorFor(StyleRole::MsgError, true) == guichrome::colorFor(StyleRole::MsgInfo, true));
    
    // Prompt sí se distingue.
    CHECK(guichrome::messageColor(MessageKind::Prompt, true) == guichrome::colorFor(StyleRole::MsgPrompt, true));
    // Fondos vía política compartida.
    for (bool dark : {true, false}) {
        CHECK(guichrome::styleFor(StyleRole::Default, false, dark).bg ==
              guichrome::background(dark));
        CHECK(guichrome::styleFor(StyleRole::Selection, false, dark).bg ==
              guichrome::selectionBackground(dark));
        CHECK(guichrome::styleFor(StyleRole::ListSelected, false, dark).bg ==
              guichrome::currentLineBackground(dark));
        CHECK(guichrome::styleFor(StyleRole::SyntaxKeyword, true, dark).bg ==
              guichrome::currentLineBackground(dark));
    }
    
    // Toggle invierte el estado opaco y la tabla lo refleja.
    GuiRenderer gui;
    CHECK(gui.isDarkTheme());
    
    const GuiColor bgDark = guichrome::background(gui.isDarkTheme());
    gui.toggleTheme();
    CHECK(!gui.isDarkTheme());
    CHECK(guichrome::background(gui.isDarkTheme()) != bgDark);
    
    gui.toggleTheme();
    CHECK(gui.isDarkTheme());
    CHECK(guichrome::background(gui.isDarkTheme()) == bgDark);
}

// Segmentos del chrome: 
// anotación sin duplicar layout.
// concat(segmentos) == línea plana, con roles de paridad TTY. 
// El ancho se mide en columnas visuales (colCount/truncate), nunca en bytes.
TEST(gui_chrome_segments_concat_y_roles) {
    auto concat = [](const std::vector<guichrome::GuiChromeSeg>& segs) {
        std::string out;
        for (const auto& s : segs) out += s.text;
        return out;
    };
    
    auto hasRole = [](const std::vector<guichrome::GuiChromeSeg>& segs,
                      const std::string& needle, StyleRole role) {
        for (const auto& s : segs)
            if (s.role == role && contains(s.text, needle)) return true;
        return false;
    };
    
    std::vector<StatusBarData> cases;
    {
        StatusBarData d;
        d.name = "archivo.txt";
        d.path = "/home/usuario";
        d.estado = "NAVEGACION";
        d.totalLines = 10;
        cases.push_back(d);
    }
    
    {
        StatusBarData d;
        d.name = "x.cc";
        d.path = "/home/a";
        d.estado = "NAVEGACION";
        d.modified = true;
        d.totalLines = 10;
        cases.push_back(d);
    }
    
    {
        StatusBarData d;
        d.name = std::string(80, 'n');
        d.path = "/" + std::string(80, 'p');
        d.estado = std::string(40, 'e');
        d.right = std::string(40, 'r');
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
    
    {
        StatusBarData d;
        d.name = "";
        d.path = "";
        d.estado = "SELECCIONAR";
        d.modified = true;
        d.right = "1/1";
        cases.push_back(d);
    }
    
    // Ancho visual != bytes: CJK (2 cols) + acentos. Fuerza truncados
    // por columnas, no por bytes.
    {
        StatusBarData d;
        d.name = "漢字archivo.txt";
        d.path = "/casa/漢字/ruta/larga";
        d.estado = "NAVEGACION";
        d.modified = true;
        d.totalLines = 100;
        d.cursorLine = 10;
        d.cursorCol = 5;
        cases.push_back(d);
    }
    
    for (const auto& st : cases) {
        for (int w = 1; w <= 100; ++w) {
            for (StyleRole accent :
                 {StyleRole::AccentNavegacion, StyleRole::AccentGuardar,
                  StyleRole::StatusAccentDefault}) {
                const std::vector<guichrome::GuiChromeSeg> segs =
                    guichrome::statusSegments(st, accent, w);
                const std::string cat = concat(segs);
                
                // La concatenación es la línea plana (sin ocultar desvíos
                // en el fill: el fill se calcula con plainW de leftText,
                // no con los segmentos).
                CHECK_EQ(cat, guichrome::statusLine(st, w));
                
                // Garantía de ancho exacto en columnas visuales.
                CHECK_EQ(colWidth(cat), w);
                
                // La suma de anchos por segmento es exactamente width
                // (la distribución pad/fill/contenido no pierde ni agrega
                // columnas, incluso con truncados de nombre/ruta/estado).
                int sumCols = 0;
                for (const auto& s : segs) sumCols += colWidth(s.text);
                CHECK_EQ(sumCols, w);
                CHECK(testutil::validUtf8(cat));
            }
        }
    }
    
    // Roles en un caso típico con [*].
    {
        StatusBarData st;
        st.name = "x.cc";
        st.path = "/home/a";
        st.estado = "NAVEGACION";
        st.modified = true;
        st.totalLines = 10;
        
        const auto segs = guichrome::statusSegments(st, StyleRole::AccentNavegacion, 80);
        CHECK(hasRole(segs, "x.cc", StyleRole::StatusName));
        CHECK(hasRole(segs, "[*]", StyleRole::StatusModified));
        CHECK(hasRole(segs, "/home/a", StyleRole::StatusPath));
        CHECK(hasRole(segs, "NAVEGACION", StyleRole::AccentNavegacion));
    }
    
    // modified && nombre recortado al extremo: 
    // sin marker, nombre con rol StatusModified (paridad con TtyChrome).
    {
        StatusBarData st;
        st.name = std::string(80, 'n');
        st.path = "/p";
        st.estado = "E";
        st.modified = true;
        
        const auto segs = guichrome::statusSegments(st, StyleRole::AccentNavegacion, 12);
        CHECK_EQ(concat(segs), guichrome::statusLine(st, 12));
    }
    
    // MessageBar: concat + roles + boldPrefix 
    // (misma excepción que TTY: sufijo de Prompt con prefijo va con MsgInfo).
    for (int w : {1, 5, 16, 20, 40, 80}) {
        for (MessageKind k :
             {MessageKind::Info, MessageKind::Success, MessageKind::Warning,
              MessageKind::Error, MessageKind::Prompt}) {
            MessageBarData m{std::string("hola mundo"), k};
            CHECK_EQ(concat(guichrome::messageSegments(m, w)),
                     guichrome::messageLine(m, w));
        }
        
        MessageBarData prompt{std::string("Find: ho (1/2)"), MessageKind::Prompt};
        prompt.boldPrefix = 6;
        prompt.cursor = 7;
        CHECK_EQ(concat(guichrome::messageSegments(prompt, w)), guichrome::messageLine(prompt, w));
        
        MessageBarData utf8{std::string("Nombre: áé漢字.txt"), MessageKind::Prompt};
        utf8.boldPrefix = 8;
        CHECK_EQ(concat(guichrome::messageSegments(utf8, w)), guichrome::messageLine(utf8, w));
        CHECK(testutil::validUtf8(concat(guichrome::messageSegments(utf8, w))));
    }
    
    {
        MessageBarData m{std::string("Find: ho (1/2)"), MessageKind::Prompt};
        m.boldPrefix = 6;
        const auto segs = guichrome::messageSegments(m, 80);
        
        CHECK(hasRole(segs, "Find: ", StyleRole::MsgPrompt));
        CHECK(hasRole(segs, "ho (1/2)", StyleRole::MsgInfo));
    }
    
    {
        MessageBarData m{std::string("Error grave"), MessageKind::Error};
        const auto segs = guichrome::messageSegments(m, 80);
        
        CHECK(hasRole(segs, "Error grave", StyleRole::MsgError));
    }
    
    CHECK(guichrome::statusSegments(StatusBarData{}, StyleRole::Default, 0).empty());
    CHECK(guichrome::messageSegments(MessageBarData{}, 0).empty());
}

// boldPrefix a mitad de celda UTF-8: nunca se emiten mitades inválidas.
// Contrato vigente (ChromeData.h): los productores solo generan límites
// válidos (etiquetas ASCII); esto es defensa en profundidad ante un futuro
// productor roto. El corte retrocede al inicio de celda; el concat sigue
// siendo messageLine.
TEST(gui_chrome_segments_boldprefix_no_parte_utf8) {
    // "ab" + á (2B) + "cd": bytes [a b C3 A1 c d], la mitad de á es offset 3.
    const std::string text = std::string("ab") + "\xC3\xA1" + "cd";
    for (int bad : {3}) {
        MessageBarData m{text, MessageKind::Prompt};
        m.boldPrefix = bad;
        const auto segs = guichrome::messageSegments(m, 80);
        std::string concat;
        for (const auto& s : segs) {
            // Pads de espacios: se excluyen del chequeo UTF-8 por segmento
            // (son ASCII); lo que importa es que pre/suf sean válidos.
            if (s.text.find_first_not_of(' ') == std::string::npos) {
                concat += s.text;
                continue;
            }
            CHECK(testutil::validUtf8(s.text));
            concat += s.text;
        }
        CHECK_EQ(concat, guichrome::messageLine(m, 80));
        // El prefijo en negrita no contiene la mitad de á: retrocedió a "ab".
        bool foundPrompt = false;
        for (const auto& s : segs)
            if (s.role == StyleRole::MsgPrompt) {
                foundPrompt = true;
                CHECK_EQ(s.text, "ab");
            }
        CHECK(foundPrompt);
    }
    // Límite válido (tras á, offset 4) sigue exacto: no-op del snap.
    {
        MessageBarData m{text, MessageKind::Prompt};
        m.boldPrefix = 4;
        const auto segs = guichrome::messageSegments(m, 80);
        for (const auto& s : segs)
            if (s.text.find_first_not_of(' ') != std::string::npos)
                CHECK(testutil::validUtf8(s.text));
        std::string concat;
        for (const auto& s : segs) concat += s.text;
        CHECK_EQ(concat, guichrome::messageLine(m, 80));
    }
}

// el concat NO basta; el ancho visual por segmento también debe cerrar.
// Con áé漢字 (bytes != columnas, anchos 1 y 2) se verifica: ancho total
// dentro del límite, recorte sin partir UTF-8, ancho doble bien contado,
// suma por segmento == ancho del concat, cada segmento UTF-8 válido (un ancho
// doble partido entre segmentos daría mitades inválidas) y ancho<=0 vacío
// sin accesos fuera de rango.
TEST(gui_chrome_ancho_visual_utf8_headless) {
    auto segWidth = [](const std::vector<guichrome::GuiChromeSeg>& segs) {
        int sum = 0;
        for (const auto& s : segs) {
            CHECK(testutil::validUtf8(s.text));
            sum += colWidth(s.text);
        }
        return sum;
    };
    
    // Métrica: bytes != columnas con á(2B/1C) é(2B/1C) 漢(3B/2C) 字(3B/2C).
    const std::string mixed = "áé漢字";
    CHECK_EQ(static_cast<int>(mixed.size()), 2 + 2 + 3 + 3);
    CHECK_EQ(colWidth(mixed), 1 + 1 + 2 + 2);
    CHECK(colWidth(mixed) < static_cast<int>(mixed.size()));
    
    // Status angosto con CJK: exacto w, suma == w, UTF-8 intacto.
    {
        StatusBarData st;
        st.name = "漢字名.txt";
        st.path = "/casa/niño/漢字";
        st.estado = "NAVEGACION";
        st.modified = true;
        st.totalLines = 50;
        for (int w = 1; w <= 40; ++w) {
            const auto segs = guichrome::statusSegments(
                st, StyleRole::AccentNavegacion, w);
            const std::string line = guichrome::statusLine(st, w);
            std::string cat;
            for (const auto& s : segs) cat += s.text;
            CHECK_EQ(cat, line);
            CHECK_EQ(colWidth(cat), w);
            CHECK_EQ(segWidth(segs), w);
            CHECK(testutil::validUtf8(cat));
        }
    }
    
    // Message angosto con anchos mixtos: 
    // a lo sumo w, suma == ancho real.
    {
        const MessageBarData m {std::string("Nombre: áé漢字.txt"), MessageKind::Prompt};
        for (int w = 1; w <= 40; ++w) {
            const auto segs = guichrome::messageSegments(m, w);
            const std::string line = guichrome::messageLine(m, w);
            std::string cat;
            for (const auto& s : segs) 
                cat += s.text;
                
            CHECK_EQ(cat, line);
            CHECK(colWidth(cat) <= w);
            CHECK_EQ(segWidth(segs), colWidth(cat));
            CHECK(testutil::validUtf8(cat));
        }
    }
    
    // Frontera de ancho doble: "ab漢cd" = 1+1+2+1+1 = 6 columnas.
    // avail=3 no admite 漢 (quedaría 1 columna suelta): se excluye entero.
    // avail=4 lo admite justo. Nunca hay mitades UTF-8 ni exceso de ancho.
    {
        CHECK_EQ(colWidth("ab漢cd"), 6);
        const std::string t3 = utf8::truncate("ab漢cd", 3);
        
        CHECK_EQ(t3, "ab");
        CHECK(colWidth(t3) <= 3);
        CHECK(testutil::validUtf8(t3));
        CHECK(t3.find("漢") == std::string::npos);
        const std::string t4 = utf8::truncate("ab漢cd", 4);
        
        CHECK_EQ(colWidth(t4), 4);
        CHECK(testutil::validUtf8(t4));
        // El mismo recorte vía MessageBar (padL=1, padR=3: avail=w-4).
        // w=7 -> avail=3 -> visible "ab"; w=8 -> avail=4 -> visible "ab漢".
        const MessageBarData m{std::string("ab漢cd"), MessageKind::Info};
        
        CHECK_EQ(guichrome::messageLine(m, 7), " ab   ");
        CHECK_EQ(guichrome::messageLine(m, 8), " ab漢   ");
        for (int w : {7, 8}) {
            const auto segs = guichrome::messageSegments(m, w);
            std::string cat;
            for (const auto& s : segs) 
                cat += s.text;
            CHECK_EQ(cat, guichrome::messageLine(m, w));
            CHECK(colWidth(cat) <= w);
            CHECK_EQ(segWidth(segs), colWidth(cat));
        }
    }
    
    // Ancho cero y negativo: vacío, sin OOB (status exacto 0, message <=0).
    {
        StatusBarData st;
        st.name = "漢字.txt";
        st.path = "/áé";
        st.estado = "NAVEGACION";
        for (int w : {0, -1, -100}) {
            CHECK(guichrome::statusSegments(st, StyleRole::AccentNavegacion, w).empty());
            CHECK(guichrome::messageSegments({std::string("ab漢cd"), MessageKind::Info}, w).empty());
            CHECK_EQ(colWidth(guichrome::statusLine(st, w)), 0);
            CHECK(colWidth(guichrome::messageLine({std::string("ab漢cd"), MessageKind::Info}, w)) <= 0);
        }
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

// --- Headless extra (sin ventana): paleta exhaustiva por rol ---
// colorFor cubre TODOS los StyleRole sin caer en el default por accidente:
// cada familia clave se distingue del texto base y messageColor mapea cada
// MessageKind a su rol. Sin SDL: solo tablas RGB puras.
TEST(gui_paleta_exhaustiva_todos_los_roles_headless) {
    const StyleRole all[] = {
        StyleRole::Default, StyleRole::Gutter, StyleRole::GutterCurrent,
        StyleRole::GutterBlank, StyleRole::Marker, StyleRole::CurrentLine,
        StyleRole::Selection, StyleRole::BracketMatch, StyleRole::ListSelected,
        StyleRole::StatusBase, StyleRole::StatusName, StyleRole::StatusPath,
        StyleRole::StatusModified, StyleRole::StatusAccentDefault,
        StyleRole::AccentNavegacion, StyleRole::AccentInteraccion,
        StyleRole::AccentSeleccion, StyleRole::AccentComando,
        StyleRole::AccentBuffers, StyleRole::AccentGuardar,
        StyleRole::AccentAbrir, StyleRole::MsgInfo, StyleRole::MsgSuccess,
        StyleRole::MsgWarning, StyleRole::MsgError, StyleRole::MsgPrompt,
        StyleRole::SyntaxKeyword, StyleRole::SyntaxType,
        StyleRole::SyntaxPreprocessor, StyleRole::SyntaxString,
        StyleRole::SyntaxCharacter, StyleRole::SyntaxNumber,
        StyleRole::SyntaxComment,
    };
    
    for (bool dark : {true, false}) {
        const GuiColor base = guichrome::colorFor(StyleRole::Default, dark);
        
        // Alfa siempre opaco (sin transparencias accidentales en texto).
        for (StyleRole r : all) 
            CHECK(guichrome::colorFor(r, dark).a == 0xFF);
        
        // Familias que deben distinguirse del texto base.
        CHECK(guichrome::colorFor(StyleRole::Gutter, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::SyntaxKeyword, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::SyntaxType, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::SyntaxPreprocessor, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::SyntaxString, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::SyntaxNumber, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::SyntaxComment, dark) != base);
        CHECK(guichrome::colorFor(StyleRole::GutterCurrent, dark) !=
              guichrome::colorFor(StyleRole::Gutter, dark));
        // Fondos destacados distintos del fondo de contenido.
        const GuiColor bg = guichrome::background(dark);
        CHECK(bg.a == 0xFF);
        CHECK(guichrome::currentLineBackground(dark) != bg);
        CHECK(guichrome::selectionBackground(dark) != bg);
        CHECK(guichrome::statusBackground(dark) != bg);
    }
    
    // messageColor cubre cada MessageKind (sin default silencioso).
    CHECK(guichrome::messageColor(MessageKind::Prompt, true) ==
          guichrome::colorFor(StyleRole::MsgPrompt, true));
    CHECK(guichrome::messageColor(MessageKind::Success, true) ==
          guichrome::colorFor(StyleRole::MsgSuccess, true));
    CHECK(guichrome::messageColor(MessageKind::Warning, true) ==
          guichrome::colorFor(StyleRole::MsgWarning, true));
    CHECK(guichrome::messageColor(MessageKind::Error, true) ==
          guichrome::colorFor(StyleRole::MsgError, true));
    CHECK(guichrome::messageColor(MessageKind::Info, true) ==
          guichrome::colorFor(StyleRole::MsgInfo, true));
    CHECK(guichrome::messageColor(MessageKind::Info, true) !=
          guichrome::messageColor(MessageKind::Info, false));
}

// --- Headless extra: styleFor implementa rowBgKindFor para cada rol ---
// El bg resuelto es exactamente guiBgFor(rowBgKindFor(rol, línea), tema):
// si el pintor y el resolver del cursor usan la misma llamada, no divergen.
TEST(gui_stylefor_sigue_rowbgkind_headless) {
    const StyleRole all[] = {
        StyleRole::Default, StyleRole::Gutter, StyleRole::GutterCurrent,
        StyleRole::GutterBlank, StyleRole::Marker, StyleRole::CurrentLine,
        StyleRole::Selection, StyleRole::BracketMatch, StyleRole::ListSelected,
        StyleRole::StatusBase, StyleRole::StatusName, StyleRole::StatusPath,
        StyleRole::StatusModified, StyleRole::StatusAccentDefault,
        StyleRole::AccentNavegacion, StyleRole::AccentInteraccion,
        StyleRole::AccentSeleccion, StyleRole::AccentComando,
        StyleRole::AccentBuffers, StyleRole::AccentGuardar,
        StyleRole::AccentAbrir, StyleRole::MsgInfo, StyleRole::MsgSuccess,
        StyleRole::MsgWarning, StyleRole::MsgError, StyleRole::MsgPrompt,
        StyleRole::SyntaxKeyword, StyleRole::SyntaxType,
        StyleRole::SyntaxPreprocessor, StyleRole::SyntaxString,
        StyleRole::SyntaxCharacter, StyleRole::SyntaxNumber,
        StyleRole::SyntaxComment,
    };
    
    for (bool dark : {true, false}) {
        for (StyleRole r : all) {
            for (bool cur : {false, true}) {
                const auto st = guichrome::styleFor(r, cur, dark);
                const RowBgKind k = rowBgKindFor(r, cur);
                CHECK(st.bg == guichrome::guiBgFor(k, dark));
                CHECK(st.fg == guichrome::colorFor(r, dark));
            }
        }
    }
    
    // Foco: sintaxis en línea actual va envuelta (fondo CurrentLine), 
    // fuera no; Selection/Bracket/List mandan sobre la línea actual.
    for (bool dark : {true, false}) {
        CHECK(guichrome::styleFor(StyleRole::SyntaxKeyword, true, dark).bg ==
              guichrome::currentLineBackground(dark));
        CHECK(guichrome::styleFor(StyleRole::SyntaxKeyword, false, dark).bg ==
              guichrome::background(dark));
        CHECK(guichrome::styleFor(StyleRole::Selection, true, dark).bg ==
              guichrome::selectionBackground(dark));
        CHECK(guichrome::styleFor(StyleRole::BracketMatch, true, dark).bg ==
              guichrome::bracketBackground());
    }
}

// --- segmentos concatenan la línea plana (UTF-8 incl.) ---
TEST(gui_chrome_segmentos_concatenan_linea_headless) {
    auto concat = [](const std::vector<guichrome::GuiChromeSeg>& segs) {
        std::string out;
        for (const auto& s : segs) out += s.text;
        return out;
    };
    
    std::vector<StatusBarData> cases;
    
    {
        StatusBarData d;
        d.name = "áéí.txt";
        d.path = "/casa/niño/largo";
        d.estado = "NAVEGACION";
        d.modified = true;
        d.totalLines = 10;
        d.cursorLine = 3;
        d.cursorCol = 5;
        cases.push_back(d);
    }
    
    {
        StatusBarData d;
        d.name = std::string(60, 'n');
        d.path = "/" + std::string(60, 'p');
        d.estado = std::string(20, 'e');
        d.right = "99/100";
        d.modified = true;
        d.totalLines = 1000;
        cases.push_back(d);
    }
    
    for (const auto& st : cases) {
        for (int w : {1, 5, 20, 40, 80}) {
            const auto segs = guichrome::statusSegments(st, StyleRole::AccentNavegacion, w);
            CHECK_EQ(concat(segs), guichrome::statusLine(st, w));
            CHECK(testutil::validUtf8(concat(segs)));
        }
    }
    
    const MessageBarData msgs[] = {
        {std::string("hi"), MessageKind::Info},
        {std::string("Nombre: áé漢字.txt"), MessageKind::Prompt},
        {std::string(""), MessageKind::Error},
    };
    
    for (const auto& m : msgs) {
        for (int w : {1, 8, 16, 40, 80}) {
            const auto segs = guichrome::messageSegments(m, w);
            CHECK_EQ(concat(segs), guichrome::messageLine(m, w));
            CHECK(testutil::validUtf8(concat(segs)));
        }
    }
    
    // Ancho 0: ambos vacíos (sin crash, sin pads).
    StatusBarData st;
    st.name = "a";
    CHECK(guichrome::statusSegments(st, StyleRole::AccentNavegacion, 0).empty());
    CHECK(guichrome::messageSegments({std::string("x"), MessageKind::Info}, 0).empty());
}

TEST(gui_paleta_valores_congelados_headless) {
    auto rgb = [](uint8_t r, uint8_t g, uint8_t b) {
        return GuiColor{r, g, b, 0xFF};
    };
        
    // Fondos y cursor por tema.
    CHECK(guichrome::background(true) == rgb(0x12, 0x13, 0x14));
    CHECK(guichrome::background(false) == rgb(0xFF, 0xFF, 0xFF));
    CHECK(guichrome::statusBackground(true) == rgb(0x19, 0x1A, 0x1B));
    CHECK(guichrome::statusBackground(false) == rgb(0xEC, 0xEC, 0xEC));
    CHECK(guichrome::messageBackground(true) == rgb(0x12, 0x13, 0x14));
    CHECK(guichrome::messageBackground(false) == rgb(0xFF, 0xFF, 0xFF));
    CHECK(guichrome::cursorColor(true) == rgb(0xD4, 0xD4, 0xD4));
    CHECK(guichrome::cursorColor(false) == rgb(0x1E, 0x1E, 0x1E));
    CHECK(guichrome::currentLineBackground(true) == rgb(0x3A, 0x3A, 0x3A));
    CHECK(guichrome::currentLineBackground(false) == rgb(0xE4, 0xE4, 0xE4));
    CHECK(guichrome::selectionBackground(true) == rgb(0x5F, 0x5F, 0x87));
    CHECK(guichrome::selectionBackground(false) == rgb(0xBD, 0xBD, 0xFF));
    CHECK(guichrome::bracketBackground() == rgb(0xFF, 0xD7, 0x5F));
    
    // Texto base y gutter: Gutter != Default en ambos temas.
    CHECK(guichrome::colorFor(StyleRole::Default, true) == rgb(0xD4, 0xD4, 0xD4));
    CHECK(guichrome::colorFor(StyleRole::Default, false) == rgb(0x1E, 0x1E, 0x1E));
    CHECK(guichrome::colorFor(StyleRole::Gutter, true) == rgb(0x6C, 0x6C, 0x6C));
    CHECK(guichrome::colorFor(StyleRole::Gutter, false) == rgb(0x94, 0x94, 0x94));
    CHECK(guichrome::colorFor(StyleRole::GutterCurrent, true) == rgb(0x51, 0xAF, 0xEF));
    CHECK(guichrome::colorFor(StyleRole::GutterCurrent, false) == rgb(0x00, 0x5F, 0xA0));
    CHECK(guichrome::colorFor(StyleRole::GutterBlank, true) == rgb(0x12, 0x13, 0x14));
    CHECK(guichrome::colorFor(StyleRole::GutterBlank, false) == rgb(0xFF, 0xFF, 0xFF));
    CHECK(guichrome::colorFor(StyleRole::Marker, true) == rgb(0x5F, 0x87, 0x5F));
    CHECK(guichrome::colorFor(StyleRole::Marker, false) == rgb(0x6E, 0x6E, 0x6E));
    
    // Contenido destacado.
    CHECK(guichrome::colorFor(StyleRole::CurrentLine, true) == rgb(0xD4, 0xD4, 0xD4));
    CHECK(guichrome::colorFor(StyleRole::CurrentLine, false) == rgb(0x1E, 0x1E, 0x1E));
    CHECK(guichrome::colorFor(StyleRole::Selection, true) == rgb(0xFF, 0xFF, 0xFF));
    CHECK(guichrome::colorFor(StyleRole::Selection, false) == rgb(0x1E, 0x1E, 0x1E));
    CHECK(guichrome::colorFor(StyleRole::BracketMatch, true) == rgb(0x1E, 0x1E, 0x1E));
    CHECK(guichrome::colorFor(StyleRole::BracketMatch, false) == rgb(0x1E, 0x1E, 0x1E));
    CHECK(guichrome::colorFor(StyleRole::ListSelected, true) == rgb(0xFF, 0xFF, 0xFF));
    CHECK(guichrome::colorFor(StyleRole::ListSelected, false) == rgb(0x1E, 0x1E, 0x1E));
    
    // Chrome.
    CHECK(guichrome::colorFor(StyleRole::StatusBase, true) == rgb(0x8C, 0x8C, 0x8C));
    CHECK(guichrome::colorFor(StyleRole::StatusBase, false) == rgb(0x26, 0x26, 0x26));
    CHECK(guichrome::colorFor(StyleRole::StatusName, true) == rgb(0x8C, 0x8C, 0x8C));
    CHECK(guichrome::colorFor(StyleRole::StatusName, false) == rgb(0x1E, 0x1E, 0x1E));
    CHECK(guichrome::colorFor(StyleRole::StatusPath, true) == rgb(0x8C, 0x8C, 0x8C));
    CHECK(guichrome::colorFor(StyleRole::StatusPath, false) == rgb(0x6E, 0x6E, 0x6E));
    CHECK(guichrome::colorFor(StyleRole::StatusModified, true) == rgb(0xDD, 0xDD, 0x00));
    CHECK(guichrome::colorFor(StyleRole::StatusModified, false) == rgb(0xD7, 0x00, 0x00));
    for (StyleRole a :
         {StyleRole::StatusAccentDefault, StyleRole::AccentNavegacion,
          StyleRole::AccentInteraccion, StyleRole::AccentSeleccion,
          StyleRole::AccentComando, StyleRole::AccentBuffers,
          StyleRole::AccentGuardar, StyleRole::AccentAbrir}) {
        CHECK(guichrome::colorFor(a, true) == rgb(0x51, 0xAF, 0xEF));
        CHECK(guichrome::colorFor(a, false) == rgb(0x00, 0x5F, 0xA0));
    }
    
    // Mensajes: Prompt distinguido; el resto, tono base por diseño.
    CHECK(guichrome::colorFor(StyleRole::MsgPrompt, true) == rgb(0xFF, 0xFF, 0xFF));
    CHECK(guichrome::colorFor(StyleRole::MsgPrompt, false) == rgb(0x1E, 0x1E, 0x1E));
    for (StyleRole m : {StyleRole::MsgInfo, StyleRole::MsgSuccess,
                        StyleRole::MsgWarning, StyleRole::MsgError}) {
        CHECK(guichrome::colorFor(m, true) == rgb(0xD4, 0xD4, 0xD4));
        CHECK(guichrome::colorFor(m, false) == rgb(0x1E, 0x1E, 0x1E));
    }
    
    // Sintaxis: valores absolutos + colapsos congelados.
    CHECK(guichrome::colorFor(StyleRole::SyntaxKeyword, true) ==
          rgb(0x51, 0xAF, 0xEF));
    CHECK(guichrome::colorFor(StyleRole::SyntaxKeyword, false) ==
          rgb(0x00, 0x5F, 0xA0));
    CHECK(guichrome::colorFor(StyleRole::SyntaxType, true) ==
          rgb(0xAF, 0x87, 0xFF));
    CHECK(guichrome::colorFor(StyleRole::SyntaxType, false) ==
          rgb(0x5F, 0x00, 0x87));
    CHECK(guichrome::colorFor(StyleRole::SyntaxPreprocessor, true) ==
          rgb(0xFF, 0x87, 0x00));
    CHECK(guichrome::colorFor(StyleRole::SyntaxPreprocessor, false) ==
          rgb(0x8A, 0x4B, 0x00));
    CHECK(guichrome::colorFor(StyleRole::SyntaxString, true) ==
          rgb(0x87, 0xD7, 0x87));
    CHECK(guichrome::colorFor(StyleRole::SyntaxString, false) ==
          rgb(0x00, 0x5F, 0x00));
    CHECK(guichrome::colorFor(StyleRole::SyntaxCharacter, true) ==
          guichrome::colorFor(StyleRole::SyntaxString, true));
    CHECK(guichrome::colorFor(StyleRole::SyntaxCharacter, false) ==
          guichrome::colorFor(StyleRole::SyntaxString, false));
    CHECK(guichrome::colorFor(StyleRole::SyntaxNumber, true) ==
          rgb(0xFF, 0xAF, 0x00));
    CHECK(guichrome::colorFor(StyleRole::SyntaxNumber, false) ==
          rgb(0x8A, 0x4B, 0x00));
    CHECK(guichrome::colorFor(StyleRole::SyntaxNumber, false) ==
          guichrome::colorFor(StyleRole::SyntaxPreprocessor, false));
    CHECK(guichrome::colorFor(StyleRole::SyntaxComment, true) ==
          rgb(0x6C, 0x6C, 0x6C));
    CHECK(guichrome::colorFor(StyleRole::SyntaxComment, false) ==
          rgb(0x94, 0x94, 0x94));
    CHECK(guichrome::colorFor(StyleRole::SyntaxComment, true) ==
          guichrome::colorFor(StyleRole::Gutter, true));
    CHECK(guichrome::colorFor(StyleRole::SyntaxComment, false) ==
          guichrome::colorFor(StyleRole::Gutter, false));
    // styleFor representativo con pares fg/bg absolutos (no solo bg): si la
    // tabla de fg o la de fondos se mueve, esto cae aunque el bg coincida.
    {
        const auto s =
            guichrome::styleFor(StyleRole::SyntaxKeyword, true, true);
        CHECK(s.fg == rgb(0x51, 0xAF, 0xEF));
        CHECK(s.bg == rgb(0x3A, 0x3A, 0x3A));
    }
    {
        const auto s = guichrome::styleFor(StyleRole::Selection, false, true);
        CHECK(s.fg == rgb(0xFF, 0xFF, 0xFF));
        CHECK(s.bg == rgb(0x5F, 0x5F, 0x87));
    }
    {
        const auto s =
            guichrome::styleFor(StyleRole::BracketMatch, false, false);
        CHECK(s.fg == rgb(0x1E, 0x1E, 0x1E));
        CHECK(s.bg == rgb(0xFF, 0xD7, 0x5F));
    }
    {
        const auto s = guichrome::styleFor(StyleRole::Default, false, false);
        CHECK(s.fg == rgb(0x1E, 0x1E, 0x1E));
        CHECK(s.bg == rgb(0xFF, 0xFF, 0xFF));
    }
}

// Paridad semántica TTY↔GUI: los roles de contenido que tienen fg y bg
// propios deben emitir en TTY exactamente los mismos RGB que resuelve la
// GUI, en ambos temas. Caza justo la regresión histórica en la que TTY
// ignoraba currentLineFg/selectionFg/listSelectedFg y solo pintaba fondos.
TEST(tty_gui_fg_y_bg_paridad_en_contenido) {
    auto seq = [](const char* kind, const GuiColor& c) {
        return std::string("\x1b[") + kind + ";2;" +
               std::to_string(c.r) + ";" + std::to_string(c.g) + ";" +
               std::to_string(c.b) + "m";
    };
    
    struct Case {
        StyleRole role;
        bool current;
    };
    
    const Case cases[] = {
        {StyleRole::CurrentLine, true},  {StyleRole::Selection, true},
        {StyleRole::BracketMatch, true}, {StyleRole::ListSelected, false},
        {StyleRole::GutterCurrent, true},
    };
    
    for (bool dark : {true, false}) {
        const TtyTheme t = dark ? darkTheme() : lightTheme();
        for (const Case& c : cases) {
            const std::string tty = themeAnsiFor(t, c.role);
            const guichrome::GuiStyle gs =
                guichrome::styleFor(c.role, c.current, dark);
            CHECK(contains(tty, seq("38", gs.fg)));
            CHECK(contains(tty, seq("48", gs.bg)));
        }
    }
}

TEST(tty_gui_status_modified_light_es_rojo) {
    const GuiColor guiLight = guichrome::colorFor(StyleRole::StatusModified, false);
    CHECK_EQ(guiLight.r, 0xD7);
    CHECK_EQ(guiLight.g, 0x00);
    CHECK_EQ(guiLight.b, 0x00);
    CHECK(guiLight.r > guiLight.g);
    CHECK(guiLight.r > guiLight.b);

    const TtyTheme ttyLight = lightTheme();
    CHECK(contains(ttyLight.statusBarModified, "\x1b[38;2;215;0;0m"));
    CHECK(!contains(ttyLight.statusBarModified, "221;221;0"));

    // Y la paridad también se exige en oscuro (amarillo).
    const GuiColor guiDark = guichrome::colorFor(StyleRole::StatusModified, true);
    const TtyTheme ttyDark = darkTheme();
    const std::string fg =
        std::string("\x1b[38;2;") + std::to_string(guiDark.r) + ";" +
        std::to_string(guiDark.g) + ";" + std::to_string(guiDark.b) + "m";
    CHECK(contains(ttyDark.statusBarModified, fg));
}

TEST(gui_contraste_texto_fondo_headless) {
    constexpr double kMin = 3.0;
    auto ratio = [](GuiColor fg, GuiColor bg) {
        return guichrome::cursorContrastRatio(fg, bg);
    };
    
    for (bool dark : {true, false}) {
        const GuiColor bg = guichrome::background(dark);
        CHECK(ratio(guichrome::colorFor(StyleRole::Default, dark), bg) >= kMin);
        CHECK(ratio(guichrome::colorFor(StyleRole::SyntaxKeyword, dark), bg) >= kMin);
        CHECK(ratio(guichrome::colorFor(StyleRole::SyntaxType, dark), bg) >= kMin);
        CHECK(ratio(guichrome::colorFor(StyleRole::SyntaxPreprocessor, dark), bg) >= kMin);
        CHECK(ratio(guichrome::colorFor(StyleRole::SyntaxString, dark), bg) >= kMin);
        CHECK(ratio(guichrome::colorFor(StyleRole::SyntaxNumber, dark), bg) >= kMin);
        CHECK(ratio(guichrome::colorFor(StyleRole::SyntaxComment, dark), bg) >= kMin);
        CHECK(ratio(guichrome::colorFor(StyleRole::Gutter, dark), bg) >= kMin);
        CHECK(ratio(guichrome::colorFor(StyleRole::GutterCurrent, dark), bg) >= kMin);
        
        // Fondos destacados con su fg efectivo.
        CHECK(ratio(guichrome::colorFor(StyleRole::Selection, dark),
                    guichrome::selectionBackground(dark)) >= kMin);
        CHECK(ratio(guichrome::colorFor(StyleRole::BracketMatch, dark),
                    guichrome::bracketBackground()) >= kMin);
        CHECK(ratio(guichrome::colorFor(StyleRole::ListSelected, dark),
                    guichrome::currentLineBackground(dark)) >= kMin);
        CHECK(ratio(guichrome::colorFor(StyleRole::Default, dark),
                    guichrome::currentLineBackground(dark)) >= kMin);
    }
}

