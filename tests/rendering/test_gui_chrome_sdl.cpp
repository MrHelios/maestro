// Tests de GUI (chrome + contenido + modales + pintado) — capa SDL,
// con pintado real headless (driver "dummy" + renderer por software +
// SDL_RenderReadPixels). Requiere HAVE_SDL2 y video dummy; si faltan,
// se skipean.

#include "helpers/test_gui_chrome_common.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

#include <cstdlib>
#endif

#ifdef HAVE_SDL2
namespace {

struct SdlDummy {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    int w = 800;
    int h = 600;
    int drawableW = 0;
    int drawableH = 0;
    bool ok = false;
    std::string prevVideo;
    std::string prevAudio;
    bool hadVideo = false;
    bool hadAudio = false;
    SdlDummy(int w = 800, int h = 600) : w(w), h(h) {
        if (const char* p = std::getenv("SDL_VIDEODRIVER")) {
            prevVideo = p;
            hadVideo = true;
        }
        if (const char* p = std::getenv("SDL_AUDIODRIVER")) {
            prevAudio = p;
            hadAudio = true;
        }
        ::setenv("SDL_VIDEODRIVER", "dummy", 1);
        ::setenv("SDL_AUDIODRIVER", "dummy", 1);
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return;
        win = SDL_CreateWindow("maestro-test", 0, 0, w, h, SDL_WINDOW_HIDDEN);
        if (!win) return;
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
        ok = (ren != nullptr);
        if (ren) SDL_GetRendererOutputSize(ren, &drawableW, &drawableH);
        if (drawableW <= 0 || drawableH <= 0) {
            drawableW = w;
            drawableH = h;
        }
    }
    ~SdlDummy() {
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
        if (hadVideo)
            ::setenv("SDL_VIDEODRIVER", prevVideo.c_str(), 1);
        else
            ::unsetenv("SDL_VIDEODRIVER");
        if (hadAudio)
            ::setenv("SDL_AUDIODRIVER", prevAudio.c_str(), 1);
        else
            ::unsetenv("SDL_AUDIODRIVER");
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

bool readRow(SDL_Renderer* r, int y, int w, std::vector<GuiColor>& out) {
    if (w <= 0) return false;
    std::vector<Uint32> buf(static_cast<size_t>(w));
    SDL_Rect rc{0, y, w, 1};
    if (SDL_RenderReadPixels(r, &rc, SDL_PIXELFORMAT_ARGB8888, buf.data(),
                             w * static_cast<int>(sizeof(Uint32))) != 0)
        return false;
    out.resize(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x) {
        const Uint32 v = buf[static_cast<size_t>(x)];
        out[static_cast<size_t>(x)] =
            GuiColor{static_cast<uint8_t>((v >> 16) & 0xFF),
                     static_cast<uint8_t>((v >> 8) & 0xFF),
                     static_cast<uint8_t>(v & 0xFF), 0xFF};
    }
    return true;
}

std::vector<int> glyphFreeColumns(
    const std::vector<guichrome::GuiChromeSeg>& segs) {
    std::vector<int> cols;
    int col = 0;
    for (const auto& sg : segs) {
        const int n = chrome::colCount(sg.text);
        if (n > 0 && sg.text.find_first_not_of(' ') == std::string::npos)
            for (int i = 0; i < n; ++i) cols.push_back(col + i);
        col += n;
    }
    return cols;
}

int chromeColX(int baseCol, int col, int cw) {
    return (baseCol + col) * cw + cw / 2;
}

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

bool inkExtent(SDL_Renderer* r, int col0, int col1, int y0, int y1, int cw,
               const GuiColor& bg, int& top, int& bot) {
    top = -1;
    bot = -1;
    for (int y = y0; y < y1; ++y) {
        bool ink = false;
        for (int c = col0; c < col1 && !ink; ++c)
            for (int dx = 0; dx < cw && !ink; ++dx) {
                GuiColor p{};
                if (!readPixel(r, c * cw + dx, y, p)) continue;
                if (p != bg) ink = true;
            }
        if (ink) {
            if (top < 0) top = y;
            bot = y;
        }
    }
    return top >= 0;
}

bool colorExtent(SDL_Renderer* r, int px, int y0, int y1,
                 const GuiColor& want, int& top, int& bot) {
    top = -1;
    bot = -1;
    for (int y = y0; y < y1; ++y) {
        GuiColor p{};
        if (!readPixel(r, px, y, p)) continue;
        if (p == want) {
            if (top < 0) top = y;
            bot = y;
        }
    }
    return top >= 0;
}

// ===========================================================================
// Tests de regresión del chrome: invariante "pintar una sola vez por frame"
// y cursor Bar del prompt.
// ===========================================================================

struct ChromeFixture {
    SdlDummy sdl;
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    Cursor cur;
    Viewport vp;
    Message msg{"Guardado", MessageKind::Info, std::nullopt};

    explicit ChromeFixture(int w = 800, int h = 600) : sdl(w, h) {
        if (!sdl.ok) return;
        gui.initForRenderer(sdl.ren);
        // Fase 2/P3: setDrawableMetrics puede rechazar métricas inválidas; en
        // un fixture de prueba las dimensiones deben ser válidas, así que se
        // exige explícitamente que la aplicación tuvo éxito.
        CHECK(gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH,
                                     gui.contentTopInset()));
        doc.restore({"hola", "mundo", "linea 3"});
        vp = makeVp(sdl.drawableH / gui.cellH() - 2, sdl.drawableW / gui.cellW());
    }

    bool ok() const { return sdl.ok; }

    void render(State st = State::Navegacion) {
        gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                             makeChromeRequest(msg, st), sink,
                             std::nullopt, std::nullopt, std::nullopt);
    }
};

struct Shot {
    std::vector<GuiColor> px;
    int w = 0, h = 0, inset = 0, ch = 0, cw = 0;
    bool dark = false;
    Layout lo;
    guichrome::ChromePaintGeometry g;

    GuiColor at(int x, int y) const {
        return px[static_cast<size_t>(y) * w + x];
    }
};

// Redimensiona para que H (alto util, sin inset) sea exactamente `H`.
void setUsableHeight(ChromeFixture& f, int H) {
    SDL_SetWindowSize(f.sdl.win, 800, f.gui.contentTopInset() + H);
    SDL_PumpEvents();
    // Fase 2: el renderer consume las métricas del drawable almacenadas
    // (setDrawableMetrics desde el loop); sin este refresco usaría el tamaño
    // inicial y pintaría sobre geometría vieja tras el resize.
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(f.sdl.ren, &w, &h);
    f.sdl.drawableW = w;
    f.sdl.drawableH = h;
    f.gui.setDrawableMetrics(w, h, f.gui.contentTopInset());
    f.gui.invalidateCache();
}

// Lee pixeles y calcula la geometria con los mismos datos que el renderer.
bool grabShot(ChromeFixture& f, Shot& s) {
    if (!f.gui.readWindowPixels(s.px, s.w, s.h)) return false;
    s.inset = f.gui.contentTopInset();
    s.ch = f.gui.cellH();
    s.cw = f.gui.cellW();
    s.dark = f.gui.isDarkTheme();
    s.lo = f.gui.lastLayout();
    s.g = guichrome::chromePaintGeometry(s.lo, s.w, s.h - s.inset, s.ch, s.cw);
    return true;
}

// Cuenta pixeles distintos de `expected` en [x0,x1) x [y0,y1). `total`
// recibe la cantidad examinada para poder exigir que no sea cero.
int countMismatch(const Shot& s, int x0, int x1, int y0, int y1,
                  GuiColor expected, int& total) {
    int bad = 0;
    total = 0;
    for (int y = y0; y < y1 && y < s.h; ++y)
        for (int x = x0; x < x1 && x < s.w; ++x) {
            ++total;
            if (s.at(x, y) != expected) ++bad;
        }
    return bad;
}

}  // namespace
#endif
TEST(gui_sdl_fondo_contenido_y_linea_actual) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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

