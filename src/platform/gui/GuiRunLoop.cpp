#include "platform/gui/GuiRunLoop.h"

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

GuiRunLoop::GuiRunLoop(Editor& editor) : editor_(editor) {}

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
}

int GuiRunLoop::idleDelayMs(std::chrono::steady_clock::time_point now) const {
    int waitMs = editor_.nextTimeoutMs(now);
    if (waitMs < 0 || waitMs > 30) waitMs = 30;
    // Tope 30ms << periodo de blink (530ms): acota el SDL_Delay de la rama
    // sin eventos para que, en ese camino, el repintado sea lo bastante
    // frecuente para animar la barra. No planifica nada por sí mismo: la
    // frecuencia real depende de que el loop drene eventos sin bloquearse.
    return waitMs;
}

void GuiRunLoop::updateImeRect() const {
#ifdef HAVE_SDL2
    if (!gui_) return;
    const GuiPixelRect r = gui_->imeRectPx();
    // POLÍTICA IME (explícita): si no hay cursor válido se conserva el
    // último rect configurado en SDL (no se toca nada). SDL2 no ofrece
    // "ocultar candidata" sin SDL_StopTextInput(), y detener la entrada
    // mataría una composición en curso; la candidata queda anclada a la
    // última celda válida hasta el próximo cursor válido. No es un olvido:
    // es la posición de respaldo durante estados sin cursor.
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
// Talla base a escala 1 (puntos == píxeles del drawable). El tamaño real se
// escala por eje vertical con guiScaledFontPixels (contrato visual-estable).
constexpr int kBaseFontPixels = 16;

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
    //      fuera de la ventana igual llega a esta ventana.
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

    SDL_Window* win = SDL_CreateWindow(
        "Maestro", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 600,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
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
    if (gui_) gui_->initForRenderer(static_cast<void*>(ren));
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
        if (outW <= 0 || outH <= 0) {
            outW = winW > 0 ? winW : 960;
            outH = winH > 0 ? winH : 600;
        }
        if (gui_)
            gui_->ensureFontPixels(
                guiScaledFontPixels(kBaseFontPixels, winH, outH));
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
        updateImeRect();
    };
    // Puntos de evento SDL -> celda 0-based (dominio neutro CellPos): la
    // MISMA métrica que el resize (fuente escalada o fallback escalado) y el
    // MISMO espacio (drawable). Los eventos llegan en puntos de ventana: con
    // ALLOW_HIGHDPI a escala N hay que subir N× ANTES de dividir por la
    // celda (guiScaleCoord por eje); dividir puntos por celda TTF da la
    // celda N× chica. Medidas frescas por evento (barato): la escala puede
    // cambiar al mover la ventana entre monitores sin resize. El Editor ya
    // resuelve chrome/gutter/EOL (screenToCursor) y el autoscroll por
    // bordes; acá solo se convierte, sin offsets propios.
    auto mouseCell = [&](int ptX, int ptY) {
        int winW = 0, winH = 0, outW = 0, outH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_GetRendererOutputSize(ren, &outW, &outH);
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
        return guiCellFromPixels(pxX, pxY, cw, ch);
    };
    editor_.resize(currentSize());
    renderWithBlink();

    // Resto fraccionario de rueda sin emitir (ver SDL_MOUSEWHEEL): un táctil
    // manda fracciones de preciseY que se juntan hasta 1.0. Vive todo el
    // run; no se resetea con el tiempo (0.9 parado + 0.2 tarde igual mueve:
    // inofensivo, un solo paso de 3 líneas).
    double wheelAcc = 0.0;

    while (editor_.isRunning()) {
        // ÚNICO bombeo externo por vuelta (paridad con el pollExternalEvents
        // del TtyRunLoop en readiness): inotify vía pollEvents(), sin fd. El
        // clipboard SDL es síncrono (no-op). Va ANTES de drenar SDL para que
        // una recarga por cambio externo quede visible en los renders de
        // eventos de este mismo frame; la rama sin eventos reutiliza este
        // bombeo (idleStep ya no lo repite: solo tick+render).
        editor_.pollExternalEvents();
        SDL_Event ev;
        bool hadEvent = false;
        while (SDL_PollEvent(&ev)) {
            hadEvent = true;
            switch (ev.type) {
                case SDL_QUIT:
                    // Cierre de ventana = intento de salida (respeta
                    // buffers modificados: requestQuit(false) avisa).
                    if (!editor_.requestQuit(false)) {
                        renderWithBlink();
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
                        renderWithBlink();
                    } else if (guiFocusLossEndsGesture(ev.window.event)) {
                        // Pérdida de foco con gesto en curso: la captura cae
                        // sola y el BUTTONUP puede ir a otra ventana, así que
                        // se sintetiza el release (el Editor lo ignora si no
                        // hay gesto armado; la celda no se usa en release).
                        // Sin esto, el autoscroll quedaría corriendo con el
                        // botón ya suelto hasta que el oráculo lo note.
                        SDL_CaptureMouse(SDL_FALSE);
                        // Fábrica testeada (misma que el press/drag: tipo +
                        // celda en un solo punto de construcción).
                        InputEvent ie = makeMouseReleaseEvent(CellPos{});
                        editor_.handleEvent(ie);
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        renderWithBlink();
                    }
                    break;
                case SDL_KEYDOWN: {
                    // Teclado completo vía GuiKeymap: SDL_Keysym se adapta
                    // a vocabulario propio (GuiKey.h, sin SDL) en el límite
                    // SDL y el keymap traduce a InputEvent semántico. La
                    // política de repetición vive en el keymap (flechas,
                    // Home/End, PgUp/PgDn y borrado repiten; Enter, Escape,
                    // Prefix/Save/Quit/Undo/Redo no).
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
                        renderWithBlink();
                    }
                    break;
                }
                case SDL_TEXTINPUT: {
                    // Texto CONFIRMADO (teclado o commit IME): fuente única
                    // del texto, se acepta SIEMPRE sin filtrar por
                    // modificadores. El evento SDL_TEXTINPUT no trae mods y
                    // los combos de control (Ctrl+S/Ctrl+Q/...) no generan
                    // texto en SDL, así que no hay doble inserción; en
                    // cambio AltGr (p.ej. @/|/\) SÍ llega por aquí y un
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
                        renderWithBlink();
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
                    // Captura explícita (best-effort): el up/motion fuera de
                    // la ventana igual llega acá. SDL ≥2.0.22 ya hace
                    // auto-capture durante el botón, pero la explícita cubre
                    // versiones viejas y deja la intención en el código; es
                    // seguro llamarla aunque ya haya captura.
                    // Construcción por fábrica testeada (makeMousePressEvent:
                    // tipo + celda 0-based en un solo punto, igual que el
                    // drag): el gate de arriba y la fábrica son las mismas
                    // funciones que cubren los tests, no una réplica.
                    if (guiPressTypeForButton(ev.button.button).has_value()) {
                        InputEvent ie = makeMousePressEvent(
                            mouseCell(ev.button.x, ev.button.y));
                        editor_.handleEvent(ie);
                        SDL_CaptureMouse(SDL_TRUE);
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        renderWithBlink();
                    }
                    break;
                }
                case SDL_MOUSEBUTTONUP: {
                    // Release izquierdo -> MouseRelease (cierra el gesto;
                    // sin drag previo es no-op, con drag permanece en
                    // Selección y apaga el autoscroll). Libera la captura
                    // del press (también cae sola al perder foco, ver
                    // SDL_WINDOWEVENT). Fábrica testeada igual que el press.
                    if (guiReleaseTypeForButton(ev.button.button).has_value()) {
                        InputEvent ie = makeMouseReleaseEvent(
                            mouseCell(ev.button.x, ev.button.y));
                        editor_.handleEvent(ie);
                        SDL_CaptureMouse(SDL_FALSE);
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        renderWithBlink();
                    }
                    break;
                }
                case SDL_MOUSEMOTION: {
                    // Arrastre con izquierdo presionado -> MouseDrag por
                    // evento (1 línea / 1 celda + autoscroll en bordes en
                    // el Editor). Sin LMask no se emite nada: el Editor ya
                    // ignora drags sin press previo, pero no se genera
                    // tráfico espurio de hover.
                    if (!guiLeftHeld(ev.motion.state)) break;
                    InputEvent ie = makeMouseDragEvent(
                        mouseCell(ev.motion.x, ev.motion.y));
                    editor_.handleEvent(ie);
                    const auto now = std::chrono::steady_clock::now();
                    editor_.clearExpiredMessages(now);
                    renderWithBlink();
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
                    renderWithBlink();
                    break;
                }
                default:
                    break;
            }
        }
        if (!hadEvent) {
            // Sin eventos: tick por timeout (mensajes/autoscroll) y pausa
            // para no quemar CPU. El delay (tope 30ms << blink de 530ms)
            // acota la pausa de ESTA rama para reinyectar el tiempo y
            // repintar con frecuencia suficiente; con eventos pendientes o
            // una operación bloqueante el repintado puede retrasarse.
            // idleStep ya inyecta el tiempo de blink; acá solo se actualiza
            // el rect IME.
            const auto now = std::chrono::steady_clock::now();
            const int waitMs = idleDelayMs(now);
            if (waitMs > 0) SDL_Delay(static_cast<Uint32>(waitMs));
            idleStep(std::chrono::steady_clock::now());
            updateImeRect();
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
