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

// Geometría completa de PINTADO del chrome inferior. A diferencia de
// BottomChromeGeometry (que conserva su contrato para hit-testing), esta
// estructura siempre trae posiciones válidas, en modo anclado o legacy, y
// distingue la banda visible de la MessageBar de la posición efectiva del
// texto/cursor dentro de ella.
struct ChromePaintGeometry {
    bool anchored = false;

    int gapTop = 0;             // inicio del residuo (solo anchored)
    int gapHeight = 0;          // alto del residuo (0 = no rellenar)

    int statusTop = 0;          // fila del StatusBar
    int statusTailTop = 0;      // inicio de la cola del StatusBar (solo legacy, chrome.height == 1)
    int statusTailHeight = 0;   // alto de la cola del StatusBar (0 = no rellenar)
    int statusTailWidth = 0;    // ancho de la cola del StatusBar (0 = no rellenar)

    int messageBandTop = 0;     // inicio de la banda visible de la MessageBar
    int messageBandHeight = 0;  // alto de la banda visible (celda + residuo)
    int messageTextTop = 0;     // top de la celda de texto/cursor centrada
};

// Resuelve la geometría de pintado para ambos modos. `W` y `H` son el ancho
// y alto útiles del drawable (sin barra de título), `ch` el alto de celda
// y `cw` el ancho de celda en píxeles.
// - anchored: StatusBar y MessageBar pegadas al borde inferior; el residuo
//   queda entre contenido y chrome y se rellena con background.
// - legacy: StatusBar en la fila lógica y MessageBar en su banda visible
//   (celda + franja residual), sin relleno de gap.
inline ChromePaintGeometry chromePaintGeometry(const Layout& layout, int W,
                                               int H, int ch, int cw) {
    ChromePaintGeometry p;
    if (ch <= 0 || layout.chrome.height <= 0) return p;

    const BottomChromeGeometry g =
        bottomChromeGeometry(H, ch, layout.content.height, layout.chrome.height);
    p.anchored = g.anchored;
    if (g.anchored) {
        p.gapTop = g.gapTop;
        p.gapHeight = g.gapPx;
        p.statusTop = g.statusTop;
        p.messageBandTop = g.messageTop;
        p.messageBandHeight = (layout.chrome.height >= 2) ? ch : 0;
        // El texto/cursor se centra en la celda exacta: la banda ya mide ch.
        p.messageTextTop = g.messageTop;
        return p;
    }

    // Legacy: la grilla no cubre la ventana (o el residuo no es de la barra).
    p.statusTop = layout.chrome.row * ch;
    p.messageBandTop = (layout.chrome.row + 1) * ch;
    if (layout.chrome.height >= 2) {
        int restH = H - (layout.chrome.row + layout.chrome.height) * ch;
        if (restH <= 0 || restH >= ch) restH = 0;
        p.messageBandHeight = ch + restH;
        // La celda de texto/cursor (alto ch) se centra en la banda visible.
        p.messageTextTop = p.messageBandTop + restH / 2;
    } else {
        // Una sola fila de chrome: la cola del StatusBar cubre el residuo
        // hasta el borde inferior del área útil, empezando en la columna
        // del área de chrome y abarcando hasta el borde derecho del drawable.
        int restH = H - (layout.chrome.row + layout.chrome.height) * ch;
        if (restH > 0 && restH < ch) {
            p.statusTailTop = p.statusTop + ch;
            p.statusTailHeight = restH;
            p.statusTailWidth = W - layout.chrome.col * cw;
        }
    }
    return p;
}

} // namespace guichrome