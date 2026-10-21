// Tests del caché diferencial por línea de GuiRenderer (rendimiento GUI).
//
// Sin SDL (snapshots + stats): verifican que el diff decide full vs parcial
// igual con o sin ventana (el pintado SDL solo ejecuta la decisión).
// Con SDL dummy: paridad de píxeles parcial == full (el parcial no cambia
// lo pintado, solo cuánto trabajo hace). Las lecturas de píxeles usan
// GuiRenderer::readLastSurfacePixels (textura persistente, portable): leer
// el backbuffer tras Present NO es portable (SDL2 lo declara invalidado).

#include <string>
#include <vector>

#include "test_framework.h"

#include "app/ChromePresentation.h"
#include "app/Editor.h"
#include "base/utf8.h"
#include "diagnostics/Instrument.h"
#include "document/Selection.h"
#include "layout/Gutter.h"
#include "layout/SoftWrap.h"
#include "platform/gui/GuiRunLoop.h"
#include "platform/gui/GuiSdlMapper.h"
#include "rendering/Sink.h"
#include "rendering/gui/GuiChrome.h"
#include "rendering/gui/GuiRenderer.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
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

void renderDoc(GuiRenderer& gui, NullTestSink& sink, Document& doc,
               Cursor& cur, Viewport& vp, const ChromeRequest& cr) {
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, cr, sink, std::nullopt,
                         std::nullopt, std::nullopt);
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Contenido con líneas que ocupan varias filas visuales con textWidth 17:
// L0 y L2/L3 usan 3 filas; la línea corta usa 1. Viewport 6 de alto deja
// visibles continuaciones de L0 y el comienzo de L2.
std::vector<std::string> softWrapLines() {
    return {std::string(40, 'a'), "corta", std::string(40, 'b'),
            std::string(40, 'c')};
}

void captureContent(GuiRenderer& gui, std::vector<std::string>& plain,
                    std::vector<bool>& current) {
    plain.clear();
    current.clear();
    for (const auto& row : gui.lastContentRows()) {
        plain.push_back(row.plain());
        current.push_back(row.isCurrentLine);
    }
}

#ifdef HAVE_SDL2
struct SdlDummyLocal {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    bool ok = false;
    std::string prevVideo;
    std::string prevAudio;
    bool hadVideo = false;
    bool hadAudio = false;
    SdlDummyLocal(int w = 400, int h = 300) {
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
        win = SDL_CreateWindow("maestro-diff-test", 0, 0, w, h, SDL_WINDOW_HIDDEN);
        if (!win) return;
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
        ok = (ren != nullptr);
    }
    ~SdlDummyLocal() {
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

// Lee la superficie persistente de `gui` (ver readLastSurfacePixels, única
// vía portable). Vacío + false si no hay framebuffer o falla ReadPixels.
bool readSurfaceLocal(GuiRenderer& gui, std::vector<GuiColor>& out) {
    int w = 0, h = 0;
    if (!gui.readLastSurfacePixels(out, w, h) || out.empty()) {
        out.clear();
        return false;
    }
    return true;
}
#endif

}  // namespace

TEST(gui_diff_primer_frame_es_completo) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola", "mundo"});
    Viewport vp = makeVp();
    Cursor cur;
    CHECK(!gui.hasCache());
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.hasCache());
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 5);  // contentH == height del viewport
    CHECK_EQ(gui.lastSkipped(), 0);
    CHECK(gui.lastChromeDirty());
}

TEST(gui_diff_pantalla_quieta_no_repinta_filas) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola", "mundo"});
    Viewport vp = makeVp();
    Cursor cur;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    CHECK_EQ(gui.lastSkipped(), 5);
    CHECK(!gui.lastChromeDirty());
    // El snapshot sigue idéntico (el parcial no degrada el DTO).
    const std::string before = gui.lastContentRows()[0].plain();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK_EQ(gui.lastContentRows()[0].plain(), before);
}

TEST(gui_diff_mover_cursor_repinta_dos_filas) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"a", "b", "c", "d", "e"});
    Viewport vp = makeVp();
    Cursor cur;  // (0,0): línea actual = fila 0
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    cur.line = 1;  // mueve el cursor: cambian los flags CurrentLine de 2 filas
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    // Fila vieja + fila nueva (más la del cursor si coincide, ya incluida).
    CHECK(gui.lastRepainted() <= 3);
    CHECK(gui.lastRepainted() >= 1);
    CHECK_EQ(gui.lastRepainted() + gui.lastSkipped(), 5);
}

TEST(gui_diff_editar_una_linea_repinta_acotado) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"aaa", "bbb", "ccc", "ddd", "eee"});
    Viewport vp = makeVp();
    Cursor cur;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    doc.insertText(2, 1, "X");  // solo la fila 2 cambia (+ chrome por col)
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    CHECK(gui.lastRepainted() <= 3);
    CHECK(contains(gui.lastContentRows()[2].plain(), "X"));
}

TEST(gui_diff_invalidateCache_fuerza_completo) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp();
    Cursor cur;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    gui.invalidateCache();
    CHECK(!gui.hasCache());
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 5);
}

TEST(gui_diff_toggle_theme_fuerza_completo) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp();
    Cursor cur;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    gui.toggleTheme();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 5);
}

TEST(gui_diff_resize_fuerza_completo) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp(5, 40);
    Cursor cur;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    Viewport wide = makeVp(5, 60);
    renderDoc(gui, sink, doc, cur, wide, navChrome());
    CHECK(gui.lastWasFull());
}

TEST(gui_diff_caches_editor_y_modal_no_hacen_thrash) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp();
    Cursor cur;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    std::vector<std::string> names = {"a.txt", "b.txt"};
    gui.renderBufferList(names, 0, 40, 5, sink);
    CHECK(gui.lastWasFull());  // primer modal: full en su propio caché
    gui.renderBufferList(names, 0, 40, 5, sink);
    CHECK(!gui.lastWasFull());  // steady del modal: parcial
    CHECK_EQ(gui.lastRepainted(), 0);
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    // Volver al editor reutiliza SU caché (no lo invalidó el modal).
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
}

