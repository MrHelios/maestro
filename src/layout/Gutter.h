#pragma once

#include <algorithm>

// Ancho visible del gutter de numeros de linea (estilo vim).
//
// Unica fuente de verdad para Renderer, Editor y ScreenToCursor.
// `viewportWidth` es obligatorio: el valor devuelto es siempre el ancho
// realmente usado en pantalla/hit-test, nunca la formula intermedia.
//
// Politica: `d(n)+1` columnas (n = digitos del numero mas largo), minimo
// de 3 para no saltar de ancho con archivos chicos (+1 = separador),
// clampleado al ancho disponible: 0 <= retorno <= viewportWidth.
inline int gutterWidth(int totalLines, int viewportWidth) {
    int digits = 1;
    for (int n = totalLines; n >= 10; n /= 10) ++digits;
    const int raw = std::max(3, digits + 1);
    return std::min(raw, viewportWidth);
}
