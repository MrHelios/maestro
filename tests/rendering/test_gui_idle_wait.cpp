// Tests del plan v2 (GuiRunLoop: espera idle por deadline).
//
// Cubre, sin SDL ni ventana:
//   - GuiRenderer::msUntilBlinkToggle: timeline de la barra, Block sin
//     animación, cursor invisible, reseteo del ancla al mover el cursor.
//   - GuiRunLoop::computeIdleWaitMs: combinación explícita de deadlines
//     ausentes (el -1 del Editor nunca tapa el deadline del blink) + cota.
//   - GuiRunLoop::watcherServiceDue: periodicidad propia del watcher.
//   - GuiRunLoop::idleOnTimeout: solo renderiza al vencer un deadline real;
//     los wakes de polling no añaden renders (contados con
//     CountingGuiRenderer, no con snapshots) y el watcher se sirve por
//     periodicidad.
// La prueba con actividad SDL continua vive al final (HAVE_SDL2, con SKIP
// sin video dummy).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "test_framework.h"

#include "app/Editor.h"
#include "filesystem/FileWatcher.h"
#include "filesystem/InotifyFileWatcher.h"
#include "filesystem/NullFileWatcher.h"
#include "helpers/FakeClipboard.h"
#include "platform/CellPos.h"
#include "platform/gui/GuiRunLoop.h"
#include "rendering/Sink.h"
#include "rendering/gui/GuiRenderer.h"

namespace {

using clk = std::chrono::steady_clock;

// GuiRenderer que cuenta renders reales (peticiones de construir/pintar el
// frame), para probar "cero renders espurios" por observación directa en
// vez de comparar snapshots visuales.
struct CountingGuiRenderer : public GuiRenderer {
    std::atomic<int> renderCount{0};
    // Cursor/viewport que vio cada render (protegido: el loop escribe desde
    // otro hilo y los tests de lote leen antes del join para no esperar).
    std::mutex samplesMutex;
    std::vector<std::pair<int, int>> renderedCursors;
    void renderScreenDiff(const Document& doc, const Cursor& cursor,
                          const Viewport& viewport, const std::string& filename,
                          bool modified, const ChromeRequest& chrome, Sink& sink,
                          const std::optional<Selection>& selection,
                          const std::optional<Selection>& searchHighlight,
                          const std::optional<BracketPair>& bracketPair) override {
        ++renderCount;
        {
            std::lock_guard<std::mutex> lk(samplesMutex);
            renderedCursors.emplace_back(cursor.line, viewport.top);
        }
        GuiRenderer::renderScreenDiff(doc, cursor, viewport, filename, modified,
                                      chrome, sink, selection, searchHighlight,
                                      bracketPair);
    }
    std::vector<std::pair<int, int>> cursorSamples() {
        std::lock_guard<std::mutex> lk(samplesMutex);
        return renderedCursors;
    }
};

// Watcher con compuerta: el primer poll se bloquea hasta release(). El
// loop llama al watcher al tope de cada vuelta, ANTES de drenar SDL, así
// que la compuerta deja el hilo detenido con la cola intacta: el test
// puede encolar un lote completo y liberar para que se procese de una vez.
struct BlockingWatcher : public FileWatcher {
    std::atomic<int> pollCount{0};
    std::atomic<bool> armed{false};
    std::mutex m;
    std::condition_variable cv;
    bool blocked = false;
    bool released = false;
    void watch(const std::string&) override {}
    void unwatch(const std::string&) override {}
    int fd() const override { return -1; }
    void pollEvents(
        const std::function<void(const FileChangeEvent&)>&) override {
        ++pollCount;
        if (!armed.load(std::memory_order_acquire)) return;
        std::unique_lock<std::mutex> lk(m);
        if (!blocked) {
            blocked = true;
            cv.notify_all();
            cv.wait(lk, [&] { return released; });
        }
    }
    void arm() { armed.store(true, std::memory_order_release); }
    bool waitBlocked() {
        std::unique_lock<std::mutex> lk(m);
        return cv.wait_for(lk, std::chrono::seconds(5),
                           [&] { return blocked; });
    }
    void release() {
        std::lock_guard<std::mutex> lk(m);
        released = true;
        cv.notify_all();
    }
};

struct CountingWatcher : public FileWatcher {
    // Contador atómico: el loop lo escribe desde su hilo en el test de
    // actividad continua y el test lo lee para la compuerta de arranque.
    std::atomic<int> pollCount{0};
    // Instantes de cada servicio (solo los escribe el hilo del loop; el
    // test los lee tras el join). Permiten probar que el servicio ocurrió
    // DURANTE la actividad, no antes ni después.
    std::vector<std::chrono::steady_clock::time_point> serviceTimes;
    void watch(const std::string&) override {}
    void unwatch(const std::string&) override {}
    int fd() const override { return -1; }
    void pollEvents(
        const std::function<void(const FileChangeEvent&)>&) override {
        ++pollCount;
        serviceTimes.push_back(std::chrono::steady_clock::now());
    }
};

InputEvent insertCharEv(const std::string& text) {
    InputEvent ie;
    ie.type = InputEventType::InsertChar;
    ie.text = text;
    return ie;
}

InputEvent mousePressAt(int col, int row) {
    InputEvent e;
    e.type = InputEventType::MousePress;
    e.cell = CellPos{col - 1, row - 1};
    return e;
}

InputEvent mouseDragAt(int col, int row) {
    InputEvent e;
    e.type = InputEventType::MouseDrag;
    e.cell = CellPos{col - 1, row - 1};
    return e;
}

// Editor + CountingGuiRenderer + GuiRunLoop cableados (renderer del Editor
// y gui_ del loop apuntan al mismo objeto, como en main.cpp).
struct Harness {
    NullSink sink;
    CountingGuiRenderer* gui = nullptr;
    std::unique_ptr<Editor> ed;
    std::unique_ptr<GuiRunLoop> loop;
};

Harness makeHarness(std::unique_ptr<FileWatcher> watcher) {
    Harness h;
    auto gui = std::make_unique<CountingGuiRenderer>();
    h.gui = gui.get();
    h.ed = std::make_unique<Editor>(std::make_unique<FakeClipboard>(),
                                    std::move(watcher));
    h.ed->setRenderer(std::move(gui));
    h.ed->setSink(h.sink);
    h.loop = std::make_unique<GuiRunLoop>(*h.ed);
    h.loop->setGuiRenderer(h.gui);
    return h;
}

Harness makeHarness() {
    return makeHarness(std::make_unique<NullFileWatcher>());
}

}  // namespace