TEST(gui_diff_modal_mover_seleccion_repinta_vieja_y_nueva) {
    GuiRenderer gui;
    NullTestSink sink;
    std::vector<std::string> names = {"a.txt", "b.txt", "c.txt"};
    gui.renderBufferList(names, 0, 40, 5, sink);
    gui.renderBufferList(names, 1, 40, 5, sink);
    CHECK(!gui.lastWasFull());
    // Fila 0 (pierde destacado) + fila 1 (lo gana).
    CHECK_EQ(gui.lastRepainted(), 2);
    CHECK_EQ(gui.lastRepainted() + gui.lastSkipped(), 5);
}

TEST(gui_diff_chrome_solo_no_repinta_contenido) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp();
    Cursor cur;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    // Mismo contenido, otro mensaje -> solo chrome dirty.
    Message busy{std::string("Guardado"), MessageKind::Info, std::nullopt};
    renderDoc(gui, sink, doc, cur, vp, makeChromeRequest(busy, State::Navegacion));
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    CHECK(gui.lastChromeDirty());
}

TEST(gui_diff_totales_acumulan_frames) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore({"hola"});
    Viewport vp = makeVp();
    Cursor cur;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK_EQ(gui.totalFrames(), 2);
    CHECK_EQ(gui.totalFull(), 1);
    CHECK_EQ(gui.totalRepainted(), 5);
    CHECK_EQ(gui.totalSkipped(), 5);
}

// Paridad de píxeles: el parcial pinta lo mismo que el full. Se fuerza un
// full (invalidate) y un parcial (steady + edición de 1 fila) y se comparan
// las superficies persistentes (lectura portable, no el backbuffer).
TEST(gui_sdl_parcial_iguala_full_en_pixeles) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);  // fuente real si hay (más tinta que verificar)
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo", "segunda fila", "tercera"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    // Full de referencia.
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> ref;
    if (!readSurfaceLocal(gui, ref)) SKIP("ReadPixels no disponible");
    // Steady parcial: sin cambios -> 0 filas, mismos píxeles.
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    std::vector<GuiColor> steady;
    CHECK(readSurfaceLocal(gui, steady));
    CHECK(steady == ref);
    // Edición de 1 fila en parcial.
    doc.insertText(1, 0, "XX");
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    std::vector<GuiColor> parcial;
    CHECK(readSurfaceLocal(gui, parcial));
    // Full equivalente del mismo estado.
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> full;
    CHECK(readSurfaceLocal(gui, full));
    CHECK(full == parcial);
#endif
}

// Regresión P1 (una textura por superficie): editor -> modal -> editor sin
// cambios debe volver a mostrar el EDITOR, no el modal. Con una sola textura
// compartida, el tercer Present mostraba la lista (el caché del editor sigue
// válido con 0 filas dirty y nada repintaba lo cubierto por el modal).
TEST(gui_sdl_volver_de_modal_muestra_editor) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);  // fuente real si hay (más tinta que verificar)
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo", "segunda fila", "tercera"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> editorPx;
    if (!readSurfaceLocal(gui, editorPx)) SKIP("ReadPixels no disponible");
    // Modal (superficie distinta, su propio full).
    std::vector<std::string> names = {"a.txt", "b.txt"};
    gui.renderBufferList(names, 0, 40, 10, sink);
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> modalPx;
    CHECK(readSurfaceLocal(gui, modalPx));
    CHECK(modalPx != editorPx);  // cordura: el modal realmente se ve distinto
    // De vuelta al editor sin cambios: 0 filas, pero la IMAGEN es el editor.
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    std::vector<GuiColor> back;
    CHECK(readSurfaceLocal(gui, back));
    CHECK(back == editorPx);
    // Y de vuelta al modal sin cambios: también su imagen, sin repintar.
    gui.renderBufferList(names, 0, 40, 10, sink);
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    std::vector<GuiColor> backModal;
    CHECK(readSurfaceLocal(gui, backModal));
    CHECK(backModal == modalPx);
#endif
}

// Regresión P2 (barra residual en el chrome): prompt con barra visible en el
// MessageBar -> mismo chrome estructural pero cursor invisible. El chrome
// debe repintarse para borrar la barra aunque chromeDirty sea falso (la
// decisión mira AMBOS cursores). Sin la corrección, el parcial dejaba la
// barra dibujada y difería del full.
TEST(gui_sdl_barra_en_chrome_no_queda_residual) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);  // fuente real si hay (más tinta que verificar)
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo", "segunda"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 100;  // fuera del viewport: el cursor de contenido es invisible
    cur.col = 0;
    MessageBarData mbar{std::string("Find: ho"), MessageKind::Prompt};
    mbar.cursor = 8;
    ChromeRequest prompt;
    prompt.message = mbar;
    prompt.estado = "X";
    prompt.accent = StyleRole::StatusAccentDefault;
    prompt.cursorShape = FrameCursorShape::Bar;
    prompt.cursorVisibleByMode = true;
    prompt.cursorInMessageBar = true;
    // 1. Prompt: barra visible en el MessageBar.
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, prompt, sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(gui.lastCursor().visible);
    CHECK(gui.lastCursor().shape == FrameCursorShape::Bar);
    // 2. MISMO chrome estructural, cursor invisible: debe borrar la barra.
    ChromeRequest plain = prompt;
    plain.cursorInMessageBar = false;
    plain.cursorShape = FrameCursorShape::Block;
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, plain, sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(!gui.lastWasFull());
    CHECK(!gui.lastChromeDirty());
    CHECK(!gui.lastCursor().visible);
    std::vector<GuiColor> parcial;
    CHECK(readSurfaceLocal(gui, parcial));
    // 3. Full equivalente: píxeles idénticos (sin barra residual).
    gui.invalidateCache();
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, plain, sink,
                         std::nullopt, std::nullopt, std::nullopt);
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> full;
    CHECK(readSurfaceLocal(gui, full));
    CHECK(full == parcial);
#endif
}

