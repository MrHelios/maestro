#include <cstdio>
#include <string>

#include "test_framework.h"
#include "base/utf8.h"
#include "layout/Gutter.h"
#include "layout/Layout.h"
#include "platform/InputEvent.h"
#include "rendering/Sink.h"
#include "rendering/tty/TtyRenderer.h"
#include "helpers/test_render_utils.h"
#include "helpers/test_tty_renderer.h"
#define private public
#include "app/Editor.h"
#include "app/ChromePresentation.h"
#undef private

// Cursor durante Renombrar (Ctrl+K r):
// - cursorVisibleForMode(Renombrar) == true (solo Busqueda oculta).
// - El renderer pinta UN solo cursor (un CUP + un SHOW) en la posicion
//   del documento; el prompt vive como texto en el MessageBar (igual que
//   IrAFila: ningun prompt mueve el cursor fisico al chrome).
// - Tras Enter/Esc el prompt desaparece del MessageBar y el cursor sigue
//   siendo unico en el lugar del archivo.

namespace Ansi {
constexpr const char* CURSOR_SHOW = "\x1b[?25h";
}

namespace {

using testutil::contains;

std::string cursorMoveSeq(const Document& doc, const Cursor& cur,
                          const Viewport& vp) {
    int gutterW = gutterWidth(doc.lineCount(), vp.width);
    Layout layout = computeLayout(vp.height, vp.width);
    int absCol = utf8::columnOf(doc.lineAt(cur.line), cur.col);
    int visCol = absCol - vp.left;
    int outRow = cur.line - vp.top + 1;
    int outCol = gutterW + visCol + 1 + layout.content.col;
    return "\x1b[" + std::to_string(outRow) + ";" + std::to_string(outCol) +
           "H";
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
void pressEvent(Editor& ed, const InputEvent& e) { ed.handleEvent(e); }
void typeBytes(Editor& ed, const std::string& s) {
    for (unsigned char c : s) ed.handleEvent(insert(c));
}
void openRename(Editor& ed) {
    press(ed, InputEventType::Prefix);
    pressEvent(ed, insert('r'));
}
void clearRename(Editor& ed) {
    while (!ed.renameQuery_.empty()) press(ed, InputEventType::Backspace);
}

// Frame completo del Editor (invalidate + renderFrame) para no depender
// del fast path del diff entre capturas.
std::string fullFrame(Editor& ed, CaptureSink& sink) {
    ed.invalidateScreen();
    sink.out.clear();
    ed.renderFrame();
    return sink.out;
}

std::string expectedCursorSeq(Editor& ed) {
    Buffer& b = ed.active();
    return cursorMoveSeq(b.document, b.cursor, b.viewport);
}

}  // namespace

TEST(rename_cursorVisibleForMode_es_true) {
    CHECK(cursorVisibleForMode(State::Renombrar));
    CHECK(!cursorVisibleForMode(State::Busqueda));
    CHECK(cursorVisibleForMode(State::Navegacion));
    CHECK(cursorVisibleForMode(State::IrAFila));
}

TEST(rename_prompt_cursor_unico_en_posicion_documento) {
    Document doc;
    doc.restore({"hola mundo"});
    Viewport vp;
    vp.top = 0;
    vp.left = 0;
    vp.height = 5;
    vp.width = 30;
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    TtyRenderer r;
    Message prompt{std::string("Nombre del archivo: orig.txt"),
                   MessageKind::Prompt, std::nullopt};
    std::string frame = r.buildScreen(doc, cur, vp, "t", false,
                                      makeChromeRequest(prompt, State::Renombrar),
                                      std::nullopt, std::nullopt);
    CHECK(contains(frame, "Nombre del archivo:"));
    const std::string seq = cursorMoveSeq(doc, cur, vp);
    CHECK_EQ(countOccurrences(frame, seq), 1);
    CHECK_EQ(countOccurrences(frame, Ansi::CURSOR_SHOW), 1);
    // Contraste: Busqueda oculta el cursor en la misma posicion.
    std::string fBus = r.buildScreen(doc, cur, vp, "t", false,
                                     makeChromeRequest("", State::Busqueda),
                                     std::nullopt, std::nullopt);
    CHECK(!contains(fBus, Ansi::CURSOR_SHOW));
    CHECK(!contains(fBus, seq));
}

TEST(rename_esc_saca_prompt_y_cursor_vuelve_al_archivo) {
    testfw::TempFile f;
    f.write("hola mundo");
    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink sink;
    ed.setSink(sink);
    CHECK(ed.loadIntoActiveBuffer(f.path));

    openRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    const std::string seq = expectedCursorSeq(ed);
    std::string during = fullFrame(ed, sink);
    CHECK(contains(during, "Nombre del archivo:"));
    CHECK_EQ(countOccurrences(during, seq), 1);
    CHECK_EQ(countOccurrences(during, Ansi::CURSOR_SHOW), 1);

    press(ed, InputEventType::Escape);
    std::string after = fullFrame(ed, sink);
    CHECK(!contains(after, "Nombre del archivo:"));
    CHECK_EQ(countOccurrences(after, seq), 1);
    CHECK_EQ(countOccurrences(after, Ansi::CURSOR_SHOW), 1);
}

TEST(rename_enter_saca_prompt_y_cursor_vuelve_al_archivo) {
    testfw::TempFile fOrig;
    fOrig.write("hola mundo");
    const std::string newBase =
        std::filesystem::path(testfw::tmpPath()).filename().string();
    const std::string newAbs =
        (std::filesystem::path(fOrig.path).parent_path() / newBase).string();
    std::remove(newAbs.c_str());

    Editor ed;
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink sink;
    ed.setSink(sink);
    CHECK(ed.loadIntoActiveBuffer(fOrig.path));

    openRename(ed);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Renombrar));
    clearRename(ed);
    typeBytes(ed, newBase);
    press(ed, InputEventType::InsertNewline);
    CHECK(static_cast<int>(ed.state_) == static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().filename, newAbs);
    CHECK(std::filesystem::exists(newAbs));

    const std::string seq = expectedCursorSeq(ed);
    std::string after = fullFrame(ed, sink);
    CHECK(!contains(after, "Nombre del archivo:"));
    CHECK_EQ(countOccurrences(after, seq), 1);
    CHECK_EQ(countOccurrences(after, Ansi::CURSOR_SHOW), 1);

    std::remove(newAbs.c_str());
}