// ---------------------------------------------------------------------------
// msUntilBlinkToggle
// ---------------------------------------------------------------------------

TEST(idle_blink_deadline_bar_timeline) {
    Harness h = makeHarness();
    h.ed->handleEvent(insertCharEv("i"));  // Navegacion -> Interaccion (Bar)
    const auto t0 = clk::now();
    h.loop->idleStep(t0);
    CHECK(h.gui->lastCursor().shape == FrameCursorShape::Bar);

    auto rest = [&](int ms) { return h.gui->msUntilBlinkToggle(t0 + std::chrono::milliseconds(ms)); };
    CHECK(rest(0).has_value());
    CHECK(*rest(0) == 530);
    CHECK(*rest(200) == 330);
    CHECK(*rest(529) == 1);
    CHECK(*rest(530) == 530);
    CHECK(*rest(800) == 260);
    CHECK(*rest(1059) == 1);
    CHECK(*rest(1060) == 530);
    CHECK(*rest(1590) == 530);
}

TEST(idle_blink_deadline_block_sin_animacion) {
    Harness h = makeHarness();
    const auto t0 = clk::now();
    h.loop->idleStep(t0);  // Navegacion: Bloque fijo
    CHECK(h.gui->lastCursor().shape == FrameCursorShape::Block);
    for (int ms : {0, 200, 530, 800, 1060, 5000}) {
        CHECK(!h.gui->msUntilBlinkToggle(t0 + std::chrono::milliseconds(ms)).has_value());
    }
}

TEST(idle_blink_deadline_sin_primer_render) {
    Harness h = makeHarness();
    // Sin ningún render con tiempo inyectado no hay ancla: sin deadline.
    CHECK(!h.gui->msUntilBlinkToggle(clk::now()).has_value());
}

TEST(idle_blink_deadline_resetea_al_mover_cursor) {
    Harness h = makeHarness();
    h.ed->handleEvent(insertCharEv("i"));
    const auto t0 = clk::now();
    h.loop->idleStep(t0);
    // Fase OFF a los 800ms.
    h.gui->setBlinkNow(t0 + std::chrono::milliseconds(800));
    CHECK(!h.gui->blinkPhaseOn());
    // Mover el cursor (insertar mueve a la celda siguiente) resetea el
    // ancla: la barra vuelve a ON completo aunque la fase anterior era OFF.
    h.ed->handleEvent(insertCharEv("x"));
    h.loop->idleStep(t0 + std::chrono::milliseconds(800));
    CHECK(h.gui->blinkPhaseOn());
    auto rest = [&](int ms) {
        return h.gui->msUntilBlinkToggle(t0 + std::chrono::milliseconds(800 + ms));
    };
    CHECK(rest(0).has_value());
    CHECK(*rest(0) == 530);
    CHECK(*rest(529) == 1);
    CHECK(*rest(530) == 530);
}

TEST(idle_blink_mark_cursor_activity_fuerza_reset) {
    // El loop GUI marca actividad de lote aunque la celda final coincida con
    // la anterior (AvPág + RePág). Sin la marca, el ancla vieja mantendría
    // la fase OFF; con ella, el render vuelve a ON.
    Harness h = makeHarness();
    h.ed->handleEvent(insertCharEv("i"));
    const auto t0 = clk::now();
    h.loop->idleStep(t0);
    h.gui->setBlinkNow(t0 + std::chrono::milliseconds(800));
    CHECK(!h.gui->blinkPhaseOn());
    h.gui->markCursorActivity();
    h.loop->idleStep(t0 + std::chrono::milliseconds(800));
    CHECK(h.gui->blinkPhaseOn());
    auto rest = h.gui->msUntilBlinkToggle(t0 + std::chrono::milliseconds(800));
    CHECK(rest.has_value());
    CHECK(*rest == 530);
}

TEST(idle_blink_deadline_cursor_invisible) {
    // Rueda (scroll solo-viewport, cursor quieto): el viewport se aleja del
    // cursor y el render lo deja invisible (suppressScrollToCursor_). Una
    // barra invisible no se pinta nunca -> nada que animar, aunque el shape
    // sea Bar (cubre el guard de visibilidad, no el de Block).
    Harness h = makeHarness();
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i) lines.push_back("l" + std::to_string(i));
    h.ed->getActiveBufferForTesting().document.restore(lines);
    h.ed->getActiveBufferForTesting().viewport.height = 4;
    h.ed->getActiveBufferForTesting().viewport.width = 20;
    h.ed->getActiveBufferForTesting().viewport.top = 0;
    h.ed->getActiveBufferForTesting().viewport.left = 0;
    h.ed->getActiveBufferForTesting().cursor.line = 1;
    h.ed->getActiveBufferForTesting().cursor.col = 0;
    h.ed->handleEvent(insertCharEv("i"));  // Interaccion: Bar visible
    const auto t0 = clk::now();
    h.loop->idleStep(t0);
    CHECK(h.gui->lastCursor().shape == FrameCursorShape::Bar);
    CHECK(h.gui->lastCursor().visible);
    CHECK(h.gui->msUntilBlinkToggle(t0).has_value());
    InputEvent scroll;
    scroll.type = InputEventType::ScrollDown;
    h.ed->handleEvent(scroll);
    h.ed->handleEvent(scroll);  // top 0 -> 6, cursor (línea 1) fuera
    const auto t1 = t0 + std::chrono::milliseconds(100);
    h.loop->idleStep(t1);
    CHECK(!h.gui->lastCursor().visible);
    CHECK(!h.gui->msUntilBlinkToggle(t1).has_value());
    CHECK(!h.gui->msUntilBlinkToggle(t1 + std::chrono::milliseconds(800)).has_value());
}

// ---------------------------------------------------------------------------
// computeIdleWaitMs
// ---------------------------------------------------------------------------

TEST(idle_wait_bloque_sin_timers_es_la_cota) {
    Harness h = makeHarness();
    const auto t0 = clk::now();
    h.loop->idleStep(t0);  // Bloque, sin timers
    CHECK(h.ed->nextTimeoutMs(t0) < 0);  // precondición: nada pendiente
    CHECK(h.loop->computeIdleWaitMs(t0) == GuiRunLoop::kMaxIdleWaitMs);
    CHECK(h.loop->idleDelayMs(t0) == GuiRunLoop::kMaxIdleWaitMs);
}

