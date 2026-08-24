#pragma once

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

// Geometria UNICA del frame: dado el tamano de la terminal (`rows` x `cols`)
// devuelve el area de contenido y la de la barra comun. Es la unica fuente
// de la altura del contenido: ninguna pantalla recalcula donde termina el
// contenido, ni el Renderer ni BufferManager. Por eso fitViewport() y
// calculateLayout() delegan aqui.
//
//   80 x 24  -> content 80 x 22 ; statusBar filas 23-24
//   120 x 40 -> content 120 x 38 ; statusBar filas 39-40
//
// En terminales muy chicas (rows <= kStatusBarRows) el contenido nunca baja
// de 1 fila y la barra se recorta para no escribir fuera del rango visible:
// rows=1 -> content 1 + status 0; rows=2 -> content 1 + status 1;
// rows=3 -> content 1 + status 2. Asi siempre content.row+height <=
// statusBar.row y statusBar.row+height <= rows.
inline Layout computeLayout(int rows, int cols) {
    Layout layout;
    const int contentRows = rows > kStatusBarRows ? rows - kStatusBarRows : 1;
    int statusRows = rows - contentRows;
    if (statusRows < 0) statusRows = 0;
    if (statusRows > kStatusBarRows) statusRows = kStatusBarRows;
    layout.content = Rect{0, 0, cols, contentRows};
    layout.statusBar = Rect{contentRows, 0, cols, statusRows};
    return layout;
}