#pragma once

#include <optional>
#include <string>
#include <vector>
#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Layout.h"
#include "core/Selection.h"
#include "core/Theme.h"
#include "core/Viewport.h"
#include "ui/EditorState.h"
#include "ui/Message.h"
#include "ui/StatusBar.h"

// El estilo de la fila del cursor (kCurrentLineStyle) y los colores de la
// barra (kStatusBar*) viven en core/Theme.h (v1.2): el Renderer usa un
// Theme en vez de colores hardcodeados.

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
    // Tema de colores del contenido (gutter, fila actual, seleccion) y de la
    // barra de estado. Default: defaultTheme() (aspecto identico a v1.1).
    void setTheme(const Theme& t) { theme_ = t; }
    const Theme& theme() const { return theme_; }

    // Construye la secuencia ANSI completa (todo el frame) y la devuelve
    // como string. Es la pieza pura y testeable: no toca la terminal.
    std::string buildScreen(const Document& doc,
                            const Cursor& cursor,
                            const Viewport& viewport,
                            const std::string& filename,
                            bool modified,
                            const Message& message,
                            State state,
                            const std::optional<Selection>& selection = std::nullopt);

    // Construye el frame con buildScreen() y lo escribe a STDOUT.
    void renderScreen(const Document& doc,
                      const Cursor& cursor,
                      const Viewport& viewport,
                      const std::string& filename,
                      bool modified,
                      const Message& message,
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
                                    const Message& message,
                                    int width,
                                    int height);

    void renderFileList(const std::vector<std::string>& names,
                        int selected,
                        int scroll,
                        const std::string& path,
                        const Message& message,
                        int width,
                        int height);

private:
    // Tema de colores del contenido y de la barra.
    Theme theme_ = defaultTheme();

    // ---- Frame completo (v1.0) ----
    // El Renderer controla el frame: calcula el Layout UNA vez y delega en
    // las dos partes. Ninguna pantalla decide por si misma donde termina el
    // contenido; la ultima fila queda reservada para el StatusBar.
    //
    //   renderFrame()
    //     ├── calculateLayout()  -> area de contenido + barra comun
    //     ├── renderContent()    -> la pantalla dibuja su contenido
    //     └── renderStatusBar()  -> la barra comun dibuja su chrome
    //
    // `contentRows` es la altura disponible para el contenido (viewport):
    // el area de la barra ocupa las 2 filas finales (fila fija + mensajes).
    Layout calculateLayout(int contentRows, int width) const;

    // Dibuja solo el CONTENIDO del Editor (filas del documento) dentro de
    // `area`. Emite cada fila terminada en \r\n, dejando el cursor terminal
    // al inicio de la fila del StatusBar.
    void renderEditorContent(std::string& out,
                             const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::optional<Normalized>& sel,
                             const Rect& area,
                             int gutterW) const;

    // CONTENIDO del selector de buffers: la lista (`names`, el seleccionado
    // en video inverso) y las filas vacias con "~". Solo contenido; la barra
    // la dibuja el StatusBar comun.
    void renderBufferListContent(std::string& out,
                                 const std::vector<std::string>& names,
                                 int selected,
                                 const Rect& area) const;

    // CONTENIDO del explorador de archivos: la lista con ventana (scroll) y
    // las filas vacias con "~". Solo contenido; la barra la dibuja el
    // StatusBar comun.
    void renderFileListContent(std::string& out,
                               const std::vector<std::string>& names,
                               int selected,
                               int scroll,
                               const Rect& area) const;

    // Dibuja la barra comun (StatusBar) dentro de `area` a partir de los
    // datos que cada pantalla produce. No conoce editor, buffer ni
    // documento: solo recibe un StatusBarData ya armado.
    void renderStatusBar(std::string& out,
                         const Rect& area,
                         const StatusBarData& data) const;

    // Unico lugar del Renderer que emite la secuencia de posicionamiento
    // "\x1b[{row};{col}H" (fila/columna 1-indexadas). Paso 9 (coordenadas).
    void moveCursorTo(std::string& out, int row, int col) const;

    // Ciclo de vida del frame (v1.4). Antes cada pantalla emitia su propio
    // preludio/epilogo y quedaron INCONSISTENTES: el editor omitia el
    // limpiado "\x1b[J" que el selector y el explorador si emitian. Es
    // responsabilidad del frame global (no de la pantalla que dibuja su
    // contenido), asi que un solo lugar lo garantiza identico en las tres.
    //   beginFrame: ocultar cursor mientras dibujamos, mover a home y limpiar
    //               todo lo que quede de la pantalla anterior.
    //   endFrame  : volver a mostrar el cursor, ya dibujado el frame.
    void beginFrame(std::string& out) const;
    void endFrame(std::string& out) const;
};