TEST(idle_wait_no_tapa_blink_con_indefinido) {
    // P1.1: editor en -1 (indefinido) + blink en 10ms -> la espera es 10,
    // no 30 (clamp viejo) ni indefinida (min ingenuo con -1).
    Harness h = makeHarness();
    h.ed->handleEvent(insertCharEv("i"));
    const auto t0 = clk::now();
    h.loop->idleStep(t0);
    const auto q = t0 + std::chrono::milliseconds(520);
    CHECK(h.ed->nextTimeoutMs(q) < 0);
    const auto blink = h.gui->msUntilBlinkToggle(q);
    CHECK(blink.has_value());
    CHECK(*blink == 10);
    CHECK(h.loop->computeIdleWaitMs(q) == *blink);
}

TEST(idle_wait_editor_cerca_vencimiento) {
    // Mensaje de acción con timeout (undo sin historial): cerca del
    // vencimiento manda el deadline del Editor, no la cota.
    Harness h = makeHarness();
    InputEvent undo;
    undo.type = InputEventType::Undo;
    const auto tMsg = clk::now();
    h.ed->handleEvent(undo);
    h.loop->idleStep(tMsg);  // Navegacion: Bloque, blink ausente
    const auto q = tMsg + Editor::kActionMessageTimeout - std::chrono::milliseconds(10);
    const int editorMs = h.ed->nextTimeoutMs(q);
    CHECK(editorMs >= 0);
    CHECK(editorMs < GuiRunLoop::kMaxIdleWaitMs);
    CHECK(h.loop->computeIdleWaitMs(q) == editorMs);
}

TEST(idle_wait_autoscroll_armado) {
    // P2 (revisor): no basta un timeout no negativo menor que la cota
    // (podría venir de otro timer): se prueba el efecto observable del
    // deadline de autoscroll. Gesto armado (drag fuera del viewport) con
    // intervalo de 100ms: antes del deadline el viewport NO avanza; tras
    // vencerlo, idleOnTimeout hace tick y el viewport avanza un paso.
    Harness h = makeHarness();
    std::vector<std::string> lines;
    for (int i = 0; i < 20; ++i) lines.push_back("l" + std::to_string(i));
    h.ed->getActiveBufferForTesting().document.restore(lines);
    h.ed->getActiveBufferForTesting().viewport.height = 4;
    h.ed->getActiveBufferForTesting().viewport.width = 20;
    h.ed->getActiveBufferForTesting().viewport.top = 10;
    h.ed->getActiveBufferForTesting().viewport.left = 0;
    h.ed->getActiveBufferForTesting().cursor.line = 11;
    h.ed->getActiveBufferForTesting().cursor.col = 0;
    h.ed->handleEvent(mousePressAt(4, 2));
    h.ed->handleEvent(mouseDragAt(4, 5));  // fuera por abajo: arma + paso
    const auto t0 = clk::now();
    h.loop->idleStep(t0);
    // Premisa de armado, observable: el drag dio el paso del evento.
    const int top0 = h.ed->getActiveBufferForTesting().viewport.top;
    CHECK(top0 == 11);
    const int renders0 = h.gui->renderCount;
    // ANTES del deadline (intervalo 100ms, margen 50ms): el wake no tiene
    // nada vencido -> ni tick ni render, viewport quieto.
    CHECK(!h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(50)));
    CHECK(h.gui->renderCount == renders0);
    CHECK(h.ed->getActiveBufferForTesting().viewport.top == top0);
    // El deadline vencido se despacha sin esperar (espera 0, no cota).
    const auto late = t0 + std::chrono::milliseconds(150);
    CHECK(h.ed->nextTimeoutMs(late) == 0);
    CHECK(h.loop->computeIdleWaitMs(late) == 0);
    // DESPUÉS del deadline: tick + render, el viewport avanza un paso.
    CHECK(h.loop->idleOnTimeout(late));
    CHECK(h.gui->renderCount == renders0 + 1);
    CHECK(h.ed->getActiveBufferForTesting().viewport.top == top0 + 1);
}

// ---------------------------------------------------------------------------
// watcherServiceDue
// ---------------------------------------------------------------------------

TEST(idle_watcher_periodicidad_propia) {
    const auto t0 = clk::now();
    // Nunca servido (epoch): debido de inmediato.
    CHECK(GuiRunLoop::watcherServiceDue(std::chrono::steady_clock::time_point{}, t0));
    CHECK(!GuiRunLoop::watcherServiceDue(t0, t0 + std::chrono::milliseconds(29)));
    CHECK(GuiRunLoop::watcherServiceDue(t0, t0 + std::chrono::milliseconds(30)));
    CHECK(GuiRunLoop::watcherServiceDue(t0, t0 + std::chrono::milliseconds(100)));
}

// ---------------------------------------------------------------------------
// idleOnTimeout: solo renderiza al vencer un deadline real
// ---------------------------------------------------------------------------

TEST(idle_timeout_polling_quieto_no_renderiza) {
    // Bloque sin timers: wake de polling -> false y CERO renders añadidos
    // (observación directa del contador, no comparación de snapshots).
    Harness h = makeHarness();
    const auto t0 = clk::now();
    h.loop->idleStep(t0);
    const int base = h.gui->renderCount;
    CHECK(base > 0);
    CHECK(!h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(10)));
    CHECK(h.gui->renderCount == base);
    CHECK(!h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(30)));
    CHECK(h.gui->renderCount == base);
}

TEST(idle_timeout_blink_renderiza_solo_al_cambiar_fase) {
    Harness h = makeHarness();
    h.ed->handleEvent(insertCharEv("i"));
    const auto t0 = clk::now();
    h.loop->idleStep(t0);
    const int base = h.gui->renderCount;
    // A los 100ms la fase sigue ON: sin render.
    CHECK(!h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(100)));
    CHECK(h.gui->renderCount == base);
    CHECK(h.gui->cursorShown());
    // A los 600ms la fase cambió a OFF: un render, y el cursor se apaga.
    CHECK(h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(600)));
    CHECK(h.gui->renderCount == base + 1);
    CHECK(!h.gui->cursorShown());
    // Wake posterior sin cambio (aún OFF): sin render.
    CHECK(!h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(700)));
    CHECK(h.gui->renderCount == base + 1);
}

