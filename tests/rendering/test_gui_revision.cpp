// Revisión por componente: GuiRenderer.
//
// Cubre lo pedido en la revisión:
//   1. Selección + búsqueda simultáneas: resultado visual y prioridad de
//      estilos (no solo que se activa el camino diferencial). FrameBuilder
//      fusiona ambos en StyleRole::Selection; el test afirma el DTO visual.
//   2. Stress scroll/resize/tema en DOS entornos:
//      - headless (sin ventana): invalidaciones, contadores y estado diff.
//      - SDL con renderer (dummy): texturas reales, framebuffers, resize y
//        destrucción de recursos. Lo headless NO valida GPU.
//   3. Cambio de tema con prompt activo: texto, fondo, cursor Bar y acento
//      del MessageBar se representan con el tema nuevo. No se afirma que
//      haya que vaciar texturas: el color va en la clave de GuiFont, el
//      clearCache del toggle es ahorro de memoria, no corrección.

#include <string>
#include <vector>

#include "test_framework.h"

#include "app/ChromePresentation.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "layout/Viewport.h"
#include "rendering/Sink.h"
#include "rendering/Style.h"
#include "rendering/gui/GuiChrome.h"
#include "rendering/gui/GuiRenderer.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

#include <cstdlib>
#endif

namespace {

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

ChromeRequest navChrome() {
    Message msg{std::string(""), MessageKind::Info, std::nullopt};
    return makeChromeRequest(msg, State::Navegacion);
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Texto concatenado de los segmentos con un rol dado (para afirmar el
// resultado visual fusionado, sin contar el gutter: su rol nunca es
// Selection/BracketMatch).
std::string concatRole(const GuiContentRow& row, StyleRole role) {
    std::string out;
    for (const auto& s : row.segs)
        if (s.role == role) out += s.text;
    return out;
}

// Rol del segmento que cubre la columna visible `col` del TEXTO de la fila
// (0-based, sin contar el gutter: los segmentos de gutter se saltan). Para
// el ASCII de estos tests la columna es el offset en bytes. Fuera de rango
// devuelve StyleRole::Default.
StyleRole roleAtTextCol(const GuiContentRow& row, int col) {
    int cur = 0;
    for (const auto& s : row.segs) {
        if (s.role == StyleRole::Gutter ||
            s.role == StyleRole::GutterCurrent ||
            s.role == StyleRole::GutterBlank)
            continue;
        const int w = chrome::colCount(s.text);
        if (col >= cur && col < cur + w) return s.role;
        cur += w;
    }
    return StyleRole::Default;
}

#ifdef HAVE_SDL2
// Fixture SDL aislado: solo libera lo que inicializó. Si el subsistema de
// video ya estaba inicializado antes del fixture, SDL_VIDEODRIVER=dummy no
// tendría efecto (el driver se elige en el primer init) y los píxeles no
// serían comparables: en ese caso se marca videoAlreadyInit_ para que el
// test haga SKIP en vez de usar un driver ajeno. El destructor solo deshace
// SDL_INIT_VIDEO si este fixture lo inicializó (nunca SDL_Quit global sobre
// subsistemas de otros).
struct SdlDummyRev {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    bool ok = false;
    bool didInit_ = false;
    bool videoAlreadyInit_ = false;
    std::string prevVideo;
    std::string prevAudio;
    bool hadVideo = false;
    bool hadAudio = false;
    SdlDummyRev(int w = 800, int h = 600) {
        if (const char* p = std::getenv("SDL_VIDEODRIVER")) {
            prevVideo = p;
            hadVideo = true;
        }
        if (const char* p = std::getenv("SDL_AUDIODRIVER")) {
            prevAudio = p;
            hadAudio = true;
        }
        if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
            videoAlreadyInit_ = true;
            return;
        }
        ::setenv("SDL_VIDEODRIVER", "dummy", 1);
        ::setenv("SDL_AUDIODRIVER", "dummy", 1);
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return;
        didInit_ = true;
        win = SDL_CreateWindow("maestro-gui-revision", 0, 0, w, h,
                               SDL_WINDOW_HIDDEN);
        if (!win) return;
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
        ok = (ren != nullptr);
    }
    ~SdlDummyRev() {
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        if (didInit_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
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

// Lee la superficie persistente (vía portable readLastSurfacePixels).
// Devuelve también sus dimensiones en píxeles: el framebuffer mide la
// grilla (ancho chrome × celdas, alto contenido+chrome × celdas), no la
// ventana SDL (ver framePxSize en GuiRenderer.cpp).
bool readSurfaceRev(GuiRenderer& gui, std::vector<GuiColor>& out, int& wPx,
                    int& hPx) {
    wPx = 0;
    hPx = 0;
    if (!gui.readLastSurfacePixels(out, wPx, hPx) || out.empty()) {
        out.clear();
        wPx = 0;
        hPx = 0;
        return false;
    }
    return true;
}

bool readSurfaceRev(GuiRenderer& gui, std::vector<GuiColor>& out) {
    int w = 0, h = 0;
    return readSurfaceRev(gui, out, w, h);
}

// Dimensiones esperadas del framebuffer para el layout actual: la textura
// persistente mide la grilla completa (contenido + chrome) en píxeles.
void expectedFbDims(const GuiRenderer& gui, int& wPx, int& hPx) {
    const Layout lo = gui.lastLayout();
    wPx = lo.chrome.width * gui.cellW();
    hPx = (lo.content.height + lo.chrome.height) * gui.cellH();
}

// Píxel (x,y) de una superficie en row-major de ancho wPx.
GuiColor surfPx(const std::vector<GuiColor>& px, int wPx, int x, int y) {
    return px[static_cast<size_t>(y) * static_cast<size_t>(wPx) +
              static_cast<size_t>(x)];
}

// ¿Hay al menos un píxel exacto `want` en [x0,x1)×[y0,y1)? Para texto con
// fuente los interiores de glifo dan el fg exacto (los bordes son mezcla
// antialiada); los fills (fondos, barra) son exactos en toda la celda.
bool surfHas(const std::vector<GuiColor>& px, int wPx, int hPx, int x0, int x1,
             int y0, int y1, const GuiColor& want) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > wPx) x1 = wPx;
    if (y1 > hPx) y1 = hPx;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (surfPx(px, wPx, x, y) == want) return true;
    return false;
}
#endif

}  // namespace

// ---------------------------------------------------------------------------
// 1. Selección + búsqueda simultáneas: visual + prioridad.
// ---------------------------------------------------------------------------

// Solape parcial: la unión se pinta como un único bloque Selection, sin
// hueco Default en medio, aunque FrameBuilder reciba dos intervalos.
TEST(gui_revision_seleccion_y_busqueda_solapadas_fusion_visual) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo hola"});
    Viewport vp = makeVp(5, 40);
    Cursor cur;
    // sel [0,4) + search [2,8): unión [0,8) = "hola mun".
    Selection sel{{0, 0}, {0, 4}};
    Selection search{{0, 2}, {0, 8}};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, navChrome(), sink, sel,
                         search, std::nullopt);
    CHECK(!gui.lastContentRows().empty());
    const GuiContentRow& row = gui.lastContentRows()[0];
    CHECK(row.hasRole(StyleRole::Selection));
    // Todo el tramo unido lleva Selection: el texto seleccionado concatenado
    // cubre la unión sin cortes, y cada columna de la unión tiene ese rol
    // (sin huecos) mientras que la columna siguiente ya no.
    CHECK_EQ(concatRole(row, StyleRole::Selection), "hola mun");
    for (int c = 0; c < 8; ++c)
        CHECK_EQ(static_cast<int>(roleAtTextCol(row, c)),
                 static_cast<int>(StyleRole::Selection));
    for (int c = 8; c < 15; ++c)
        CHECK(roleAtTextCol(row, c) != StyleRole::Selection);
}