// Regresión end-to-end del bug: en tema claro el StatusBar renderizado pinta
// la tinta real de [*] en rojo (no amarillo). Localiza el segmento [*] con la
// misma composición que usa el pintor y muestrea esa celda en el framebuffer.
TEST(gui_sdl_status_modified_light_pinta_tinta_roja) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    if (!gui.hasFont()) SKIP("sin fuente");
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    if (gui.isDarkTheme()) gui.toggleTheme();
    CHECK(!gui.isDarkTheme());
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "x.cc", true,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    const Layout lo = gui.lastLayout();
    const auto segs = guichrome::statusSegments(
        gui.lastChrome().statusBar, StyleRole::AccentNavegacion,
        lo.chrome.width);
    int markerCol = -1;
    int col = 0;
    for (const auto& s : segs) {
        const size_t at = s.text.find("[*]");
        if (at != std::string::npos) {
            markerCol = col + colWidth(s.text.substr(0, at));
            break;
        }
        col += colWidth(s.text);
    }
    CHECK(markerCol >= 0);
    const int cw = gui.cellW(), ch = gui.cellH();
    bool redInk = false;
    for (int c = markerCol; c < markerCol + 3 && !redInk; ++c)
        for (int dx = 0; dx < cw && !redInk; ++dx)
            for (int dy = 0; dy < ch && !redInk; ++dy) {
                GuiColor p{};
                if (!readPixel(sdl.ren, (lo.chrome.col + c) * cw + dx,
                               lo.chrome.row * ch + dy, p))
                    break;
                if (p.r > p.g + 25 && p.r > p.b + 25) redInk = true;
            }
    CHECK(redInk);
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
    // Línea 0 = actual; bloque aparcado en el relleno (celda 13, EOL),
    // fuera de las celdas muestreadas salvo la 13, que se afirma como fill.
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
        CHECK(bgAt(7, 0, got));
        CHECK(got ==
              guichrome::styleFor(StyleRole::BracketMatch, true, dark).bg);
        CHECK(bgAt(8, 0, got));
        CHECK(got == guichrome::styleFor(StyleRole::Selection, true, dark).bg);
        CHECK(bgAt(11, 0, got));
        CHECK(got == guichrome::styleFor(StyleRole::CurrentLine, true,
                                         dark)
                         .bg);
        CHECK(bgAt(12, 0, got));
        CHECK(got ==
              guichrome::styleFor(StyleRole::BracketMatch, true, dark).bg);
        // Celda 13: el cursor (fill sobre espacio de relleno, sin glifo).
        CHECK_EQ(gui.lastCursor().cell.col, 13);
        CHECK(bgAt(13, 0, got));
        CHECK(got == guichrome::cursorColor(dark));
        // Fila no actual: texto plano sobre fondo base.
        CHECK(bgAt(7, 1, got));
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
    CHECK_EQ(gw, 7);

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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
    // Ambos lados del par llevan BracketMatch (gutter 7 + byte).
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
        gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
        gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    gui.renderBufferList({"a.txt", "b.txt"}, 1, 0, 40, 10, sink);
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    std::vector<std::string> names = {"a.txt", "b.txt"};
    gui.renderBufferList(names, 1, 0, 40, 10, sink);
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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

// Chrome anclado: con una ventana que no es múltiplo de la altura de celda,
// StatusBar y MessageBar quedan pegados al borde inferior con altura fija ch,
// y el residuo entre el contenido y el chrome se pinta con el fondo del
// editor. Se leen píxeles REALES del target de ventana (overlay), no del
// framebuffer.
TEST(gui_sdl_franja_bajo_grilla_fondo_messagebar) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;  // 800x600
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    const int cw = gui.cellW(), ch = gui.cellH();
    Viewport vp = makeVp(sdl.drawableH / ch - 2, sdl.drawableW / cw);
    Cursor cur;
    Message msg{std::string("Guardado"), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    const bool dark = gui.isDarkTheme();
    const Layout lo = gui.lastLayout();
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(sdl.ren, &outW, &outH);
    const int inset = gui.contentTopInset();
    const guichrome::BottomChromeGeometry geom =
        guichrome::bottomChromeGeometry(outH - inset, ch,
                                        lo.content.height, lo.chrome.height);
    if (!geom.anchored) SKIP("ventana múltiplo de la celda (sin residuo)");
    const int xMid = outW / 2;

    // Coordenadas absolutas en pantalla = inset + geometría relativa.
    const int statusY  = inset + geom.statusTop  + ch / 2;
    const int messageY = inset + geom.messageTop + ch / 2;
    const int gapY     = inset + geom.gapTop + geom.gapPx / 2;

    // StatusBar anclado al bloque inferior (ya no en la fila de grilla).
    CHECK(readPixel(sdl.ren, xMid, statusY, probe));
    CHECK(probe == guichrome::statusBackground(dark));
    // MessageBar con altura fija ch, tocando el borde inferior.
    CHECK(readPixel(sdl.ren, xMid, messageY, probe));
    CHECK(probe == guichrome::messageBackground(dark));
    CHECK_EQ(geom.messageTop + ch, outH - inset);  // relativo vs. área útil
    // El residuo queda entre contenido y chrome, pintado con el fondo.
    CHECK(geom.gapPx > 0);
    CHECK(readPixel(sdl.ren, xMid, gapY, probe));
    CHECK(probe == guichrome::background(dark));
    // Sin mensaje la banda visible conserva el mismo color de fondo.
    Message emptyMsg{std::string(""), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(emptyMsg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(readPixel(sdl.ren, xMid, messageY, probe));
    CHECK(probe == guichrome::messageBackground(dark));
#endif
}

// Resize vertical consecutivo: el bloque de chrome se recalcula en cada
// frame y sigue pegado al borde inferior (altura fija ch), sin acumular
// desplazamiento. Se valida sobre el overlay real tras cada render.
TEST(gui_sdl_messagebar_texto_centrado_en_banda_visible) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;  // 800x600
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    if (!gui.hasFont()) SKIP("sin fuente");
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    const int cw = gui.cellW(), ch = gui.cellH();
    Viewport vp = makeVp(sdl.drawableH / ch - 2, sdl.drawableW / cw);
    Cursor cur;
    Message msg{std::string("Guardado"), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    const bool dark = gui.isDarkTheme();
    const Layout lo = gui.lastLayout();
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(sdl.ren, &outW, &outH);
    const int inset = gui.contentTopInset();
    const int H = outH - inset;
    const guichrome::BottomChromeGeometry geom =
        guichrome::bottomChromeGeometry(H, ch, lo.content.height, lo.chrome.height);
    if (!geom.anchored) SKIP("modo legacy inesperado");

    // Banda del MessageBar en coordenadas absolutas de pantalla.
    const int msgTop = inset + geom.messageTop;
    const int msgBot = msgTop + ch;

    const GuiColor mbg = guichrome::messageBackground(dark);
    int top = 0, bot = 0;
    CHECK(inkExtent(sdl.ren, 1, 9, msgTop, msgBot, cw, mbg, top, bot));
    // La tinta está DENTRO de la celda del MessageBar.
    CHECK(top >= msgTop);
    CHECK(bot <= msgBot);
    // Y no en la posición legacy (centro de la banda visible), también
    // expresada en coordenadas absolutas.
    const int legacyBandCenter = inset + (lo.chrome.row + 1) * ch + H / 2;
    const int inkCenter = (top + bot) / 2;
    CHECK(inkCenter < legacyBandCenter);
#endif
}

// Centrado REAL del cursor Bar de la MessageBar (prompt Busqueda): con chrome
// anclado, el cursor Bar llena la celda del MessageBar (altura fija ch).
TEST(gui_sdl_messagebar_cursor_bar_centrado_en_banda_visible) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;  // 800x600
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    if (!gui.hasFont()) SKIP("sin fuente");
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo"});
    const int cw = gui.cellW(), ch = gui.cellH();
    Viewport vp = makeVp(sdl.drawableH / ch - 2, sdl.drawableW / cw);
    Cursor cur;
    Message prompt{std::string("Find: ho (1/2)"), MessageKind::Prompt,
                   std::nullopt, 7, 6};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(prompt, State::Busqueda), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Bar);
    CHECK(gui.lastCursor().cell.valid());
    const bool dark = gui.isDarkTheme();
    const Layout lo = gui.lastLayout();
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(sdl.ren, &outW, &outH);
    const int inset = gui.contentTopInset();
    const int H = outH - inset;
    const guichrome::BottomChromeGeometry geom =
        guichrome::bottomChromeGeometry(H, ch, lo.content.height, lo.chrome.height);
    if (!geom.anchored) SKIP("modo legacy inesperado");

    // Banda del MessageBar en coordenadas absolutas de pantalla.
    const int msgTop = inset + geom.messageTop;
    const int msgBot = msgTop + ch;

    // Columna dentro del Bar de 2px: su primera columna (+1 si cabe).
    const int px = gui.lastCursor().cell.col * cw + (cw > 2 ? 1 : 0);
    int top = 0, bot = 0;
    CHECK(colorExtent(sdl.ren, px, msgTop, msgBot,
                      guichrome::cursorColor(dark), top, bot));
    // El cursor está DENTRO de la celda del MessageBar.
    CHECK(top >= msgTop);
    CHECK(bot <= msgBot);
    // Su centro está cerca del centro de la celda (tolerancia 8px).
    const int barCenter = (top + bot) / 2;
    const int cellCenter = msgTop + ch / 2;
    CHECK(std::abs(barCenter - cellCenter) <= 8);
#endif
}

