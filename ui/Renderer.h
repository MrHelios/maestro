#pragma once

#include <optional>
#include <string>
#include <vector>
#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Selection.h"
#include "core/Viewport.h"
#include "ui/EditorState.h"

// Placeholder de estilo para la fila del cursor (Paso 2). Fondo gris
// (\x1b[100m = bright black, de la paleta basica de 16 colores) como
// reemplazo provisorio del color real de fondo (etapa de colores,
// pendiente): visible en cualquier terminal con color (el 256-color
// 48;5;236 resultaba casi invisible en fondos oscuros), y sin chocar con
// el video inverso de la seleccion. Se expone para que los tests y un grep
// trivial encuentren el hook cuando haya que sustituirlo por el definitivo.
inline constexpr const char* kCurrentLineStyle = "\x1b[100m"; // gris, placeholder

// Estilo de la barra de estado (texto negro sobre fondo gris 60%). El
// gris se mide con 100% = negro y 0% = blanco: 60% => nivel 0.4*255 = 102,
// RGB(102,102,102) en truecolor. Reemplaza al video inverso que se usaba
// para marcar la fila fija.
inline constexpr const char* kStatusBarStyle = "\x1b[30m\x1b[48;2;102;102;102m";

// Fragmentos de la barra de estado. El fondo (kStatusBarStyle) se aplica
// una sola vez al inicio; los fragmentos solo cambian el color/atributos
// del texto manteniendo ese fondo:
//  - kStatusBarName:     nombre (y ruta) del archivo en blanco.
//  - kStatusBarReset:    vuelve a la base (negro sobre gris 60%).
//  - kStatusBarCommand:  etiqueta de estado (comando) en negrita dorada
//                        opaca (dorado de la paleta 256, 38;5;178, + bold).
//  - kStatusBarPath:     ruta del archivo en negro, distinguiendola del
//                        nombre (blanco) y del comando (dorado).
inline constexpr const char* kStatusBarName = "\x1b[37m";                        // blanco
inline constexpr const char* kStatusBarPath = "\x1b[30m";                          // negro
inline constexpr const char* kStatusBarReset =
    "\x1b[0m\x1b[30m\x1b[48;2;102;102;102m";                                      // base
inline constexpr const char* kStatusBarCommand = "\x1b[1m\x1b[38;5;178m";        // bold + dorado

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

    // v0.6.4: pantalla del explorador de archivos. Igual que el selector
    // de buffers pero lleva la ruta actual en la barra de estado y la
    // ayuda de navegacion en la fila de mensajes. `names` son las
    // entradas formateadas (carpetas con "/"), `selected` el indice y
    // `scroll` el offset de ventana visible.
    std::string buildFileListScreen(const std::vector<std::string>& names,
                                    int selected,
                                    int scroll,
                                    const std::string& path,
                                    const std::string& statusMessage,
                                    int width,
                                    int height);

    void renderFileList(const std::vector<std::string>& names,
                        int selected,
                        int scroll,
                        const std::string& path,
                        const std::string& statusMessage,
                        int width,
                        int height);
};
