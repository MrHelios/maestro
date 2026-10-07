#pragma once

class Editor;

// GuiRunLoop: dueño del loop SDL2 (Fase GUI-0: ventana + fondo + cerrar;
// Fase GUI-1: fuente SDL_ttf con métricas reales para el resize).
//
// Espejo de TtyRunLoop pero sin terminal: abre una ventana SDL2 y bombea
// SDL_PollEvent hacia la fachada neutra del Editor
// (resize/handleEvent/tick/renderFrame). Sin friend: solo usa API pública.
//
// FASE 0: ventana + fondo + cerrar + resize. FASE 1: abre la fuente
// (GuiRenderer::initForRenderer) y el resize usa la celda real en vez de
// estimada, pero solo pinta fondo + una línea de prueba. Pendientes: el
// teclado/mouse completos (GuiKeymap) y el pintado completo del Frame
// (filas, gutter, sintaxis), que llega en Fase 2.
class GuiRenderer;

class GuiRunLoop {
public:
    explicit GuiRunLoop(Editor& editor);
    void setGuiRenderer(GuiRenderer* r) { gui_ = r; }
    // Devuelve código de salida (0 ok, 1 sin SDL2).
    int run();

private:
    Editor& editor_;
    GuiRenderer* gui_ = nullptr;
};
