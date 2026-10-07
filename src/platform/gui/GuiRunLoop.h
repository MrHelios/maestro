#pragma once

#include <chrono>

class Editor;

// GuiRunLoop: dueño del loop SDL2 (ventana + fondo + cerrar; fuente SDL_ttf
// con métricas reales para el resize; chrome StatusBar + MessageBar +
// cursor en prompts; contenido del editor y listas modales).
//
// Espejo de TtyRunLoop pero sin terminal: abre una ventana SDL2 y bombea
// SDL_PollEvent hacia la fachada neutra del Editor
// (resize/handleEvent/tick/renderFrame). Sin friend: solo usa API pública.
//
// El teclado/mouse completos (GuiKeymap) siguen pendientes: hoy solo ESC y
// cerrar/resize llegan al Editor.
class GuiRenderer;

class GuiRunLoop {
public:
    explicit GuiRunLoop(Editor& editor);
    void setGuiRenderer(GuiRenderer* r) { gui_ = r; }
    // Devuelve código de salida (0 ok, 1 sin SDL2).
    int run();

    // Camino idle (vuelta sin eventos) extraído para tests: EXACTAMENTE lo
    // que run() hace sin eventos, menos el SDL_Delay (propio del loop real
    // con ventana). tick(now) expira mensajes con timeout y avanza el
    // autoscroll; renderFrame repinta. Sin SDL: solo toca Editor + GuiRenderer.
    void idleStep(std::chrono::steady_clock::time_point now);
    // Delay del idle derivado del Editor (clamp 0..30ms; indefinido → 30).
    // Puro y sin SDL: la misma fórmula que run() pasa a SDL_Delay.
    int idleDelayMs(std::chrono::steady_clock::time_point now) const;

private:
    Editor& editor_;
    GuiRenderer* gui_ = nullptr;
};
