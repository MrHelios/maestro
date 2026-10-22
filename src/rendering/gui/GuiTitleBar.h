#pragma once

// ---------------------------------------------------------------------------
// GuiTitleBar: barra de título propia (client-side decorations) del backend
// SDL2. No depende de SDL en el header: el layout y el hit-test son puros y
// testeables; el pintado recibe el SDL_Renderer* opaco y la fuente.
//
// La barra vive ARRIBA del contenido del editor. GuiRunLoop reserva su alto
// en la grilla y GuiRenderer la pinta sobre el backbuffer antes del Present.
// Los botones (minimizar/maximizar/cerrar) son regiones puras: el loop usa
// buttonAt() para el hit-test y para resolver los clicks.
// ---------------------------------------------------------------------------

#include "rendering/gui/GuiChrome.h"

class GuiFont;

enum class GuiTitleBarButton {
    None,
    Minimize,
    Maximize,
    Close,
};

struct GuiTitleBarRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool contains(int px, int py) const {
        return w > 0 && h > 0 && px >= x && py >= y && px < x + w &&
               py < y + h;
    }
};

class GuiTitleBar {
public:
    // Alto de la barra para una celda TTF: una franja algo mayor que la celda
    // (28px mínimo) para que los botones sean cómodos a escala 1 sin
    // duplicar la altura con HiDPI. Devuelve 0 con métricas inválidas.
    static int heightForCell(int cellH);

    // Rectángulo de un botón en coordenadas de drawable. Los tres botones
    // ocupan el borde derecho, de ancho `height` cada uno.
    static GuiTitleBarRect buttonRect(GuiTitleBarButton button, int width,
                                      int height);

    // Hit-test del botón bajo (x,y). Fuera de la barra devuelve None.
    GuiTitleBarButton buttonAt(int x, int y, int width, int height) const;

    void setMaximized(bool maximized) { maximized_ = maximized; }
    bool maximized() const { return maximized_; }
    // Devuelve true si el hover cambió (para pedir repintado solo entonces).
    bool setHover(GuiTitleBarButton button);
    GuiTitleBarButton hover() const { return hover_; }
    void setPressed(GuiTitleBarButton button) { pressed_ = button; }
    GuiTitleBarButton pressed() const { return pressed_; }

    // Pinta fondo, icono, título centrado y botones. Sin SDL es no-op.
    void paint(void* sdlRenderer, GuiFont& font, int width, int height,
               bool dark) const;

private:
    bool maximized_ = false;
    GuiTitleBarButton hover_ = GuiTitleBarButton::None;
    GuiTitleBarButton pressed_ = GuiTitleBarButton::None;
};
