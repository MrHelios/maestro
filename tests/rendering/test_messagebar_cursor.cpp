#include <string>
#include <vector>

#include "test_framework.h"
#include "base/utf8.h"
#include "layout/Gutter.h"
#include "layout/Layout.h"
#include "platform/InputEvent.h"
#include "rendering/RenderUtil.h"
#include "rendering/Sink.h"
#include "rendering/tty/TtyRenderer.h"
#include "helpers/test_render_utils.h"
#include "helpers/test_tty_renderer.h"
#define private public
#include "app/Editor.h"
#include "app/ChromePresentation.h"
#undef private

// Cursor parpadeante en el MessageBar para los 4 inputs (Búsqueda, IrAFila,
// Renombrar, Guardar como): se desactiva el cursor del contenido/lista y hay
// un único cursor (SHOW + blink) al final del texto visible del MessageBar.

namespace Ansi {
constexpr const char* CURSOR_SHOW = "\x1b[?25h";
constexpr const char* CURSOR_BLINK = "\x1b[1 q";
constexpr const char* CURSOR_STEADY = "\x1b[2 q";
}

namespace {

using testutil::contains;

std::string docSeq(const Document& doc, const Cursor& cur, const Viewport& vp) {
    int gutterW = gutterWidth(doc.lineCount(), vp.width);
    Layout layout = computeLayout(vp.height, vp.width);
    int absCol = utf8::columnOf(doc.lineAt(cur.line), cur.col);
    int visCol = absCol - vp.left;
    int outRow = cur.line - vp.top + 1;
    int outCol = gutterW + visCol + 1 + layout.content.col;
    return "\x1b[" + std::to_string(outRow) + ";" + std::to_string(outCol) + "H";
}

std::string mbarSeq(const MessageBarData& msg, const Viewport& vp) {
    Layout layout = computeLayout(vp.height + kChromeRows, vp.width);
    CellPos c = chrome::messageBarCursorCell(layout.chrome, msg);
    return "\x1b[" + std::to_string(c.row + 1) + ";" +
           std::to_string(c.col + 1) + "H";
}

int countOccurrences(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (std::size_t p = 0; (p = hay.find(needle, p)) != std::string::npos;
         ++n, ++p) {
    }
    return n;
}

class CaptureSink : public Sink {
public:
    bool writeStdout(const std::string& s) override {
        out += s;
        return true;
    }
    std::string out;
};

InputEvent insert(char c) {
    InputEvent e;
    e.type = InputEventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}
void press(Editor& ed, InputEventType t) {
    InputEvent e;
    e.type = t;
    ed.handleEvent(e);
}
void typeBytes(Editor& ed, const std::string& s) {
    for (unsigned char c : s) ed.handleEvent(insert(c));
}
std::string fullFrame(Editor& ed, CaptureSink& sink) {
    ed.invalidateScreen();
    sink.out.clear();
    ed.renderFrame();
    return sink.out;
}
std::string expectedDocSeq(Editor& ed) {
    Buffer& b = ed.active();
    return docSeq(b.document, b.cursor, b.viewport);
}

}  // namespace

TEST(messagebar_busqueda_render_cursor_en_messagebar) {
    Document doc;
    doc.restore({"hola mundo"});
    Viewport vp;
    vp.top = 0; vp.left = 0; vp.height = 5; vp.width = 30;
    Cursor cur;
    cur.line = 0; cur.col = 0;
    TtyRenderer r;
    Message prompt{std::string("Find: ho"), MessageKind::Prompt, std::nullopt};
    ChromeRequest chrome = makeChromeRequest(prompt, State::Busqueda);
    std::string frame =
        r.buildScreen(doc, cur, vp, "t", false, chrome, std::nullopt, std::nullopt);
    CHECK(contains(frame, "Find: ho"));
    CHECK(!contains(frame, docSeq(doc, cur, vp)));
    CHECK_EQ(countOccurrences(frame, mbarSeq(chrome.message, vp)), 1);
    CHECK_EQ(countOccurrences(frame, Ansi::CURSOR_SHOW), 1);
    CHECK(contains(frame, Ansi::CURSOR_BLINK));
    CHECK(!contains(frame, Ansi::CURSOR_STEADY));
}