// Disjuntos: dos bloques Selection separados, el medio sin destacar.
TEST(gui_revision_seleccion_y_busqueda_disjuntas_dos_bloques) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo hola"});
    Viewport vp = makeVp(5, 40);
    Cursor cur;
    Selection sel{{0, 0}, {0, 4}};     // primer "hola"
    Selection search{{0, 11}, {0, 15}};  // segundo "hola"
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, navChrome(), sink, sel,
                         search, std::nullopt);
    const GuiContentRow& row = gui.lastContentRows()[0];
    CHECK_EQ(concatRole(row, StyleRole::Selection), "holahola");
    // Colocación por columna: [0,4) y [11,15) en Selection, el medio fuera.
    for (int c = 0; c < 4; ++c)
        CHECK_EQ(static_cast<int>(roleAtTextCol(row, c)),
                 static_cast<int>(StyleRole::Selection));
    for (int c = 4; c < 11; ++c)
        CHECK(roleAtTextCol(row, c) != StyleRole::Selection);
    for (int c = 11; c < 15; ++c)
        CHECK_EQ(static_cast<int>(roleAtTextCol(row, c)),
                 static_cast<int>(StyleRole::Selection));
    CHECK(contains(row.plain(), "hola mundo hola"));
}

// Prioridad: Selection gana sobre sintaxis y sobre BracketMatch en el mismo
// tramo (Style.h: Selection > BracketMatch > sintaxis). Ni el keyword ni el
// paréntesis "roban" celdas dentro de la unión selección+búsqueda.
TEST(gui_revision_seleccion_gana_a_sintaxis_y_bracket) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"int f() { return 0; }"});
    Viewport vp = makeVp(5, 40);
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    // Cubre "int" (keyword) y el '(' del par: ambos dentro de Selection.
    Selection sel{{0, 0}, {0, 4}};
    Selection search{{0, 3}, {0, 6}};
    // '{' en 8, '}' en 20 (ver test existente de bracket).
    BracketPair pair{{0, 8}, {0, 20}};
    gui.renderScreenDiff(doc, cur, vp, "t.cpp", false, navChrome(), sink, sel,
                         search, pair);
    const GuiContentRow& row = gui.lastContentRows()[0];
    CHECK(row.hasRole(StyleRole::Selection));
    // La unión [0,6) entera en Selection.
    CHECK_EQ(concatRole(row, StyleRole::Selection), "int f(");
    // BracketMatch sobrevive FUERA de la selección (el par '{'/'}'), pero
    // no dentro: ningún segmento BracketMatch contiene texto de la unión.
    CHECK(row.hasRole(StyleRole::BracketMatch));
    for (const auto& s : row.segs) {
        if (s.role != StyleRole::BracketMatch) continue;
        CHECK(s.text.find("int") == std::string::npos);
        CHECK(s.text.find("(") == std::string::npos);
    }
    // La sintaxis tampoco fragmenta la unión: ningún segmento de sintaxis
    // contiene "int" (el keyword quedó absorbido por Selection).
    for (const auto& s : row.segs) {
        if (!isSyntaxRole(s.role)) continue;
        CHECK(s.text.find("int") == std::string::npos);
    }
}

