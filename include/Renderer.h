#pragma once

#include <optional>
#include <string>
#include <vector>
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
//
// v0.6.3: el Renderer NO sabe que existen N buffers. Renderiza solo el
// buffer activo (lo recibe por parametro). La unica excepcion es la
// pantalla del selector (Ctrl+K t), para la que recibe una lista de
// NOMBRES (no los Buffer en si) y el indice seleccionado.
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
    void renderScreen(const Document& doc,
                      const Cursor& cursor,
                      const Viewport& viewport,
                      const std::string& filename,
                      bool modified,
                      const std::string& statusMessage,
                      State state,
                      const std::optional<Selection>& selection = std::nullopt);

    // v0.6.3: pantalla del selector de buffers. Mantiene el aspecto visual del
    // editor normal: el area principal muestra la lista de buffers (con el
    // indice seleccionado en video inverso) y las filas vacias el marcador
    // "~". La ultima fila es una barra en video inverso que solo dice
    // MULTIBUFFER, aclarando el modo sin repetir el resto del chrome del
    // editor (sin ruta, sin Linea/Col, sin fila de mensajes).
    std::string buildBufferListScreen(const std::vector<std::string>& names,
                                      int selected,
                                      int width,
                                      int height);

    void renderBufferList(const std::vector<std::string>& names,
                          int selected,
                          int width,
                          int height);
};