// Regresión P2-resize: un cambio de métricas físicas (celda en píxeles, como
// el re-escalado de fuente ante un cambio HiDPI) fuerza frame completo
// aunque la grilla en celdas sea idéntica. sameLayout() solo ve celdas y
// Editor::resize no invalida a propósito, así que el renderer lo detecta
// solo; si no, el parcial pintaría con la geometría vieja.
TEST(gui_sdl_cambio_metricas_misma_grilla_fuerza_full) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);  // abre la fuente real y mide la celda
    if (!gui.hasFont()) SKIP("sin fuente");
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo", "segunda fila", "tercera"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    const int cw0 = gui.cellW(), ch0 = gui.cellH();
    // Misma grilla (mismo viewport), otra talla de fuente: simula el
    // re-escalado HiDPI del loop (ensureFontPixels antes del resize).
    gui.ensureFontPixels(gui.fontPixels() * 2);
    if (gui.cellW() == cw0 && gui.cellH() == ch0)
        SKIP("la fuente no cambió de métricas");
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());  // grilla igual, superficie distinta
    std::vector<GuiColor> full;
    if (!readSurfaceLocal(gui, full)) SKIP("ReadPixels no disponible");
    // Y con métricas estables vuelve el parcial, con la misma imagen
    // (la superficie se reconstruyó correctamente, no reciclada).
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    std::vector<GuiColor> steady;
    CHECK(readSurfaceLocal(gui, steady));
    CHECK(steady == full);
#endif
}

// Texto largo -> texto corto en parcial: no quedan restos del texto
// eliminado a la derecha. La fila repintada limpia su fondo antes de los
// segmentos; sin ese fill, la textura persistente conservaría la cola vieja
// y el parcial diferiría del full.
TEST(gui_sdl_texto_largo_a_corto_sin_restos) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);  // fuente real: sin ella el texto nunca se
    if (!gui.hasFont()) SKIP("sin fuente");  // rasteriza y el test es ciego
    NullTestSink sink;
    Document doc;
    doc.restore({"0123456789ABCDEFGHIJ0123456789ABCDEFGHIJ", "segunda"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 1;  // la fila acortada (0) NO es la actual: sin pad de
                   // CurrentLine, solo el fill previo la limpia entera
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    // Solo se acorta la fila 0; el resto y el chrome no cambian.
    doc.restore({"corto", "segunda"});
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    CHECK(!gui.lastChromeDirty());
    CHECK_EQ(gui.lastRepainted(), 1);
    std::vector<GuiColor> parcial;
    if (!readSurfaceLocal(gui, parcial)) SKIP("ReadPixels no disponible");
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> full;
    CHECK(readSurfaceLocal(gui, full));
    CHECK(full == parcial);
#endif
}

// Cambio de tema con SDL: el steady parcial en el tema nuevo coincide con
// el full del tema nuevo (fondos y texturas del otro tema no sobreviven en
// la superficie persistente).
TEST(gui_sdl_toggle_theme_paridad_en_tema_nuevo) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);  // fuente real si hay (más tinta que verificar)
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo", "segunda fila"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    std::vector<GuiColor> dark;
    if (!readSurfaceLocal(gui, dark)) SKIP("ReadPixels no disponible");
    gui.toggleTheme();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> light;
    CHECK(readSurfaceLocal(gui, light));
    CHECK(light != dark);  // cordura: el tema realmente cambió la imagen
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    std::vector<GuiColor> steady;
    CHECK(readSurfaceLocal(gui, steady));
    CHECK(steady == light);
#endif
}

// Camino rápido de cursor-move (etapa ↑/↓ sin scroll): con el mismo
// viewport.top y sin cambios, mover el cursor construye O(1) filas
// (CurrentLine vieja+nueva), no las H del viewport. Se mide con
// Instrument::renderEditorRow (cada buildContentRow cuenta 1).
TEST(gui_diff_cursor_move_same_top_construye_O1_filas) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i)
        lines.push_back("linea numero " + std::to_string(i) +
                        " con algo de texto");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 5;
    renderDoc(gui, sink, doc, cur, vp, navChrome());  // full
    CHECK(gui.lastWasFull());
    instrument::enable(true);
    instrument::resetFrame();
    cur.line = 6;  // mismo viewport.top: sin scroll
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    auto m = instrument::snapshotAndReset();
    instrument::enable(false);
    CHECK(!gui.lastWasFull());
    CHECK(gui.lastRepainted() <= 3);
    CHECK_EQ(gui.lastRepainted() + gui.lastSkipped(), 10);
    CHECK(m.renderEditorRow <= 3);  // O(1), no H=10
    CHECK(m.renderEditorRow >= 1);
}

// Paridad DTO: el camino rápido deja el mismo snapshot/chrome/cursor que el
// camino completo de referencia (no solo contadores).
TEST(gui_diff_cursor_move_paridad_dto_con_full) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i)
        lines.push_back("linea numero " + std::to_string(i) +
                        " con algo de texto");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 5;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    cur.line = 6;
    renderDoc(gui, sink, doc, cur, vp, navChrome());  // camino rápido
    CHECK(!gui.lastWasFull());
    std::vector<std::string> fastPlain;
    std::vector<bool> fastCur;
    for (auto& r : gui.lastContentRows()) {
        fastPlain.push_back(r.plain());
        fastCur.push_back(r.isCurrentLine);
    }
    const ChromeData fastChrome = gui.lastChrome();
    const FrameCursor fastCursor = gui.lastCursor();
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());  // referencia completa
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastContentRows().size(), fastPlain.size());
    for (size_t i = 0; i < fastPlain.size(); ++i) {
        CHECK_EQ(gui.lastContentRows()[i].plain(), fastPlain[i]);
        CHECK_EQ(gui.lastContentRows()[i].isCurrentLine, fastCur[i]);
    }
    CHECK(gui.lastChrome() == fastChrome);
    CHECK(gui.lastCursor().visible == fastCursor.visible);
    CHECK(gui.lastCursor().shape == fastCursor.shape);
    CHECK(gui.lastCursor().cell == fastCursor.cell);
}