TEST(idle_timeout_deadline_coincide_con_cambio_de_forma) {
    // P1 (revisor): un render por deadline que coincide con un cambio de
    // forma del cursor (Bar -> Block) resetea el ancla durante el render.
    // La fase debe registrarse DESPUÉS (contra la nueva ancla): el wake
    // siguiente no provoca render adicional ni deja el blink desincronizado.
    Harness h = makeHarness();
    h.ed->handleEvent(insertCharEv("i"));  // Interaccion: Bar
    const auto t0 = clk::now();
    h.loop->idleStep(t0);
    CHECK(h.gui->lastCursor().shape == FrameCursorShape::Bar);
    const int base = h.gui->renderCount;
    // A los 600ms la fase está OFF; antes del wake el cursor cambia de
    // forma (Escape -> Navegacion = Block) sin render intermedio.
    InputEvent esc;
    esc.type = InputEventType::Escape;
    h.ed->handleEvent(esc);
    CHECK(h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(600)));
    CHECK(h.gui->renderCount == base + 1);
    CHECK(h.gui->lastCursor().shape == FrameCursorShape::Block);
    CHECK(h.gui->cursorShown());  // Block: fijo, siempre visible
    // El ancla se reseteó al cambiar de forma: sin animación pendiente y
    // sin renders adicionales en los wakes siguientes.
    CHECK(!h.gui->msUntilBlinkToggle(t0 + std::chrono::milliseconds(600)).has_value());
    CHECK(!h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(630)));
    CHECK(h.gui->renderCount == base + 1);
    CHECK(!h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(1200)));
    CHECK(h.gui->renderCount == base + 1);
    CHECK(h.gui->cursorShown());
}

TEST(idle_timeout_expira_mensaje) {
    Harness h = makeHarness();
    InputEvent undo;
    undo.type = InputEventType::Undo;
    const auto t0 = clk::now();
    h.ed->handleEvent(undo);
    h.loop->idleStep(t0);
    CHECK(!h.gui->lastChrome().message.text.empty());
    const int base = h.gui->renderCount;
    // Antes del vencimiento: polling quieto, sin render.
    CHECK(!h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(30)));
    CHECK(h.gui->renderCount == base);
    // Tras el timeout: tick + render y el MessageBar queda vacío.
    const auto later = t0 + Editor::kActionMessageTimeout + std::chrono::milliseconds(50);
    CHECK(h.loop->idleOnTimeout(later));
    CHECK(h.gui->renderCount == base + 1);
    CHECK(h.gui->lastChrome().message.text.empty());
}

TEST(idle_timeout_watcher_por_periodicidad) {
    auto watcher = std::make_unique<CountingWatcher>();
    CountingWatcher* raw = watcher.get();
    Harness h = makeHarness(std::move(watcher));
    const auto t0 = clk::now();
    h.loop->idleStep(t0);
    // Primer servicio: debido (nunca servido).
    CHECK(!h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(1)));
    CHECK(raw->pollCount.load() == 1);
    // Servicio reciente: no se repite en cada wake.
    CHECK(!h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(2)));
    CHECK(raw->pollCount.load() == 1);
    // Vencida la periodicidad: se sirve de nuevo.
    CHECK(!h.loop->idleOnTimeout(t0 + std::chrono::milliseconds(31)));
    CHECK(raw->pollCount.load() == 2);
}

