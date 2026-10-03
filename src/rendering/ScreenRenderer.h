#pragma once

#include <optional>
#include <string>
#include <vector>

#include "app/EditorState.h"
#include "app/Message.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "document/Selection.h"
#include "layout/BracketMatcher.h"
#include "layout/Viewport.h"
#include "rendering/Sink.h"
#include "syntax/SyntaxCache.h"

// Item estructurado de la lista de archivos: `name` es el nombre base sin
// sufijos; `isDirectory` dice si es carpeta. El sufijo visual "/" lo pone
// cada backend al pintar, no app/. Vive acá (zona neutra) para que ni
// app/ ni los futuros backends dependan de rendering/tty/.
struct FileListItem {
    std::string name;
    bool isDirectory = false;
};

// ---------------------------------------------------------------------------
// ScreenRenderer: puerto neutro de presentación.
//
// Es la única cosa de rendering que app/ conoce: una interfaz pura con las
// operaciones que el Editor necesita (pintar pantalla/listas vía un Sink,
// invalidar el cache diferencial, enganchar el SyntaxCache externo y
// conmutar el tema). No nombra ningún backend, no emite bytes, no conoce
// ANSI, terminales ni temas concretos (el tema se maneja como estado
// opaco: solo conmutar + preguntar oscuro/claro para el mensaje).
//
// Implementaciones:
//   - TtyRenderer  (rendering/tty/): backend real de terminal.
//   - NullScreenRenderer (acá al lado): no-op neutro para el default del
//     Editor en tests de lógica y builds sin backend (ver Editor.h).
//
// REGLA: nada de lo que devuelva bytes crudos del backend (buildScreen,
// buildDiffFrame, theme(), primitivas del encoder) pertenece a este puerto:
// eso es API observacional propia de cada backend y la usan sus tests y
// benches directamente, nunca a través del Editor.
// ---------------------------------------------------------------------------
class ScreenRenderer {
public:
    virtual ~ScreenRenderer() = default;

    // Invalida el cache diferencial: el próximo render es frame completo.
    virtual void invalidateCache() = 0;

    // Engancha/desengancha el SyntaxCache externo del buffer activo.
    virtual void setExternalSyntaxCache(SyntaxCache* c) = 0;

    // Tema como estado opaco del backend.
    virtual void toggleTheme() = 0;
    virtual bool isDarkTheme() const = 0;

    // Presentación: frame/listas ya resueltos, entregados vía Sink.
    virtual void renderScreenDiff(const Document& doc,
                                  const Cursor& cursor,
                                  const Viewport& viewport,
                                  const std::string& filename,
                                  bool modified,
                                  const Message& message,
                                  State state,
                                  Sink& sink,
                                  const std::optional<Selection>& selection = std::nullopt,
                                  const std::optional<Selection>& searchHighlight = std::nullopt,
                                  const std::optional<BracketPair>& bracketPair = std::nullopt) = 0;

    virtual void renderBufferList(const std::vector<std::string>& names,
                                  int selected,
                                  int width,
                                  int height,
                                  Sink& sink) = 0;

    // Precondición FileBrowser: 0 <= scroll <= items.size(),
    // 0 <= selected < items.size() (si no vacío) y
    // selected en [scroll, scroll+height). El caller (Editor) debe
    // clampear antes de renderizar.
    virtual void renderFileList(const std::vector<FileListItem>& items,
                                int selected,
                                int scroll,
                                const std::string& path,
                                const Message& message,
                                int width,
                                int height,
                                Sink& sink) = 0;

    // Igual que renderFileList pero para "Guardar como": misma lista y
    // mismo chrome; solo cambian etiqueta ("GUARDAR COMO") y accent.
    // El input del nombre viaja compuesto en `message` (lo arma el caller:
    // linea de input si no hay mensaje activo), en la fila de mensajes
    // debajo del statusbar, como el resto de los prompts modales.
    virtual void renderSaveAsFileList(const std::vector<FileListItem>& items,
                                      int selected,
                                      int scroll,
                                      const std::string& path,
                                      const Message& message,
                                      int width,
                                      int height,
                                      Sink& sink) = 0;
};