// Fallback correcto: con selección activa el camino rápido declina (el
// highlight cubre N filas) y el camino completo resuelve bien.
TEST(gui_diff_cursor_move_con_seleccion_cae_a_completo_pero_correcto) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i)
        lines.push_back("linea numero " + std::to_string(i) +
                        " con algo de texto");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 5;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    const std::optional<Selection> sel =
        Selection{Position{5, 0}, Position{5, 3}};
    instrument::enable(true);
    instrument::resetFrame();
    cur.line = 6;
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, navChrome(), sink, sel,
                         std::nullopt, std::nullopt);
    auto m = instrument::snapshotAndReset();
    instrument::enable(false);
    // Construyó todo el viewport (fallback), pero el pintado sigue siendo
    // diferencial (caché válido) y el highlight quedó aplicado.
    CHECK_EQ(m.renderEditorRow, 10u);
    CHECK(!gui.lastWasFull());
    CHECK(gui.lastContentRows()[5].hasRole(StyleRole::Selection));
}

// Paridad de píxeles (SDL dummy): el cursor-move rápido pinta lo mismo que el
// full, incluido el glifo bajo el cursor Block (resuelto vía snapshot en el
// camino rápido y vía Frame en el completo).
TEST(gui_sdl_cursor_move_paridad_pixeles_con_full) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    NullTestSink sink;
    Document doc;
    doc.restore({"hola mundo", "segunda fila", "tercera línea",
                 "cuarta fila", "quinta"});
    Viewport vp = makeVp(10, 40);
    Cursor cur;
    cur.line = 1;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    cur.line = 2;  // mismo viewport.top: camino rápido (Block en navegación)
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    std::vector<GuiColor> parcial;
    if (!readSurfaceLocal(gui, parcial)) SKIP("ReadPixels no disponible");
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> full;
    CHECK(readSurfaceLocal(gui, full));
    CHECK(full == parcial);
#endif
}

// Scroll +1: rota snapshots y construye O(1) filas (entrante + CurrentLine),
// no las H del viewport. El repintado sigue saliendo del caché hasta el
// scroll-blit (etapa siguiente): acá se mide CONSTRUCCIÓN, no pintado.
TEST(gui_diff_scroll_plus1_construye_O1_filas) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i)
        lines.push_back("linea numero " + std::to_string(i) +
                        " con algo de texto");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    vp.top = 10;
    Cursor cur;
    cur.line = 12;
    renderDoc(gui, sink, doc, cur, vp, navChrome());  // full
    CHECK(gui.lastWasFull());
    instrument::enable(true);
    instrument::resetFrame();
    vp.top = 11;  // scroll +1 con el cursor acompañando (caso ↑/↓ en borde)
    cur.line = 13;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    auto m = instrument::snapshotAndReset();
    instrument::enable(false);
    CHECK(!gui.lastWasFull());
    // Entrante (línea 20) + CurrentLine vieja (12) y nueva (13): O(1).
    CHECK(m.renderEditorRow <= 4);
    CHECK(m.renderEditorRow >= 1);
    // La fila entrante quedó cacheada con el contenido correcto.
    CHECK(contains(gui.lastContentRows()[9].plain(), "linea numero 20"));
}

// Scroll -1: simétrico al +1.
TEST(gui_diff_scroll_minus1_construye_O1_filas) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i)
        lines.push_back("linea numero " + std::to_string(i) +
                        " con algo de texto");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    vp.top = 11;
    Cursor cur;
    cur.line = 13;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    instrument::enable(true);
    instrument::resetFrame();
    vp.top = 10;
    cur.line = 12;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    auto m = instrument::snapshotAndReset();
    instrument::enable(false);
    CHECK(!gui.lastWasFull());
    CHECK(m.renderEditorRow <= 4);
    CHECK(m.renderEditorRow >= 1);
    CHECK(contains(gui.lastContentRows()[0].plain(), "linea numero 10"));
}

// Paridad DTO del scroll: rotación + entrante + CurrentLine dan el mismo
// snapshot/chrome/cursor que el camino completo (gutter, tabs y roles
// incluidos: la entrada i sigue siendo docLine top+i).
TEST(gui_diff_scroll_paridad_dto_con_full) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i)
        lines.push_back("linea numero " + std::to_string(i) +
                        " con algo de texto");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    vp.top = 10;
    Cursor cur;
    cur.line = 12;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    vp.top = 11;
    cur.line = 13;
    renderDoc(gui, sink, doc, cur, vp, navChrome());  // scroll rápido
    CHECK(!gui.lastWasFull());
    std::vector<std::string> fastPlain;
    std::vector<bool> fastCur;
    for (auto& r : gui.lastContentRows()) {
        fastPlain.push_back(r.plain());
        fastCur.push_back(r.isCurrentLine);
    }
    const ChromeData fastChrome = gui.lastChrome();
    const FrameCursor fastCursor = gui.lastCursor();
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());  // referencia completa
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastContentRows().size(), fastPlain.size());
    for (size_t i = 0; i < fastPlain.size(); ++i) {
        CHECK_EQ(gui.lastContentRows()[i].plain(), fastPlain[i]);
        CHECK_EQ(gui.lastContentRows()[i].isCurrentLine, fastCur[i]);
    }
    CHECK(gui.lastChrome() == fastChrome);
    CHECK(gui.lastCursor().visible == fastCursor.visible);
    CHECK(gui.lastCursor().shape == fastCursor.shape);
    CHECK(gui.lastCursor().cell == fastCursor.cell);
}

// Scroll con selección IGUAL (no cambió): el overlay es función de docLine y
// la rotación lo preserva, así que el camino rápido sigue valiendo.
TEST(gui_diff_scroll_con_seleccion_igual_reutiliza) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i)
        lines.push_back("linea numero " + std::to_string(i) +
                        " con algo de texto");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    vp.top = 10;
    Cursor cur;
    cur.line = 12;
    const std::optional<Selection> sel =
        Selection{Position{12, 0}, Position{14, 4}};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, navChrome(), sink, sel,
                         std::nullopt, std::nullopt);
    CHECK(gui.lastWasFull());
    instrument::enable(true);
    instrument::resetFrame();
    vp.top = 11;
    cur.line = 13;
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, navChrome(), sink, sel,
                         std::nullopt, std::nullopt);
    auto m = instrument::snapshotAndReset();
    instrument::enable(false);
    CHECK(!gui.lastWasFull());
    CHECK(m.renderEditorRow <= 4);
    // El highlight rotó con las filas (líneas 12-14 siguen marcadas donde
    // corresponde bajo el nuevo top).
    CHECK(gui.lastContentRows()[1].hasRole(StyleRole::Selection));
    CHECK(gui.lastContentRows()[3].hasRole(StyleRole::Selection));
}