// El caso degenerado selección==búsqueda en el mismo rango no duplica ni
// rompe: un solo bloque Selection idéntico al de cada efecto por separado.
TEST(gui_revision_seleccion_igual_a_busqueda_mismo_bloque) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo"});
    Viewport vp = makeVp(5, 40);
    Cursor cur;
    Selection both{{0, 0}, {0, 4}};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, navChrome(), sink, both,
                         both, std::nullopt);
    CHECK_EQ(concatRole(gui.lastContentRows()[0], StyleRole::Selection),
             "hola");
    // Paridad con cada efecto aislado.
    GuiRenderer soloSel, soloSearch;
    soloSel.renderScreenDiff(doc, cur, vp, "t.txt", false, navChrome(), sink,
                             both, std::nullopt, std::nullopt);
    soloSearch.renderScreenDiff(doc, cur, vp, "t.txt", false, navChrome(),
                                sink, std::nullopt, both, std::nullopt);
    CHECK_EQ(concatRole(soloSel.lastContentRows()[0], StyleRole::Selection),
             "hola");
    CHECK_EQ(concatRole(soloSearch.lastContentRows()[0], StyleRole::Selection),
             "hola");
}

// ---------------------------------------------------------------------------
// 2a. Stress headless: scroll rápido + resize + tema.
// Solo invalidaciones, contadores y estado del diff. No afirma píxeles.
// ---------------------------------------------------------------------------
TEST(gui_revision_stress_headless_scroll_resize_tema) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 60; ++i)
        lines.push_back("linea numero " + std::to_string(i) + " con texto");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    vp.top = 10;
    Cursor cur;
    cur.line = 12;

    auto render = [&](Viewport v, Cursor c) {
        gui.renderScreenDiff(doc, c, v, "t.txt", false, navChrome(), sink,
                             std::nullopt, std::nullopt, std::nullopt);
    };

    render(vp, cur);
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 10);
    const long long frames0 = gui.totalFrames();

    // Scroll rápido: saltos grandes consecutivos (AvPág simulado).
    for (int top : {20, 5, 30, 0, 25}) {
        vp.top = top;
        cur.line = top + 2;
        render(vp, cur);
        // El scroll no es full (misma geometría/tema), pero repinta y el
        // snapshot sigue representando docLine top+i.
        CHECK(!gui.lastWasFull());
        CHECK(gui.lastRepainted() > 0);
        CHECK_EQ(gui.lastRepainted() + gui.lastSkipped(), 10);
        CHECK(contains(gui.lastContentRows()[0].plain(),
                       "linea numero " + std::to_string(top)));
    }

    // Resize: cambia el ancho -> frame completo.
    vp.top = 10;
    cur.line = 12;
    vp.width = 60;
    render(vp, cur);
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 10);
    // Estable tras el resize: steady con 0 filas.
    render(vp, cur);
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    CHECK_EQ(gui.lastSkipped(), 10);

    // Tema: fuerza completo y el estado sigue coherente.
    gui.toggleTheme();
    render(vp, cur);
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 10);
    render(vp, cur);
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    CHECK(!gui.lastChromeDirty());

    // Edición de una línea: repintado acotado, sin full.
    doc.insertText(11, 0, "XX");
    render(vp, cur);
    CHECK(!gui.lastWasFull());
    CHECK(gui.lastRepainted() >= 1);
    CHECK(gui.lastRepainted() <= 3);
    CHECK(contains(gui.lastContentRows()[1].plain(), "XX"));

    CHECK(gui.totalFrames() > frames0);
    CHECK(gui.totalFull() >= 3);  // inicial + resize + tema
    CHECK(gui.hasCache());
}

