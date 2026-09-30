#pragma once

// Tamaño de ventana con métricas para GUI (celdas + píxeles + cell size).
// TTY usa solo rows/cols; GUI rellena pixelW/pixelH y cellW/cellH.
// Vive en su propio header con namespace para no obligar a la GUI a
// incluir el Editor ni colisionar con un `Size` genérico global.
namespace platform {

struct WindowSize {
    int rows = 24;
    int cols = 80;
    int cellW = 0;   // ancho de celda en píxeles (0 = desconocido)
    int cellH = 0;   // alto de celda en píxeles (0 = desconocido)
    int pixelW = 0;  // ancho total en píxeles (0 = desconocido)
    int pixelH = 0;  // alto total en píxeles (0 = desconocido)
};

}  // namespace platform
