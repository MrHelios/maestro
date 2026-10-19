#include "platform/gui/GuiRunLoop.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "app/Editor.h"
#include "base/utf8.h"
#include "platform/MouseEvent.h"
#include "platform/WindowSize.h"
#include "rendering/Sink.h"
#include "rendering/gui/GuiRenderer.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

#include "platform/gui/GuiSdlMapper.h"
#endif

int GuiRunLoop::maxBatchMsOverride_ = 0;

void GuiRunLoop::setMaxBatchMsForTests(int ms) {
    maxBatchMsOverride_ = ms;
}

GuiRunLoop::GuiRunLoop(Editor& editor, const GuiOptions& opts)
    : editor_(editor), options_(opts) {}

void GuiRunLoop::idleStep(std::chrono::steady_clock::time_point now) {
    // Inyecta el tiempo de blink antes de pintar: la fase ON/OFF sale de
    // GuiRenderer (Bar parpadea, Block fijo; el ancla se resetea al mover el
    // cursor). Sin gui_ es solo tick+render (tests del Editor).
    // Sin bombeo externo a propósito: run() lo hace UNA vez al tope de cada
    // vuelta (antes de drenar SDL), así el camino idle no lo repite. Quien
    // llame idleStep() directo (tests) bombea antes vía
    // editor_.pollExternalEvents(), igual que run().
    if (gui_) gui_->setBlinkNow(now);
    editor_.tick(now);
    editor_.renderFrame();
    if (gui_) lastBlinkPhaseOn_ = gui_->blinkPhaseOn();
}

int GuiRunLoop::computeIdleWaitMs(
    std::chrono::steady_clock::time_point now) const {
    // Mínimo explícito entre deadlines reales: los ausentes no intervienen.
    // (Un std::min ingenuo con el -1 de nextTimeoutMs taparía el deadline
    // del blink y el loop despertaría tarde.)
    int waitMs = kMaxIdleWaitMs;
    const int editorMs = editor_.nextTimeoutMs(now);
    if (editorMs >= 0) waitMs = std::min(waitMs, editorMs);
    if (gui_) {
        if (const auto blinkMs = gui_->msUntilBlinkToggle(now))
            waitMs = std::min(waitMs, *blinkMs);
    }
    return waitMs;
}

int GuiRunLoop::idleDelayMs(std::chrono::steady_clock::time_point now) const {
    return computeIdleWaitMs(now);
}

void GuiRunLoop::serviceWatcherIfDue(
    std::chrono::steady_clock::time_point now) {
    if (!watcherServiceDue(lastWatcherService_, now)) return;
    editor_.pollExternalEvents();
    lastWatcherService_ = now;
}

bool GuiRunLoop::idleOnTimeout(std::chrono::steady_clock::time_point now) {
    // Reloj de blink fresco ANTES de servir al watcher: si el
    // procesamiento externo recarga y renderiza tras bambalinas
    // (Editor::handleFileChange lo hace sincrónicamente), lo hace con la
    // fase actual en vez de un tiempo obsoleto. Inyectar sin renderar es
    // seguro (ver comentario en la rama del blink, abajo).
    if (gui_) gui_->setBlinkNow(now);
    // Periodicidad propia del watcher (ver invariante en el header).
    serviceWatcherIfDue(now);
    // Deadline vencido del Editor (mensaje/autoscroll): tick + render con
    // tiempo de blink inyectado. Se despacha sin iniciar otra espera.
    if (editor_.nextTimeoutMs(now) == 0) {
        idleStep(now);
        return true;
    }
    // Sin animación pendiente (Block/invisible/sin ancla) no hay fase que
    // seguir: se omite el render especulativo (el loop solo pinta al
    // vencer un deadline real).
    // Sin animación pendiente (Block/invisible/sin ancla) no hay fase que
    // seguir: se omite el render especulativo (el loop solo pinta al
    // vencer un deadline real).
    if (gui_ && gui_->msUntilBlinkToggle(now).has_value()) {
        // Cambio de fase del blink: se informa el tiempo y se compara con
        // la fase del último render. Inyectar sin renderar es seguro: el
        // ancla solo cambia durante un render (noteCursorForBlink) y la
        // consulta de deadline usa el ancla, no el tiempo inyectado.
        const bool phaseOn = gui_->blinkPhaseOn();
        if (phaseOn != lastBlinkPhaseOn_) {
            editor_.renderFrame();
            // Registrar DESPUÉS del render: noteCursorForBlink() puede
            // haber reseteado el ancla (celda/forma/visibilidad) y la fase
            // efectiva se mide contra la nueva ancla (igual que idleStep
            // y renderWithBlink).
            lastBlinkPhaseOn_ = gui_->blinkPhaseOn();
            return true;
        }
        lastBlinkPhaseOn_ = phaseOn;
    }
    // Wake de polling sin cambios: sin render ni present.
    return false;
}