// ---------------------------------------------------------------------------
// 2b. Stress SDL: texturas reales, framebuffers, resize y destrucción.
// Requiere renderer real (dummy+software): sin SDL se skipea.
// ---------------------------------------------------------------------------
TEST(gui_revision_stress_sdl_framebuffer_resize_y_recursos) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyRev sdl;
    if (sdl.videoAlreadyInit_)
        SKIP("SDL video ya inicializado: sin driver dummy aislado");
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 40; ++i)
        lines.push_back("linea SDL " + std::to_string(i) + " de prueba");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 2;

    auto render = [&](Viewport v, Cursor c) {
        gui.renderScreenDiff(doc, c, v, "t.txt", false, navChrome(), sink,
                             std::nullopt, std::nullopt, std::nullopt);
    };

    render(vp, cur);
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> ref;
    int refW = 0, refH = 0;
    if (!readSurfaceRev(gui, ref, refW, refH)) SKIP("ReadPixels no disponible");
    {
        int expW = 0, expH = 0;
        expectedFbDims(gui, expW, expH);
        CHECK(expW > 0 && expH > 0);
        CHECK_EQ(refW, expW);
        CHECK_EQ(refH, expH);
        CHECK_EQ(static_cast<int>(ref.size()), refW * refH);
    }

    // Scroll +1: el framebuffer persiste y cambia solo lo necesario; la
    // superficie sigue legible y equivale al full del mismo estado.
    vp.top = 1;
    cur.line = 3;
    render(vp, cur);
    CHECK(!gui.lastWasFull());
    std::vector<GuiColor> scrolled;
    CHECK(readSurfaceRev(gui, scrolled));
    CHECK(scrolled != ref);
    gui.invalidateCache();
    render(vp, cur);
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> full;
    CHECK(readSurfaceRev(gui, full));
    CHECK(full == scrolled);

    // Resize LÓGICO del viewport (columnas 40 -> 60): el layout cambia, la
    // textura se recrea con las dimensiones nuevas y el frame es completo.
    // Se verifica tamaño (w/h en píxeles) además de píxeles, y que el
    // diferencial tras el resize equivale al full del mismo estado.
    vp.width = 60;
    render(vp, cur);
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> resized;
    int resW = 0, resH = 0;
    CHECK(readSurfaceRev(gui, resized, resW, resH));
    {
        int expW = 0, expH = 0;
        expectedFbDims(gui, expW, expH);
        CHECK_EQ(gui.lastLayout().chrome.width, 60);
        CHECK_EQ(resW, expW);
        CHECK_EQ(resH, expH);
        CHECK_EQ(static_cast<int>(resized.size()), resW * resH);
        CHECK(resW != refW);  // el ancho en píxeles siguió a las columnas
        CHECK(resH == refH);  // el alto no cambió (misma altura de viewport)
    }
    CHECK(resized != full);
    render(vp, cur);
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    std::vector<GuiColor> steady;
    CHECK(readSurfaceRev(gui, steady));
    CHECK(steady == resized);
    // Diferencial post-resize == full del mismo estado y dimensiones.
    gui.invalidateCache();
    render(vp, cur);
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> fullResized;
    int fullW = 0, fullH = 0;
    CHECK(readSurfaceRev(gui, fullResized, fullW, fullH));
    CHECK_EQ(fullW, resW);
    CHECK_EQ(fullH, resH);
    CHECK(fullResized == resized);

    // Resize SDL de la VENTANA (no del viewport): cambia el drawable de la
    // ventana, no la grilla. La superficie persistente mide la grilla
    // (framePxSize), así que sus dimensiones NO cambian y sus píxeles
    // tampoco (present copia 1:1 arriba-izquierda; el resto es clear).
    SDL_SetWindowSize(sdl.win, 1024, 768);
    {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(sdl.win, &winW, &winH);
        CHECK(winW == 1024 && winH == 768);
    }
    render(vp, cur);
    std::vector<GuiColor> afterWinResize;
    int winResW = 0, winResH = 0;
    CHECK(readSurfaceRev(gui, afterWinResize, winResW, winResH));
    CHECK_EQ(winResW, resW);
    CHECK_EQ(winResH, resH);
    CHECK(afterWinResize == resized);

    // Tema con SDL: la superficie del tema nuevo difiere y persiste.
    gui.toggleTheme();
    render(vp, cur);
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> themed;
    CHECK(readSurfaceRev(gui, themed));
    CHECK(themed != steady);

    // Ciclo de vida REAL del renderer (obligación de GuiFont.h, igual que
    // GuiRunLoop al salir: desacoplar ANTES de destruir). Se destruye el
    // SDL_Renderer de verdad (sus texturas mueren con él), se crea otro y se
    // re-inyecta. Sin renderer no hay superficie; con el nuevo, el primer
    // render reconstruye los framebuffers y pinta la misma imagen, incluso
    // sin invalidar el caché de contenido (los snapshots son DTOs lógicos y
    // la textura fresca fuerza repintado completo interno).
    gui.setSdlRenderer(nullptr);
    CHECK(!readSurfaceRev(gui, ref));  // sin renderer no hay superficie
    SDL_DestroyRenderer(sdl.ren);
    sdl.ren = nullptr;
    sdl.ren = SDL_CreateRenderer(sdl.win, -1, SDL_RENDERER_SOFTWARE);
    if (!sdl.ren) sdl.ren = SDL_CreateRenderer(sdl.win, -1, 0);
    CHECK(sdl.ren != nullptr);
    gui.setSdlRenderer(sdl.ren);
    render(vp, cur);
    std::vector<GuiColor> revived;
    int revW = 0, revH = 0;
    CHECK(readSurfaceRev(gui, revived, revW, revH));
    CHECK_EQ(revW, resW);
    CHECK_EQ(revH, resH);
    CHECK(revived == themed);
    // Y la referencia completa del mismo estado sigue coincidiendo.
    gui.invalidateCache();
    render(vp, cur);
    CHECK(gui.lastWasFull());
    CHECK(readSurfaceRev(gui, ref));
    CHECK(ref == themed);
