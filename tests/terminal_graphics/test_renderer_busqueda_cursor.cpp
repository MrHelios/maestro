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

namespace Ansi {
    constexpr const char* CURSOR_SHOW  = "\x1b[?25h";
    constexpr const char* CURSOR_HIDE  = "\x1b[?25l";
    constexpr const char* CURSOR_STEADY = "\x1b[2 q";
    constexpr const char* CURSOR_BLINK = "\x1b[1 q";
    constexpr const char* HIGHLIGHT_BG = "\x1b[48;5;60m";
    constexpr const char* RESET        = "\x1b[0m";
}

namespace {

int gutterWFor(int totalLines) {
    int digits = std::to_string(std::max(1, totalLines)).length();
    return std::max(3, digits + 1);
}

std::string cursorMoveSeq(const Document& doc, const Cursor& cur, const Viewport& vp) {
    int gutterW = std::min(gutterWFor(doc.lineCount()), vp.width);
    Layout layout = computeLayout(vp.height, vp.width);

    int absCol = utf8::columnOf(doc.lineAt(cur.line), cur.col);
    int visCol = absCol - vp.left;

    int outRow = cur.line - vp.top + 1;
    int outCol = gutterW + visCol + 1 + layout.content.col;

    return "\x1b[" + std::to_string(outRow) + ";" +
           std::to_string(outCol) + "H";
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
    CHECK(!contains(frame, Ansi::CURSOR_SHOW));
    CHECK(contains(frame, Ansi::CURSOR_HIDE));
}

TEST(renderer_navegacion_sigue_posicionando_cursor){
    Document doc; doc.restore({"hola mundo hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    std::string frame = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    std::string seq = cursorMoveSeq(doc, cur, vp);
    CHECK(contains(frame, seq));
    CHECK(contains(frame, Ansi::CURSOR_SHOW));
}

TEST(renderer_busqueda_highlight_sigue_apareciendo){
    Document doc; doc.restore({"hola mundo hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    Selection sel; sel.anchor={0,0}; sel.position={0,4};
    std::optional<Selection> hl = sel;
    std::string frame = r.buildScreen(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(contains(frame, std::string(Ansi::HIGHLIGHT_BG) + "hola" + Ansi::RESET));
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
    CHECK(contains(f1, Ansi::CURSOR_SHOW));
    // diff to Busqueda should hide cursor
    std::string f2 = r.buildDiffFrame(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    std::string seq = cursorMoveSeq(doc, cur, vp);
    CHECK(!contains(f2, seq));
    CHECK(!contains(f2, Ansi::CURSOR_SHOW));
    CHECK(contains(f2, std::string(Ansi::HIGHLIGHT_BG) + "hola" + Ansi::RESET));
    // also test fresh Busqueda diff (cache miss path)
    Renderer r2;
    std::string f3 = r2.buildDiffFrame(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(!contains(f3, seq));
    CHECK(!contains(f3, Ansi::CURSOR_SHOW));
    CHECK(contains(f3, std::string(Ansi::HIGHLIGHT_BG) + "hola" + Ansi::RESET));
}

TEST(renderer_busqueda_transicion_oculto_y_visible){
    Document doc; doc.restore({"hola mundo hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    Selection sel; sel.anchor={0,0}; sel.position={0,4};
    std::optional<Selection> hl = sel;
    std::string fNav1 = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(fNav1, Ansi::CURSOR_SHOW));
    std::string fBus = r.buildScreen(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(!contains(fBus, Ansi::CURSOR_SHOW));
    CHECK(contains(fBus, std::string(Ansi::HIGHLIGHT_BG) + "hola" + Ansi::RESET));
    std::string fNav2 = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(fNav2, Ansi::CURSOR_SHOW));
    std::string seq = cursorMoveSeq(doc, cur, vp);
    CHECK(contains(fNav2, seq));
    // diff version of transition
    Renderer rd;
    std::string d1 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(d1, Ansi::CURSOR_SHOW));
    std::string d2 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(!contains(d2, Ansi::CURSOR_SHOW));
    std::string d3 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(d3, Ansi::CURSOR_SHOW));
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
    CHECK(contains(d1, Ansi::CURSOR_SHOW));
    std::string d2 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(!contains(d2, Ansi::CURSOR_SHOW));
    CHECK(contains(d2, std::string(Ansi::HIGHLIGHT_BG) + "hola" + Ansi::RESET));
    doc.restore({"hola mundo X"});
    std::string d3 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(d3, Ansi::CURSOR_SHOW));
    CHECK(contains(d3, "hola mundo X"));
    CHECK(!contains(d3, std::string(Ansi::HIGHLIGHT_BG) + "hola" + Ansi::RESET));
    std::string seq = cursorMoveSeq(doc, cur, vp);
    CHECK(contains(d3, seq));
    Renderer ref;
    std::string expected = ref.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(d3, "hola mundo X"));
}

TEST(renderer_navegacion_cursor_steady){
    Document doc; doc.restore({"hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    std::string f = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(f, Ansi::CURSOR_STEADY));
    CHECK(!contains(f, Ansi::CURSOR_BLINK));
}

TEST(renderer_interaccion_cursor_blinking){
    Document doc; doc.restore({"hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    std::string f = r.buildScreen(doc, cur, vp, "t", false, "", State::Interaccion, std::nullopt, std::nullopt);
    CHECK(contains(f, Ansi::CURSOR_BLINK));
    CHECK(!contains(f, Ansi::CURSOR_STEADY));
}

TEST(renderer_seleccion_cursor_steady){
    Document doc; doc.restore({"hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    Selection sel; sel.anchor={0,0}; sel.position={0,2};
    std::string f = r.buildScreen(doc, cur, vp, "t", false, "", State::Seleccion, sel, std::nullopt);
    CHECK(contains(f, Ansi::CURSOR_STEADY));
    CHECK(!contains(f, Ansi::CURSOR_BLINK));
}

TEST(renderer_interaccion_diff_blinking){
    Document doc; doc.restore({"hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    std::string f1 = r.buildDiffFrame(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(f1, Ansi::CURSOR_STEADY));
    std::string f2 = r.buildDiffFrame(doc, cur, vp, "t", false, "", State::Interaccion, std::nullopt, std::nullopt);
    CHECK(contains(f2, Ansi::CURSOR_BLINK));
    CHECK(!contains(f2, Ansi::CURSOR_STEADY));
}

TEST(renderer_navegacion_diff_steady_after_interaccion){
    Document doc; doc.restore({"hola"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    std::string f1 = r.buildDiffFrame(doc, cur, vp, "t", false, "", State::Interaccion, std::nullopt, std::nullopt);
    CHECK(contains(f1, Ansi::CURSOR_BLINK));
    std::string f2 = r.buildDiffFrame(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(contains(f2, Ansi::CURSOR_STEADY));
    CHECK(!contains(f2, Ansi::CURSOR_BLINK));
}

TEST(renderer_documento_vacio_no_falla){
    Document doc; doc.restore({""});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    std::string fNav = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(!fNav.empty());
    CHECK(contains(fNav, Ansi::CURSOR_SHOW));
    CHECK(contains(fNav, Ansi::CURSOR_STEADY));
    CHECK(!contains(fNav, std::string(Ansi::HIGHLIGHT_BG) + "hola"));
    std::string seq = cursorMoveSeq(doc, cur, vp);
    CHECK(contains(fNav, seq));
    std::string fBus = r.buildScreen(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, std::nullopt);
    CHECK(!fBus.empty());
    CHECK(!contains(fBus, Ansi::CURSOR_SHOW));
    CHECK(contains(fBus, Ansi::CURSOR_HIDE));
    Renderer rd;
    std::string d1 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt, std::nullopt);
    CHECK(!d1.empty());
    CHECK(contains(d1, Ansi::CURSOR_SHOW));
    std::string d2 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, std::nullopt);
    CHECK(!d2.empty());
    CHECK(!contains(d2, Ansi::CURSOR_SHOW));
}

TEST(renderer_cursor_fuera_viewport_seguro) {
    Document doc;
    doc.restore({
        "linea0", "linea1", "linea2", "linea3", "linea4", "linea5",
        "linea6", "linea7", "linea8", "linea9", "linea10", "linea11"
    });

    Viewport vp;
    vp.top = 0;
    vp.left = 0;
    vp.height = 5;
    vp.width = 30;

    Cursor cur;
    cur.line = 10;
    cur.col = 0;

    Renderer r;

    std::string fNav = r.buildScreen(
        doc, cur, vp, "t", false, "",
        State::Navegacion, std::nullopt, std::nullopt
    );

    CHECK(!fNav.empty());
    CHECK(contains(fNav, Ansi::CURSOR_SHOW));

    std::string fBus = r.buildScreen(
        doc, cur, vp, "t", false, "",
        State::Busqueda, std::nullopt, std::nullopt
    );

    CHECK(!fBus.empty());
    CHECK(!contains(fBus, Ansi::CURSOR_SHOW));
    CHECK(contains(fBus, Ansi::CURSOR_HIDE));

    Renderer rd;

    std::string d1 = rd.buildDiffFrame(
        doc, cur, vp, "t", false, "",
        State::Navegacion, std::nullopt, std::nullopt
    );

    CHECK(!d1.empty());
    CHECK(contains(d1, Ansi::CURSOR_SHOW));

    std::string d2 = rd.buildDiffFrame(
        doc, cur, vp, "t", false, "",
        State::Busqueda, std::nullopt, std::nullopt
    );

    CHECK(!d2.empty());
    CHECK(!contains(d2, Ansi::CURSOR_SHOW));
}

TEST(renderer_resaltado_multilinea){
    Document doc; doc.restore({"hola mundo","adios mundo","tercera linea"});
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=30;
    Cursor cur; cur.line=0; cur.col=0;
    Renderer r;
    Selection sel; sel.anchor={0,2}; sel.position={1,3};
    std::optional<Selection> hl = sel;
    std::string fSel = r.buildScreen(doc, cur, vp, "t", false, "", State::Seleccion, hl, std::nullopt);
    CHECK(!fSel.empty());
    CHECK(contains(fSel, Ansi::HIGHLIGHT_BG));
    CHECK(contains(fSel, std::string(Ansi::HIGHLIGHT_BG) + "la mundo" + Ansi::RESET));
    CHECK(contains(fSel, std::string(Ansi::HIGHLIGHT_BG) + "adi" + Ansi::RESET));
    std::string fBus = r.buildScreen(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(!fBus.empty());
    CHECK(contains(fBus, Ansi::HIGHLIGHT_BG));
    CHECK(contains(fBus, std::string(Ansi::HIGHLIGHT_BG) + "la mundo" + Ansi::RESET));
    CHECK(contains(fBus, std::string(Ansi::HIGHLIGHT_BG) + "adi" + Ansi::RESET));
    CHECK(!contains(fBus, Ansi::CURSOR_SHOW));
    Selection sel2; sel2.anchor={0,2}; sel2.position={2,4};
    std::optional<Selection> hl2 = sel2;
    std::string fMulti3 = r.buildScreen(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl2);
    CHECK(contains(fMulti3, Ansi::HIGHLIGHT_BG));
    CHECK(contains(fMulti3, std::string(Ansi::HIGHLIGHT_BG) + "adios mundo" + Ansi::RESET));
    Renderer rd;
    std::string d1 = rd.buildDiffFrame(doc, cur, vp, "t", false, "", State::Busqueda, std::nullopt, hl);
    CHECK(contains(d1, Ansi::HIGHLIGHT_BG));
    CHECK(contains(d1, std::string(Ansi::HIGHLIGHT_BG) + "adi" + Ansi::RESET));
}
