// FASE E paso 0 — goldens de bytes TTY (captura pre-refactor).
//
// Congela el comportamiento byte-a-byte que los pasos 1-7 deben preservar:
//   1. scroll ±1 usa región CSI S/T sin borrado total; ±3 y >=contentH van
//      por slow-path (sin región, sin 2J).
//   2. cursor-move puro solo emite CUP + show (sin rewrites de fila).
//   3. status bar: los 9 State × 2 temas llevan su accent; 5 MessageKind
//      pintan la fila de mensajes.
//   4. listas: bordes (vacío, scroll!=0, seleccionado inicio/fin).
//
// Nota: usan solo la API pública de Renderer (sobreviven al paso 1).

#include <string>
#include <vector>

#include "test_framework.h"
#include "helpers/test_render_utils.h"

#include "app/EditorState.h"
#include "app/Message.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "layout/Viewport.h"
#include "rendering/Renderer.h"
#include "rendering/tty/Theme.h"
#include "rendering/tty/TtyLists.h"

namespace {

Document numberedDoc(int lines) {
    Document doc;
    std::vector<std::string> v;
    for (int i = 0; i < lines; ++i)
        v.push_back("line " + std::to_string(1000 + i) + " " + std::string(60, 'x'));
    doc.restore(v);
    return doc;
}

Viewport contentVp(int top, int height, int width = 80) {
    Viewport vp;
    vp.top = top;
    vp.left = 0;
    vp.height = height;  // filas de contenido (sin chrome)
    vp.width = width;
    return vp;
}

int countOccurrences(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (std::size_t p = 0; (p = hay.find(needle, p)) != std::string::npos; ++n, ++p) {}
    return n;
}

const std::string& accentForState(const Theme& t, State s) {
    switch (s) {
        case State::Navegacion: return t.accentNavegacion;
        case State::Interaccion: return t.accentInteraccion;
        case State::Seleccion: return t.accentSeleccion;
        case State::Prefix: return t.accentComando;
        case State::BufferSelector: return t.accentBuffers;
        case State::SaveAs: return t.accentGuardar;
        case State::FileBrowser: return t.accentAbrir;
        case State::Busqueda: return t.accentGuardar;
        case State::IrAFila: return t.accentNavegacion;
    }
    return t.statusBarAccent;
}

}  // namespace

TEST(fase_e_golden_scroll_pm1_usa_region_sin_borrado) {
    Document doc = numberedDoc(100);
    Cursor cur;
    cur.line = 20;
    cur.col = 0;
    Renderer r;
    const int h = 10;

    // Prime del cache diferencial.
    Viewport vp0 = contentVp(20, h);
    (void)r.buildDiffFrame(doc, cur, vp0, "a.txt", false, "", State::Navegacion);

    // Scroll +1: región con S.
    Viewport vp1 = contentVp(21, h);
    cur.line = 21;
    std::string down = r.buildDiffFrame(doc, cur, vp1, "a.txt", false, "", State::Navegacion);
    CHECK(down.find("\x1b[2J") == std::string::npos);
    CHECK(down.find("S\x1b[r") != std::string::npos);
    CHECK(down.find("T\x1b[r") == std::string::npos);

    // Scroll -1: región con T.
    Viewport vp2 = contentVp(20, h);
    cur.line = 20;
    std::string up = r.buildDiffFrame(doc, cur, vp2, "a.txt", false, "", State::Navegacion);
    CHECK(up.find("\x1b[2J") == std::string::npos);
    CHECK(up.find("T\x1b[r") != std::string::npos);
}

TEST(fase_e_golden_scroll_pm3_y_grande_sin_region_ni_borrado) {
    Document doc = numberedDoc(100);
    Cursor cur;
    cur.line = 20;
    Renderer r;
    const int h = 10;

    Viewport vp0 = contentVp(20, h);
    (void)r.buildDiffFrame(doc, cur, vp0, "a.txt", false, "", State::Navegacion);
    const std::string full = r.buildScreen(doc, cur, vp0, "a.txt", false, "",
                                           State::Navegacion);

    // ±3: slow-path a propósito (solo ±1 usa región): reescribe las filas
    // sin borrado total. Sin cota de tamaño (el slow-path emite CUP+reset+K
    // por fila y puede acercarse al full): el contrato es rewrites==viewport.
    Viewport vp3 = contentVp(23, h);
    cur.line = 23;
    std::string d3 = r.buildDiffFrame(doc, cur, vp3, "a.txt", false, "",
                                      State::Navegacion);
    CHECK(d3.find("\x1b[2J") == std::string::npos);
    CHECK(d3.find("S\x1b[r") == std::string::npos);
    CHECK(d3.find("T\x1b[r") == std::string::npos);
    CHECK(countOccurrences(d3, "\x1b[K") >= h);

    // >= contentH: también slow-path, reescribe todo sin borrado total.
    Viewport vpBig = contentVp(20 + h, h);
    cur.line = 20 + h;
    std::string dBig = r.buildDiffFrame(doc, cur, vpBig, "a.txt", false, "",
                                        State::Navegacion);
    CHECK(dBig.find("\x1b[2J") == std::string::npos);
    CHECK(dBig.find("S\x1b[r") == std::string::npos);
    CHECK(dBig.find("T\x1b[r") == std::string::npos);
    CHECK(countOccurrences(dBig, "\x1b[K") >= h);
}