TEST(idle_timeout_cambio_externo_en_bloque_renderiza) {
    // P1 (revisor): cambio externo en disco con cursor Bloque quieto, sin
    // timers ni eventos SDL. El procesamiento del watcher recarga y
    // renderiza sincrónicamente (Editor::handleFileChange): la pantalla
    // debe reflejar el cambio sin necesidad de deadlines visuales ni de
    // render periódico.
    testfw::TempFile tmp;
    tmp.write("linea uno\n");
    Harness h = makeHarness(std::make_unique<InotifyFileWatcher>());
    CHECK(h.ed->loadIntoActiveBuffer(tmp.path));
    CHECK(h.ed->getStateForTesting() == State::Navegacion);
    h.loop->idleStep(clk::now());
    CHECK(h.gui->lastCursor().shape == FrameCursorShape::Block);
    const int base = h.gui->renderCount;
    // Asienta el reloj de servicio del watcher (primera llamada: epoch ->
    // debida) sin cambios pendientes: no renderiza.
    CHECK(!h.loop->idleOnTimeout(clk::now()));
    CHECK(h.gui->renderCount == base);
    // Cambio externo (distinto tamaño para no depender de la granularidad
    // del mtime) + espera de entrega inotify con reintentos.
    tmp.write("linea uno modificada desde otro programa\nexterna dos\n");
    bool reloaded = false;
    for (int i = 0; i < 200 && !reloaded; ++i) {
        h.loop->idleOnTimeout(clk::now());
        reloaded = h.gui->lastChrome().message.text.find("recargado") != std::string::npos;
        if (!reloaded) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(reloaded);
    CHECK(h.gui->renderCount > base);
    // La pantalla (snapshot del último frame) refleja el contenido externo.
    bool seen = false;
    for (const auto& row : h.gui->lastContentRows())
        if (row.plain().find("modificada") != std::string::npos) seen = true;
    CHECK(seen);
}

TEST(cursor_view_snapshot_incluye_identidad_de_buffer) {
    // Cambio de buffer con las MISMAS coordenadas/estado (otro buffer en
    // 0,0): el snapshot debe diferir por la identidad del buffer, para que
    // el lote registre actividad intermedia y resetee el blink.
    Harness h = makeHarness();
    const Editor::CursorViewSnapshot a = h.ed->cursorViewSnapshot();
    CHECK(a.state == State::Navegacion);
    CHECK(a.cursorLine == 0);
    CHECK(a.cursorCol == 0);
    h.ed->executeCommand("buffer.nuevo");
    const Editor::CursorViewSnapshot b = h.ed->cursorViewSnapshot();
    CHECK(a != b);
    CHECK(a.bufferId != b.bufferId);
    // Volver al anterior: coordenadas/estado iguales al inicial y mismo
    // buffer, así que el snapshot coincide (la actividad intermedia ya se
    // registró al comparar paso a paso en el lote).
    h.ed->executeCommand("buffer.anterior");
    const Editor::CursorViewSnapshot c = h.ed->cursorViewSnapshot();
    CHECK(c == a);
    CHECK(c.bufferId == a.bufferId);
}

// ---------------------------------------------------------------------------
// Integración SDL: el watcher se sirve durante actividad continua
// ---------------------------------------------------------------------------
#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

#include <cstdlib>
#include <thread>

namespace {
// Sonda de video dummy (patrón SdlDummy de test_gui_chrome.cpp): init +
// ventana oculta para verificar disponibilidad sin efectos laterales.
struct VideoProbe {
    bool ok = false;
    VideoProbe() {
        if (const char* p = std::getenv("SDL_VIDEODRIVER")) prevVideo = p;
        if (const char* p = std::getenv("SDL_AUDIODRIVER")) prevAudio = p;
        ::setenv("SDL_VIDEODRIVER", "dummy", 1);
        ::setenv("SDL_AUDIODRIVER", "dummy", 1);
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return;
        SDL_Window* w = SDL_CreateWindow("probe", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
        if (!w) {
            SDL_Quit();
            return;
        }
        SDL_DestroyWindow(w);
        SDL_Quit();
        ok = true;
    }
    ~VideoProbe() {
        if (prevVideo.empty())
            ::unsetenv("SDL_VIDEODRIVER");
        else
            ::setenv("SDL_VIDEODRIVER", prevVideo.c_str(), 1);
        if (prevAudio.empty())
            ::unsetenv("SDL_AUDIODRIVER");
        else
            ::setenv("SDL_AUDIODRIVER", prevAudio.c_str(), 1);
    }
    std::string prevVideo;
    std::string prevAudio;
};

// Fija los drivers dummy durante TODO el test y restaura el entorno previo
// al salir del scope, incluso por return temprano o excepción.
struct SdlDummyDrivers {
    std::string prevVideo;
    std::string prevAudio;
    bool hadVideo = false;
    bool hadAudio = false;
    SdlDummyDrivers() {
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
    }
    ~SdlDummyDrivers() {
        if (hadVideo)
            ::setenv("SDL_VIDEODRIVER", prevVideo.c_str(), 1);
        else
            ::unsetenv("SDL_VIDEODRIVER");
        if (hadAudio)
            ::setenv("SDL_AUDIODRIVER", prevAudio.c_str(), 1);
        else
            ::unsetenv("SDL_AUDIODRIVER");
    }
    SdlDummyDrivers(const SdlDummyDrivers&) = delete;
    SdlDummyDrivers& operator=(const SdlDummyDrivers&) = delete;
};

// Garantiza el cierre del loop al salir del scope: ejecuta el release del
// watcher (si lo hay), encola SDL_QUIT con reintentos y une el hilo.
// Dismiss() cuando el test ya cerró.
struct LoopCloser {
    std::function<void()> release;
    std::thread& runner;
    bool closed = false;
    LoopCloser(std::function<void()> rel, std::thread& t)
        : release(std::move(rel)), runner(t) {}
    ~LoopCloser() {
        if (closed || !runner.joinable()) return;
        if (release) release();
        for (int i = 0; i < 500; ++i) {
            SDL_Event q{};
            q.type = SDL_QUIT;
            if (SDL_PushEvent(&q) == 1) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        runner.join();
    }
    void dismiss() { closed = true; }
    LoopCloser(const LoopCloser&) = delete;
    LoopCloser& operator=(const LoopCloser&) = delete;
};

// Restaura el tope de lote de tests aunque el test salga por return o
// excepción.
struct MaxBatchMsGuard {
    int prev;
    explicit MaxBatchMsGuard(int ms)
        : prev(GuiRunLoop::maxBatchMsForTests()) {
        GuiRunLoop::setMaxBatchMsForTests(ms);
    }
    ~MaxBatchMsGuard() { GuiRunLoop::setMaxBatchMsForTests(prev); }
    MaxBatchMsGuard(const MaxBatchMsGuard&) = delete;
    MaxBatchMsGuard& operator=(const MaxBatchMsGuard&) = delete;
};
}  // namespace

TEST(sdl_dummy_drivers_restaura_entorno_al_salir) {
    // La limpieza RAII restaura el entorno aunque el scope termine por
    // return/return temprano o excepción, sin depender del flujo normal.
    ::setenv("SDL_VIDEODRIVER", "orig_video", 1);
    ::unsetenv("SDL_AUDIODRIVER");
    {
        SdlDummyDrivers env;
        CHECK(std::string(std::getenv("SDL_VIDEODRIVER")) == "dummy");
        CHECK(std::string(std::getenv("SDL_AUDIODRIVER")) == "dummy");
    }
    const char* video = std::getenv("SDL_VIDEODRIVER");
    CHECK(video && std::string(video) == "orig_video");
    CHECK(std::getenv("SDL_AUDIODRIVER") == nullptr);
}

TEST(gui_runloop_idle_sirve_watcher_con_actividad_continua) {
    {
        VideoProbe probe;
        if (!probe.ok) SKIP("sin video dummy");
    }
    SdlDummyDrivers env;

    auto watcher = std::make_unique<CountingWatcher>();
    CountingWatcher* rawWatcher = watcher.get();
    Harness h = makeHarness(std::move(watcher));
    int rc = 1;
    std::thread runner([&] { rc = h.loop->run(); });
    LoopCloser closer({}, runner);
    // Compuerta observable: el loop está vivo y sirviendo (primer servicio
    // del watcher) antes de inundar. Acotada: si el video nunca inicia,
    // run() ya devolvió 1 y el join no cuelga.
    bool ready = false;
    for (int i = 0; i < 300 && !ready; ++i) {
        ready = rawWatcher->pollCount.load() >= 1;
        if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(ready);
    // Flood SOSTENIDO a toda máquina (sin lotes ni pausas): el productor
    // supera al drenado (push ~ns vs drenado con reloj+switch por evento),
    // así la cola permanece no vacía y el loop no vuelve a la espera en
    // toda la ventana. Duración >> periodicidad (30ms) a propósito: no
    // depende del costo por evento de cada máquina. Los drops por cola
    // llena no importan (la cola sigue no vacía, que es lo que fuerza el
    // camino de drenado).
    const auto floodStart = clk::now();
    const auto floodEndTarget = floodStart + std::chrono::milliseconds(300);
    long pushed = 0;
    while (clk::now() < floodEndTarget) {
        SDL_Event ev{};
        ev.type = SDL_MOUSEMOTION;  // sin botón: se drena sin renderizar
        SDL_PushEvent(&ev);
        ++pushed;
    }
    const auto floodEnd = clk::now();
    CHECK(pushed > 1000);
    // Cierre con reintentos (la cola puede estar llena justo al terminar).
    bool quitPushed = false;
    for (int i = 0; i < 500 && !quitPushed; ++i) {
        SDL_Event q{};
        q.type = SDL_QUIT;
        if (SDL_PushEvent(&q) == 1) quitPushed = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(quitPushed);
    runner.join();
    closer.dismiss();

    CHECK(rc == 0);
    // Servicios estrictamente DENTRO de la ventana de flood (se excluyen
    // el de arranque anterior y los de la cola de salida posterior): con
    // ~10 períodos de 30ms en 300ms se esperan ~10; se exigen 2 por margen.
    std::vector<clk::time_point> inside;
    for (const auto& t : rawWatcher->serviceTimes)
        if (t > floodStart && t < floodEnd) inside.push_back(t);
    CHECK(inside.size() >= 2);
    // Postergación acotada DURANTE la actividad: ningún gap entre servicios
    // consecutivos dentro de la ventana supera 5× la periodicidad. Sin el
    // servicio dentro del drenado, un drenado gigante pospondría el polling
    // toda la ventana (gap ~300ms) y este bound lo cazaría.
    auto maxGap = std::chrono::milliseconds(0);
    for (size_t i = 1; i < inside.size(); ++i)
        maxGap = std::max(maxGap, std::chrono::duration_cast<std::chrono::milliseconds>(
                                      inside[i] - inside[i - 1]));
    CHECK(maxGap < std::chrono::milliseconds(150));
}
TEST(gui_runloop_idle_evento_sdl_mueve_cursor_reinicia_blink) {
    // P2 (revisor): el cursor se mueve por un evento SDL real antes del
    // vencimiento del blink (segundo camino de despertar: handleSdlEvent).
    // El render del evento debe resetear el ancla: la barra vuelve a ON y
    // el deadline siguiente se calcula desde la nueva ancla.
    {
        VideoProbe probe;
        if (!probe.ok) SKIP("sin video dummy");
    }
    SdlDummyDrivers env;

    auto watcher = std::make_unique<CountingWatcher>();
    CountingWatcher* rawWatcher = watcher.get();
    Harness h = makeHarness(std::move(watcher));
    // Archivo real con contenido (sin modificar: el QUIT final debe ser
    // aceptado) para que la flecha tenga a dónde moverse.
    testfw::TempFile tmp;
    tmp.write("hello\n");
    CHECK(h.ed->loadIntoActiveBuffer(tmp.path));
    h.ed->handleEvent(insertCharEv("i"));  // Interaccion: Bar, cursor (0,0)
    GuiRenderer* gui = h.gui;
    int rc = 1;
    std::thread runner([&] { rc = h.loop->run(); });
    LoopCloser closer({}, runner);
    // Compuerta: primer servicio del loop (ocurre tras el primer render,
    // que fija el ancla inicial). Así los 800ms siguientes miden fase real,
    // sin depender de la velocidad de init. Acotada y no fatal: si el video
    // nunca inicia, run() ya devolvió 1 y el join no cuelga.
    bool ready = false;
    for (int i = 0; i < 300 && !ready; ++i) {
        ready = rawWatcher->pollCount.load() >= 1;
        if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(ready);
    // Espera a que la fase inicial quede OFF (800ms >> medio período de
    // 530ms desde el primer render): así el reseteo del ancla es
    // distinguible (sin él, la fase seguiría OFF).
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    // SDL_KEYDOWN real (flecha derecha): mueve el cursor una celda a la
    // derecha SIN modificar el buffer (un InsertChar marcaría modified y
    // el QUIT posterior sería correctamente rechazado, colgando el join).
    const auto tPush = clk::now();
    {
        SDL_Event ev{};
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = SDLK_RIGHT;
        ev.key.keysym.mod = KMOD_NONE;
        ev.key.state = SDL_PRESSED;
        ev.key.repeat = 0;
        SDL_PushEvent(&ev);
    }
    SDL_Event quit{};
    quit.type = SDL_QUIT;
    SDL_PushEvent(&quit);
    runner.join();
    closer.dismiss();

    CHECK(rc == 0);
    // El cursor se movió por el evento SDL (coords de contenido: la celda
    // de pantalla suma el gutter, por eso no se afirma contra ella).
    CHECK(gui->lastCursor().shape == FrameCursorShape::Bar);
    CHECK(gui->lastCursor().visible);
    CHECK(h.ed->getActiveBufferForTesting().cursor.col == 1);
    // El ancla se reseteó al moverse: fase ON aunque el reloj de la fase
    // anterior estaba en OFF. cursorShown() usa el último tiempo inyectado
    // (el del render del evento), así que es determinista, sin relojes.
    CHECK(gui->cursorShown());
    // El deadline siguiente sale de la nueva ancla: consultado 100ms
    // después del push (el evento se procesa en ms), el resto ronda 430+;
    // con el ancla vieja (~850ms atrás) rondaría ~210. Margen 400.
    const auto rest = gui->msUntilBlinkToggle(tPush + std::chrono::milliseconds(100));
    CHECK(rest.has_value());
    CHECK(*rest > 400);
    CHECK(*rest <= 530);
}
TEST(gui_runloop_idle_quit_limpio) {
    {
        VideoProbe probe;
        if (!probe.ok) SKIP("sin video dummy");
    }
    SdlDummyDrivers env;

    Harness h = makeHarness();
    int rc = 1;
    std::thread runner([&] { rc = h.loop->run(); });
    LoopCloser closer({}, runner);
    for (int i = 0; i < 200 && !(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SDL_Event quit{};
    quit.type = SDL_QUIT;
    SDL_PushEvent(&quit);
    runner.join();
    closer.dismiss();

    CHECK(rc == 0);
}

TEST(gui_runloop_batch_repeats_un_solo_render_y_sin_movimiento_posterior) {
    // Reproduce el lag de AvPág: KEYDOWN + N repeats + KEYUP en un solo
    // lote. El loop aplica todos los eventos (FIFO) pero pinta UNA vez, y
    // tras soltar no quedan repeticiones pendientes que muevan la vista.
    {
        VideoProbe probe;
        if (!probe.ok) SKIP("sin video dummy");
    }
    SdlDummyDrivers env;

    auto watcher = std::make_unique<BlockingWatcher>();
    BlockingWatcher* raw = watcher.get();
    Harness h = makeHarness(std::move(watcher));
    static constexpr int kLines = 2000;
    std::string content;
    for (int i = 0; i < kLines; ++i)
        content += "linea " + std::to_string(i) + "\n";
    testfw::TempFile tmp;
    tmp.write(content);
    CHECK(h.ed->loadIntoActiveBuffer(tmp.path));
    int rc = 1;
    std::thread runner([&] { rc = h.loop->run(); });
    LoopCloser closer([raw] { raw->release(); }, runner);
    // Esperar a que el loop haya drenado los eventos de creación de la
    // ventana y esté en reposo (render estable + watcher ya servido); recién
    // ahí se arma la compuerta para no bloquear a mitad del drenado inicial.
    int lastRender = -1;
    int stable = 0;
    bool idle = false;
    for (int i = 0; i < 500 && !idle; ++i) {
        const int rcNow = h.gui->renderCount.load();
        if (raw->pollCount.load() >= 1 && rcNow == lastRender) {
            if (++stable >= 10) idle = true;
        } else {
            stable = 0;
        }
        lastRender = rcNow;
        if (!idle) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(idle);
    raw->arm();
    const bool blocked = raw->waitBlocked();
    CHECK(blocked);
    if (!blocked) return;
    const int base = h.gui->renderCount.load();
    const int hgt = h.ed->getActiveBufferForTesting().viewport.height;
    CHECK(hgt > 0);
    CHECK(hgt < kLines);
    // Referencia secuencial: mismo documento y misma geometría, procesando
    // los AvPág uno a uno con la semántica real del Editor. La expectativa
    // no reimplementa la fórmula de applyPage; solo verifica que el lote
    // produce el mismo estado que el manejo secuencial (paso real).
    constexpr int kPageDowns = 5;
    Harness ref = makeHarness();
    CHECK(ref.ed->loadIntoActiveBuffer(tmp.path));
    {
        Buffer& rb = ref.ed->getActiveBufferForTesting();
        const Buffer& lb = h.ed->getActiveBufferForTesting();
        rb.viewport.height = lb.viewport.height;
        rb.viewport.width = lb.viewport.width;
        rb.viewport.top = lb.viewport.top;
        rb.viewport.left = lb.viewport.left;
        rb.cursor.line = lb.cursor.line;
        rb.cursor.col = lb.cursor.col;
    }
    InputEvent pageDown;
    pageDown.type = InputEventType::PageDown;
    for (int i = 0; i < kPageDowns; ++i)
        ref.ed->processEventForTesting(pageDown);
    const int expectedTop = ref.ed->getActiveBufferForTesting().viewport.top;
    const int expectedCursor = ref.ed->getActiveBufferForTesting().cursor.line;
    const size_t samplesBefore = h.gui->cursorSamples().size();

    auto pushKeyDown = [](SDL_Keycode sym, bool repeat) {
        SDL_Event ev{};
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = sym;
        ev.key.keysym.mod = KMOD_NONE;
        ev.key.state = SDL_PRESSED;
        ev.key.repeat = repeat ? 1 : 0;
        CHECK(SDL_PushEvent(&ev) == 1);
    };
    pushKeyDown(SDLK_PAGEDOWN, false);
    for (int i = 1; i < kPageDowns; ++i) pushKeyDown(SDLK_PAGEDOWN, true);
    SDL_Event up{};
    up.type = SDL_KEYUP;
    up.key.keysym.sym = SDLK_PAGEDOWN;
    up.key.state = SDL_RELEASED;
    CHECK(SDL_PushEvent(&up) == 1);
    raw->release();

    bool rendered = false;
    for (int i = 0; i < 300 && !rendered; ++i) {
        rendered = h.gui->renderCount.load() > base;
        if (!rendered) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(rendered);
    // Dejar terminar el render (el contador sube al inicio del frame).
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(h.ed->getActiveBufferForTesting().viewport.top == expectedTop);
    CHECK(h.ed->getActiveBufferForTesting().cursor.line == expectedCursor);
    // La compuerta garantiza que la cola estaba vacía y que lo único
    // encolado es el lote de teclas: cualquier render posterior es de este
    // lote. Se exige UN render y UNA muestra, y que esa muestra ya tenga el
    // estado final (ningún frame intermedio).
    CHECK(h.gui->renderCount.load() == base + 1);
    const auto samples = h.gui->cursorSamples();
    CHECK(samples.size() == samplesBefore + 1);
    if (samples.size() == samplesBefore + 1)
        CHECK(samples.back() == std::make_pair(expectedCursor, expectedTop));
    // Sin eventos pendientes: ni renders ni movimiento adicionales.
    const int settled = h.gui->renderCount.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(h.gui->renderCount.load() == settled);
    CHECK(h.ed->getActiveBufferForTesting().viewport.top == expectedTop);
    CHECK(h.ed->getActiveBufferForTesting().cursor.line == expectedCursor);

    bool quitPushed = false;
    for (int i = 0; i < 500 && !quitPushed; ++i) {
        SDL_Event q{};
        q.type = SDL_QUIT;
        if (SDL_PushEvent(&q) == 1) quitPushed = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(quitPushed);
    runner.join();
    closer.dismiss();
    CHECK(rc == 0);
}

TEST(gui_runloop_batch_cursor_vuelve_al_origen_resetea_blink) {
    // AvPág + RePág en el MISMO lote: la celda final es igual a la inicial,
    // pero hubo actividad intermedia. El render debe resetear el ancla
    // (Bar ON) aunque el renderer no vea cambio de celda.
    {
        VideoProbe probe;
        if (!probe.ok) SKIP("sin video dummy");
    }
    SdlDummyDrivers env;

    auto watcher = std::make_unique<BlockingWatcher>();
    BlockingWatcher* raw = watcher.get();
    Harness h = makeHarness(std::move(watcher));
    std::string content;
    for (int i = 0; i < 200; ++i)
        content += "linea " + std::to_string(i) + "\n";
    testfw::TempFile tmp;
    tmp.write(content);
    CHECK(h.ed->loadIntoActiveBuffer(tmp.path));
    h.ed->executeCommand("navegacion.interaccion");  // Bar sin modificar
    int rc = 1;
    std::thread runner([&] { rc = h.loop->run(); });
    LoopCloser closer([raw] { raw->release(); }, runner);
    // Esperar a que el loop haya drenado los eventos de creación de la
    // ventana y esté en reposo (render estable + watcher ya servido); recién
    // ahí se arma la compuerta para no bloquear a mitad del drenado inicial.
    int lastRender = -1;
    int stable = 0;
    bool idle = false;
    for (int i = 0; i < 500 && !idle; ++i) {
        const int rcNow = h.gui->renderCount.load();
        if (raw->pollCount.load() >= 1 && rcNow == lastRender) {
            if (++stable >= 10) idle = true;
        } else {
            stable = 0;
        }
        lastRender = rcNow;
        if (!idle) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(idle);
    raw->arm();
    const bool blocked = raw->waitBlocked();
    CHECK(blocked);
    if (!blocked) return;
    CHECK(h.gui->lastCursor().shape == FrameCursorShape::Bar);
    const int base = h.gui->renderCount.load();
    const int top0 = h.ed->getActiveBufferForTesting().viewport.top;
    const int cursor0 = h.ed->getActiveBufferForTesting().cursor.line;
    const int hgt = h.ed->getActiveBufferForTesting().viewport.height;
    CHECK(hgt > 0);
    // Llevar la fase a OFF de forma determinista: el loop está bloqueado,
    // así que se inyecta el reloj sin render y se espera el cambio de fase.
    // (Un sleep fijo no sirve: el ancla puede tener una edad arbitraria por
    // los renders idle previos.)
    for (int i = 0; i < 200 && h.gui->blinkPhaseOn(); ++i) {
        h.gui->setBlinkNow(clk::now());
        if (h.gui->blinkPhaseOn())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(!h.gui->blinkPhaseOn());

    auto push = [](SDL_Keycode sym) {
        SDL_Event ev{};
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = sym;
        ev.key.keysym.mod = KMOD_NONE;
        ev.key.state = SDL_PRESSED;
        ev.key.repeat = 0;
        CHECK(SDL_PushEvent(&ev) == 1);
    };
    push(SDLK_PAGEDOWN);
    push(SDLK_PAGEUP);
    raw->release();

    bool rendered = false;
    for (int i = 0; i < 300 && !rendered; ++i) {
        rendered = h.gui->renderCount.load() > base;
        if (!rendered) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(rendered);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(h.ed->getActiveBufferForTesting().viewport.top == top0);
    CHECK(h.ed->getActiveBufferForTesting().cursor.line == cursor0);
    // El AvPág intermedio no se renderizó: todos los samples tienen el
    // cursor en la posición original (el lote se aplicó completo).
    for (const auto& sample : h.gui->cursorSamples()) {
        CHECK(sample.first == cursor0);
    }
    // Sin la actividad intermedia el ancla seguiría en el primer render y
    // la fase continuaría OFF.
    CHECK(h.gui->cursorShown());
    const auto rest = h.gui->msUntilBlinkToggle(clk::now());
    CHECK(rest.has_value());
    CHECK(*rest > 300);

    bool quitPushed = false;
    for (int i = 0; i < 500 && !quitPushed; ++i) {
        SDL_Event q{};
        q.type = SDL_QUIT;
        if (SDL_PushEvent(&q) == 1) quitPushed = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(quitPushed);
    runner.join();
    closer.dismiss();
    CHECK(rc == 0);
}



TEST(gui_runloop_batch_tope_de_tiempo_evita_inanicion) {
    // Lote grande con render pedido: con el tope de tiempo se pinta más de
    // una vez ANTES de haber procesado todo (sin tope habría un único render
    // al final). El hook de tests acorta el tope para que la prueba no
    // dependa del costo por evento de la máquina; el orden FIFO se verifica
    // por el cursor final.
    {
        VideoProbe probe;
        if (!probe.ok) SKIP("sin video dummy");
    }
    SdlDummyDrivers env;

    auto watcher = std::make_unique<BlockingWatcher>();
    BlockingWatcher* raw = watcher.get();
    Harness h = makeHarness(std::move(watcher));
    static constexpr int kEvents = 60000;
    std::string content(kEvents + 100, 'a');
    content += "\n";
    testfw::TempFile tmp;
    tmp.write(content);
    CHECK(h.ed->loadIntoActiveBuffer(tmp.path));

    int rc = 1;
    std::thread runner([&] { rc = h.loop->run(); });
    LoopCloser closer([raw] { raw->release(); }, runner);
    int lastRender = -1, stable = 0;
    bool idle = false;
    for (int i = 0; i < 500 && !idle; ++i) {
        const int r = h.gui->renderCount.load();
        if (raw->pollCount.load() >= 1 && r == lastRender) {
            if (++stable >= 10) idle = true;
        } else {
            stable = 0;
        }
        lastRender = r;
        if (!idle) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(idle);
    raw->arm();
    const bool blocked = raw->waitBlocked();
    CHECK(blocked);
    if (!blocked) return;
    MaxBatchMsGuard batchCap(5);
    const int base = h.gui->renderCount.load();
    for (int i = 0; i < kEvents; ++i) {
        SDL_Event ev{};
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = SDLK_RIGHT;
        ev.key.keysym.mod = KMOD_NONE;
        ev.key.state = SDL_PRESSED;
        ev.key.repeat = 0;
        CHECK(SDL_PushEvent(&ev) == 1);
    }
    raw->release();

    bool done = false;
    for (int i = 0; i < 2000 && !done; ++i) {
        done = h.ed->getActiveBufferForTesting().cursor.col == kEvents;
        if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const int renders = h.gui->renderCount.load() - base;
    CHECK(done);
    CHECK(renders >= 2);
    CHECK(h.ed->getActiveBufferForTesting().cursor.col == kEvents);

    bool quitPushed = false;
    for (int i = 0; i < 500 && !quitPushed; ++i) {
        SDL_Event q{};
        q.type = SDL_QUIT;
        if (SDL_PushEvent(&q) == 1) quitPushed = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(quitPushed);
    runner.join();
    closer.dismiss();
    CHECK(rc == 0);
}

#endif
