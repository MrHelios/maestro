// Fase B/C-1: el Frame es puro (sin ANSI) y el pipeline nuevo preserva
// comportamiento byte a byte respecto de los shims del Renderer.
//
//   - pureza: Frame::hasAnsi() == false aun con sintaxis/selección/brackets,
//     y el accent viaja como StyleRole (el DTO no lleva ANSI).
//   - roles: la fila del cursor lleva GutterCurrent, hay segmentos de
//     Selection y de sintaxis (roles, no secuencias).
//   - paridad: TtyEncoder(buildFrame) == Renderer::buildScreen, y el primer
//     buildDiffFrame (rebuild total) coincide con buildScreen.
#include <optional>
#include <string>
#include <vector>

#include "test_framework.h"

#include "app/ChromePresentation.h"
#include "app/EditorState.h"
#include "app/Message.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "document/Selection.h"
#include "layout/BracketMatcher.h"
#include "layout/Layout.h"
#include "layout/Viewport.h"
#include "rendering/tty/TtyRenderer.h"
#include "rendering/RenderUtil.h"
#include "rendering/Style.h"
#include "rendering/frame/Frame.h"
#include "rendering/frame/FrameBuilder.h"
#include "rendering/tty/TtyEncoder.h"
#include "syntax/SyntaxLanguage.h"

namespace {

Document makeDoc() {
    Document doc;
    doc.restore({"int main() {", "  return áé\tfin;", "}", "", "(selección)"});
    return doc;
}

Viewport makeVp(int top = 0, int left = 0, int height = 5) {
    Viewport vp;
    vp.top = top;
    vp.left = left;
    vp.height = height;
    vp.width = 40;
    return vp;
}

bool hasRole(const Frame& f, StyleRole role) {
    for (const auto& r : f.contentRows)
        for (const auto& s : r.segs)
            if (s.role == role) return true;
    return false;
}

bool segTextHasEsc(const Frame& f) {
    for (const auto& r : f.contentRows)
        for (const auto& s : r.segs)
            if (s.text.find('\x1b') != std::string::npos) return true;
    return false;
}

} // namespace

TEST(frame_sin_ansi_y_con_roles) {
    Document doc = makeDoc();
    Viewport vp = makeVp();
    Cursor cur;
    cur.line = 1;
    cur.col = 3;

    Selection sel;
    sel.anchor = Position{0, 0};
    sel.position = Position{1, 4};
    Selection search;
    search.anchor = Position{0, 4};
    search.position = Position{0, 8};
    // "(selección)": '(' byte 0, ')' byte 11 (ó ocupa 2 bytes).
    BracketPair br{{4, 0}, {4, 11}};

    FrameBuilder b;
    // Priming como el Editor: cache dimensionado antes del frame (con el
    // cache interno frío el isValidThrough heredado salta el primer ensure).
    b.activeCache().setLanguage(SyntaxLanguage::Cpp);
    b.activeCache().ensureValid(doc, doc.lineCount());
    Frame f = b.buildFrame(doc, cur, vp, "a.cpp", true,
                           makeChromeRequest(Message("hola", MessageKind::Info, std::nullopt),
                                             State::Navegacion),
                           sel, search, br);

    // Regla arquitectónica: ni un solo ESC en el Frame.
    CHECK(!f.hasAnsi());
    CHECK(!segTextHasEsc(f));
    // El accent va como rol aparte, no como ANSI en el DTO.
    CHECK(f.statusAccent == StyleRole::AccentNavegacion);
    CHECK(f.cursor.visible);

    // Roles esperados, no secuencias.
    CHECK(hasRole(f, StyleRole::GutterCurrent));
    CHECK(hasRole(f, StyleRole::Gutter));
    CHECK(hasRole(f, StyleRole::Selection));
    CHECK(hasRole(f, StyleRole::BracketMatch));
    bool hasSyntax = hasRole(f, StyleRole::SyntaxKeyword) ||
                     hasRole(f, StyleRole::SyntaxType) ||
                     hasRole(f, StyleRole::SyntaxNumber);
    CHECK(hasSyntax);
}