TEST(messagebar_irafila_render_cursor_en_messagebar) {
    Document doc;
    doc.restore({"a", "b", "c"});
    Viewport vp;
    vp.top = 0; vp.left = 0; vp.height = 5; vp.width = 30;
    Cursor cur;
    cur.line = 0; cur.col = 0;
    TtyRenderer r;
    Message prompt{std::string("ir a fila: 12"), MessageKind::Prompt, std::nullopt};
    ChromeRequest chrome = makeChromeRequest(prompt, State::IrAFila);
    std::string frame =
        r.buildScreen(doc, cur, vp, "t", false, chrome, std::nullopt, std::nullopt);
    CHECK(!contains(frame, docSeq(doc, cur, vp)));
    CHECK_EQ(countOccurrences(frame, mbarSeq(chrome.message, vp)), 1);
    CHECK_EQ(countOccurrences(frame, Ansi::CURSOR_SHOW), 1);
    CHECK(contains(frame, Ansi::CURSOR_BLINK));
}

TEST(messagebar_renombrar_render_cursor_en_messagebar) {
    Document doc;
    doc.restore({"hola"});
    Viewport vp;
    vp.top = 0; vp.left = 0; vp.height = 5; vp.width = 30;
    Cursor cur;
    cur.line = 0; cur.col = 0;
    TtyRenderer r;
    Message prompt{std::string("Nombre del archivo: a.txt"), MessageKind::Prompt,
                   std::nullopt};
    ChromeRequest chrome = makeChromeRequest(prompt, State::Renombrar);
    std::string frame =
        r.buildScreen(doc, cur, vp, "t", false, chrome, std::nullopt, std::nullopt);
    CHECK(!contains(frame, docSeq(doc, cur, vp)));
    CHECK_EQ(countOccurrences(frame, mbarSeq(chrome.message, vp)), 1);
    CHECK_EQ(countOccurrences(frame, Ansi::CURSOR_SHOW), 1);
    CHECK(contains(frame, Ansi::CURSOR_BLINK));
}

TEST(messagebar_saveas_render_cursor_en_messagebar_no_en_lista) {
    TtyRenderer r;
    std::vector<FileListItem> items = {{"a.txt", false}, {"b.txt", false},
                                       {"c.txt", false}, {"dir", true},
                                       {"e.txt", false}};
    // Input "nuevo.txt" (9) tras "Nombre del Archivo: " (20): el cursor va al
    // final del nombre (offset 29), no al final de la línea (54 visibles
    // truncadas a 36 con width=40). CUP fijo "\x1b[12;31H"; el del final
    // sería "\x1b[12;38H".
    MessageBarData msg{std::string("Nombre del Archivo: nuevo.txt (Control+S para Guardar)"),
                       MessageKind::Prompt};
    msg.cursor = 20 + 9;
    const std::string mbarSeq = "\x1b[12;31H";
    const std::string endSeq = "\x1b[12;38H";
    CHECK(mbarSeq != endSeq);
    // Contrato general: sea cual sea el seleccionado/scroll, el cursor va al
    // MessageBar y NUNCA a la fila de la lista (antes: CUP selected-scroll+1,1).
    const struct Case { int selected; int scroll; } cases[] = {
        {0, 0}, {1, 0}, {4, 1},
    };
    for (const auto& c : cases) {
        std::string frame = r.buildSaveAsFileListScreen(items, c.selected, c.scroll,
                                                        "/tmp", msg, 40, 10);
        const std::string listSeq = "\x1b[" + std::to_string(c.selected - c.scroll + 1) +
                                    ";1H";
        CHECK(listSeq != mbarSeq);
        CHECK_EQ(countOccurrences(frame, mbarSeq), 1);
        CHECK(!contains(frame, listSeq));
        CHECK(!contains(frame, endSeq));
        CHECK_EQ(countOccurrences(frame, Ansi::CURSOR_SHOW), 1);
        CHECK(contains(frame, Ansi::CURSOR_BLINK));
    }
}

TEST(messagebar_busqueda_editor_escribir_mueve_cursor) {
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink sink;
    ed.setSink(sink);
    ed.active().document.restore({"hola mundo hola"});
    ed.executeCommand("navegacion.buscar");
    CHECK(ed.getStateForTesting() == State::Busqueda);
    std::string f0 = fullFrame(ed, sink);
    const std::string doc0 = expectedDocSeq(ed);
    CHECK(!contains(f0, doc0));
    CHECK(contains(f0, Ansi::CURSOR_BLINK));
    typeBytes(ed, "ho");
    std::string f1 = fullFrame(ed, sink);
    CHECK(contains(f1, "Find: ho"));
    CHECK(!contains(f1, expectedDocSeq(ed)));
    CHECK_EQ(countOccurrences(f1, Ansi::CURSOR_SHOW), 1);
    press(ed, InputEventType::Backspace);
    std::string f2 = fullFrame(ed, sink);
    CHECK(contains(f2, "Find: h"));
    CHECK(!contains(f2, expectedDocSeq(ed)));
    CHECK_EQ(countOccurrences(f2, Ansi::CURSOR_SHOW), 1);
}