// Fallback: scroll + selección CAMBIADA reconstruye todo (correcto).
TEST(gui_diff_scroll_con_seleccion_cambiada_fallback_completo) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i)
        lines.push_back("linea numero " + std::to_string(i) +
                        " con algo de texto");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    vp.top = 10;
    Cursor cur;
    cur.line = 12;
    const std::optional<Selection> sel1 =
        Selection{Position{12, 0}, Position{12, 3}};
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, navChrome(), sink, sel1,
                         std::nullopt, std::nullopt);
    CHECK(gui.lastWasFull());
    const std::optional<Selection> sel2 =
        Selection{Position{13, 0}, Position{15, 4}};
    instrument::enable(true);
    instrument::resetFrame();
    vp.top = 11;
    cur.line = 13;
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, navChrome(), sink, sel2,
                         std::nullopt, std::nullopt);
    auto m = instrument::snapshotAndReset();
    instrument::enable(false);
    CHECK_EQ(m.renderEditorRow, 10u);  // camino completo
    CHECK(!gui.lastWasFull());        // ...pero pintado diferencial válido
    CHECK(gui.lastContentRows()[2].hasRole(StyleRole::Selection));
}

// Fallback: salto grande (delta != ±1) reconstruye todo.
TEST(gui_diff_scroll_delta_grande_fallback_completo) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i)
        lines.push_back("linea numero " + std::to_string(i) +
                        " con algo de texto");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    vp.top = 10;
    Cursor cur;
    cur.line = 12;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    instrument::enable(true);
    instrument::resetFrame();
    vp.top = 15;  // AvPág/salto: delta +5
    cur.line = 17;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    auto m = instrument::snapshotAndReset();
    instrument::enable(false);
    CHECK_EQ(m.renderEditorRow, 10u);
    CHECK(contains(gui.lastContentRows()[0].plain(), "linea numero 15"));
}

// Paridad de píxeles del scroll (SDL dummy): la rotación + entrante pintan lo
// mismo que el full, con gutter y cursor Block correctos tras el ráster.
TEST(gui_sdl_scroll_paridad_pixeles_con_full) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 15; ++i)
        lines.push_back("linea " + std::to_string(i) + " de prueba");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    vp.top = 0;
    Cursor cur;
    cur.line = 2;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    vp.top = 1;
    cur.line = 3;  // scroll +1 rápido
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    std::vector<GuiColor> parcial;
    if (!readSurfaceLocal(gui, parcial)) SKIP("ReadPixels no disponible");
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> full;
    CHECK(readSurfaceLocal(gui, full));
    CHECK(full == parcial);
#endif
}

// Scroll de una fila en AMBAS direcciones con el cursor Block pegado al
// borde del viewport: el blit debe dejar exactamente los mismos píxeles que
// el frame completo, incluida la fila de borde (borrado del cursor viejo y
// dibujado del nuevo), que el desplazamiento de región puede dejar residual.
TEST(gui_sdl_scroll_bordes_cursor_block_paridad_pixeles) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i)
        lines.push_back("linea " + std::to_string(i) + " de prueba");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    Cursor cur;

    auto paridad = [&](int fromTop, int fromLine, int toTop, int toLine,
                       int filaCursorEsperada) -> bool {
        gui.invalidateCache();
        vp.top = fromTop;
        cur.line = fromLine;
        renderDoc(gui, sink, doc, cur, vp, navChrome());
        CHECK(gui.lastWasFull());
        CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
        CHECK_EQ(gui.lastCursor().cell.row, fromLine - fromTop);
        vp.top = toTop;
        cur.line = toLine;
        renderDoc(gui, sink, doc, cur, vp, navChrome());
        CHECK(!gui.lastWasFull());  // scroll ±1 por el camino rápido
        CHECK(gui.lastCursor().shape == FrameCursorShape::Block);
        CHECK_EQ(gui.lastCursor().cell.row, filaCursorEsperada);
        std::vector<GuiColor> parcial;
        if (!readSurfaceLocal(gui, parcial)) return false;
        gui.invalidateCache();
        vp.top = toTop;
        cur.line = toLine;
        renderDoc(gui, sink, doc, cur, vp, navChrome());
        CHECK(gui.lastWasFull());
        std::vector<GuiColor> full;
        CHECK(readSurfaceLocal(gui, full));
        CHECK(full == parcial);
        return true;
    };

    // +1 con el cursor Block en la última fila visible.
    if (!paridad(4, 13, 5, 14, 9)) SKIP("ReadPixels no disponible");
    // -1 con el cursor Block en la primera fila visible.
    if (!paridad(5, 5, 4, 4, 0)) SKIP("ReadPixels no disponible");
#endif
}

// Framebuffer: si falla SOLO la adquisición del destino del scroll-blit, el
// origen sigue vigente y la rama de recuperación repinta todo el contenido
// sobre él. El frame actual NO se queda con la imagen vieja (ni sin pintar):
// los píxeles deben igualar al frame completo del estado nuevo.
TEST(gui_sdl_scroll_dst_fb_falla_repinta_frame_actual) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setFramebufAcquireFailAfterForTests(-1);
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i)
        lines.push_back("linea " + std::to_string(i) + " de prueba");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    vp.top = 4;
    Cursor cur;
    cur.line = 4;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());

    vp.top = 5;
    cur.line = 5;
    gui.setFramebufAcquireFailAfterForTests(1);  // deja pasar el origen
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    std::vector<GuiColor> reparado;
    if (!readSurfaceLocal(gui, reparado)) SKIP("ReadPixels no disponible");
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> full;
    CHECK(readSurfaceLocal(gui, full));
    CHECK(reparado == full);
    gui.setFramebufAcquireFailAfterForTests(-1);
