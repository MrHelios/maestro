#pragma once

#include <string>
#include "Document.h"
#include "Cursor.h"
#include "Viewport.h"

// El Renderer sabe DIBUJAR, pero nunca modifica el Document, el
// Cursor ni el Viewport (salvo scrollToCursor, que es responsabilidad
// del propio Viewport, llamada desde Editor antes de renderizar).
//
//   Documento -> Renderer -> Terminal
//
// El documento nunca sabe como se dibuja, y el renderer nunca
// modifica el documento.
class Renderer {
public:
    void render(const Document& doc,
                const Cursor& cursor,
                const Viewport& viewport,
                const std::string& filename,
                bool modified,
                const std::string& statusMessage);
};