// Ventana múltiplo EXACTO de la celda: sin franja residual la geometría
// compartida no introduce desplazamiento; el texto queda en su celda.
TEST(gui_sdl_messagebar_sin_franja_no_desplaza) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // La celda sólo se conoce tras abrir la fuente: primer dummy para medirla.
    int ch = 0;
    {
        SdlDummy probeSdl;
        if (!probeSdl.ok) SKIP("sin video dummy");
        GuiRenderer g;
        g.initForRenderer(probeSdl.ren);
        ch = g.cellH();
    }
    if (ch <= 0) SKIP("sin celda");
    SdlDummy sdl(800, ch * 24);  // 24 filas exactas
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    if (!gui.hasFont()) SKIP("sin fuente");
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    const int cw = gui.cellW();
    Viewport vp = makeVp(24 - 2, sdl.drawableW / cw);
    Cursor cur;
    Message msg{std::string("Guardado"), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    const bool dark = gui.isDarkTheme();
    const Layout lo = gui.lastLayout();
    const int yBar = (lo.chrome.row + 1) * ch;
    const int gridBottom = (lo.chrome.row + lo.chrome.height) * ch;
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(sdl.ren, &outW, &outH);
    CHECK_EQ(outH, gridBottom);  // múltiplo exacto: sin franja
    const GuiColor mbg = guichrome::messageBackground(dark);
    int top = 0, bot = 0;
    CHECK(inkExtent(sdl.ren, 1, 9, yBar, outH, cw, mbg, top, bot));
    const int ink2 = top + bot;
    const int cell2 = 2 * yBar + ch;
    // Sin franja no hay desplazamiento adicional: tinta centrada en la celda.
    CHECK(std::abs(ink2 - cell2) <= 4);
#endif
}

