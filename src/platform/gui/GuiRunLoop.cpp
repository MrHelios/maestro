#include "platform/gui/GuiRunLoop.h"

#include <chrono>
#include <cstdio>

#include "app/Editor.h"
#include "platform/WindowSize.h"
#include "rendering/Sink.h"
#include "rendering/gui/GuiRenderer.h"

GuiRunLoop::GuiRunLoop(Editor& editor) : editor_(editor) {}

void GuiRunLoop::idleStep(std::chrono::steady_clock::time_point now) {
    editor_.tick(now);
    editor_.renderFrame();
}

int GuiRunLoop::idleDelayMs(std::chrono::steady_clock::time_point now) const {
    int waitMs = editor_.nextTimeoutMs(now);
    if (waitMs < 0 || waitMs > 30) waitMs = 30;
    return waitMs;
}

#ifndef HAVE_SDL2

int GuiRunLoop::run() {
    std::fprintf(stderr,
                 "maestro: --gui requiere compilar con SDL2 "
                 "(WITH_SDL2=1, falta libsdl2-dev).\n");
    return 1;
}

#else

#include <SDL2/SDL.h>

namespace {
// Celda de respaldo si la fuente no cargó (GuiFont usa la misma).
constexpr int kFallbackCellW = 9;
constexpr int kFallbackCellH = 18;

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

    auto currentSize = [&] {
        int pxW = 960, pxH = 600;
        SDL_GetWindowSize(win, &pxW, &pxH);
        const int cw = gui_ ? gui_->cellW() : kFallbackCellW;
        const int ch = gui_ ? gui_->cellH() : kFallbackCellH;
        return sizeFor(pxW, pxH, cw, ch);
    };
    editor_.resize(currentSize());
    editor_.renderFrame();

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
                        editor_.renderFrame();
                    }
                    break;
                case SDL_WINDOWEVENT:
                    if (ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                        ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        editor_.resize(currentSize());
                        editor_.renderFrame();
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
                        editor_.renderFrame();
                    }
                    break;
                }
                default:
                    break;
            }
        }
        if (!hadEvent) {
            // Sin eventos: tick por timeout (mensajes/autoscroll) y pausa
            // para no quemar CPU. El delay sale del Editor.
            const auto now = std::chrono::steady_clock::now();
            const int waitMs = idleDelayMs(now);
            if (waitMs > 0) SDL_Delay(static_cast<Uint32>(waitMs));
            idleStep(std::chrono::steady_clock::now());
        }
    }

    if (gui_) gui_->setSdlRenderer(nullptr);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

#endif
