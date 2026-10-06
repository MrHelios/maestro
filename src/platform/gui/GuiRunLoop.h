#pragma once

class Editor;

// GuiRunLoop: dueño del loop SDL2 (Fase GUI-0, stub funcional).
//
// Espejo de TtyRunLoop pero sin terminal: abre una ventana SDL2 y bombea
// SDL_PollEvent hacia la fachada neutra del Editor
// (resize/handleEvent/tick/renderFrame). Sin friend: solo usa API pública.
//
// FASE 0: ventana + fondo + cerrar + resize aproximado. El teclado/mouse
// completos (GuiKeymap) y el dibujado de texto (SDL_ttf) llegan en fases
// siguientes.
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