// Chrome anclado: detecta independientemente (por color de píxel, desde el
// borde inferior hacia arriba) dónde comienzan y terminan las bandas de
// StatusBar y MessageBar, y solo entonces las compara contra la geometría
// del helper. El gap se pinta con el fondo del editor.
TEST(gui_sdl_chrome_limites_de_fila_y_borde_inferior) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;  // 800x600
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    if (!gui.hasFont()) SKIP("sin fuente");
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    const int cw = gui.cellW(), ch = gui.cellH();
    const int rows = sdl.drawableH / ch - 2;
    Viewport vp = makeVp(rows, sdl.drawableW / cw);
    Cursor cur;
    Message msg{std::string("Guardado"), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    const bool dark = gui.isDarkTheme();
    const Layout lo = gui.lastLayout();
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(sdl.ren, &outW, &outH);
    const int inset = gui.contentTopInset();
    const int usableH = outH - inset;
    const guichrome::BottomChromeGeometry geom =
        guichrome::bottomChromeGeometry(usableH, ch,
                                        lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);
    // Columna dentro del área del chrome, cerca de su borde derecho, para
    // evitar el texto centrado del MessageBar y asegurar que cae dentro del
    // rango de columnas que el chrome realmente pinta.
    const int xRight = (lo.chrome.col + lo.chrome.width - 1) * cw - 1;

    // --- Detección independiente de bandas desde el borde inferior ---
    // Orden visual de abajo hacia arriba: MessageBar, StatusBar, gap (si
    // hay residuo), contenido. Se detectan las transiciones de color por
    // píxel sin usar el helper para ubicar las bandas.
    const int yBottom = inset + usableH - 1;
    CHECK(readPixel(sdl.ren, xRight, yBottom, probe));
    CHECK(probe == guichrome::messageBackground(dark));

    // Contar filas de messageBackground hacia arriba (debe ser exactamente ch).
    int msgRows = 0;
    int y = yBottom;
    while (y >= inset && msgRows <= ch) {
        if (!readPixel(sdl.ren, xRight, y, probe)) break;
        if (probe != guichrome::messageBackground(dark)) break;
        ++msgRows;
        --y;
    }
    CHECK_EQ(msgRows, ch);
    // msgTopObserved se deriva directamente de la transición observada:
    // y apunta a la primera fila por encima del MessageBar.
    const int msgTopObserved = y + 1 - inset;  // relativo al área útil

    // La fila `y` es la primera por encima del MessageBar: debe ser
    // statusBackground (última fila del StatusBar).
    CHECK(readPixel(sdl.ren, xRight, y, probe));
    CHECK(probe == guichrome::statusBackground(dark));

    // Contar filas de statusBackground hacia arriba (debe ser exactamente ch).
    int statusRows = 0;
    while (y >= inset && statusRows <= ch) {
        if (!readPixel(sdl.ren, xRight, y, probe)) break;
        if (probe != guichrome::statusBackground(dark)) break;
        ++statusRows;
        --y;
    }
    CHECK_EQ(statusRows, ch);
    const int statusTopObserved = y + 1 - inset;  // relativo al área útil

    // Si hay residuo, la fila `y` (primera no-StatusBar) debe ser el gap
    // pintado con el fondo del editor.
    if (geom.gapPx > 0) {
        CHECK(readPixel(sdl.ren, xRight, y, probe));
        CHECK(probe == guichrome::background(dark));
    }

    // Comparar límites observados con la geometría del helper.
    CHECK_EQ(msgTopObserved, geom.messageTop);
    CHECK_EQ(statusTopObserved, geom.statusTop);
    if (lo.chrome.height >= 2) {
        CHECK_EQ(geom.messageTop, geom.statusTop + ch);
    }

    // Cada celda de chrome debe tener su color en los tres límites de fila,
    // usando los límites observados (no el helper para ubicar).
    for (int obsY : {statusTopObserved, statusTopObserved + ch / 2,
                     statusTopObserved + ch - 1}) {
        CHECK(readPixel(sdl.ren, xRight, inset + obsY, probe));
        CHECK(probe == guichrome::statusBackground(dark));
    }
    if (lo.chrome.height >= 2) {
        for (int obsY : {msgTopObserved, msgTopObserved + ch / 2,
                         msgTopObserved + ch - 1}) {
            CHECK(readPixel(sdl.ren, xRight, inset + obsY, probe));
            CHECK(probe == guichrome::messageBackground(dark));
        }
    }

    // El chrome ocupa exactamente hasta el borde inferior útil.
    CHECK_EQ(geom.chromeTop + geom.chromeHeight, usableH);
#endif
}

// Chrome anclado con barra de título propia (inset > 0): el inset se descuenta
// una sola vez y el chrome sigue anclado al borde inferior del área útil.
TEST(gui_sdl_chrome_con_titulo_inset) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;  // 800x600
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, 28);  // barra de título propia
    if (!gui.hasFont()) SKIP("sin fuente");
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    const int cw = gui.cellW(), ch = gui.cellH();
    const int rows = (sdl.drawableH - 28) / ch - 2;
    Viewport vp = makeVp(rows, sdl.drawableW / cw);
    Cursor cur;
    Message msg{std::string("Guardado"), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    const bool dark = gui.isDarkTheme();
    const Layout lo = gui.lastLayout();
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(sdl.ren, &outW, &outH);
    const int inset = gui.contentTopInset();
    const int usableH = outH - inset;
    const guichrome::BottomChromeGeometry geom =
        guichrome::bottomChromeGeometry(usableH, ch,
                                        lo.content.height, lo.chrome.height);
    CHECK(geom.anchored);
    // Columna dentro del área del chrome, cerca de su borde derecho, para
    // evitar el texto centrado del MessageBar y asegurar que cae dentro del
    // rango de columnas que el chrome realmente pinta (la ventana no es
    // múltiplo de cw en general).
    const int xRight = (lo.chrome.col + lo.chrome.width) * cw - 1;

    // Coordenadas absolutas: el inset se suma una sola vez.
    CHECK(readPixel(sdl.ren, xRight, inset + geom.statusTop + ch / 2, probe));
    CHECK(probe == guichrome::statusBackground(dark));
    if (lo.chrome.height >= 2) {
        CHECK(readPixel(sdl.ren, xRight,
                        inset + geom.messageTop + ch / 2, probe));
        CHECK(probe == guichrome::messageBackground(dark));
    }
    CHECK(readPixel(sdl.ren, xRight, inset + usableH - 1, probe));
    CHECK(probe == guichrome::messageBackground(dark));
    // El área útil comienza justo debajo de la barra de título: toda la
    // franja y = 0 .. inset-1 debe conservar el fondo del editor (no hay
    // título inyectado en el test, así que nada debe dibujarse por encima
    // del inset).
    for (int y = 0; y < inset; ++y) {
        CHECK(readPixel(sdl.ren, xRight, y, probe));
        CHECK(probe == guichrome::background(dark));
    }
#endif
}

