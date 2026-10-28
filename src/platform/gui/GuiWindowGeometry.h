#pragma once

// ---------------------------------------------------------------------------
// Geometría pura del fallback de ventana sin hit-test nativo.
//
// Cuando SDL_SetWindowHitTest devuelve -1, la ventana borderless no recibe
// arrastre/redimensionado del WM. GuiRunLoop detecta el borde en puntos de
// ventana y aplica el delta del mouse por su cuenta. Estas funciones no
// dependen de SDL para poder testearse sin ventana.
// ---------------------------------------------------------------------------

// Grosor del borde de redimensionado en PUNTOS DE VENTANA (window points).
// Valor fijo de 8 puntos, usado por:
// - guiTitleBarHitTest() (hit-test nativo SDL)
// - guiWindowEdgeAt() (detección geométrica pura)
// - hoverCursorType() (cursor en hover sin click)
// - fallback manual de redimensionado (cuando hit-test nativo no disponible)
//
// NOTA: Un comentario anterior sugería calcularlo como
// std::max(4, std::min(8, titleH / 4)) según la altura de la barra.
// Esa fórmula dinámica NO se usa; el borde es fijo en 8 puntos para
// simplicidad y consistencia visual en todas las configuraciones.
inline constexpr int kResizeBorderPts = 8;

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

// ============================================================================
// Lógica pura de cursor de redimensionado (sin SDL, testeable sin ventana).
// Coincide exactamente con el hit-test nativo (guiTitleBarHitTest) y el
// handler SDL_MOUSEMOTION de GuiRunLoop.
// ============================================================================

// Tipo de cursor de sistema para redimensionado (enum portátil, sin SDL).
enum class ResizeCursorType {
    Arrow,
    SizeWE,      // Left, Right
    SizeNS,      // Top, Bottom
    SizeNWSE,    // TopLeft, BottomRight
    SizeNESW     // TopRight, BottomLeft
};

// Convierte GuiWindowEdge a tipo de cursor (pura, sin estado).
inline ResizeCursorType cursorTypeForEdge(GuiWindowEdge edge) {
    switch (edge) {
        case GuiWindowEdge::Left:
        case GuiWindowEdge::Right:
            return ResizeCursorType::SizeWE;
        case GuiWindowEdge::Top:
        case GuiWindowEdge::Bottom:
            return ResizeCursorType::SizeNS;
        case GuiWindowEdge::TopLeft:
        case GuiWindowEdge::BottomRight:
            return ResizeCursorType::SizeNWSE;
        case GuiWindowEdge::TopRight:
        case GuiWindowEdge::BottomLeft:
            return ResizeCursorType::SizeNESW;
        case GuiWindowEdge::None:
        default:
            return ResizeCursorType::Arrow;
    }
}

// Calcula el tipo de cursor para hover (sin click) en una posición dada.
// Parámetros en PUNTOS DE VENTANA (igual que eventos SDL y hit-test nativo).
//
// Prioridad (idéntica al hit-test nativo guiTitleBarHitTest):
// 1. Scrollbar track -> Arrow (se maneja ANTES en el caller)
// 2. Botones de la barra de título -> Arrow (se maneja ANTES en el caller)
// 3. Bordes de redimensionado -> ResizeCursorType según edge
// 4. Zona arrastrable (resto de barra de título) -> Arrow
// 5. Contenido -> Arrow
//
// NOTA: Esta función NO maneja scrollbar track ni botones de título.
// El caller debe comprobarlos ANTES y solo llamar a esta función para bordes/contenido.
inline ResizeCursorType hoverCursorType(int ptX, int ptY, int winW, int winH,
                                         int border) {
    return cursorTypeForEdge(guiWindowEdgeAt(ptX, ptY, winW, winH, border));
}
