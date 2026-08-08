#pragma once

#include <optional>
#include <string>
#include "Document.h"
#include "Cursor.h"
#include "Selection.h"
#include "Viewport.h"

// Hacia adelante para el estado de la maquina de estados (definido en
// Editor.h). Solo se usa como etiqueta visual en la barra de estado.
enum class State;

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
    // Construye la secuencia ANSI completa (todo el frame) y la devuelve
    // como string. Es la pieza pura y testeable: no toca la terminal.
    std::string buildScreen(const Document& doc,
                            const Cursor& cursor,
                            const Viewport& viewport,
                            const std::string& filename,
                            bool modified,
                            const std::string& statusMessage,
                            State state,
                            const std::optional<Selection>& selection = std::nullopt);

    // Construye el frame con buildScreen() y lo escribe a STDOUT.
    void render(const Document& doc,
                const Cursor& cursor,
                const Viewport& viewport,
                const std::string& filename,
                bool modified,
                const std::string& statusMessage,
                State state,
                const std::optional<Selection>& selection = std::nullopt);
};