void GuiRunLoop::updateImeRect() const {
#ifdef HAVE_SDL2
    if (!gui_) return;
    const GuiPixelRect r = gui_->imeRectPx();
    // POLÍTICA IME (explícita): el rect se recalcula desde la geometría
    // actual del cursor en cada render (y al recuperar el foco, ver
    // FOCUS_GAINED abajo). Si no hay cursor válido se conserva el último
    // rect configurado en SDL (no se toca nada) como fallback aceptado:
    // SDL2 no ofrece "ocultar candidata" sin SDL_StopTextInput(), y detener
    // la entrada mataría una composición en curso; la candidata queda
    // anclada a la última celda válida hasta el próximo cursor válido.
    if (!r.valid) return;
    // UNIDADES (P2-IME): imeRectPx viene en píxeles del drawable (celda TTF ×
    // celda), pero SDL_SetTextInputRect habla en puntos de ventana (supuesto
    // documentado en guiImeRectToWindow: SDL3 lo explicita, SDL2 se infiere
    // del uso junto a eventos en puntos). Con ALLOW_HIGHDPI a escala N hay
    // que bajar N× por eje; a escala 1 es identidad. Sin handles o con
    // medidas inválidas se pasa tal cual (comportamiento pre-HiDPI).
    GuiImeRectPx pr{r.x, r.y, r.w, r.h};
    int x = pr.x, y = pr.y, w = pr.w, h = pr.h;
    if (sdlWin_ && sdlRen_) {
        int winW = 0, winH = 0, outW = 0, outH = 0;
        SDL_GetWindowSize(static_cast<SDL_Window*>(sdlWin_), &winW, &winH);
        SDL_GetRendererOutputSize(static_cast<SDL_Renderer*>(sdlRen_), &outW,
                                  &outH);
        const GuiImeRectPx o = guiImeRectToWindow(pr, winW, winH, outW, outH);
        x = o.x;
        y = o.y;
        w = o.w;
        h = o.h;
    }
    SDL_Rect sr;
    sr.x = x;
    sr.y = y;
    sr.w = w;
    sr.h = h;
    SDL_SetTextInputRect(&sr);
#endif
}

#ifndef HAVE_SDL2

int GuiRunLoop::run() {
    std::fprintf(stderr,
                 "maestro: --gui requiere compilar con SDL2 "
                 "(WITH_SDL2=1, falta libsdl2-dev).\n");
    return 1;
}

#else

namespace {
// Adaptador SDL -> vocabulario propio GUI: vive en GuiSdlMapper.h (límite
// SDL testeable, único lugar que nombra SDL_Keycode/KMOD_*); acá solo se
// usa. Ver las reglas (Caps/Num pelados, AltGr como Alt, keypad mínimo) en
// ese header y en tests/unit/test_guisdlmapper.cpp.
// El cálculo de grilla rows/cols = píxeles/celda vive en GuiSdlMapper.h
// (guiGridSize, pura y testeada sin SDL): acá solo se usa.
// La celda de respaldo es la canónica (platform::kFallbackCellW/H, ver
// WindowSize.h): con HiDPI se escala por eje con guiScaleSize, ver
// currentSize/mouseCell.
// CONTRATO DE UNIDADES (fuente y geometría):
//   - `--font-size N` son PÍXELES LÓGICOS (== píxeles del drawable a escala
//     1), no puntos tipográficos (1/72in) ni píxeles físicos. Antes de cada
//     TTF_OpenFont la base se convierte a píxeles del drawable con
//     guiScaledFontPixels (eje vertical, redondeo): a 2×, 16 -> 32. Así
//     `--font-size 16` da la misma altura visual y la misma grilla en
//     cualquier monitor (contrato visual-estable).
//   - `--geometry WxH` son COORDENADAS DE VENTANA (puntos, no píxeles
//     físicos): se pasan tal cual a SDL_CreateWindow; con HiDPI el drawable
//     resulta N× mayor y grilla/fuente se derivan del drawable.
// El valor por defecto de la base es 16 px lógicos; se cambia con --font-size.

#ifdef HAVE_SDL2
// Guard RAII del estado TextInput de SDL: garantiza SDL_StopTextInput()
// en TODOS los caminos de salida de run() (retorno anticipado futuro,
// excepción, salida normal). Sin esto, un `return` nuevo tras el Start
// dejaría la entrada de texto global de SDL activada tras cerrar la
// ventana. No copiable; vive en la pila de run() desde que hay renderer
// hasta el teardown.
class SdlTextInputGuard {
public:
    SdlTextInputGuard() { SDL_StartTextInput(); }
    ~SdlTextInputGuard() { SDL_StopTextInput(); }
    SdlTextInputGuard(const SdlTextInputGuard&) = delete;
    SdlTextInputGuard& operator=(const SdlTextInputGuard&) = delete;
};

// Captura de mouse best-effort (NO fatal): SDL_CaptureMouse() devuelve 0
// en éxito y -1 si el entorno no la soporta. En Wayland el compositor
// puede denegarla (el protocolo no garantiza captura global implícita
// como X11); eso es una LIMITACIÓN DEL ENTORNO, no un error del editor:
// el press/drag/release ya entregado sigue siendo válido y la selección
// queda coherente con la última celda conocida (ver nota en el press).
// Por eso el retorno se ignora a propósito y se limpia el error de SDL
// para no contaminar el diagnóstico posterior de SDL_WaitEventTimeout
// (que distingue timeout/error por SDL_GetError).
inline void sdlCaptureMouseBestEffort(SDL_bool enabled) {
    if (SDL_CaptureMouse(enabled) != 0) SDL_ClearError();
}
#endif
}  // namespace