#endif
}

// Framebuffer: si falla la adquisición en el camino rápido de cursor, la
// caché se invalida y el frame actual queda pendiente, pero el SIGUIENTE ya
// debe ser completo y correcto (nada a medio pintar entre frames).
TEST(gui_sdl_fb_falla_reconstruye_siguiente_frame) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setFramebufAcquireFailAfterForTests(-1);
    NullTestSink sink;
    Document doc;
    doc.restore({"linea 0", "linea 1", "linea 2", "linea 3", "linea 4",
                 "linea 5", "linea 6", "linea 7"});
    Viewport vp = makeVp(10, 40);
    vp.top = 0;
    Cursor cur;
    cur.line = 3;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());

    // Movimiento de cursor (mismo top): tryFastCursorMove adquiere una sola
    // superficie; el hook la hace fallar y la caché se invalida.
    cur.line = 4;
    gui.setFramebufAcquireFailAfterForTests(0);
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.hasContentCache());

    gui.setFramebufAcquireFailAfterForTests(-1);
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    CHECK(gui.hasContentCache());
    std::vector<GuiColor> recompuesto;
    if (!readSurfaceLocal(gui, recompuesto)) SKIP("ReadPixels no disponible");
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> full;
    CHECK(readSurfaceLocal(gui, full));
    CHECK(recompuesto == full);
#endif
}

// Framebuffer: si falla la adquisición del ORIGEN del scroll-blit no hay
// imagen vigente desde la que desplazar. La caché se invalida y el frame
// siguiente debe ser completo e idéntico a un render limpio.
TEST(gui_sdl_scroll_src_fb_falla_invalida_y_reconstruye) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    gui.setFramebufAcquireFailAfterForTests(-1);
    NullTestSink sink;
    Document doc;
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i)
        lines.push_back("linea " + std::to_string(i) + " de prueba");
    doc.restore(lines);
    Viewport vp = makeVp(10, 40);
    vp.top = 4;
    Cursor cur;
    cur.line = 4;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());

    vp.top = 5;
    cur.line = 5;
    gui.setFramebufAcquireFailAfterForTests(0);  // falla el origen
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.hasContentCache());

    gui.setFramebufAcquireFailAfterForTests(-1);
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    CHECK(gui.hasContentCache());
    std::vector<GuiColor> recompuesto;
    if (!readSurfaceLocal(gui, recompuesto)) SKIP("ReadPixels no disponible");
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> full;
    CHECK(readSurfaceLocal(gui, full));
    CHECK(recompuesto == full);
#endif
}

// Framebuffer: recreación de las superficies persistentes (resize/renderer).
// El próximo render debe repintar la imagen completa aunque el diff lógico
// diga "sin filas sucias": la superficie vieja (y su contenido) ya no existe.
TEST(gui_sdl_framebuffer_recreado_repinta_imagen_completa) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    NullTestSink sink;
    Document doc;
    doc.restore({"linea 0", "linea 1", "linea 2", "linea 3", "linea 4",
                 "linea 5", "linea 6", "linea 7"});
    Viewport vp = makeVp(10, 40);
    vp.top = 0;
    Cursor cur;
    cur.line = 3;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    std::vector<GuiColor> ref;
    if (!readSurfaceLocal(gui, ref)) SKIP("ReadPixels no disponible");

    gui.dropContentFramebuffersForTests();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    std::vector<GuiColor> recreado;
    CHECK(readSurfaceLocal(gui, recreado));
    CHECK(recreado == ref);  // imagen completa, no una superficie en blanco

    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(!gui.lastWasFull());
    CHECK_EQ(gui.lastRepainted(), 0);
    std::vector<GuiColor> steady;
    CHECK(readSurfaceLocal(gui, steady));
    CHECK(steady == ref);
#endif
}

// Soft Wrap: el camino rápido de cursor-move no puede mapear
// `docLine - viewport.top` a filas visuales. Al moverse dentro de una línea
// larga cambian de CurrentLine TODAS sus continuaciones, no solo una fila.
// También al saltar entre líneas lógicas el mapeo deja de ser 1:1. El fast
// path debe declinar y el resultado (DTO) coincidir con el frame completo.
TEST(gui_diff_softwrap_cursor_move_fuerza_completo_y_paridad_dto) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore(softWrapLines());
    Viewport vp = makeVp(6, 24);
    Cursor cur;
    const int textWidth = vp.width - gutterWidth(doc.lineCount(), vp.width);
    softwrap::Index idx;
    CHECK(idx.ensure(doc, textWidth, true));
    CHECK(idx.enabled());
    gui.setWrapIndex(&idx);

    renderDoc(gui, sink, doc, cur, vp, navChrome());  // full inicial
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastContentRows().size(), static_cast<size_t>(6));

    // Movimiento dentro de L0: de la primera fila visual a su continuación.
    instrument::enable(true);
    instrument::resetFrame();
    cur.col = 20;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    auto moved = instrument::snapshotAndReset();
    instrument::enable(false);
    CHECK_EQ(moved.renderEditorRow, 6u);  // frame completo, no O(1)
    CHECK(gui.lastContentRows()[1].isCurrentLine);
    CHECK(gui.lastContentRows()[2].isCurrentLine);
    std::vector<std::string> fastPlain;
    std::vector<bool> fastCur;
    captureContent(gui, fastPlain, fastCur);
    const std::vector<GuiContentRow> fastRows = gui.lastContentRows();
    const ChromeData fastChrome = gui.lastChrome();
    const FrameCursor fastCursor = gui.lastCursor();

    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastContentRows().size(), fastPlain.size());
    CHECK_EQ(gui.lastContentRows().size(), static_cast<size_t>(6));
    // Paridad fila a fila: segmentos, roles y CurrentLine de TODAS las filas
    // visuales (no solo el texto aplanado).
    CHECK(gui.lastContentRows() == fastRows);
    for (size_t i = 0; i < fastPlain.size(); ++i) {
        CHECK_EQ(gui.lastContentRows()[i].plain(), fastPlain[i]);
        CHECK_EQ(gui.lastContentRows()[i].isCurrentLine, fastCur[i]);
    }
    CHECK(gui.lastChrome() == fastChrome);
    CHECK(gui.lastCursor().visible == fastCursor.visible);
    CHECK(gui.lastCursor().shape == fastCursor.shape);
    CHECK(gui.lastCursor().cell == fastCursor.cell);

    // Movimiento entre líneas lógicas (L0 -> L1, una sola fila visual).
    instrument::enable(true);
    instrument::resetFrame();
    cur.line = 1;
    cur.col = 0;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    moved = instrument::snapshotAndReset();
    instrument::enable(false);
    CHECK_EQ(moved.renderEditorRow, 6u);
    captureContent(gui, fastPlain, fastCur);
    const std::vector<GuiContentRow> fastRowsL1 = gui.lastContentRows();
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastContentRows().size(), fastPlain.size());
    CHECK(gui.lastContentRows() == fastRowsL1);
    for (size_t i = 0; i < fastPlain.size(); ++i) {
        CHECK_EQ(gui.lastContentRows()[i].plain(), fastPlain[i]);
        CHECK_EQ(gui.lastContentRows()[i].isCurrentLine, fastCur[i]);
    }
}