// Modo legacy: cuando la ventana no ancla (residuo >= ch o negativo), el
// MessageBar NO toca el borde inferior del área útil.
TEST(gui_sdl_chrome_legacy_no_toca_borde_inferior) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl(800, 437);
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    if (!gui.hasFont()) SKIP("sin fuente");
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    const int cw = gui.cellW(), ch = gui.cellH();
    // Viewport con menos filas de las que caben en la ventana: contentH fijo
    // para que r = usableH - (contentH+chromeH)*ch >= ch (modo legacy).
    Viewport vp = makeVp(20, sdl.drawableW / cw);
    Cursor cur;
    Message msg{std::string("Guardado"), MessageKind::Info, std::nullopt};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    const bool dark = gui.isDarkTheme();
    const Layout lo = gui.lastLayout();
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(sdl.ren, &outW, &outH);
    const int inset = gui.contentTopInset();
    const int usableH = outH - inset;
    const guichrome::BottomChromeGeometry geom =
        guichrome::bottomChromeGeometry(usableH, ch,
                                        lo.content.height, lo.chrome.height);
    CHECK(!geom.anchored);
    // Columna dentro del área del chrome, cerca de su borde derecho, para
    // evitar el texto centrado del MessageBar y asegurar que cae dentro del
    // rango de columnas que el chrome realmente pinta (la ventana no es
    // múltiplo de cw en general).
    const int xRight = (lo.chrome.col + lo.chrome.width) * cw - 1;

    // En legacy el último píxel del área útil es fondo del editor, no chrome.
    CHECK(readPixel(sdl.ren, xRight, inset + usableH - 1, probe));
    CHECK(probe == guichrome::background(dark));
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
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
TEST(gui_sdl_linea_actual_vacia_cubre_fila) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
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
TEST(gui_sdl_chrome_no_residuos_framebuffer_resize) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl(800, 600);
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    if (!gui.hasFont()) SKIP("sin fuente");
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    const int cw = gui.cellW(), ch = gui.cellH();
    Cursor cur;
    Message msg{std::string("Guardado"), MessageKind::Info, std::nullopt};
    const int inset = gui.contentTopInset();

    // Barrido de 2*ch + 4 alturas consecutivas: cualquier intervalo de esa
    // longitud cruza al menos dos límites de fila y obliga a la geometría
    // anclada a recalcularse en cada frame, igual que al arrastrar el borde
    // superior de la ventana. Usamos residuo inicial >= 3 para que haya gap
    // y la posición lógica quede dentro de él.
    const int startUsable = 20 * ch + 3;  // residuo = 3, hay gap
    const int endUsable = startUsable + 2 * ch + 4;

    for (int usableH = startUsable; usableH <= endUsable; ++usableH) {
        const int targetH = usableH + inset;
        SDL_SetWindowSize(sdl.win, 800, targetH);
        SDL_PumpEvents();
        int outW = 0, outH = 0;
        SDL_GetRendererOutputSize(sdl.ren, &outW, &outH);
        CHECK_EQ(outH, targetH);
        sdl.drawableW = outW;
        sdl.drawableH = outH;

        // Fase 2: el renderer consume métricas almacenadas del drawable; el
        // test debe re-medir (como haría el loop) tras cada resize.
        gui.setDrawableMetrics(outW, outH, gui.contentTopInset());

        const int rows = (outH - inset) / ch;
        Viewport vp = makeVp(rows - 2, outW / cw);
        gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             std::nullopt, std::nullopt, std::nullopt);
        const Layout lo = gui.lastLayout();
        const guichrome::BottomChromeGeometry geom =
            guichrome::bottomChromeGeometry(outH - inset, ch,
                                            lo.content.height, lo.chrome.height);
        CHECK(geom.anchored);

        // Columnas del chrome ocupadas solo por espacios: posiciones donde el
        // fondo no lleva glifos, de modo que se puede exigir estrictamente su
        // color sin tolerar residuos en otras columnas.
        const std::vector<int> stBlank = glyphFreeColumns(guichrome::statusSegments(
            gui.lastChrome().statusBar, gui.lastAccent(), lo.chrome.width));
        const std::vector<int> msgBlank = glyphFreeColumns(
            guichrome::messageSegments(gui.lastChrome().message, lo.chrome.width));
        CHECK(!stBlank.empty());
        CHECK(!msgBlank.empty());

        const bool dark = gui.isDarkTheme();
        const GuiColor statusBg = guichrome::statusBackground(dark);
        const GuiColor msgBg = guichrome::messageBackground(dark);
        const GuiColor editorBg = guichrome::background(dark);
        std::vector<GuiColor> row;

        // Posicion logica del StatusBar dentro del framebuffer.
        const int logicalStatusY = inset + lo.chrome.row * ch;
        // Posicion anclada real (donde debería pintarse finalmente).
        const int anchoredStatusY = inset + geom.statusTop;

        // En la posicion anclada SIEMPRE debe haber StatusBar: se exige en
        // TODAS las columnas libres de glifos, no solo en una.
        CHECK(readRow(sdl.ren, anchoredStatusY + ch / 2, outW, row));
        int statusMism = 0;
        for (int col : stBlank) {
            const int x = chromeColX(lo.chrome.col, col, cw);
            if (x >= outW || row[static_cast<size_t>(x)] != statusBg) ++statusMism;
        }
        CHECK_EQ(statusMism, 0);

        // Si hay gap real (posicion logica estrictamente antes de la anclada),
        // la fila COMPLETA debe ser fondo del editor: el gap queda fuera del
        // contenido y del chrome, asi que no admite glifos ni residuos.
        if (logicalStatusY < anchoredStatusY) {
            for (int y = logicalStatusY; y < anchoredStatusY; ++y) {
                CHECK(readRow(sdl.ren, y, outW, row));
                int mism = 0;
                for (int x = 0; x < outW; ++x)
                    if (row[static_cast<size_t>(x)] != editorBg) ++mism;
                CHECK_EQ(mism, 0);
            }
        }

        // MessageBar: la fila logica antigua se recorre completa (ch px).
        // Segun el residuo puede quedar cubierta por el StatusBar anclado,
        // por el MessageBar anclado o por el fondo del editor; en cada tramo
        // se exige estrictamente el color de la region que la ocupa en todas
        // las columnas libres de glifos de esa region.
        if (lo.chrome.height >= 2) {
            const int logicalMsgY = inset + (lo.chrome.row + 1) * ch;
            const int anchoredMsgY = inset + geom.messageTop;
            const int anchoredStatusEnd = anchoredStatusY + ch;

            // En la posicion anclada SIEMPRE debe haber MessageBar.
            CHECK(readRow(sdl.ren, anchoredMsgY + ch / 2, outW, row));
            int msgMism = 0;
            for (int col : msgBlank) {
                const int x = chromeColX(lo.chrome.col, col, cw);
                if (x >= outW || row[static_cast<size_t>(x)] != msgBg) ++msgMism;
            }
            CHECK_EQ(msgMism, 0);

            for (int y = logicalMsgY; y < logicalMsgY + ch; ++y) {
                if (y < anchoredStatusY) continue;  // gap: ya verificado
                CHECK(readRow(sdl.ren, y, outW, row));
                int mism = 0;
                if (y < anchoredStatusEnd) {
                    // Tramo ocupado por el StatusBar anclado.
                    for (int col : stBlank) {
                        const int x = chromeColX(lo.chrome.col, col, cw);
                        if (x >= outW || row[static_cast<size_t>(x)] != statusBg)
                            ++mism;
                    }
                } else if (y < anchoredMsgY + ch) {
                    // Tramo ocupado por el MessageBar anclado.
                    for (int col : msgBlank) {
                        const int x = chromeColX(lo.chrome.col, col, cw);
                        if (x >= outW || row[static_cast<size_t>(x)] != msgBg)
                            ++mism;
                    }
                } else {
                    // Fuera del chrome anclado: fondo del editor.
                    for (int x = 0; x < outW; ++x)
                        if (row[static_cast<size_t>(x)] != editorBg) ++mism;
                }
                CHECK_EQ(mism, 0);
            }
        }
    }
#endif
}


TEST(gui_sdl_chrome_paint_once_full_fresh) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    ChromeFixture f;
    if (!f.ok()) SKIP("sin video dummy");

    f.gui.resetChromePaintCountersForTests();
    f.render();

    CHECK_EQ(f.gui.chromePaintCalls(), 1);
    CHECK_EQ(f.gui.messageBarCursorPaints(), 0);
#endif
}

TEST(gui_sdl_chrome_paint_once_parcial_contenido) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    ChromeFixture f;
    if (!f.ok()) SKIP("sin video dummy");

    // Frame base para llenar la caché de contenido.
    f.render();
    CHECK(f.gui.lastWasFull());

    // Camino parcial: estado estable sin cambios de contenido.
    f.gui.resetChromePaintCountersForTests();
    f.render();

    // Debe ser parcial (no full) y el chrome debe pintarse una sola vez.
    CHECK(!f.gui.lastWasFull());
    CHECK_EQ(f.gui.chromePaintCalls(), 1);
#endif
}

TEST(gui_sdl_chrome_paint_once_parcial_chrome_dirty) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    ChromeFixture f;
    if (!f.ok()) SKIP("sin video dummy");

    f.render();
    CHECK(f.gui.lastWasFull());

    // Contenido intacto, chrome distinto: escenario del doble pintado original.
    f.msg = Message{"Guardado correctamente", MessageKind::Info, std::nullopt};
    f.gui.resetChromePaintCountersForTests();
    f.render();

    CHECK(!f.gui.lastWasFull());
    CHECK(f.gui.lastChromeDirty());
    CHECK_EQ(f.gui.chromePaintCalls(), 1);
