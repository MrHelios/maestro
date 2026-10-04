#pragma once
#include <algorithm>

// Regiones rectangulares del frame completo que dibuja el Renderer.
//
// Regla de la arquitectura de pantallas (v1.0): las pantallas (Editor,
// BufferSelector, FileBrowser) dibujan SOLO su contenido; el Renderer
// dibuja el layout y el chrome. Para eso el Renderer calcula el
// Layout UNA sola vez (computeLayout) y cada pantalla dibuja dentro de
// `content`, mientras que el chrome vive en `chrome`.
//
//   Renderer
//   ├── Content    -> area donde cada pantalla dibuja su contenido
//   └── Chrome     -> StatusBar (fila fija superior) + MessageBar (fila inferior)
//        ├── StatusBar  -> fila fija: nombre, ruta, estado, % (fila,col)
//        └── MessageBar -> fila de mensajes/prompts/avisos (Message)
struct Rect {
    int row = 0;    // fila inicial (0-indexada)
    int col = 0;    // columna inicial (0-indexada)
    int width = 0;  // columnas
    int height = 0; // filas
};

struct Layout {
    Rect content;  // area de contenido de la pantalla activa
    Rect chrome;   // chrome inferior: StatusBar (fila superior) + MessageBar (fila inferior)
};

// Filas del chrome inferior. El editor usa DOS: la fila fija del StatusBar y
// la fila del MessageBar (mensajes/prompts/avisos, NO se colapsa a una sola).
// Politica degenerada: el MessageBar se recorta primero (height==1 -> solo
// StatusBar; height==0 -> sin chrome). El backend sigue tratando al chrome
// como una unica region (Rect chrome).
inline constexpr int kChromeRows = 2;

inline Layout computeLayout(int rows, int cols) {
    Layout layout;
    
    // 1. El contenido siempre tiene al menos 1 fila.
    // Si la terminal es más grande que el chrome, le restamos su espacio.
    const int contentRows = (rows > kChromeRows) ? (rows - kChromeRows) : 1;
    
    // 2. El chrome ocupa el espacio restante.
    // std::max asegura que no sea negativo.
    // std::min asegura que nunca exceda kChromeRows.
    const int chromeRows = std::max(0, std::min(kChromeRows, rows - contentRows));
    
    layout.content = Rect{0, 0, cols, contentRows};
    layout.chrome = Rect{contentRows, 0, cols, chromeRows};
    
    return layout;
}