TEST(frame_markdown_inline_sobre_heading) {
    Document doc;
    doc.restore({"# Head **bold**"});
    Viewport vp = makeVp(0, 0, 1);
    Cursor cur;

    FrameBuilder b;
    Frame f = b.buildFrame(doc, cur, vp, "a.md", false,
                           makeChromeRequest(Message("", MessageKind::Info, std::nullopt),
                                             State::Navegacion),
                           std::nullopt);

    CHECK(hasRole(f, StyleRole::SyntaxHeading));
    CHECK(hasRole(f, StyleRole::SyntaxBold));
}

TEST(frame_busqueda_cursor_en_messagebar) {
    Document doc = makeDoc();
    Viewport vp = makeVp();
    Cursor cur;
    FrameBuilder b;
    Message prompt{std::string("Find: ho"), MessageKind::Prompt, std::nullopt};
    Frame f = b.buildFrame(doc, cur, vp, "a.cpp", false, makeChromeRequest(prompt, State::Busqueda),
                           std::nullopt);
    CHECK(f.cursor.visible);
    CHECK(f.cursor.cell.valid());
    const CellPos expected = chrome::messageBarCursorCell(f.layout.chrome, f.chrome.message);
    CHECK(expected.valid());
    CHECK(f.cursor.cell == expected);
    CHECK(!f.hasAnsi());

    TtyEncoder enc;
    std::string out = enc.encodeFrame(f);
    // Cursor parpadeante posicionado en el MessageBar.
    CHECK(out.find(" q") != std::string::npos);
}

TEST(frame_encode_parity_con_buildscreen) {
    struct Case {
        State state;
        const char* filename;
        bool modified;
        int top;
        int left;
    };
    const std::vector<Case> cases = {
        {State::Navegacion, "a.cpp", false, 0, 0},
        {State::Interaccion, "a.cpp", true, 1, 2},
        {State::Seleccion, "a.cpp", false, 0, 0},
        {State::Busqueda, "a.cpp", false, 0, 0},
        {State::Navegacion, "a.txt", true, 2, 5},
        {State::Prefix, "a.cpp", false, 0, 0},
    };
    for (const TtyTheme& theme : {darkTheme(), lightTheme()}) {
        for (const auto& c : cases) {
            Document doc = makeDoc();
            Viewport vp = makeVp(c.top, c.left);
            Cursor cur;
            cur.line = 1;
            cur.col = 4;

            Selection sel;
            sel.anchor = Position{0, 0};
            sel.position = Position{1, 4};
            Selection search;
            search.anchor = Position{0, 4};
            search.position = Position{0, 8};
            BracketPair br{{4, 0}, {4, 11}};
            Message msg("nota", MessageKind::Info, std::nullopt);

            TtyRenderer r;
            r.setTheme(theme);
            FrameBuilder b;
            TtyRenderer r2;
            r2.setTheme(theme);
            // Caches tibios como en flujo real (Editor): con el cache frío
            // el rebuild del diff parsea sintaxis y el buildScreen no
            // (quirk heredado del Renderer original), así que la paridad
            // solo es exigible en caliente.
            const SyntaxLanguage lang = languageFromFilename(c.filename);
            r.activeCache().setLanguage(lang);
            r.activeCache().ensureValid(doc, doc.lineCount());
            b.activeCache().setLanguage(lang);
            b.activeCache().ensureValid(doc, doc.lineCount());
            r2.activeCache().setLanguage(lang);
            r2.activeCache().ensureValid(doc, doc.lineCount());
            const std::string legacy = r.buildScreen(
                doc, cur, vp, c.filename, c.modified, makeChromeRequest(msg, c.state),
                c.state == State::Seleccion ? std::optional<Selection>(sel)
                                            : std::nullopt,
                search, br);

            Frame f = b.buildFrame(
                doc, cur, vp, c.filename, c.modified, makeChromeRequest(msg, c.state),
                c.state == State::Seleccion ? std::optional<Selection>(sel)
                                            : std::nullopt,
                search, br);
            CHECK(!f.hasAnsi());
            TtyEncoder enc(theme);
            const std::string nuevo = enc.encodeFrame(f);
            CHECK(legacy == nuevo);

            // Primer diff frame (rebuild total) == full screen.
            const std::string diff = r2.buildDiffFrame(
                doc, cur, vp, c.filename, c.modified, makeChromeRequest(msg, c.state),
                c.state == State::Seleccion ? std::optional<Selection>(sel)
                                            : std::nullopt,
                search, br);
            CHECK(legacy == diff);
        }
    }
}