TEST(fase_e_golden_cursor_move_solo_cup_y_show) {
    Document doc = numberedDoc(100);
    Cursor cur;
    cur.line = 20;
    cur.col = 0;
    Renderer r;
    const int h = 10;
    Viewport vp = contentVp(20, h);

    (void)r.buildDiffFrame(doc, cur, vp, "a.txt", false, "", State::Navegacion);
    const std::string full = r.buildScreen(doc, cur, vp, "a.txt", false, "",
                                           State::Navegacion);

    cur.col = 5;  // misma versión del documento: fast-path de cursor.
    // El status muestra la columna, así que su rewrite (<=2 filas de chrome)
    // es esperado; lo que no debe haber es rewrite de contenido ni clear.
    std::string delta =
        r.buildDiffFrame(doc, cur, vp, "a.txt", false, "", State::Navegacion);
    CHECK(delta.find("\x1b[2J") == std::string::npos);
    CHECK(countOccurrences(delta, "\x1b[K") <= 2);
    CHECK(delta.find("\x1b[") != std::string::npos);   // CUP presente
    CHECK(delta.find("\x1b[?25h") != std::string::npos);  // show cursor
    CHECK(delta.size() < full.size() / 4);
}

TEST(fase_e_golden_status_todos_los_estados_ambos_temas) {
    const State states[] = {
        State::Navegacion, State::Interaccion,  State::Seleccion,
        State::Prefix,     State::BufferSelector, State::SaveAs,
        State::FileBrowser, State::Busqueda,     State::IrAFila,
    };
    Document doc = numberedDoc(30);
    Cursor cur;
    Viewport vp = contentVp(0, 5);
    for (const Theme& theme : {darkTheme(), lightTheme()}) {
        Renderer r;
        r.setTheme(theme);
        for (State s : states) {
            std::string f = r.buildScreen(doc, cur, vp, "a.txt", false,
                                          Message("nota"), s);
            CHECK(!f.empty());
            // El accent del estado viaja en los bytes (vía style, no DTO).
            CHECK(f.find(accentForState(theme, s)) != std::string::npos);
            CHECK(f.find(theme.statusBar) != std::string::npos);
        }
        // Los temas difieren byte-a-byte.
        Renderer rd;
        rd.setTheme(darkTheme());
        Renderer rl;
        rl.setTheme(lightTheme());
        CHECK(rd.buildScreen(doc, cur, vp, "a.txt", false, "", State::Navegacion) !=
              rl.buildScreen(doc, cur, vp, "a.txt", false, "", State::Navegacion));
    }
}

TEST(fase_e_golden_status_todos_los_message_kinds) {
    const MessageKind kinds[] = {
        MessageKind::Info,    MessageKind::Success, MessageKind::Warning,
        MessageKind::Error,   MessageKind::Prompt,
    };
    Document doc = numberedDoc(30);
    Cursor cur;
    Viewport vp = contentVp(0, 5);
    Renderer r;
    for (MessageKind k : kinds) {
        Message m("m-kind", k, std::nullopt);
        std::string f = r.buildScreen(doc, cur, vp, "a.txt", false, m,
                                      State::Navegacion);
        CHECK(testutil::contains(testutil::stripAnsi(f), "m-kind"));
    }
    // Prompt va en negrita; Info no.
    Message prompt("p", MessageKind::Prompt, std::nullopt);
    Message info("p", MessageKind::Info, std::nullopt);
    std::string fp = r.buildScreen(doc, cur, vp, "a.txt", false, prompt,
                                   State::Navegacion);
    std::string fi = r.buildScreen(doc, cur, vp, "a.txt", false, info,
                                   State::Navegacion);
    CHECK(fp != fi);
    CHECK(fp.find("\x1b[1m") != std::string::npos);
}

TEST(fase_e_golden_listas_bordes) {
    TtyLists lists;
    const int content = 4;
    const int width = 80;
    const int total = content + 2;  // + kStatusBarRows

    // Buffer: vacía no crashea y llena el chrome.
    std::string empty = lists.buildBufferListScreen({}, 0, width, content);
    CHECK_EQ((int)testutil::visibleRows(empty).size(), total);

    // Buffer: seleccionado inicio y fin visibles.
    std::string first = lists.buildBufferListScreen({"a.txt", "b.txt", "c.txt"},
                                                    0, width, content);
    std::string last = lists.buildBufferListScreen({"a.txt", "b.txt", "c.txt"},
                                                   2, width, content);
    CHECK(testutil::contains(testutil::stripAnsi(first), "a.txt"));
    CHECK(testutil::contains(testutil::stripAnsi(last), "c.txt"));
    CHECK(first != last);
    CHECK(first.find("\x1b[") != std::string::npos);  // CUP al item

    // File: vacía, y con scroll!=0 muestra la ventana correcta.
    std::string fEmpty = lists.buildFileListScreen({}, 0, 0, "/datos/proyecto",
                                                   Message(""), width, content);
    CHECK_EQ((int)testutil::visibleRows(fEmpty).size(), total);
    std::vector<FileListItem> items = {
        {"a.txt", false}, {"b.txt", false}, {"c.txt", false}, {"d", true}};
    std::string fScrolled = lists.buildFileListScreen(
        items, 2, 1, "/datos/proyecto", Message(""), width, content);
    std::string plain = testutil::stripAnsi(fScrolled);
    CHECK(testutil::contains(plain, "b.txt"));
    CHECK(testutil::contains(plain, "c.txt"));
    CHECK(testutil::contains(plain, "d/"));
    CHECK(!testutil::contains(plain, "a.txt"));
}