// Soft Wrap + scroll de una fila: `viewport.top` avanza una FILA VISUAL.
// Rotar snapshots como docLine+1 y construir `top + H - 1` como línea
// documental reconstruiría la fila equivocada (y perdería continuaciones).
// El camino rápido debe declinar y coincidir con el frame completo.
TEST(gui_diff_softwrap_scroll_fuerza_completo_y_paridad_dto) {
    GuiRenderer gui;
    NullTestSink sink;
    Document doc;
    doc.restore(softWrapLines());
    Viewport vp = makeVp(6, 24);
    Cursor cur;
    cur.col = 20;  // continuación de L0
    const int textWidth = vp.width - gutterWidth(doc.lineCount(), vp.width);
    softwrap::Index idx;
    CHECK(idx.ensure(doc, textWidth, true));
    CHECK(idx.enabled());
    gui.setWrapIndex(&idx);

    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    CHECK(gui.lastContentRows()[1].isCurrentLine);
    CHECK(gui.lastContentRows()[2].isCurrentLine);

    instrument::enable(true);
    instrument::resetFrame();
    vp.top = 1;  // scroll +1 en filas visuales
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    auto scrolled = instrument::snapshotAndReset();
    instrument::enable(false);
    CHECK_EQ(scrolled.renderEditorRow, 6u);  // frame completo, no O(1)
    // La nueva fila 0 es la continuación L0 en columnBegin 17, no L0 desde 0.
    CHECK(gui.lastContentRows()[0].isCurrentLine);
    CHECK(contains(gui.lastContentRows()[0].plain(), std::string(16, 'a')));
    CHECK(contains(gui.lastContentRows()[2].plain(), "corta"));
    std::vector<std::string> fastPlain;
    std::vector<bool> fastCur;
    captureContent(gui, fastPlain, fastCur);
    const std::vector<GuiContentRow> fastRowsScrolled = gui.lastContentRows();

    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastContentRows().size(), fastPlain.size());
    CHECK_EQ(gui.lastContentRows().size(), static_cast<size_t>(6));
    CHECK(gui.lastContentRows() == fastRowsScrolled);
    for (size_t i = 0; i < fastPlain.size(); ++i) {
        CHECK_EQ(gui.lastContentRows()[i].plain(), fastPlain[i]);
        CHECK_EQ(gui.lastContentRows()[i].isCurrentLine, fastCur[i]);
    }
}

// Paridad de píxeles con Soft Wrap: movimiento dentro de una línea larga
// (continuaciones visibles) y scroll visual +1. Si el fast path tratara las
// filas como líneas documentales, el parcial diferiría del frame completo.
TEST(gui_sdl_softwrap_fast_paths_paridad_pixeles_con_full) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren);
    NullTestSink sink;
    Document doc;
    doc.restore(softWrapLines());
    Viewport vp = makeVp(6, 24);
    Cursor cur;
    const int textWidth = vp.width - gutterWidth(doc.lineCount(), vp.width);
    softwrap::Index idx;
    CHECK(idx.ensure(doc, textWidth, true));
    CHECK(idx.enabled());
    gui.setWrapIndex(&idx);

    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());

    // Movimiento a la continuación de L0.
    cur.col = 20;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    const std::vector<GuiContentRow> movedRows = gui.lastContentRows();
    std::vector<GuiColor> movedPx;
    if (!readSurfaceLocal(gui, movedPx)) SKIP("ReadPixels no disponible");
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastContentRows().size(), static_cast<size_t>(6));
    CHECK(gui.lastContentRows() == movedRows);  // todas las filas visuales
    std::vector<GuiColor> movedFull;
    CHECK(readSurfaceLocal(gui, movedFull));
    CHECK(movedFull == movedPx);

    // Scroll visual +1, con la continuación de L0 como primera fila.
    vp.top = 1;
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    const std::vector<GuiContentRow> scrolledRows = gui.lastContentRows();
    std::vector<GuiColor> scrolledPx;
    if (!readSurfaceLocal(gui, scrolledPx)) SKIP("ReadPixels no disponible");
    gui.invalidateCache();
    renderDoc(gui, sink, doc, cur, vp, navChrome());
    CHECK(gui.lastWasFull());
    CHECK_EQ(gui.lastContentRows().size(), static_cast<size_t>(6));
    CHECK(gui.lastContentRows() == scrolledRows);  // todas las filas visuales
    std::vector<GuiColor> scrolledFull;
    CHECK(readSurfaceLocal(gui, scrolledFull));
    CHECK(scrolledFull == scrolledPx);
#endif
}