// ---------------------------------------------------------------------------
// Regresiones del layout completo (anclado/legacy): FrameBuilder::buildFrame
// con const Layout& debe producir un Frame cuyo content+chrome llenen
// EXACTAMENTE las filas totales, sin alternancia.
// Invariante central: content.height + chrome.height == rows,
// por tanto 0 <= rows % ch < ch y anchored == true en todo resize normal.
// ---------------------------------------------------------------------------

TEST(frame_buildFrame_layout_completo_minimo) {
    // rows=1: content=1, chrome=0 (anchored=false legítimo, mínimo)
    Document doc;
    doc.restore({"hola"});
    Viewport vp{0, 0, 1, 80};
    Cursor cur;
    FrameBuilder b;
    const Layout layout = computeLayout(1, 80);
    Frame f = b.buildFrame(doc, cur, vp, "t.txt", false,
                           makeChromeRequest(Message("", MessageKind::Info, std::nullopt),
                                             State::Navegacion),
                           std::nullopt, std::nullopt, std::nullopt, nullptr, layout);
    CHECK_EQ(f.layout.content.height + f.layout.chrome.height, 1);
    CHECK_EQ(f.layout.content.height, 1);
    CHECK_EQ(f.layout.chrome.height, 0);
}

TEST(frame_buildFrame_layout_completo_regresion_principal) {
    // rows=2: content=1, chrome=1 (anchored=true: el caso problemático)
    Document doc;
    doc.restore({"hola"});
    Viewport vp{0, 0, 1, 80};
    Cursor cur;
    FrameBuilder b;
    const Layout layout = computeLayout(2, 80);
    Frame f = b.buildFrame(doc, cur, vp, "t.txt", false,
                           makeChromeRequest(Message("", MessageKind::Info, std::nullopt),
                                             State::Navegacion),
                           std::nullopt, std::nullopt, std::nullopt, nullptr, layout);
    CHECK_EQ(f.layout.content.height + f.layout.chrome.height, 2);
    CHECK_EQ(f.layout.content.height, 1);
    CHECK_EQ(f.layout.chrome.height, 1);
}

TEST(frame_buildFrame_layout_completo_limite_normal) {
    // rows=3: content=1, chrome=2 (anchored=true: límite normal)
    Document doc;
    doc.restore({"hola"});
    Viewport vp{0, 0, 1, 80};
    Cursor cur;
    FrameBuilder b;
    const Layout layout = computeLayout(3, 80);
    Frame f = b.buildFrame(doc, cur, vp, "t.txt", false,
                           makeChromeRequest(Message("", MessageKind::Info, std::nullopt),
                                             State::Navegacion),
                           std::nullopt, std::nullopt, std::nullopt, nullptr, layout);
    CHECK_EQ(f.layout.content.height + f.layout.chrome.height, 3);
    CHECK_EQ(f.layout.content.height, 1);
    CHECK_EQ(f.layout.chrome.height, 2);
}

TEST(frame_buildFrame_layout_completo_habitual) {
    // rows=4: content=2, chrome=2 (anchored=true: comportamiento habitual)
    Document doc;
    doc.restore({"hola", "mundo"});
    Viewport vp{0, 0, 2, 80};
    Cursor cur;
    FrameBuilder b;
    const Layout layout = computeLayout(4, 80);
    Frame f = b.buildFrame(doc, cur, vp, "t.txt", false,
                           makeChromeRequest(Message("", MessageKind::Info, std::nullopt),
                                             State::Navegacion),
                           std::nullopt, std::nullopt, std::nullopt, nullptr, layout);
    CHECK_EQ(f.layout.content.height + f.layout.chrome.height, 4);
    CHECK_EQ(f.layout.content.height, 2);
    CHECK_EQ(f.layout.chrome.height, 2);
}

