#pragma once

// ---------------------------------------------------------------------------
// Geometría pura del fallback de ventana sin hit-test nativo.
//
// Cuando SDL_SetWindowHitTest devuelve -1, la ventana borderless no recibe
// arrastre/redimensionado del WM. GuiRunLoop detecta el borde en puntos de
// ventana y aplica el delta del mouse por su cuenta. Estas funciones no
// dependen de SDL para poder testearse sin ventana.
// ---------------------------------------------------------------------------

// Grosor del borde de redimensionado en PUNTOS DE VENTANA.
// Mismo valor que usa guiTitleBarHitTest() y guiWindowEdgeAt().
// Debe coincidir con std::max(4, std::min(8, titleH / 4)) donde titleH >= 28.
// Para titleH=28 -> 7; titleH=32 -> 8; titleH=40 -> 8.
inline constexpr int kResizeBorderPx = 8;

enum class GuiWindowEdge {
    None,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

struct GuiWindowRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Borde bajo (x,y) con el mismo criterio que el callback SDL: las esquinas
// ganan a los lados. `border <= 0` o coordenadas fuera de la ventana dan
// None (el click sigue su camino normal).
inline GuiWindowEdge guiWindowEdgeAt(int x, int y, int width, int height,
                                     int border) {
    if (width <= 0 || height <= 0 || border <= 0 || x < 0 || y < 0 ||
        x >= width || y >= height) {
        return GuiWindowEdge::None;
    }
    const bool left = x < border;
    const bool right = x >= width - border;
    const bool top = y < border;
    const bool bottom = y >= height - border;
    if (top && left) return GuiWindowEdge::TopLeft;
    if (top && right) return GuiWindowEdge::TopRight;
    if (bottom && left) return GuiWindowEdge::BottomLeft;
    if (bottom && right) return GuiWindowEdge::BottomRight;
    if (left) return GuiWindowEdge::Left;
    if (right) return GuiWindowEdge::Right;
    if (top) return GuiWindowEdge::Top;
    if (bottom) return GuiWindowEdge::Bottom;
    return GuiWindowEdge::None;
}

// Rectángulo resultante de arrastrar `edge` un delta (dx,dy) desde `start`.
// El borde opuesto queda fijo; si el tamaño cae por debajo del mínimo, se
// clampea moviendo también el borde que se está arrastrando. Con None
// devuelve `start` sin cambios.
inline GuiWindowRect guiResizeWindowRect(const GuiWindowRect& start,
                                         GuiWindowEdge edge, int dx, int dy,
                                         int minW, int minH) {
    GuiWindowRect next = start;
    if (edge == GuiWindowEdge::None || minW <= 0 || minH <= 0) return next;
    const bool hasLeft = edge == GuiWindowEdge::Left ||
                         edge == GuiWindowEdge::TopLeft ||
                         edge == GuiWindowEdge::BottomLeft;
    const bool hasRight = edge == GuiWindowEdge::Right ||
                          edge == GuiWindowEdge::TopRight ||
                          edge == GuiWindowEdge::BottomRight;
    const bool hasTop = edge == GuiWindowEdge::Top ||
                        edge == GuiWindowEdge::TopLeft ||
                        edge == GuiWindowEdge::TopRight;
    const bool hasBottom = edge == GuiWindowEdge::Bottom ||
                           edge == GuiWindowEdge::BottomLeft ||
                           edge == GuiWindowEdge::BottomRight;
    if (hasLeft) {
        next.x += dx;
        next.w -= dx;
    }
    if (hasRight) next.w += dx;
    if (hasTop) {
        next.y += dy;
        next.h -= dy;
    }
    if (hasBottom) next.h += dy;
    if (next.w < minW) {
        if (hasLeft) next.x = start.x + start.w - minW;
        next.w = minW;
    }
    if (next.h < minH) {
        if (hasTop) next.y = start.y + start.h - minH;
        next.h = minH;
    }
    return next;
}
