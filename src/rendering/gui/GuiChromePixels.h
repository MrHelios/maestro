#pragma once

#include <cstddef>

#include "layout/Layout.h"

// Geometría pura del chrome inferior anclado al borde inferior en píxeles.
// Sin SDL, sin estado mutable: solo cálculo a partir de H, ch y filas reales.

namespace guichrome {

struct BottomChromeGeometry {
    bool anchored = false;      // true solo cuando el layout cubre la ventana
    int contentBottom = 0;      // fin de la última fila de contenido (y útil)
    int gapTop = 0;             // inicio del residuo (== contentBottom)
    int gapPx = 0;              // r = H - (Ncontenido+Nchrome)*ch
    int chromeTop = 0;          // inicio del bloque chrome anclado
    int statusTop = 0;          // fila del StatusBar dentro del chrome
    int messageTop = 0;         // fila del MessageBar dentro del chrome
    int chromeHeight = 0;       // Nchrome * ch (alto total del chrome)
};

// Calcula la geometría del chrome inferior.
// H         : alto útil del drawable (sin barra de título)
// ch        : alto de celda en píxeles físicos (>0)
// contentH  : layout.content.height (filas de contenido reales)
// chromeH   : layout.chrome.height (filas de chrome reales, 0/1/2)
// Retorna geometría con anchored=true solo cuando 0 <= r < ch.
inline BottomChromeGeometry bottomChromeGeometry(int H, int ch,
                                                 int contentH, int chromeH) {
    BottomChromeGeometry g;
    if (ch <= 0 || chromeH <= 0 || H < chromeH * ch) {
        return g; // anchored=false, camino legacy
    }
    const int Nlayout = contentH + chromeH;
    const int r = H - Nlayout * ch;
    if (r < 0 || r >= ch) {
        return g; // layout más chico que la ventana o residuo >= ch
    }
    g.anchored = true;
    g.contentBottom = contentH * ch;
    g.gapTop = g.contentBottom;
    g.gapPx = r;
    g.chromeTop = H - chromeH * ch;
    g.statusTop = g.chromeTop;
    g.messageTop = g.chromeTop + ch; // chromeH == 2
    g.chromeHeight = chromeH * ch;
    return g;
}

} // namespace guichrome