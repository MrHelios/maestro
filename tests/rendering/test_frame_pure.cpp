// Fase B/C-1: el Frame es puro (sin ANSI) y el pipeline nuevo preserva
// comportamiento byte a byte respecto de los shims del Renderer.
//
//   - pureza: Frame::hasAnsi() == false aun con sintaxis/selección/brackets,
//     y el accent viaja como StyleRole (estadoAccent vacío).
//   - roles: la fila del cursor lleva GutterCurrent, hay segmentos de
//     Selection y de sintaxis (roles, no secuencias).
//   - paridad: TtyEncoder(buildFrame) == Renderer::buildScreen, y el primer
//     buildDiffFrame (rebuild total) coincide con buildScreen.
#include <optional>
#include <string>
#include <vector>

#include "test_framework.h"

#include "app/EditorState.h"
#include "app/Message.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "document/Selection.h"
#include "layout/BracketMatcher.h"
#include "layout/Viewport.h"
#include "rendering/Renderer.h"
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
                           Message("hola", MessageKind::Info, std::nullopt),
                           State::Navegacion, sel, search, br);

    // Regla arquitectónica: ni un solo ESC en el Frame.
    CHECK(!f.hasAnsi());
    CHECK(!segTextHasEsc(f));
    // El accent legacy viaja vacío; el rol va aparte.
    CHECK(f.status.estadoAccent.empty());
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

TEST(frame_busqueda_oculta_cursor) {
    Document doc = makeDoc();
    Viewport vp = makeVp();
    Cursor cur;
    FrameBuilder b;
    Frame f = b.buildFrame(doc, cur, vp, "a.cpp", false, "", State::Busqueda,
                           std::nullopt);
    CHECK(!f.cursor.visible);
    CHECK(!f.hasAnsi());

    TtyEncoder enc;
    std::string out = enc.encodeFrame(f);
    // Sin estilo ni posicionamiento de cursor (camino Busqueda).
    CHECK(out.find(" q") == std::string::npos);
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
    for (const Theme& theme : {darkTheme(), lightTheme()}) {
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

            Renderer r;
            r.setTheme(theme);
            FrameBuilder b;
            Renderer r2;
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
                doc, cur, vp, c.filename, c.modified, msg, c.state,
                c.state == State::Seleccion ? std::optional<Selection>(sel)
                                            : std::nullopt,
                search, br);

            Frame f = b.buildFrame(
                doc, cur, vp, c.filename, c.modified, msg, c.state,
                c.state == State::Seleccion ? std::optional<Selection>(sel)
                                            : std::nullopt,
                search, br);
            CHECK(!f.hasAnsi());
            TtyEncoder enc(theme);
            const std::string nuevo = enc.encodeFrame(f);
            CHECK(legacy == nuevo);

            // Primer diff frame (rebuild total) == full screen.
            const std::string diff = r2.buildDiffFrame(
                doc, cur, vp, c.filename, c.modified, msg, c.state,
                c.state == State::Seleccion ? std::optional<Selection>(sel)
                                            : std::nullopt,
                search, br);
            CHECK(legacy == diff);
        }
    }
}
