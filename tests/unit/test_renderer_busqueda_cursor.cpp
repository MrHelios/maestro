#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Viewport.h"
#include "core/Layout.h"
#include "core/utf8.h"
#include "ui/Renderer.h"
#include "test_framework.h"
#include <string>
#include <vector>
#include <optional>

namespace {
int gutterWFor(int totalLines) {
    int d=1; for(int n=totalLines;n>=10;n/=10)++d; return std::max(3,d+1);
}
std::string cursorMoveSeq(const Document& doc, const Cursor& cur, const Viewport& vp){
    int gutterW = std::min(gutterWFor(doc.lineCount()), vp.width);
    Layout layout = computeLayout(vp.height, vp.width);
    int absCol = utf8::columnOf(doc.lineAt(cur.line), cur.col);
    int visCol = absCol - vp.left;
    int outRow = cur.line - vp.top + 1;
    int outCol = gutterW + visCol + 1 + layout.content.col;
    return "\x1b[" + std::to_string(outRow) + ";" + std::to_string(outCol) + "H";
}
bool contains(const std::string& hay, const std::string& needle){
    return hay.find(needle)!=std::string::npos;
}
}

TEST(renderer_busqueda_no_posiciona_cursor){
    Document doc; doc.restore({"hola mundo hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    std::string frame = r.buildScreen(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, std::nullopt);
    std::string seq = cursorMoveSeq(doc, cur, vp);
    CHECK(!contains(frame, seq));
    CHECK(!contains(frame, "\x1b[?25h"));
    CHECK(contains(frame, "\x1b[?25l"));
}

TEST(renderer_navegacion_sigue_posicionando_cursor){
    Document doc; doc.restore({"hola mundo hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    std::string frame = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    std::string seq = cursorMoveSeq(doc, cur, vp);
    CHECK(contains(frame, seq));
    CHECK(contains(frame, "\x1b[?25h"));
}

TEST(renderer_busqueda_highlight_sigue_apareciendo){
    Document doc; doc.restore({"hola mundo hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    Selection sel; sel.anchor={0,0}; sel.position={0,4};
    std::optional<Selection> hl = sel;
    std::string frame = r.buildScreen(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(contains(frame, "\x1b[7mhola\x1b[0m"));
}

TEST(renderer_busqueda_diff_no_posiciona_cursor){
    Document doc; doc.restore({"hola mundo hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    Selection sel; sel.anchor={0,0}; sel.position={0,4};
    std::optional<Selection> hl = sel;
    // prime cache with Navegacion
    std::string f1 = r.buildDiffFrame(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(f1, "\x1b[?25h"));
    // diff to Busqueda should hide cursor
    std::string f2 = r.buildDiffFrame(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    std::string seq = cursorMoveSeq(doc, cur, vp);
    CHECK(!contains(f2, seq));
    CHECK(!contains(f2, "\x1b[?25h"));
    CHECK(contains(f2, "\x1b[7mhola\x1b[0m"));
    // also test fresh Busqueda diff (cache miss path)
    Renderer r2;
    std::string f3 = r2.buildDiffFrame(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(!contains(f3, seq));
    CHECK(!contains(f3, "\x1b[?25h"));
    CHECK(contains(f3, "\x1b[7mhola\x1b[0m"));
}

TEST(renderer_busqueda_transicion_oculto_y_visible){
    Document doc; doc.restore({"hola mundo hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    Selection sel; sel.anchor={0,0}; sel.position={0,4};
    std::optional<Selection> hl = sel;
    std::string fNav1 = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(fNav1, "\x1b[?25h"));
    std::string fBus = r.buildScreen(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(!contains(fBus, "\x1b[?25h"));
    CHECK(contains(fBus, "\x1b[7mhola\x1b[0m"));
    std::string fNav2 = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(fNav2, "\x1b[?25h"));
    std::string seq = cursorMoveSeq(doc, cur, vp);
    CHECK(contains(fNav2, seq));
    // diff version of transition
    Renderer rd;
    std::string d1 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(d1, "\x1b[?25h"));
    std::string d2 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(!contains(d2, "\x1b[?25h"));
    std::string d3 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(d3, "\x1b[?25h"));
    CHECK(contains(d3, seq));
}

TEST(renderer_busqueda_diff_cache_con_modificacion){
    Document doc; doc.restore({"hola mundo"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Selection sel; sel.anchor={0,0}; sel.position={0,4};
    std::optional<Selection> hl = sel;
    Renderer rd;
    std::string d1 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(d1, "\x1b[?25h"));
    std::string d2 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(!contains(d2, "\x1b[?25h"));
    CHECK(contains(d2, "\x1b[7mhola\x1b[0m"));
    doc.restore({"hola mundo X"});
    std::string d3 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(d3, "\x1b[?25h"));
    CHECK(contains(d3, "hola mundo X"));
    CHECK(!contains(d3, "\x1b[7mhola\x1b[0m"));
    std::string seq = cursorMoveSeq(doc, cur, vp);
    CHECK(contains(d3, seq));
    Renderer ref;
    std::string expected = ref.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(d3, "hola mundo X"));
    CHECK(contains(expected, "hola mundo X"));
}