#endif
}

// ---------------------------------------------------------------------------
// 3. Cambio de tema con prompt activo (Bar en el MessageBar).
// ---------------------------------------------------------------------------

// Headless: el DTO y los colores que el pintor resuelve para cada elemento
// usan el tema nuevo. Se resuelve POR EL MISMO CAMINO que el pintor de
// chrome (segmento del chrome -> colorFor(rol, dark); fondos -> tablas por tema),
// no solo que las tablas difieren. No se afirma nada sobre el caché de
// texturas: el color va en la clave (GuiFont.h), así que vaciarlo en el
// toggle es ahorro de memoria, no corrección.
TEST(gui_revision_tema_con_prompt_representa_todo_headless) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo", "segunda fila"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 100;  // fuera del viewport: solo vive el cursor del prompt

    Message prompt{std::string("Find: ho"), MessageKind::Prompt, std::nullopt};
    prompt.cursor = 8;
    ChromeRequest cr = makeChromeRequest(prompt, State::Busqueda);

    // Estado dark de arranque.
    CHECK(gui.isDarkTheme());
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, cr, sink, std::nullopt,
                         std::nullopt, std::nullopt);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Bar);
    const bool dark = true;
    const GuiColor textDark =
        guichrome::messageColor(MessageKind::Prompt, dark);
    const GuiColor bgDark = guichrome::background(dark);
    const GuiColor mbgDark = guichrome::messageBackground(dark);
    const GuiColor sbgDark = guichrome::statusBackground(dark);
    const GuiColor barDark = guichrome::cursorColor(dark);
    const GuiColor accentDark =
        guichrome::colorFor(gui.lastAccent(), dark);
    const std::string plainBefore = gui.lastContentRows()[0].plain();
    const ChromeData chromeBefore = gui.lastChrome();

    // Tablas: el tema claro usa otros RGB en las 5 familias del prompt.
    CHECK(guichrome::messageColor(MessageKind::Prompt, false) != textDark);
    CHECK(guichrome::background(false) != bgDark);
    CHECK(guichrome::messageBackground(false) != mbgDark);
    CHECK(guichrome::statusBackground(false) != sbgDark);
    CHECK(guichrome::cursorColor(false) != barDark);
    CHECK(guichrome::colorFor(gui.lastAccent(), false) != accentDark);

    // Toggle con el prompt activo: próximo frame completo.
    gui.toggleTheme();
    CHECK(!gui.isDarkTheme());
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, cr, sink, std::nullopt,
                         std::nullopt, std::nullopt);
    CHECK(gui.lastWasFull());
    // Texto del prompt intacto (mismo DTO), cursor Bar sigue en el MessageBar
    // y el acento sigue siendo el del estado Busqueda (el ROL no cambia, su
    // RGB sí, ver tablas de arriba).
    CHECK_EQ(gui.lastContentRows()[0].plain(), plainBefore);
    CHECK(gui.lastChrome() == chromeBefore);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Bar);
    CHECK_EQ(static_cast<int>(gui.lastAccent()),
             static_cast<int>(StyleRole::AccentGuardar));
    // Colores resueltos por el camino del pintor con el tema nuevo: cada
    // segmento del chrome lleva el RGB nuevo, distinto del viejo. El cursor
    // Bar cae dentro de las filas del chrome.
    {
        const bool nowDark = gui.isDarkTheme();
        const int chw = gui.lastLayout().chrome.width;
        bool sawPrompt = false;
        for (const auto& s :
             guichrome::messageSegments(gui.lastChrome().message, chw)) {
            if (s.role != StyleRole::MsgPrompt || !contains(s.text, "Find:"))
                continue;
            sawPrompt = true;
            CHECK(guichrome::colorFor(s.role, nowDark) ==
                  guichrome::messageColor(MessageKind::Prompt, nowDark));
            CHECK(guichrome::colorFor(s.role, nowDark) != textDark);
        }
        CHECK(sawPrompt);
        bool sawAccent = false;
        for (const auto& s : guichrome::statusSegments(
                 gui.lastChrome().statusBar, gui.lastAccent(), chw)) {
            if (!contains(s.text, "BUSQUEDA")) continue;
            CHECK_EQ(static_cast<int>(s.role),
                     static_cast<int>(StyleRole::AccentGuardar));
            sawAccent = true;
            CHECK(guichrome::colorFor(s.role, nowDark) != accentDark);
        }
        CHECK(sawAccent);
        const FrameCursor c = gui.lastCursor();
        CHECK(c.cell.valid());
        CHECK(c.cell.row >= gui.lastLayout().chrome.row &&
              c.cell.row < gui.lastLayout().chrome.row +
                               gui.lastLayout().chrome.height);
        CHECK(guichrome::cursorColor(nowDark) != barDark);
        CHECK(guichrome::messageBackground(nowDark) != mbgDark);
        CHECK(guichrome::statusBackground(nowDark) != sbgDark);
    }
    // Fondos resueltos con el tema nuevo.
    CHECK(guichrome::styleFor(StyleRole::Default, false,
                              gui.isDarkTheme())
              .bg == guichrome::background(gui.isDarkTheme()));
    CHECK(guichrome::styleFor(StyleRole::Selection, false,
                              gui.isDarkTheme())
              .bg == guichrome::selectionBackground(gui.isDarkTheme()));
    // Steady posterior: 0 filas de contenido pero el chrome del prompt se
    // sigue repintando por la barra (animación), sin full.
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, cr, sink, std::nullopt,
                         std::nullopt, std::nullopt);
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    CHECK(gui.lastCursor().visible);
}

