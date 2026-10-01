#pragma once

#include <memory>
#include <optional>
#include <string>

#include "app/EditorState.h"
#include "app/Message.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "document/Selection.h"
#include "layout/BracketMatcher.h"
#include "layout/Layout.h"
#include "layout/Viewport.h"
#include "rendering/Sink.h"
#include "rendering/StatusBarData.h"
#include "rendering/frame/FrameBuilder.h"
#include "syntax/SyntaxCache.h"

// Forward declarations del backend TTY (Fase E paso 1): el header común no
// incluye rendering/tty/* de forma directa ni transitiva. Los detalles viven
// solo en Renderer.cpp. `Theme` también se forward-declara por el mismo
// motivo (su definición ANSI vive en rendering/tty/Theme.h).
class TtyDiff;
class TtyEncoder;
struct Theme;

// ---------------------------------------------------------------------------
// Renderer: pantallas del editor (TTY directo movido a rendering/tty/).
//
// La API pública se preserva intacta para no migrar la batería de tests en
// esta fase. Internamente delega en el pipeline nuevo:
//
//   buildScreen*    -> FrameBuilder::buildFrame + TtyEncoder::encodeFrame
//   buildDiffFrame* -> TtyDiff (dueño de rowCache_/statusCache_/scroll)
//
// Las pantallas de listas (buffers/archivos) viven en rendering/tty/
// (TtyLists); las primitivas de terminal para benches también
// (TtyLists::beginFrame, renderEditorContent, ...).
// ---------------------------------------------------------------------------
class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;
    // NOTA de alcance (honesta): acá no hay flag de test-mode ni estado
    // global, y build* es puro (FrameBuilder -> Frame -> string, sin I/O).
    // Pero la clase NO es backend-independent: posee TtyEncoder y TtyDiff
    // como miembros y su setTheme habla el Theme ANSI del backend TTY. Es
    // el shim de transición documentado arriba: conoce StyleRole + Frame,
    // construye, codifica vía el backend TTY que posee y entrega a un Sink
    // inyectado. La independencia total (Renderer puro + TtyRenderer en
    // tty/) queda pendiente y exigiría migrar la batería de tests.
    void setTheme(const Theme& t);
    const Theme& theme() const;
    void invalidateCache();

    std::string buildScreen(const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::string& filename,
                             bool modified,
                             const Message& message,
                             State state,
                             const std::optional<Selection>& selection = std::nullopt,
                             const std::optional<Selection>& searchHighlight = std::nullopt,
                             const std::optional<BracketPair>& bracketPair = std::nullopt);

    void renderScreen(const Document& doc,
                      const Cursor& cursor,
                      const Viewport& viewport,
                      const std::string& filename,
                      bool modified,
                      const Message& message,
                      State state,
                      Sink& sink,
                      const std::optional<Selection>& selection = std::nullopt,
                      const std::optional<Selection>& searchHighlight = std::nullopt,
                      const std::optional<BracketPair>& bracketPair = std::nullopt);

    void renderScreenDiff(const Document& doc,
                          const Cursor& cursor,
                          const Viewport& viewport,
                          const std::string& filename,
                          bool modified,
                          const Message& message,
                          State state,
                          Sink& sink,
                          const std::optional<Selection>& selection = std::nullopt,
                          const std::optional<Selection>& searchHighlight = std::nullopt,
                          const std::optional<BracketPair>& bracketPair = std::nullopt);

    std::string buildDiffFrame(const Document& doc,
                               const Cursor& cursor,
                               const Viewport& viewport,
                               const std::string& filename,
                               bool modified,
                               const Message& message,
                               State state,
                               const std::optional<Selection>& selection = std::nullopt,
                               const std::optional<Selection>& searchHighlight = std::nullopt,
                               const std::optional<BracketPair>& bracketPair = std::nullopt);

    void setExternalSyntaxCache(SyntaxCache* c);
    SyntaxCache* externalSyntaxCache() const {
        return frameBuilder_.externalSyntaxCache();
    }
    SyntaxCache& activeCache() const { return frameBuilder_.activeCache(); }

    // Observabilidad para tests (solo lectura): estado del cache diferencial
    // (vive en TtyDiff). Reemplaza el acceso directo a los viejos campos
    // hasCache_/lastViewportH_ del Renderer monolítico.
    bool hasCache() const;
    int lastViewportH() const;

private:
    // frameBuilder_ es común y va por valor; encoder_/diff_ son el backend
    // TTY y van por puntero para no incluir tty/* en este header (paso 1).
    // Se definen en Renderer.cpp.
    mutable FrameBuilder frameBuilder_;
    std::unique_ptr<TtyEncoder> encoder_;
    std::unique_ptr<TtyDiff> diff_;
};