#endif
}

TEST(gui_sdl_chrome_paint_once_cursor_rapido) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    ChromeFixture f;
    if (!f.ok()) SKIP("sin video dummy");

    // Frame base para llenar la caché.
    f.render();
    CHECK(f.gui.lastWasFull());

    // Camino rápido: mover el cursor sin cambiar el contenido.
    f.gui.resetChromePaintCountersForTests();
    f.cur.line = 2;
    f.render();

    CHECK(!f.gui.lastWasFull());
    CHECK_EQ(f.gui.chromePaintCalls(), 1);
#endif
}

TEST(gui_sdl_chrome_paint_once_scroll_normal) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    ChromeFixture f;
    if (!f.ok()) SKIP("sin video dummy");

    // Frame base para llenar la caché.
    f.render();
    CHECK(f.gui.lastWasFull());

    // Camino rápido: scroll +1 sin cambiar el contenido.
    f.gui.resetChromePaintCountersForTests();
    f.vp.top = 1;
    f.render();

    CHECK(!f.gui.lastWasFull());
    CHECK_EQ(f.gui.chromePaintCalls(), 1);
#endif
}

TEST(gui_sdl_chrome_paint_once_fallback_sin_framebuffer) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    ChromeFixture f;
    if (!f.ok()) SKIP("sin video dummy");

    f.gui.setFramebufAcquireFailAfterForTests(0);
    f.gui.resetChromePaintCountersForTests();
    f.render();
    f.gui.setFramebufAcquireFailAfterForTests(-1);

    CHECK_EQ(f.gui.chromePaintCalls(), 1);
#endif
}

TEST(gui_sdl_chrome_messagebar_cursor_bar_blink) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    ChromeFixture f;
    if (!f.ok()) SKIP("sin video dummy");

    Message prompt{"Find: ho", MessageKind::Prompt, std::nullopt, 7, 6};

    auto t0 = std::chrono::steady_clock::now();

    // Fase ON: blinkNow == anchor, elapsed = 0, phase = 0 < 530 => ON.
    // El primer render con prompt ancla el blink en t0.
    f.gui.setBlinkNow(t0);
    f.gui.resetChromePaintCountersForTests();
    f.gui.renderScreenDiff(f.doc, f.cur, f.vp, "t.txt", false,
                           makeChromeRequest(prompt, State::Busqueda),
                           f.sink, std::nullopt, std::nullopt, std::nullopt);

    CHECK_EQ(f.gui.chromePaintCalls(), 1);
    CHECK_EQ(f.gui.messageBarCursorPaints(), 1);
    CHECK(f.gui.lastCursor().visible);
    CHECK(f.gui.lastCursor().shape == FrameCursorShape::Bar);

    // Fase OFF: elapsed = 530ms, phase = 530, no es < 530 => OFF.
    // El cursor no cambia, por lo que el ancla no se mueve.
    f.gui.setBlinkNow(t0 + std::chrono::milliseconds(530));
    f.gui.resetChromePaintCountersForTests();
    f.gui.renderScreenDiff(f.doc, f.cur, f.vp, "t.txt", false,
                           makeChromeRequest(prompt, State::Busqueda),
                           f.sink, std::nullopt, std::nullopt, std::nullopt);

    CHECK_EQ(f.gui.chromePaintCalls(), 1);
    CHECK_EQ(f.gui.messageBarCursorPaints(), 0);
    CHECK(f.gui.lastCursor().visible);
    CHECK(f.gui.lastCursor().shape == FrameCursorShape::Bar);

    // Fase ON de nuevo: elapsed = 1060ms, phase = 0 < 530 => ON.
    f.gui.setBlinkNow(t0 + std::chrono::milliseconds(1060));
    f.gui.resetChromePaintCountersForTests();
    f.gui.renderScreenDiff(f.doc, f.cur, f.vp, "t.txt", false,
                           makeChromeRequest(prompt, State::Busqueda),
                           f.sink, std::nullopt, std::nullopt, std::nullopt);

    CHECK_EQ(f.gui.chromePaintCalls(), 1);
    CHECK_EQ(f.gui.messageBarCursorPaints(), 1);
    CHECK(f.gui.lastCursor().visible);
    CHECK(f.gui.lastCursor().shape == FrameCursorShape::Bar);
#endif
}

TEST(gui_sdl_chrome_statusbar_legacy_una_fila) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    ChromeFixture f(800, 600);
    if (!f.ok()) SKIP("sin video dummy");
    const int cw = f.gui.cellW(), ch = f.gui.cellH();
    f.vp = makeVp(20, f.sdl.drawableW / cw);
    f.msg = Message{"", MessageKind::Info, std::nullopt}; // solo StatusBar

    // r = ch => legacy.
    setUsableHeight(f, (20 + 2) * ch + ch);
    f.gui.setCaptureEnabled(true);
    f.render();
    Shot s;
    CHECK(grabShot(f, s));
    f.gui.setCaptureEnabled(false);

    // Precondicion: modo legacy.
    CHECK(!s.g.anchored);

    // 1. El StatusBar debe tener su fondo en su posicion legacy.
    const GuiColor statusBg = guichrome::statusBackground(s.dark);
    const int baseX = s.lo.chrome.col * cw;
    const int statusY = s.inset + s.g.statusTop + ch / 2;
    CHECK(s.at(baseX, statusY) == statusBg);

    // 2. Si hay una sola fila de chrome, verificar la cola residual
    // (statusTail), incluyendo el extremo derecho del drawable.
    if (s.lo.chrome.height == 1) {
        CHECK(s.g.statusTailHeight > 0);
        CHECK(s.g.statusTailWidth > 0);
        const int tailY = s.inset + s.g.statusTailTop + s.g.statusTailHeight / 2;
        CHECK(s.at(baseX, tailY) == statusBg);
        CHECK(s.at(s.w - 1, tailY) == statusBg);
    }
#endif
}

TEST(gui_sdl_chrome_transicion_anchored_a_legacy_sin_residuos) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    ChromeFixture f(800, 600);
    if (!f.ok()) SKIP("sin video dummy");
    const int cw = f.gui.cellW(), ch = f.gui.cellH();
    CHECK(ch >= 2);
    f.vp = makeVp(20, f.sdl.drawableW / cw);
    const int grid = (20 + 2) * ch;

    f.gui.setCaptureEnabled(true);

    // Frame 1: anclado con gap real de ch/2.
    setUsableHeight(f, grid + ch / 2);
    f.render();
    Shot a;
    CHECK(grabShot(f, a));
    CHECK_EQ(a.lo.content.height, 20);
    CHECK_EQ(a.lo.chrome.height, 2);
    CHECK_EQ(a.h - a.inset, grid + ch / 2);
    CHECK(a.g.anchored);
    CHECK_EQ(a.g.gapHeight, ch / 2);

    // Frame 2: legacy (r == ch).
    setUsableHeight(f, grid + ch);
    f.render();
    Shot leg;
    CHECK(grabShot(f, leg));
    f.gui.setCaptureEnabled(false);
    CHECK_EQ(leg.lo.chrome.height, 2);
    CHECK_EQ(leg.h - leg.inset, grid + ch);
    CHECK(!leg.g.anchored);

    const int baseX = leg.lo.chrome.col * cw;
    const int endX = (leg.lo.chrome.col + leg.lo.chrome.width) * cw;
    const GuiColor editorBg = guichrome::background(leg.dark);

    // Franja inferior bajo el chrome legacy: no debe quedar chrome viejo.
    int total = 0;
    const int bad = countMismatch(leg, baseX, endX, leg.inset + grid,
                                  leg.inset + grid + ch, editorBg, total);
    CHECK(total > 0);
    CHECK_EQ(bad, 0);