TEST(frame_buildFrame_transicion_resize) {
    // Transición 3 -> 2 -> 3 filas: no debe quedar filas residuales.
    // El layout completo (content+chrome) debe llenar exactamente las filas.
    Document doc;
    doc.restore({"l0", "l1", "l2"});
    Viewport vp{0, 0, 1, 80};
    Cursor cur;
    FrameBuilder b;

    const Layout l3 = computeLayout(3, 80);
    Frame f3 = b.buildFrame(doc, cur, vp, "t.txt", false,
                            makeChromeRequest(Message("", MessageKind::Info, std::nullopt),
                                              State::Navegacion),
                            std::nullopt, std::nullopt, std::nullopt, nullptr, l3);
    CHECK_EQ(f3.layout.content.height + f3.layout.chrome.height, 3);
    CHECK_EQ(f3.layout.content.height, 1);
    CHECK_EQ(f3.layout.chrome.height, 2);

    const Layout l2 = computeLayout(2, 80);
    Frame f2 = b.buildFrame(doc, cur, vp, "t.txt", false,
                            makeChromeRequest(Message("", MessageKind::Info, std::nullopt),
                                              State::Navegacion),
                            std::nullopt, std::nullopt, std::nullopt, nullptr, l2);
    CHECK_EQ(f2.layout.content.height + f2.layout.chrome.height, 2);
    CHECK_EQ(f2.layout.content.height, 1);
    CHECK_EQ(f2.layout.chrome.height, 1);

    const Layout l3b = computeLayout(3, 80);
    Frame f3b = b.buildFrame(doc, cur, vp, "t.txt", false,
                             makeChromeRequest(Message("", MessageKind::Info, std::nullopt),
                                               State::Navegacion),
                             std::nullopt, std::nullopt, std::nullopt, nullptr, l3b);
    CHECK_EQ(f3b.layout.content.height + f3b.layout.chrome.height, 3);
    CHECK_EQ(f3b.layout.content.height, 1);
    CHECK_EQ(f3b.layout.chrome.height, 2);
}

// ---------------------------------------------------------------------------
// Regresión para TTY: buildDiffFrame con const Layout& debe preservar la
// geometría completa (content + chrome) sin traducir a viewport.height +
// kChromeRows, que era la causa del problema P1.
// Usa una misma instancia de TtyDiff (con caché) para verificar transiciones
// reales y fast paths (cursor move / scroll de una fila).
// ---------------------------------------------------------------------------

TEST(tty_buildDiffFrame_transicion_tres_dos_tres_filas) {
    // 3 filas: content=1, chrome=2
    // 2 filas: content=1, chrome=1
    // 3 filas: content=1, chrome=2
    Document doc;
    doc.restore({"l0", "l1", "l2"});
    Viewport vp{0, 0, 1, 80};
    Cursor cur;
    FrameBuilder builder;
    TtyEncoder encoder;
    TtyDiff diff(builder, encoder);
    auto chrome = makeChromeRequest(Message("", MessageKind::Info, std::nullopt),
                                    State::Navegacion);

    auto countLines = [](const std::string& out) {
        int n = 0;
        for (char c : out)
            if (c == '\n') ++n;
        return n;
    };

    // --- Transición 1: 3 filas totales (content=1, chrome=2) ---
    const Layout l3 = computeLayout(3, 80);
    vp.height = l3.content.height; // 1 fila de contenido
    vp.top = 0;
    cur.line = 0;
    const std::string out3a = diff.buildDiffFrame(
        doc, cur, vp, "t.txt", false, chrome,
        std::nullopt, std::nullopt, std::nullopt, nullptr, l3);
    CHECK_EQ(countLines(out3a), 2); // 1 contenido + 1 statusBar

    // --- Transición 2: 2 filas totales (content=1, chrome=1) ---
    const Layout l2 = computeLayout(2, 80);
    vp.height = l2.content.height; // 1 fila de contenido
    vp.top = 0;
    cur.line = 0;
    const std::string out2 = diff.buildDiffFrame(
        doc, cur, vp, "t.txt", false, chrome,
        std::nullopt, std::nullopt, std::nullopt, nullptr, l2);
    CHECK_EQ(countLines(out2), 1); // 1 contenido

    // --- Transición 3: 3 filas totales de nuevo (content=1, chrome=2) ---
    vp.height = l3.content.height; // 1 fila de contenido
    vp.top = 0;
    cur.line = 0;
    const std::string out3b = diff.buildDiffFrame(
        doc, cur, vp, "t.txt", false, chrome,
        std::nullopt, std::nullopt, std::nullopt, nullptr, l3);
    CHECK_EQ(countLines(out3b), 2); // 1 contenido + 1 statusBar
}

