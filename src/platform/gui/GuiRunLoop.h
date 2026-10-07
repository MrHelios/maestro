#pragma once

class Editor;

// GuiRunLoop: dueño del loop SDL2 (Fase GUI-0: ventana + fondo + cerrar;
// Fase GUI-1: fuente SDL_ttf con métricas reales para el resize;
// Fase GUI-2: chrome StatusBar + MessageBar + cursor en prompts).
//
// Espejo de TtyRunLoop pero sin terminal: abre una ventana SDL2 y bombea
// SDL_PollEvent hacia la fachada neutra del Editor
// (resize/handleEvent/tick/renderFrame). Sin friend: solo usa API pública.
//
// El pintado del contenido completo (filas, gutter, sintaxis) y el
// teclado/mouse completos (GuiKeymap) siguen pendientes (Fase 3).
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