#endif
}

TEST(gui_sdl_chrome_transicion_legacy_a_anchored_sin_franjas) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    ChromeFixture f(800, 600);
    if (!f.ok()) SKIP("sin video dummy");
    const int cw = f.gui.cellW(), ch = f.gui.cellH();
    CHECK(ch >= 2);
    f.vp = makeVp(20, f.sdl.drawableW / cw);
    const int grid = (20 + 2) * ch;

    f.gui.setCaptureEnabled(true);

    // Frame 1: legacy.
    setUsableHeight(f, grid + ch);
    f.render();
    Shot l;
    CHECK(grabShot(f, l));
    CHECK_EQ(l.lo.content.height, 20);
    CHECK_EQ(l.lo.chrome.height, 2);
    CHECK_EQ(l.h - l.inset, grid + ch);
    CHECK(!l.g.anchored);

    // Frame 2: anclado con gap de ch/2.
    setUsableHeight(f, grid + ch / 2);
    f.render();
    Shot s;
    CHECK(grabShot(f, s));
    f.gui.setCaptureEnabled(false);
    CHECK_EQ(s.h - s.inset, grid + ch / 2);
    CHECK(s.g.anchored);
    CHECK(s.g.gapHeight > 0);
    CHECK_EQ(s.g.gapHeight, ch / 2);

    const int baseX = s.lo.chrome.col * cw;
    const int endX = (s.lo.chrome.col + s.lo.chrome.width) * cw;
    const GuiColor editorBg = guichrome::background(s.dark);

    // Gap completo: todo fondo del editor (incluye donde antes estaba el
    // chrome legacy).
    int total = 0;
    const int bad = countMismatch(s, baseX, endX, s.inset + s.g.gapTop,
                                  s.inset + s.g.gapTop + s.g.gapHeight,
                                  editorBg, total);
    CHECK(total > 0);
    CHECK_EQ(bad, 0);
#endif
}

TEST(gui_sdl_chrome_paint_once_lista_full) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    NullTestSink sink;
    std::vector<std::string> names = {"a.txt", "b.txt", "c.txt"};

    gui.resetChromePaintCountersForTests();
    gui.renderBufferList(names, 0, 0, 40, 10, sink);

    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.chromePaintCalls(), 1);
#endif
}

TEST(gui_sdl_chrome_paint_once_lista_parcial) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    NullTestSink sink;
    std::vector<std::string> names = {"a.txt", "b.txt", "c.txt"};

    gui.renderBufferList(names, 0, 0, 40, 10, sink);
    CHECK(gui.lastWasFull());

    gui.resetChromePaintCountersForTests();
    gui.renderBufferList(names, 1, 0, 40, 10, sink); // cambia la seleccion

    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.chromePaintCalls(), 1);
#endif
}

TEST(gui_sdl_chrome_paint_once_lista_fallback_sin_framebuffer) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH, gui.contentTopInset());
    NullTestSink sink;
    std::vector<std::string> names = {"a.txt", "b.txt", "c.txt"};

    gui.setFramebufAcquireFailAfterForTests(0); // acquireFb falla -> presentWindow
    gui.resetChromePaintCountersForTests();
    gui.renderBufferList(names, 0, 0, 40, 10, sink);
    gui.setFramebufAcquireFailAfterForTests(-1);

    CHECK_EQ(gui.chromePaintCalls(), 1);
#endif
}