int GuiRunLoop::run() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "maestro: SDL_Init falló: %s\n", SDL_GetError());
        return 1;
    }

    // ORÁCULO DE AUTOSCROLL (paridad X11MouseButtonQuery en TTY): si se
    // pierde el SDL_MOUSEBUTTONUP, el tick lo detecta vía
    // mouseButtonPressedQuery_ y desarma como un release. Es la ÚLTIMA capa
    // (respaldo), no la primera:
    //   1. Captura explícita en el press (ver SDL_MOUSEBUTTONDOWN): el up
    //      fuera de la ventana igual llega a esta ventana. Best-effort:
    //      en Wayland el compositor puede denegarla y entonces este nivel
    //      no aplica (ver sdlCaptureMouseBestEffort); el gesto sigue
    //      coherente con lo entregado dentro de la ventana y el cierre
    //      recae en los niveles 2-3.
    //   2. Release sintético en FOCUS_LOST (ver SDL_WINDOWEVENT): cubre el
    //      robo de foco, donde la captura cae sola y el up va a otro lado.
    //   3. Este oráculo: SDL_GetGlobalMouseState consulta al OS (a
    //      diferencia de SDL_GetMouseState, que solo repite el último pump
    //      y quedaría clavado en "presionado" si el release nunca entró a
    //      la cola). Límite conocido: donde el driver no pueda consultar al
    //      OS, el autoscroll por timer puede frenar antes (los pasos por
    //      evento de drag no se afectan); en el target X11 funciona.
    editor_.setMouseButtonPressedQuery([] {
        int x = 0, y = 0;
        const Uint32 st = SDL_GetGlobalMouseState(&x, &y);
        return (st & SDL_BUTTON_LMASK) != 0;
    });

    // Único lugar que decide los flags de creación de ventana (helper puro
    // testeado en test_guisdlmapper.cpp): --no-hidpi elimina
    // SDL_WINDOW_ALLOW_HIGHDPI y el cálculo de celdas sigue intacto (sin
    // HiDPI el drawable == ventana y la escala de guiScaledFontPixels es 1).
    const Uint32 windowFlags = guiWindowFlags(options_.enableHiDpi);

    int winW = options_.windowWidth.value_or(960);
    int winH = options_.windowHeight.value_or(600);

    SDL_Window* win = SDL_CreateWindow(
        "Maestro", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH,
        windowFlags);
    if (!win) {
        std::fprintf(stderr, "maestro: SDL_CreateWindow falló: %s\n",
                     SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* ren =
        SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        // Fallback a software si no hay aceleración.
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) {
        std::fprintf(stderr, "maestro: SDL_CreateRenderer falló: %s\n",
                     SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    // La GUI pinta directo en el SDL_Renderer; el Sink de bytes TTY no se
    // usa (NullSink de descarte, vive todo el run).
    NullSink discard;
    editor_.setSink(discard);
    // Handles para updateImeRect (rect IME a puntos de ventana, ver arriba).
    sdlWin_ = static_cast<void*>(win);
    sdlRen_ = static_cast<void*>(ren);
    // Abre la fuente y mide la celda real ANTES del primer resize: de acá
    // salen cols/filas = píxeles/celda (ya no estimados).
    // La base del CLI son píxeles lógicos (ver contrato de arriba): se escala
    // a píxeles del drawable con las medidas REALES de ventana/salida antes
    // de abrir, así la celda ya nace correcta en HiDPI y el primer resize no
    // paga una recarga. La misma fórmula corre en cada currentSize (cambio
    // de monitor sin resize en puntos).
    const int baseFontPx = options_.fontSize.value_or(16);
    // Solo el eje VERTICAL: la talla tipográfica es altura y el ancho no se
    // mezcla en la escala de la fuente (la grilla la calcula el primer
    // currentSize con ambos ejes). Medidas degeneradas (minimizado) dan la
    // base sin escalar vía la identidad de guiScaledFontPixels.
    int initWinH = 0, initOutH = 0;
    SDL_GetWindowSize(win, nullptr, &initWinH);
    SDL_GetRendererOutputSize(ren, nullptr, &initOutH);
    const int initFontPixels =
        guiScaledFontPixels(baseFontPx, initWinH, initOutH);
    if (gui_ &&
        !gui_->initForRenderer(static_cast<void*>(ren), initFontPixels)) {
        std::fputs(fontOpenDiagnostic(options_).c_str(), stderr);
        if (gui_) gui_->setSdlRenderer(nullptr);
        sdlWin_ = nullptr;
        sdlRen_ = nullptr;
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    // POLÍTICA TEXT INPUT (explícita): habilitado durante TODO el run,
    // también en navegación. SDL gestiona la composición de forma continua
    // y alternar Start/Stop por modo la interrumpiría al cambiar entre
    // Navegación/Interacción/prompts. El rect se actualiza tras cada render
    // (updateImeRect) a la celda del cursor lógico. El guard garantiza el
    // Stop en todos los caminos de salida.
    SdlTextInputGuard textInput;

    auto currentSize = [&] {
        // ESPACIO DRAWABLE (no puntos de ventana): con ALLOW_HIGHDPI el
        // drawable puede ser N× la ventana, y la grilla cols/filas vive en
        // el espacio donde el renderer dibuja (sin logical size/scale) y
        // donde mide la celda TTF. Con SDL_GetWindowSize a escala N la
        // grilla quedaría N× chica. Fallback a ventana si el output falla.
        // CONTRATO VISUAL-ESTABLE (P1): la fuente se escala con el drawable
        // ANTES de dividir (16 -> 32 a 2×): la celda TTF crece N× y la grilla
        // se mantiene (~106×33 en vez de duplicarse a 213×66 con texto
        // diminuto). Sin fuente, el fallback 9×18 se escala por eje igual.
        int winW = 0, winH = 0, outW = 0, outH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_GetRendererOutputSize(ren, &outW, &outH);
        // Drawable efectivo por eje (minimizado / driver degenerado): un eje
        // en 0 cae a su eje de ventana sin descartar el otro válido; con
        // todo en 0 da 960×600 y la grilla se clampe a >=1 (nunca 0×0 al
        // Editor). Minimizar no re-mide por evento (ver
        // guiResizeNeedsRemeasure): conserva la última grilla válida o esta
        // degradada; al restaurar, SIZE_CHANGED/DISPLAY_CHANGED re-mide.
        const GuiDrawableSize fb = guiDrawableOrWindow(winW, winH, outW, outH);
        outW = fb.w;
        outH = fb.h;
        if (gui_) {
            gui_->ensureFontPixels(
                guiScaledFontPixels(baseFontPx, winH, outH));
        }
        int cw = 0, ch = 0;
        if (gui_ && gui_->hasFont()) {
            cw = gui_->cellW();
            ch = gui_->cellH();
        } else {
            cw = guiScaleSize(platform::kFallbackCellW, winW, outW);
            ch = guiScaleSize(platform::kFallbackCellH, winH, outH);
        }
        return guiGridSize(outW, outH, cw, ch);
    };
    auto renderWithBlink = [&] {
        if (gui_) gui_->setBlinkNow(std::chrono::steady_clock::now());
        editor_.renderFrame();
        if (gui_) lastBlinkPhaseOn_ = gui_->blinkPhaseOn();
        updateImeRect();
    };
    // LOTE DE EVENTOS: en vez de renderizar dentro de handleSdlEvent (lo
    // que hace un frame completo por cada KEYDOWN repetido), se acumulan
    // los efectos de estado y se pinta UNA vez al vaciar la cola SDL. El
    // KEYUP entra en el mismo lote: no queda atrapado detrás de renders.
    // La semántica de cada evento se preserva en orden FIFO; solo se
    // difiere el pintado. Sin tope de lote a propósito (un límite podría
    // dejar el KEYUP para después del render).
    bool renderNeeded = false;
    auto requestRender = [&] { renderNeeded = true; };
    // Actividad de cursor/viewport DENTRO del lote: un AvPág + RePág deja
    // la misma celda final, pero el cursor sí se movió y el blink debe
    // reiniciarse (ver GuiRenderer::markCursorActivity).
    Editor::CursorViewSnapshot batchView = editor_.cursorViewSnapshot();
    bool batchCursorActivity = false;
    // Puntos de evento SDL -> celda 0-based (dominio neutro CellPos): la
    // MISMA métrica que el resize (fuente escalada o fallback escalado) y el
    // MISMO espacio (drawable). Los eventos llegan en puntos de ventana: con
    // ALLOW_HIGHDPI a escala N hay que subir N× ANTES de dividir por la
    // celda (guiScaleCoord por eje); dividir puntos por celda TTF da la
    // celda N× chica. Medidas frescas por evento (barato): la escala puede
    // cambiar al mover la ventana entre monitores sin resize. La celda se
    // ACOTA a la grilla del drawable (guiCellFromPixelsBounded con los
    // cols/rows de guiGridSize): el borde exterior queda FUERA (inválida,
    // el Editor la trata como fuera de contenido) en vez de clamparse a la
    // última columna; el pasado-EOL DENTRO de la grilla lo sigue resolviendo
    // screenToCursor a EOL. Sin offsets propios.
    auto mouseCell = [&](int ptX, int ptY) {
        int winW = 0, winH = 0, outW = 0, outH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_GetRendererOutputSize(ren, &outW, &outH);
        // Mismo fallback que currentSize (guiDrawableOrWindow, por eje): sin
        // drawable se usa la ventana (o 960×600 si tampoco hay ventana),
        // para que grilla y mouse sigan en el mismo espacio también en el
        // camino degenerado.
        const GuiDrawableSize mfb = guiDrawableOrWindow(winW, winH, outW, outH);
        outW = mfb.w;
        outH = mfb.h;
        int cw = 0, ch = 0;
        if (gui_ && gui_->hasFont()) {
            cw = gui_->cellW();
            ch = gui_->cellH();
        } else {
            cw = guiScaleSize(platform::kFallbackCellW, winW, outW);
            ch = guiScaleSize(platform::kFallbackCellH, winH, outH);
        }
        const int pxX = guiScaleCoord(ptX, winW, outW);
        const int pxY = guiScaleCoord(ptY, winH, outH);
        const platform::WindowSize g = guiGridSize(outW, outH, cw, ch);
        return guiCellFromPixelsBounded(pxX, pxY, cw, ch, g.cols, g.rows);
    };
    editor_.resize(currentSize());
    // Tema inicial ya establecido en GuiRenderer constructor
    renderWithBlink();

    // La ventana se creó OCULTA (guiWindowFlags): recién ahora se muestra,
    // con el primer frame ya compuesto. Así el usuario no ve el hueco de la
    // inicialización (SDL_CreateRenderer/GLX, apertura de la fuente TTF y
    // primer llenado de la caché de texturas), que en un WM tiling sin
    // compositor se percibía como un destello "abre/cierra/abre".
    SDL_ShowWindow(win);
    // El contenido presentado mientras la ventana estaba oculta NO se da por
    // conservado (el driver puede haberlo descartado): se fuerza un frame
    // completo sobre la ventana ya visible. La preparación previa dejó la
    // caché de texturas caliente, así que este segundo frame es barato.
    // El gestor de ventanas puede reacomodar la geometría al mostrar (i3),
    // lo que llega como RESIZED/SIZE_CHANGED y se re-mide en el handler de
    // SDL_WINDOWEVENT de abajo.
    if (gui_) gui_->invalidateCache();
    renderWithBlink();

    // Resto fraccionario de rueda sin emitir (ver SDL_MOUSEWHEEL): un táctil
    // manda fracciones de preciseY que se juntan hasta 1.0. Vive todo el
    // run; no se resetea con el tiempo (0.9 parado + 0.2 tarde igual mueve:
    // inofensivo, un solo paso de 3 líneas).
    double wheelAcc = 0.0;

    while (editor_.isRunning()) {
        // Servicio del watcher con periodicidad propia (ver invariante en el
        // header): inotify vía pollEvents(), sin fd. El clipboard SDL es
        // síncrono (no-op). Va ANTES de drenar SDL para que una recarga por
        // cambio externo quede visible en los renders de eventos de este
        // mismo frame. Solo se bombea al vencer la periodicidad, no en cada
        // vuelta incondicionalmente.
        serviceWatcherIfDue(std::chrono::steady_clock::now());
        // Camino ÚNICO de eventos: lo drenado por SDL_PollEvent y lo
        // obtenido por SDL_WaitEventTimeout se procesan acá, sin re-push.
        // (Reencolar lo esperado con SDL_PushEvent mostró livelock bajo
        // carga continua: el evento volvía a la espera sin pasar jamás por
        // el drenado. El procesado directo preserva orden FIFO sin
        // intermediarios.)
        auto handleSdlEvent = [&](SDL_Event& ev) {
            switch (ev.type) {
                case SDL_QUIT:
                    // Cierre de ventana = intento de salida (respeta
                    // buffers modificados: requestQuit(false) avisa).
                    if (!editor_.requestQuit(false)) {
                        requestRender();
                    }
                    break;
                case SDL_WINDOWEVENT:
                    if (guiResizeNeedsRemeasure(ev.window.event)) {
                        // RESIZED / SIZE_CHANGED / DISPLAY_CHANGED (decisión
                        // testeada en GuiSdlMapper.h): DISPLAY_CHANGED es el
                        // caso 1×->2× sin cambio de tamaño en puntos (la
                        // ventana lógica no se mueve pero el drawable y la
                        // escala sí). currentSize() re-consulta el drawable,
                        // re-escala la fuente y recalcula la grilla.
                        editor_.resize(currentSize());
                        requestRender();
                    } else if (guiFocusLossEndsGesture(ev.window.event)) {
                        // Pérdida de foco con gesto en curso: la captura cae
                        // sola y el BUTTONUP puede ir a otra ventana, así que
                        // se sintetiza el release (el Editor lo ignora si no
                        // hay gesto armado; la celda no se usa en release).
                        // Sin esto, el autoscroll quedaría corriendo con el
                        // botón ya suelto hasta que el oráculo lo note.
                        sdlCaptureMouseBestEffort(SDL_FALSE);
                        // Fábrica testeada (misma que el press/drag: tipo +
                        // celda en un solo punto de construcción).
                        InputEvent ie = makeMouseReleaseEvent(CellPos{});
                        editor_.handleEvent(ie);
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        requestRender();
                    } else if (guiFocusRegainRefreshesIme(ev.window.event)) {
                        // Foco recuperado: el rect IME de SDL puede haber
                        // quedado obsoleto (cursor, buffer, tamaño o escala
                        // cambiados en el medio), así que se repinta y se
                        // recalcula desde la geometría actual del cursor
                        // (renderWithBlink termina en updateImeRect con
                        // medidas frescas). Sin cursor válido aún, rige el
                        // fallback de conservar el último conocido.
                        requestRender();
                    } else if (guiExposeNeedsRepaint(ev.window.event)) {
                        // Región descubierta (p. ej. al cerrarse el overlay de
                        // una captura): sin compositor, X11 no restaura los
                        // píxeles. Se vuelve a presentar desde el framebuffer
                        // persistente; no hace falta invalidar el contenido.
                        requestRender();
                    }
                    break;
                case SDL_KEYDOWN: {
                    // Teclado completo vía GuiKeymap: SDL_Keysym se adapta
                    // a vocabulario propio (GuiKey.h, sin SDL) en el límite
                    // SDL y el keymap traduce a InputEvent semántico. La
                    // política de repetición vive en el keymap (flechas,
                    // Home/End, PgUp/PgDn y borrado repiten; Enter, Escape,
                    // Prefix/Save/SaveAs/Quit/Undo/Redo no).
                    // KEYDOWN nunca lleva texto imprimible (sin doble
                    // inserción: el texto viene por SDL_TEXTINPUT).
                    InputEvent ie;
                    bool send = false;
                    const GuiKeyPress press = guiKeyFromSdl(
                        ev.key.keysym.sym, ev.key.keysym.mod,
                        ev.key.repeat != 0);
                    if (auto t = keymap_.mapKey(press)) {
                        ie.type = *t;
                        send = true;
                    }
                    if (send) {
                        editor_.handleEvent(ie);
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        requestRender();
                    }
                    break;
                }
                case SDL_TEXTINPUT: {
                    // Texto CONFIRMADO (teclado o commit IME): fuente única
                    // del texto, se acepta SIEMPRE sin filtrar por
                    // modificadores. El evento SDL_TEXTINPUT no trae mods y
                    // los combos de control (Ctrl+S/Ctrl+Q/...) no generan
                    // texto en SDL, así que no hay doble inserción; en
                    // cambio AltGr (p.ej. @/|/\ según layout: comportamiento
                    // actual, no garantía universal) SÍ llega por aquí y un
                    // filtro global de Ctrl/Alt lo rompería. UTF-8 que
                    // puede traer varios caracteres en un solo commit.
                    // Cada celda entra como InsertChar, la operación
                    // semántica que el Editor ya usa (el handling por modo
                    // decide: inserta, completa prompts o interpreta
                    // 'i'/'s'/... en Navegación; tras Prefix, 'n'/'t'/...
                    // deciden el comando y lo desconocido cancela).
                    const char* t = ev.text.text;
                    if (t && *t) {
                        for (const std::string& cell :
                             utf8::splitCells(t)) {
                            InputEvent ie;
                            ie.type = InputEventType::InsertChar;
                            ie.text = cell;
                            editor_.handleEvent(ie);
                        }
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        requestRender();
                    }
                    break;
                }
                case SDL_TEXTEDITING: {
                    // Composición PROVISIONAL del IME: no se inserta en el
                    // Document (hacerlo duplicaría inserciones por cada
                    // actualización del candidato). Sin superficie de
                    // pintado de composición en esta fase se descarta; el
                    // rect IME ya sigue al cursor lógico y el commit llega
                    // como SDL_TEXTINPUT. Mejora futura: subrayado de
                    // composición.
                    break;
                }
                case SDL_MOUSEBUTTONDOWN: {
                    // Click izquierdo -> MousePress en la celda (0-based).
                    // Medio/derecho -> nullopt (paridad TTY: se traducen a
                    // None, sin gesto). El Editor arma el gesto (cursor +
                    // anchor, sin rango): el drag posterior crea la
                    // selección, el release sin drag es no-op.
                    // Captura explícita best-effort (ver
                    // sdlCaptureMouseBestEffort): con captura el up/motion
                    // fuera de la ventana igual llega acá. SDL ≥2.0.22 ya
                    // hace auto-capture durante el botón, pero la explícita
                    // cubre versiones viejas y deja la intención en el
                    // código; es seguro llamarla aunque ya haya captura.
                    // Si el entorno la deniega (Wayland: SDL_CaptureMouse
                    // devuelve -1), NO es error fatal: el gesto sigue
                    // coherente con lo entregado dentro de la ventana (la
                    // selección congela su borde en la última celda válida
                    // en vez de seguir al puntero de fuera) y el cierre
                    // recae en el release entregado dentro, el sintético de
                    // FOCUS_LOST o el oráculo del tick. Ver degradado en
                    // GuiRunLoop.h y README (--gui en Wayland).
                    // Construcción por fábrica testeada (makeMousePressEvent:
                    // tipo + celda 0-based en un solo punto, igual que el
                    // drag): el gate de arriba y la fábrica son las mismas
                    // funciones que cubren los tests, no una réplica.
                    if (guiPressTypeForButton(ev.button.button).has_value()) {
                        InputEvent ie = makeMousePressEvent(
                            mouseCell(ev.button.x, ev.button.y));
                        editor_.handleEvent(ie);
                        sdlCaptureMouseBestEffort(SDL_TRUE);
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        requestRender();
                    }
                    break;
                }
                case SDL_MOUSEBUTTONUP: {
                    // Release izquierdo -> MouseRelease (cierra el gesto;
                    // sin drag previo es no-op, con drag permanece en
                    // Selección y apaga el autoscroll). Libera la captura
                    // del press en modo best-effort (también cae sola al
                    // perder foco, ver SDL_WINDOWEVENT; si nunca hubo
                    // captura —Wayland— liberar es no-op seguro). Fábrica
                    // testeada igual que el press.
                    if (guiReleaseTypeForButton(ev.button.button).has_value()) {
                        InputEvent ie = makeMouseReleaseEvent(
                            mouseCell(ev.button.x, ev.button.y));
                        editor_.handleEvent(ie);
                        sdlCaptureMouseBestEffort(SDL_FALSE);
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        requestRender();
                    }
                    break;
                }
                case SDL_MOUSEMOTION: {
                    // Arrastre con izquierdo presionado -> MouseDrag por
                    // evento (1 línea / 1 celda + autoscroll en bordes en
                    // el Editor). Sin LMask no se emite nada: el Editor ya
                    // ignora drags sin press previo, pero no se genera
                    // tráfico espurio de hover. Sin captura (Wayland cuando
                    // el compositor la deniega) el motion de fuera no llega:
                    // la selección conserva su último borde válido dentro
                    // (coherente, sin rango espurio) hasta el próximo evento
                    // dentro, el release o el cierre por tick/foco.
                    if (!guiLeftHeld(ev.motion.state)) break;
                    InputEvent ie = makeMouseDragEvent(
                        mouseCell(ev.motion.x, ev.motion.y));
                    editor_.handleEvent(ie);
                    const auto now = std::chrono::steady_clock::now();
                    editor_.clearExpiredMessages(now);
                    requestRender();
                    break;
                }
                case SDL_MOUSEWHEEL: {
                    // Rueda DISCRETA con acumulación (decisión explícita, ver
                    // GuiSdlMapper.h): el Editor mueve ±3 líneas por evento
                    // (grilla en celdas) y solo-viewport con cursor quieto.
                    // `preciseY` (SDL 2.0.18+) trae la fracción real: un
                    // táctil manda y==0 con fracción, y descartarlo rompería
                    // el scroll suave. Se acumula y cada unidad entera emite
                    // un ScrollUp/Down; la fracción sola nunca sale como 3
                    // líneas. Sin umbral que alcance no hay render (nada
                    // cambió; el resto espera al próximo evento). El tipo por
                    // unidad sale de guiScrollTypeForWheel (única
                    // implementación, la misma que cubren los tests).
                    const double delta = guiWheelDelta(
                        ev.wheel.preciseY, ev.wheel.y,
                        ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED);
                    const int n = guiConsumeWheelDelta(wheelAcc, delta);
                    if (n == 0) break;
                    const auto t = guiScrollTypeForWheel(n > 0 ? 1 : -1);
                    const int m = n > 0 ? n : -n;
                    for (int i = 0; i < m; ++i) {
                        InputEvent ie;
                        ie.type = *t;
                        editor_.handleEvent(ie);
                    }
                    const auto now = std::chrono::steady_clock::now();
                    editor_.clearExpiredMessages(now);
                    requestRender();
                    break;
                }
                default:
                    break;
            }
        };
        // Envuelve el handler con la detección de actividad intermedia del
        // lote (debe definirse DESPUÉS de handleSdlEvent).
        auto processEvent = [&](SDL_Event& ev) {
            handleSdlEvent(ev);
            const Editor::CursorViewSnapshot after =
                editor_.cursorViewSnapshot();
            if (after != batchView) batchCursorActivity = true;
            batchView = after;
        };
        SDL_Event ev;
        bool hadEvent = false;
        // Tope de tiempo del lote: con entrada continua la cola puede no
        // vaciarse nunca. Si ya hay un render pedido, se corta y se pinta
        // para no postergar la presentación/blink; el resto queda para el
        // próximo lote (FIFO, sin descartes).
        const auto batchStart = std::chrono::steady_clock::now();
        while (SDL_PollEvent(&ev)) {
            hadEvent = true;
            // La actividad continua de eventos no puede posponer el polling
            // del watcher: se comprueba también dentro del drenado.
            serviceWatcherIfDue(std::chrono::steady_clock::now());
            processEvent(ev);
            if (renderNeeded &&
                std::chrono::steady_clock::now() - batchStart >=
                    std::chrono::milliseconds(batchCapMs())) {
                break;
            }
        }
        if (renderNeeded) {
            // Un solo render por lote, con el estado final ya aplicado. La
            // actividad intermedia (aunque el cursor haya vuelto al origen)
            // se le informa al renderer antes de pintar.
            if (batchCursorActivity && gui_) gui_->markCursorActivity();
            batchCursorActivity = false;
            renderNeeded = false;
            renderWithBlink();
            // Volver al tope: durante el render pueden haber llegado más
            // eventos; se drenan en el próximo lote en vez de esperar.
            continue;
        }
        if (!hadEvent) {
            // Sin eventos: espera hasta el próximo deadline real (timeout
            // del Editor o cambio de fase del blink, acotado por
            // kMaxIdleWaitMs para el polling del watcher) o hasta el
            // próximo evento de entrada, lo que ocurra primero. Solo se
            // renderiza cuando algo venció (ver idleOnTimeout): los
            // despertares de polling sin cambios no tocan la GPU. Con una
            // operación bloqueante el repintado puede retrasarse (la
            // precisión del deadline es de cálculo, no garantía del SO).
            const auto now = std::chrono::steady_clock::now();
            const int waitMs = computeIdleWaitMs(now);
            if (waitMs <= 0) {
                // Deadline ya vencido: se despacha sin esperar.
                if (idleOnTimeout(now)) updateImeRect();
                continue;
            }
            SDL_ClearError();
            SDL_Event wev;
            const int rc = SDL_WaitEventTimeout(&wev, waitMs);
            if (rc == 1) {
                // Evento durante la espera: se procesa por el camino único
                // (arriba), sin re-push a la cola.
                processEvent(wev);
                continue;
            }
            // rc == 0: timeout normal O error de espera (el contrato SDL 2.0+
            // documentado en SDL_events.h devuelve 0 en ambos casos).
            // Se distingue SOLO a efectos de pacing, nunca de corrección:
            // el texto de SDL_GetError (limpiado antes de esperar, así un
            // error viejo no se confunde con este) decide únicamente si se
            // aplica un retroceso acotado. El camino de deadlines corre
            // igual en ambos casos: un error no provoca renders espurios
            // (idleOnTimeout solo pinta ante deadlines reales) ni bloquea
            // la entrada (la próxima vuelta bombea PollEvent). Nótese que
            // SDL desaconseja el texto como señal de error en general
            // (SDL_error.h); acá un falso positivo solo costaría una
            // espera de 30ms, sin efecto en corrección.
            const char* waitErr = SDL_GetError();
            if (waitErr != nullptr && waitErr[0] != '\0') {
                // Error real de espera: retroceso acotado ANTES de seguir,
                // separado del mecanismo de deadlines (no altera timers ni
                // blink), para no producir un ciclo rápido de reintentos si
                // el fallo persiste.
                SDL_ClearError();
                SDL_Delay(static_cast<Uint32>(kMaxIdleWaitMs));
            }
            if (idleOnTimeout(std::chrono::steady_clock::now())) updateImeRect();
        }
    }

    // Sin SDL_StopTextInput() manual: lo hace ~SdlTextInputGuard (RAII),
    // así cualquier retorno anticipado futuro también limpia el estado.
    if (gui_) gui_->setSdlRenderer(nullptr);
    sdlWin_ = nullptr;
    sdlRen_ = nullptr;
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

#endif