// El cursor se queda donde se escribe (tras la query) aunque el mensaje
// siga con " - not found": "Find: xyz" son 9 cols -> CUP col 11; el final
// del texto (21 cols) sería col 23.
TEST(messagebar_busqueda_notfound_cursor_despues_de_query) {
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink sink;
    ed.setSink(sink);
    ed.active().document.restore({"hola mundo"});
    ed.executeCommand("navegacion.buscar");
    typeBytes(ed, "xyz");
    Viewport vp = ed.active().viewport;
    const std::string row = std::to_string(vp.height + 2);
    std::string f = fullFrame(ed, sink);
    CHECK(contains(f, "Find: xyz - not found"));
    CHECK_EQ(countOccurrences(f, "\x1b[" + row + ";11H"), 1);
    CHECK(!contains(f, "\x1b[" + row + ";23H"));
    CHECK_EQ(countOccurrences(f, Ansi::CURSOR_SHOW), 1);
    CHECK(contains(f, Ansi::CURSOR_BLINK));
}

// Idem con contador de matches: "Find: ho (1/2)", cursor tras "ho"
// (8 cols -> CUP col 10); el final (13 cols) sería col 15.
TEST(messagebar_busqueda_contador_cursor_despues_de_query) {
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink sink;
    ed.setSink(sink);
    ed.active().document.restore({"hola mundo hola"});
    ed.executeCommand("navegacion.buscar");
    typeBytes(ed, "ho");
    Viewport vp = ed.active().viewport;
    const std::string row = std::to_string(vp.height + 2);
    std::string f = fullFrame(ed, sink);
    CHECK(contains(f, "Find: ho (1/2)"));
    CHECK_EQ(countOccurrences(f, "\x1b[" + row + ";10H"), 1);
    CHECK(!contains(f, "\x1b[" + row + ";15H"));
    CHECK_EQ(countOccurrences(f, Ansi::CURSOR_SHOW), 1);
    CHECK(contains(f, Ansi::CURSOR_BLINK));
}

TEST(messagebar_irafila_editor_escribir_mueve_cursor) {
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink sink;
    ed.setSink(sink);
    ed.active().document.restore({"a", "b", "c", "d"});
    ed.executeCommand("navegacion.ir_a_fila");
    CHECK(ed.getStateForTesting() == State::IrAFila);
    typeBytes(ed, "2");
    std::string f1 = fullFrame(ed, sink);
    CHECK(contains(f1, "ir a fila: 2"));
    CHECK(!contains(f1, expectedDocSeq(ed)));
    CHECK_EQ(countOccurrences(f1, Ansi::CURSOR_SHOW), 1);
    CHECK(contains(f1, Ansi::CURSOR_BLINK));
}

TEST(messagebar_renombrar_editor_cursor_no_en_documento) {
    testfw::TempFile f;
    f.write("hola");
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink sink;
    ed.setSink(sink);
    CHECK(ed.loadIntoActiveBuffer(f.path));
    press(ed, InputEventType::Prefix);
    ed.handleEvent(insert('r'));
    CHECK(ed.getStateForTesting() == State::Renombrar);
    std::string during = fullFrame(ed, sink);
    CHECK(contains(during, "Nombre del archivo:"));
    CHECK(!contains(during, expectedDocSeq(ed)));
    CHECK_EQ(countOccurrences(during, Ansi::CURSOR_SHOW), 1);
    CHECK(contains(during, Ansi::CURSOR_BLINK));
}

TEST(messagebar_saveas_editor_cursor_no_en_lista) {
    testfw::TempFile f;
    f.write("hola");
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink sink;
    ed.setSink(sink);
    CHECK(ed.loadIntoActiveBuffer(f.path));
    press(ed, InputEventType::Prefix);
    press(ed, InputEventType::Save);
    CHECK(ed.getStateForTesting() == State::SaveAsFileBrowser);
    // Nombre conocido para fijar el CUP: se borra el precargado y se escribe.
    for (int i = 0; i < 500 && !ed.saveAsFileName_.empty(); ++i)
        press(ed, InputEventType::Backspace);
    CHECK(ed.saveAsFileName_.empty());
    typeBytes(ed, "nuevo.txt");
    Viewport vp = ed.active().viewport;
    const std::string row = std::to_string(vp.height + 2);
    std::string after = fullFrame(ed, sink);
    // Cursor al final del nombre (20+9=29 cols -> CUP col 31), no al final
    // de " (Control+S para Guardar)" (54 cols -> CUP col 56).
    CHECK_EQ(countOccurrences(after, "\x1b[" + row + ";31H"), 1);
    CHECK(!contains(after, "\x1b[" + row + ";56H"));
    CHECK_EQ(countOccurrences(after, Ansi::CURSOR_SHOW), 1);
    CHECK(contains(after, Ansi::CURSOR_BLINK));
}