// El framebuffer cubre EXACTAMENTE el área disponible (drawable menos inset)
// cuando los sobrantes son los residuos de guiGridSize: pseudo columna a la
// derecha y pseudo fila entre contenido y chrome. Sin estirar celdas y sin
// recrear la textura en resizes de pocos píxeles.
TEST(gui_sdl_frame_pixels_pseudo_fila_columna_y_capacidad) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummy sdl(800, 600);
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    if (!gui.hasFont()) SKIP("sin fuente");
    gui.setDrawableMetrics(sdl.drawableW, sdl.drawableH,
                           gui.contentTopInset());
    GuiColor probe{};
    if (!readPixel(sdl.ren, 0, 0, probe)) SKIP("ReadPixels no disponible");

    const int cw = gui.cellW();
    const int ch = gui.cellH();
    CHECK(cw > 1);
    CHECK(ch > 1);
    const int inset = gui.contentTopInset();
    const int colsBase = 80;
    const int contentRows = 20;
    const int chromeRows = 2;
    const int gapX = cw / 2;   // 0 < gapX < cw
    const int gapY = ch / 2;   // 0 < gapY < ch
    const int targetW = colsBase * cw + gapX;
    const int targetUsableH = (contentRows + chromeRows) * ch + gapY;
    const int targetH = inset + targetUsableH;

    auto resizeAndMeasure = [&](int w, int h) {
        SDL_SetWindowSize(sdl.win, w, h);
        SDL_PumpEvents();
        int outW = 0, outH = 0;
        SDL_GetRendererOutputSize(sdl.ren, &outW, &outH);
        sdl.drawableW = outW;
        sdl.drawableH = outH;
        gui.setDrawableMetrics(outW, outH, gui.contentTopInset());
        return std::pair<int, int>{outW, outH};
    };

    auto [outW, outH] = resizeAndMeasure(targetW, targetH);
    CHECK_EQ(outW, targetW);
    CHECK_EQ(outH, targetH);
    const int usableH = outH - inset;
    CHECK_EQ(usableH % ch, gapY);
    CHECK_EQ(outW % cw, gapX);
    const int gridRows = usableH / ch;
    CHECK_EQ(gridRows, contentRows + chromeRows);

    NullTestSink sink;
    Document doc;
    doc.restore({"hola", "mundo", "linea 3"});
    // Suficientes líneas para que un desplazamiento de viewport sea válido
    // con 20 filas de contenido.
    std::vector<std::string> extraLines(gridRows + 10, "");
    doc.insertBlock(doc.lineCount(), 0, extraLines);
    Cursor cur;
    Message msg{std::string("Guardado"), MessageKind::Info, std::nullopt};
    Viewport vp = makeVp(gridRows - chromeRows, outW / cw);

    gui.setCaptureEnabled(true);
    auto render = [&] {
        gui.renderScreenDiff(doc, cur, vp, "t.txt", false,
                             makeChromeRequest(msg, State::Navegacion), sink,
                             std::nullopt, std::nullopt, std::nullopt);
    };
    render();

    const Layout lo = gui.lastLayout();
    const guichrome::FramePixelGeometry fg = gui.lastFrameGeometry();
    CHECK(fg.valid);
    CHECK(fg.exact);
    CHECK_EQ(lo.content.height, contentRows);
    CHECK_EQ(lo.chrome.height, chromeRows);
    CHECK_EQ(fg.logicalWidth, colsBase * cw);
    CHECK_EQ(fg.logicalHeight, (contentRows + chromeRows) * ch);
    CHECK_EQ(fg.rightSpacerPx, gapX);
    CHECK_EQ(fg.preChromeSpacerPx, gapY);
    CHECK_EQ(fg.width, outW);
    CHECK_EQ(fg.height, usableH);

    // El framebuffer activo (no la capacidad reservada) mide exactamente el
    // área disponible del drawable.
    std::vector<GuiColor> fb;
    int fbW = 0, fbH = 0;
    CHECK(gui.readLastSurfacePixels(fb, fbW, fbH));
    CHECK_EQ(fbW, outW);
    CHECK_EQ(fbH, usableH);
    CHECK_EQ(static_cast<int>(fb.size()), fbW * fbH);
    const GuiColor bg = guichrome::background(gui.isDarkTheme());
    auto fbPx = [&](int x, int y) {
        return fb[static_cast<size_t>(y) * static_cast<size_t>(fbW) +
                  static_cast<size_t>(x)];
    };
    // Pseudo columna: fondo de la línea actual (CurrentLine) en la fila del cursor.
    CHECK(fbPx(fg.logicalWidth + gapX / 2, ch / 2) ==
          guichrome::currentLineBackground(gui.isDarkTheme()));
    // Pseudo fila (entre contenido y chrome): fondo.
    CHECK(fbPx(cw / 2, contentRows * ch + gapY / 2) == bg);

    // Ventana compuesta: el borde derecho y la pseudo fila también son fondo,
    // y el chrome sigue anclado al borde inferior (no se desplaza dos veces).
    std::vector<GuiColor> win;
    int winW = 0, winH = 0;
    CHECK(gui.readWindowPixels(win, winW, winH));
    CHECK_EQ(winW, outW);
    CHECK_EQ(winH, outH);
    auto winPx = [&](int x, int y) {
        return win[static_cast<size_t>(y) * static_cast<size_t>(winW) +
                   static_cast<size_t>(x)];
    };
    CHECK(winPx(outW - 1, inset + ch / 2) ==
          guichrome::currentLineBackground(gui.isDarkTheme()));
    CHECK(winPx(cw / 2, inset + contentRows * ch + gapY / 2) == bg);
    const guichrome::BottomChromeGeometry chrome =
        guichrome::bottomChromeGeometry(usableH, ch, contentRows, chromeRows);
    CHECK(chrome.anchored);
    CHECK(winPx(cw / 2, inset + chrome.statusTop + ch / 2) ==
          guichrome::statusBackground(gui.isDarkTheme()));
    CHECK(winPx(cw / 2, inset + chrome.messageTop + ch / 2) ==
          guichrome::messageBackground(gui.isDarkTheme()));
    gui.setCaptureEnabled(false);

    // Resize de 1 px en ambos ejes, sin cambiar filas/columnas: el área
    // activa crece, pero la capacidad reservada absorbe el cambio. La
    // textura NO se recrea.
    const long long creations = gui.framebufCreations();
    auto [outW2, outH2] = resizeAndMeasure(targetW + 1, targetH + 1);
    CHECK_EQ(outW2, targetW + 1);
    CHECK_EQ(outH2, targetH + 1);
    render();
    CHECK_EQ(gui.framebufCreations(), creations);
    const guichrome::FramePixelGeometry fg2 = gui.lastFrameGeometry();
    CHECK(fg2.exact);
    CHECK_EQ(fg2.rightSpacerPx, gapX + 1);
    CHECK_EQ(fg2.preChromeSpacerPx, gapY + 1);
    CHECK(gui.readLastSurfacePixels(fb, fbW, fbH));
    CHECK_EQ(fbW, outW2);
    CHECK_EQ(fbH, outH2 - inset);

    // Cruzar un borde de columna cambia el layout lógico y exige más
    // capacidad: la textura se recrea (una vez), como corresponde.
    auto [outW3, outH3] =
        resizeAndMeasure(targetW + 1 + cw, targetH + 1);
    CHECK_EQ(outW3 / cw, outW2 / cw + 1);
    // La columna lógica nueva debe entrar en el layout: el viewport manda.
    vp = makeVp(gridRows - chromeRows, outW3 / cw);
    const long long beforeColumn = gui.framebufCreations();
    render();
    CHECK(gui.framebufCreations() > beforeColumn);
    CHECK(gui.lastFrameGeometry().exact);

    // 1. Scroll rápido (ping-pong entre las dos superficies de contenido).
    //    Debe ejecutar tryFastScrollLine y no caer al camino full.
    //    Comprobamos explícitamente con !gui.lastWasFull().
    Viewport vpScroll = makeVp(gridRows - chromeRows, outW3 / cw);
    vpScroll.top = 1;
    Cursor curScroll = cur;
    curScroll.line = 1;
    gui.renderScreenDiff(doc, curScroll, vpScroll, "t.txt", false,
                         makeChromeRequest(msg, State::Navegacion), sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(!gui.lastWasFull());  // tryFastScrollLine tuvo éxito (no cayó a full)
    CHECK(gui.readLastSurfacePixels(fb, fbW, fbH));
    CHECK_EQ(fbW, outW3);
    CHECK_EQ(fbH, outH3 - inset);

    // 2. Render parcial real: modificar una línea del documento entre
    //    renders para forzar una fila dirty, y comprobar que se usa el
    //    camino parcial (no full) sobre la textura cuya capacidad puede
    //    ser mayor que el área activo.
    doc.insertChar(0, 0, 'X');
    render();
    CHECK(!gui.lastWasFull());

    // 3. Reducción del drawable seguida de crecimiento, sin superar la
    //    capacidad reservada: la textura no debe recrearse y el área activa
    //    sigue cubierta en modo exacto.
    const long long beforeShrink = gui.framebufCreations();
    auto [outW4, outH4] = resizeAndMeasure(targetW - 1, targetH - 1);
    CHECK_EQ(outW4, targetW - 1);
    CHECK_EQ(outH4, targetH - 1);
    {
        const int rows4 = (outH4 - inset) / ch;
        vp = makeVp(rows4 - chromeRows, outW4 / cw);
    }
    render();
    CHECK_EQ(gui.framebufCreations(), beforeShrink);
    const guichrome::FramePixelGeometry fg4 = gui.lastFrameGeometry();
    CHECK(fg4.exact);
    CHECK_EQ(fg4.width, outW4);
    CHECK_EQ(fg4.height, outH4 - inset);

    auto [outW5, outH5] = resizeAndMeasure(targetW + 1, targetH + 1);
    CHECK_EQ(outW5, targetW + 1);
    CHECK_EQ(outH5, targetH + 1);
    {
        const int rows5 = (outH5 - inset) / ch;
        vp = makeVp(rows5 - chromeRows, outW5 / cw);
    }
    render();
    CHECK_EQ(gui.framebufCreations(), beforeShrink);
    const guichrome::FramePixelGeometry fg5 = gui.lastFrameGeometry();
    CHECK(fg5.exact);
    CHECK_EQ(fg5.width, outW5);
    CHECK_EQ(fg5.height, outH5 - inset);
#endif
}