// SDL: cada elemento del prompt adopta el color nuevo en píxeles (fondo,
// texto, cursor Bar y acento muestreados en sus regiones), y la superficie
// tras el cambio equivale a un render completo de referencia del tema nuevo.
// Sin fuente solo hay fills: el texto/acento se verifica si hay ráster.
TEST(gui_revision_tema_con_prompt_paridad_pixeles_sdl) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyRev sdl;
    if (sdl.videoAlreadyInit_)
        SKIP("SDL video ya inicializado: sin driver dummy aislado");
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    const bool haveFont = gui.hasFont();
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo", "segunda fila"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 100;

    Message prompt{std::string("Find: ho"), MessageKind::Prompt, std::nullopt};
    prompt.cursor = 8;
    ChromeRequest cr = makeChromeRequest(prompt, State::Busqueda);

    // Geometría del chrome (estable en todo el test: mismo viewport).
    // El cursor Bar del prompt vive en la fila del MessageBar; el texto
    // "Find: ho" ocupa las celdas [chrome.col+1, chrome.col+9) y la barra la
    // celda siguiente (se excluye del escaneo de texto: su fg coincide con
    // el del texto en el tema claro).
    auto render = [&] {
        gui.renderScreenDiff(doc, cur, vp, "t.txt", false, cr, sink,
                             std::nullopt, std::nullopt, std::nullopt);
    };
    gui.setCaptureEnabled(true);
    render();
    CHECK(gui.lastWasFull());
    const Layout lo = gui.lastLayout();
    const int cw = gui.cellW(), ch = gui.cellH();
    const int stRow = lo.chrome.row, msgRow = lo.chrome.row + 1;
    const int rowX0 = lo.chrome.col * cw;
    const int rowX1 = (lo.chrome.col + lo.chrome.width) * cw;
    const int textX0 = (lo.chrome.col + 1) * cw;
    const int textX1 = (lo.chrome.col + 9) * cw;
    const CellPos barCell = gui.lastCursor().cell;
    CHECK(barCell.valid());
    CHECK_EQ(barCell.row, msgRow);
    CHECK_EQ(barCell.col, lo.chrome.col + 9);  // tras "Find: ho" (pad 1 + 8)

    // Colores esperados por tema para cada elemento.
    const GuiColor sbgD = guichrome::statusBackground(true);
    const GuiColor mbgD = guichrome::messageBackground(true);
    const GuiColor barD = guichrome::cursorColor(true);
    const GuiColor txtD = guichrome::messageColor(MessageKind::Prompt, true);
    const GuiColor accD = guichrome::colorFor(StyleRole::AccentGuardar, true);
    const GuiColor sbgL = guichrome::statusBackground(false);
    const GuiColor mbgL = guichrome::messageBackground(false);
    const GuiColor barL = guichrome::cursorColor(false);
    const GuiColor txtL = guichrome::messageColor(MessageKind::Prompt, false);
    const GuiColor accL = guichrome::colorFor(StyleRole::AccentGuardar, false);

    // Muestrea un pixel de fill (primera celda = pad, siempre espacios) y la
    // barra (2px sólidos), y escanea presencias en la fila correspondiente.
    auto checkPromptPixels = [&](const std::vector<GuiColor>& px, int wPx,
                                 int hPx, bool darkTheme) {
        const GuiColor sbg = darkTheme ? sbgD : sbgL;
        const GuiColor mbg = darkTheme ? mbgD : mbgL;
        const GuiColor bar = darkTheme ? barD : barL;
        const GuiColor txt = darkTheme ? txtD : txtL;
        const GuiColor acc = darkTheme ? accD : accL;
        const GuiColor oldAcc = darkTheme ? accL : accD;
        // Fondos: celda del pad (sin textura encima).
        CHECK(surfPx(px, wPx, rowX0 + cw / 2, stRow * ch + ch / 2) == sbg);
        CHECK(surfPx(px, wPx, rowX0 + cw / 2, msgRow * ch + ch / 2) == mbg);
        // Cursor Bar: interior de sus 2px.
        CHECK(surfPx(px, wPx, barCell.col * cw + 1,
                     barCell.row * ch + ch / 2) == bar);
        if (!haveFont) return;  // sin ráster no hay tinta que escanear
        // Texto del prompt en su franja (sin la celda de la barra): el fg
        // nuevo está presente. (No se afirma ausencia del fg viejo: en
        // dark->light el fg viejo es casi el fondo nuevo y el antialiado
        // del fg nuevo puede dar ese gris exacto; la dirección contraria
        // —textura rancia— la caza la presencia: sin repintar no habría
        // ningún píxel del fg nuevo.)
        CHECK(surfHas(px, wPx, hPx, textX0, textX1, msgRow * ch,
                      (msgRow + 1) * ch, txt));
        // Acento del estado en la fila del StatusBar: presente el nuevo y
        // ausente el viejo. Los azules saturados no pueden salir de mezclar
        // grises/blancos del antialiado, así que la ausencia es exacta y
        // robusta en ambas direcciones.
        CHECK(surfHas(px, wPx, hPx, rowX0, rowX1, stRow * ch, (stRow + 1) * ch,
                      acc));
        CHECK(!surfHas(px, wPx, hPx, rowX0, rowX1, stRow * ch, (stRow + 1) * ch,
                       oldAcc));
    };

    // 1. Tema dark: cada elemento con su color en píxeles de ventana (el
    // chrome se pinta por overlay en ventana, no en el framebuffer).
    std::vector<GuiColor> darkPx;
    int darkW = 0, darkH = 0;
    if (!gui.readWindowPixels(darkPx, darkW, darkH))
        SKIP("ReadPixels no disponible");
    CHECK(gui.isDarkTheme());
    checkPromptPixels(darkPx, darkW, darkH, true);

    // 2. Steady en dark: misma imagen de ventana.
    render();
    CHECK(!gui.lastWasFull());
    std::vector<GuiColor> darkSteady;
    CHECK(gui.readWindowPixels(darkSteady));
    CHECK(darkSteady == darkPx);

    // 3. Toggle con el prompt activo: frame completo y cada elemento con el
    // color nuevo (un solo fondo cambiado ya no bastaría para pasar).
    gui.toggleTheme();
    CHECK(!gui.isDarkTheme());
    render();
    CHECK(gui.lastWasFull());
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Bar);
    std::vector<GuiColor> lightPx;
    int lightW = 0, lightH = 0;
    CHECK(gui.readWindowPixels(lightPx, lightW, lightH));
    CHECK_EQ(lightW, darkW);
    CHECK_EQ(lightH, darkH);
    CHECK(lightPx != darkPx);
    checkPromptPixels(lightPx, lightW, lightH, false);

    // 4. Steady en el tema nuevo + referencia completa del mismo estado: las
    // tres imágenes coinciden píxel a píxel.
    render();
    CHECK(!gui.lastWasFull());
    std::vector<GuiColor> lightSteady;
    CHECK(gui.readWindowPixels(lightSteady));
    CHECK(lightSteady == lightPx);
    gui.invalidateCache();
    render();
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> lightFull;
    CHECK(gui.readWindowPixels(lightFull));
    CHECK(lightFull == lightPx);
#endif
}
