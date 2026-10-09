#include "platform/gui/GuiRunLoop.h"

#include <chrono>
#include <cstdio>

#include "app/Editor.h"
#include "base/utf8.h"
#include "platform/WindowSize.h"
#include "rendering/Sink.h"
#include "rendering/gui/GuiRenderer.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

GuiRunLoop::GuiRunLoop(Editor& editor) : editor_(editor) {}

void GuiRunLoop::idleStep(std::chrono::steady_clock::time_point now) {
    // Inyecta el tiempo de blink antes de pintar: la fase ON/OFF sale de
    // GuiRenderer (Bar parpadea, Block fijo; el ancla se resetea al mover el
    // cursor). Sin gui_ es solo tick+render (tests del Editor).
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
    SDL_Rect sr;
    sr.x = r.x;
    sr.y = r.y;
    sr.w = r.w;
    sr.h = r.h;
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
// Celda de respaldo si la fuente no cargó (GuiFont usa la misma).
constexpr int kFallbackCellW = 9;
constexpr int kFallbackCellH = 18;

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

platform::WindowSize sizeFor(int pxW, int pxH, int cellW, int cellH) {
    if (cellW <= 0) cellW = kFallbackCellW;
    if (cellH <= 0) cellH = kFallbackCellH;
    platform::WindowSize s;
    s.pixelW = pxW;
    s.pixelH = pxH;
    s.cellW = cellW;
    s.cellH = cellH;
    s.cols = pxW / cellW;
    s.rows = pxH / cellH;
    if (s.cols < 1) s.cols = 1;
    if (s.rows < 1) s.rows = 1;
    return s;
}
}  // namespace

int GuiRunLoop::run() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "maestro: SDL_Init falló: %s\n", SDL_GetError());
        return 1;
    }

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
        int pxW = 960, pxH = 600;
        SDL_GetWindowSize(win, &pxW, &pxH);
        const int cw = gui_ ? gui_->cellW() : kFallbackCellW;
        const int ch = gui_ ? gui_->cellH() : kFallbackCellH;
        return sizeFor(pxW, pxH, cw, ch);
    };
    auto renderWithBlink = [&] {
        if (gui_) gui_->setBlinkNow(std::chrono::steady_clock::now());
        editor_.renderFrame();
        updateImeRect();
    };
    editor_.resize(currentSize());
    renderWithBlink();

    while (editor_.isRunning()) {
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
                    if (ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                        ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        editor_.resize(currentSize());
                        renderWithBlink();
                    }
                    break;
                case SDL_KEYDOWN: {
                    // FASE 0: mínimo viable. El mapa completo
                    // (GuiKeymap SDL_Keycode -> InputEvent) llega después.
                    InputEvent ie;
                    bool send = false;
                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        ie.type = InputEventType::Escape;
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
                    // Texto CONFIRMADO (teclado o commit IME): UTF-8 que
                    // puede traer varios caracteres en un solo commit.
                    // Cada celda entra como InsertChar, la operación
                    // semántica que el Editor ya usa (el handling por modo
                    // decide: inserta, completa prompts o interpreta
                    // 'i'/'s'/... en Navegación). KEYDOWN nunca lleva
                    // texto imprimible (solo control como ESC): sin doble
                    // inserción.
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
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

#endif
