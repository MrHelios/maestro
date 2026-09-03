#pragma once
#include <algorithm>

// Regiones rectangulares del frame completo que dibuja el Renderer.
//
// Regla de la arquitectura de pantallas (v1.0): las pantallas (Editor,
// BufferSelector, FileBrowser) dibujan SOLO su contenido; el Renderer
// dibuja el layout y la barra comun. Para eso el Renderer calcula el
// Layout UNA sola vez (computeLayout) y cada pantalla dibuja dentro de
// `content`, mientras que la barra comun vive en `statusBar`.
//
//   Renderer
//   ├── Content  -> area donde cada pantalla dibuja su contenido
//   └── Chrome   -> StatusBar (fila fija + fila de mensajes)
struct Rect {
    int row = 0;    // fila inicial (0-indexada)
    int col = 0;    // columna inicial (0-indexada)
    int width = 0;  // columnas
    int height = 0; // filas
};

struct Layout {
    Rect content;    // area de contenido de la pantalla activa
    Rect statusBar;  // barra comun (fila fija + fila de mensajes)
};

// Filas del chrome (barra comun). El editor usa DOS: la fila fija de barra y
// la fila de mensajes/prompt (esa fila es la linea de entrada de SaveAs y de
// los comandos, asi que se conserva; NO se colapsa a una sola fila).
inline constexpr int kStatusBarRows = 2;

inline Layout computeLayout(int rows, int cols) {
    Layout layout;
    
    // 1. El contenido siempre tiene al menos 1 fila.
    // Si la terminal es más grande que la barra, le restamos el espacio de la barra.
    const int contentRows = (rows > kStatusBarRows) ? (rows - kStatusBarRows) : 1;
    
    // 2. La barra de estado ocupa el espacio restante.
    // std::max asegura que no sea negativo.
    // std::min asegura que nunca exceda kStatusBarRows.
    const int statusRows = std::max(0, std::min(kStatusBarRows, rows - contentRows));
    
    layout.content = Rect{0, 0, cols, contentRows};
    layout.statusBar = Rect{contentRows, 0, cols, statusRows};
    
    return layout;
}