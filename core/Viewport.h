#pragma once

#include "core/Cursor.h"

// El Viewport representa que parte del documento es visible en la
// terminal en un momento dado. No conoce el contenido del documento,
// solo numeros: cuantas filas/columnas hay disponibles y desde donde
// se empieza a mostrar.
class Viewport {
public:
    int top = 0;     // primera linea del documento visible en pantalla
    int height = 24;  // filas disponibles para el texto (sin contar status bar)
    int width = 80;   // columnas disponibles

    // Ajusta `top` para que el cursor siempre quede visible.
    // Esta es la unica responsabilidad del viewport: hacer scroll
    // cuando el cursor se sale de la ventana visible.
    void scrollToCursor(const Cursor& cursor) {
        if (cursor.line < top) {
            top = cursor.line;
        } else if (cursor.line >= top + height) {
            top = cursor.line - height + 1;
        }
        if (top < 0) top = 0;
    }
};
