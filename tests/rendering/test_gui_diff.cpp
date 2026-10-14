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
#include "rendering/Sink.h"
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

void renderDoc(GuiRenderer& gui, NullTestSink& sink, Document& doc,
               Cursor& cur, Viewport& vp, const ChromeRequest& cr) {
    gui.renderScreenDiff(doc, cur, vp, "t.txt", false, cr, sink, std::nullopt,
                         std::nullopt, std::nullopt);
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
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
    CHECK_EQ(m.renderEditorRow, 10);
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
