#pragma once

#include <algorithm>

// Gutter de numeros de linea (estilo vim): campo numerico centrado +
// separador antes del contenido.
//
// Unica fuente de verdad para Renderer, Editor y ScreenToCursor.
// `viewportWidth` es obligatorio: `total` es siempre el ancho realmente
// usado en pantalla/hit-test, nunca la formula intermedia.
//
// Politica:
//   - campo numerico: max(5, digitos del numero mas largo). El minimo 5
//     reserva espacio para archivos de hasta 10000 lineas;
//   - separador: 2 columnas (el doble del separador historico de 1);
//   - `total` = campo + separador, clampleado a [0, viewportWidth].
//
// En viewports mas angostos que el total deseado se prioriza el campo
// numerico y luego el separador. Ej.: ancho 6 -> campo 5 + gap 1;
// ancho 4 -> campo 4 + gap 0; ancho 0 -> 0 + 0.
inline constexpr int kGutterMinNumberCols = 5;
inline constexpr int kGutterGap = 2;

struct GutterMetrics {
    int numberField = 0; // columnas del campo donde se centra el numero
    int gap = 0;         // espacios entre el campo y el contenido
    int total = 0;       // numberField + gap (siempre <= viewportWidth)
};

inline GutterMetrics gutterMetrics(int totalLines, int viewportWidth) {
    int digits = 1;
    for (int n = totalLines; n >= 10; n /= 10) ++digits;
    const int desiredField = std::max(kGutterMinNumberCols, digits);
    const int avail = std::max(0, viewportWidth);
    GutterMetrics m;
    m.total = std::min(desiredField + kGutterGap, avail);
    m.numberField = std::min(desiredField, m.total);
    m.gap = std::min(kGutterGap, m.total - m.numberField);
    return m;
}

// Ancho visible total del gutter (campo numerico + separador).
inline int gutterWidth(int totalLines, int viewportWidth) {
    return gutterMetrics(totalLines, viewportWidth).total;
}