// El CUP del MessageBar se calcula en columnas visuales, no en bytes:
// "Nombre del archivo: " (20 ASCII) + á (U+00E1: 2 bytes, 1 col) +
// 中 (U+4E2D: 3 bytes, 2 cols) + ".txt" (4) = 29 bytes pero 27 columnas.
// Con width=40 no hay truncado (avail 36): col 0-based = 0+1+27 = 28,
// CUP "\x1b[7;29H". Un cálculo por bytes daría "\x1b[7;31H".
TEST(messagebar_utf8_wide_cursor_en_columnas_visuales_no_bytes) {
    Document doc;
    doc.restore({"hola"});
    Viewport vp;
    vp.top = 0; vp.left = 0; vp.height = 5; vp.width = 40;
    Cursor cur;
    cur.line = 0; cur.col = 0;
    TtyRenderer r;
    Message prompt{std::string("Nombre del archivo: á中.txt"), MessageKind::Prompt,
                   std::nullopt};
    ChromeRequest chrome = makeChromeRequest(prompt, State::Renombrar);
    std::string frame =
        r.buildScreen(doc, cur, vp, "t", false, chrome, std::nullopt, std::nullopt);
    CHECK(!contains(frame, docSeq(doc, cur, vp)));
    CHECK_EQ(countOccurrences(frame, "\x1b[7;29H"), 1);
    CHECK(!contains(frame, "\x1b[7;31H"));
    CHECK_EQ(countOccurrences(frame, Ansi::CURSOR_SHOW), 1);
    CHECK(contains(frame, Ansi::CURSOR_BLINK));
}

// Mismo texto con width=30: avail = 26, se trunca la 't' final (col 27) y el
// CUP queda en col 0-based = 0+1+26 = 27 -> "\x1b[7;28H". Por bytes + clamp
// daría "\x1b[7;30H".
TEST(messagebar_utf8_wide_cursor_truncado_con_ancho_angosto) {
    Document doc;
    doc.restore({"hola"});
    Viewport vp;
    vp.top = 0; vp.left = 0; vp.height = 5; vp.width = 30;
    Cursor cur;
    cur.line = 0; cur.col = 0;
    TtyRenderer r;
    Message prompt{std::string("Nombre del archivo: á中.txt"), MessageKind::Prompt,
                   std::nullopt};
    ChromeRequest chrome = makeChromeRequest(prompt, State::Renombrar);
    std::string frame =
        r.buildScreen(doc, cur, vp, "t", false, chrome, std::nullopt, std::nullopt);
    CHECK(!contains(frame, docSeq(doc, cur, vp)));
    CHECK_EQ(countOccurrences(frame, "\x1b[7;28H"), 1);
    CHECK(!contains(frame, "\x1b[7;30H"));
    CHECK_EQ(countOccurrences(frame, Ansi::CURSOR_SHOW), 1);
    CHECK(contains(frame, Ansi::CURSOR_BLINK));
}

// Terminal degeneradamente angosta (width=1): padL consume la única columna,
// avail=0, nada del texto cabe (ni siquiera una celda multibyte parcial) y el
// cálculo crudo daría col 0-based = 1, fuera de la terminal. Semántica
// explícita: con el texto ocupando todo el espacio disponible, el cursor va
// SOBRE la última celda pintable (col 0 -> CUP "\x1b[7;1H"), no después del
// texto (sin el clamp sería "\x1b[7;2H", inválido en 1 columna).
TEST(messagebar_terminal_angosta_cursor_clampeado_a_ultima_celda) {
    Document doc;
    doc.restore({"hola"});
    Viewport vp;
    vp.top = 0; vp.left = 0; vp.height = 5; vp.width = 1;
    Cursor cur;
    cur.line = 0; cur.col = 0;
    TtyRenderer r;
    Message prompt{std::string("Nombre del archivo: á中.txt"), MessageKind::Prompt,
                   std::nullopt};
    ChromeRequest chrome = makeChromeRequest(prompt, State::Renombrar);
    std::string frame =
        r.buildScreen(doc, cur, vp, "t", false, chrome, std::nullopt, std::nullopt);
    CHECK(!contains(frame, docSeq(doc, cur, vp)));
    CHECK_EQ(countOccurrences(frame, "\x1b[7;1H"), 1);
    CHECK(!contains(frame, "\x1b[7;2H"));
    CHECK_EQ(countOccurrences(frame, Ansi::CURSOR_SHOW), 1);
    CHECK(contains(frame, Ansi::CURSOR_BLINK));
}