// Ruta de error de initForRenderer con --font inexistente: run() no entra al
// ciclo de eventos, devuelve 1 y libera todo lo adquirido (ventana, renderer
// y SDL). Nada queda a medio inicializar: la fuente nunca abrió (el
// GuiRenderer queda desacoplado vía setSdlRenderer(nullptr)) y el guard de
// TextInput aún no existe en ese punto (se crea después del init).
// Headless con driver dummy; run() es dueño total del ciclo SDL acá: no hay
// ventana del fixture viva durante el run (SDL_Quit destruye las ventanas
// pendientes, así que compartirla sería doble-free).
TEST(guirunloop_font_explicita_fallida_retorna_1_y_limpia) {
#ifndef HAVE_SDL2
    SKIP("sin HAVE_SDL2: el loop GUI no existe");
#else
    struct DummyEnv {
        std::string prev;
        bool had = false;
        DummyEnv() {
            if (const char* p = std::getenv("SDL_VIDEODRIVER")) {
                prev = p;
                had = true;
            }
            ::setenv("SDL_VIDEODRIVER", "dummy", 1);
        }
        ~DummyEnv() {
            if (had)
                ::setenv("SDL_VIDEODRIVER", prev.c_str(), 1);
            else
                ::unsetenv("SDL_VIDEODRIVER");
        }
    };
    DummyEnv env;
    GuiOptions opts;
    opts.fontPath = "/maestro-test-inexistente/fuente.ttf";
    auto gui = std::make_unique<GuiRenderer>(opts);
    GuiRenderer* raw = gui.get();
    int rc = -1;
    bool fontOk = true;
    {
        // El mensaje de error por stderr es el comportamiento esperado del
        // producto en esta ruta (no un fallo del test): se silencia solo
        // durante el run para no ensuciar la salida de la suite.
        struct SilenciaStderr {
            int guardado = -1;
            SilenciaStderr() {
                guardado = ::dup(STDERR_FILENO);
                if (guardado >= 0) {
                    int nulo = ::open("/dev/null", O_WRONLY);
                    if (nulo >= 0) {
                        ::dup2(nulo, STDERR_FILENO);
                        ::close(nulo);
                    }
                }
            }
            ~SilenciaStderr() {
                if (guardado >= 0) {
                    ::dup2(guardado, STDERR_FILENO);
                    ::close(guardado);
                }
            }
        };
        SilenciaStderr sil;
        Editor ed(std::move(gui));
        GuiRunLoop loop(ed, opts);
        loop.setGuiRenderer(raw);
        rc = loop.run();
        fontOk = raw->hasFont();
    }
    CHECK_EQ(rc, 1);
    CHECK(!fontOk);
    // SDL queda reusable tras la limpieza del camino de error (Init/Quit y
    // Create/Destroy emparejados: se puede iniciar, crear y destruir de nuevo).
    bool reused = false;
    if (SDL_Init(SDL_INIT_VIDEO) == 0) {
        SDL_Window* w = SDL_CreateWindow("maestro-reuse", 0, 0, 64, 64,
                                         SDL_WINDOW_HIDDEN);
        if (w) {
            SDL_Renderer* r =
                SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
            if (r) {
                SDL_DestroyRenderer(r);
                reused = true;
            }
            SDL_DestroyWindow(w);
        }
        SDL_Quit();
    }
    CHECK(reused);
#endif
}

// Ciclo de vida HiDPI del lado renderer (integración headless con vídeo
// dummy): recorre arranque -> monitor 2× -> minimizado -> restaurado
// aplicando en cada etapa EXACTAMENTE la talla que el loop calcularía
// (guiScaledFontPixels + guiDrawableOrWindow) y verifica que la fuente
// cargada coincide con la esperada, con métricas re-medidas del ráster.
// Complementa los tests puros de la fórmula (test_guisdlmapper.cpp): esos
// fijan el contrato matemático; este fija que el renderer lo aplica en cada
// transición (el run() real no es testeable: bloquearía en el ciclo).
TEST(guirenderer_ciclo_vida_hidpi_tallas_esperadas) {
#ifndef HAVE_SDL2
    SKIP("sin HAVE_SDL2");
#else
    SdlDummyLocal sdl;
    if (!sdl.ok) SKIP("sin video dummy");
    const int base = 16;
    // Arranque 1×: medidas reales del drawable, talla esperada al abrir.
    int winW = 0, winH = 0, outW = 0, outH = 0;
    SDL_GetWindowSize(sdl.win, &winW, &winH);
    SDL_GetRendererOutputSize(sdl.ren, &outW, &outH);
    if (winW <= 0 || winH <= 0 || outW <= 0 || outH <= 0)
        SKIP("medidas dummy inválidas");
    const GuiDrawableSize fb = guiDrawableOrWindow(winW, winH, outW, outH);
    const int initPx = guiScaledFontPixels(base, winH, fb.h);
    GuiRenderer gui;
    gui.initForRenderer(sdl.ren, initPx);
    if (!gui.hasFont()) SKIP("sin fuente del sistema");
    CHECK_EQ(gui.fontPixels(), initPx);
    const int cw1 = gui.cellW(), ch1 = gui.cellH();
    CHECK(cw1 > 0 && ch1 > 0);
    // Monitor 2× (misma ventana lógica, drawable doble): el loop re-escala y
    // el renderer recarga a la talla esperada, con métricas mayores
    // (re-medidas del ráster, no el entero guardado).
    const int px2 = guiScaledFontPixels(base, winH, fb.h * 2);
    CHECK(px2 > initPx);
    gui.ensureFontPixels(px2);
    CHECK(gui.hasFont());
    CHECK_EQ(gui.fontPixels(), px2);
    CHECK(gui.cellW() > cw1);
    CHECK(gui.cellH() > ch1);
    // Minimizado (medidas en 0): fallback por eje -> identidad -> base.
    const GuiDrawableSize fmin = guiDrawableOrWindow(0, 0, 0, 0);
    gui.ensureFontPixels(guiScaledFontPixels(base, 0, fmin.h));
    CHECK(gui.hasFont());
    CHECK_EQ(gui.fontPixels(), base);
    // Restaurado a 1×: vuelve a la talla inicial con sus métricas.
    gui.ensureFontPixels(guiScaledFontPixels(base, winH, fb.h));
    CHECK_EQ(gui.fontPixels(), initPx);
    CHECK_EQ(gui.cellW(), cw1);
    CHECK_EQ(gui.cellH(), ch1);
#endif
}