TEST(tty_buildDiffFrame_cursor_move_con_layout_dos_filas) {
    // Movimiento de cursor con layout de 2 filas (content=1, chrome=1).
    // Ejercita el fast path buildCursorMoveFrame con layout reducido.
    Document doc;
    doc.restore({"l0", "l1"});
    Viewport vp{0, 0, 1, 80};
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    FrameBuilder builder;
    TtyEncoder encoder;
    TtyDiff diff(builder, encoder);
    auto chrome = makeChromeRequest(Message("", MessageKind::Info, std::nullopt),
                                    State::Navegacion);

    const Layout l2 = computeLayout(2, 80);
    vp.height = l2.content.height; // 1 fila de contenido
    vp.top = 0;

    // Primer render (full)
    const std::string out1 = diff.buildDiffFrame(
        doc, cur, vp, "t.txt", false, chrome,
        std::nullopt, std::nullopt, std::nullopt, nullptr, l2);
    CHECK(!out1.empty());

    // Movimiento de cursor a la columna 1 (mismo renglón visible, dentro del contenido)
    cur.col = 1;
    const std::string out2 = diff.buildDiffFrame(
        doc, cur, vp, "t.txt", false, chrome,
        std::nullopt, std::nullopt, std::nullopt, nullptr, l2);
    CHECK(!out2.empty());
    // El camino incremental (buildCursorMoveFrame) no emite clear screen (\x1b[2J),
    // solo reposiciona el cursor. Si cae al fallback completo, out2 contendría
    // \x1b[2J (línea 139 de TtyEncoder.cpp::beginFrame).
    CHECK(out2.find("\x1b[2J") == std::string::npos);
    // El camino incremental reutiliza el cache de filas de contenido, por lo que
    // out2 debe ser significativamente más corto que out1 (que reconstruyó todo).
    CHECK(out2.size() < out1.size());
}

TEST(tty_buildDiffFrame_scroll_con_layout_cuatro_filas) {
    // Scroll incremental con layout de 4 filas (content=2, chrome=2).
    // Ejercita el fast path buildScrollFrame: con 2 filas de contenido,
    // un scroll de +1 deja 1 fila compartida, así que useRegion es verdadero.
    Document doc;
    doc.restore({"l0", "l1", "l2", "l3"});
    Viewport vp{0, 0, 2, 80};
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    FrameBuilder builder;
    TtyEncoder encoder;
    TtyDiff diff(builder, encoder);
    auto chrome = makeChromeRequest(Message("", MessageKind::Info, std::nullopt),
                                    State::Navegacion);

    const Layout l4 = computeLayout(4, 80);
    vp.height = l4.content.height; // 2 filas de contenido
    vp.top = 0;

    // Primer render en top=0 (full frame)
    const std::string out0 = diff.buildDiffFrame(
        doc, cur, vp, "t.txt", false, chrome,
        std::nullopt, std::nullopt, std::nullopt, nullptr, l4);
    CHECK(!out0.empty());

    // Scroll +1 (top=1): debería usar fast path de scroll regional
    vp.top = 1;
    cur.line = 1;
    const std::string out1 = diff.buildDiffFrame(
        doc, cur, vp, "t.txt", false, chrome,
        std::nullopt, std::nullopt, std::nullopt, nullptr, l4);
    CHECK(!out1.empty());
    // El camino incremental (buildScrollFrame) no emite clear screen (\x1b[2J).
    CHECK(out1.find("\x1b[2J") == std::string::npos);
    // Verifica secuencias concretas del fast path de scroll regional:
    //   \x1b[1;2r  -> establecer región de scroll (2 filas de contenido)
    //   \x1b[1S    -> scroll 1 línea hacia abajo
    //   \x1b[r     -> reset de región de scroll
    CHECK(out1.find("\x1b[1;2r") != std::string::npos);
    CHECK(out1.find("\x1b[1S") != std::string::npos);
    CHECK(out1.find("\x1b[r") != std::string::npos);
}
