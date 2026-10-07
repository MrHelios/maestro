#include "rendering/gui/GuiRenderer.h"

#include "rendering/RenderUtil.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

GuiRenderer::GuiRenderer() = default;
GuiRenderer::~GuiRenderer() = default;

void GuiRenderer::setSdlRenderer(void* r) {
    // Las texturas del caché pertenecen al renderer anterior: liberación
    // ansiosa al cambiar. (Defensa en profundidad: GuiFont también vacía
    // el caché solo si textTexture() recibe un renderer distinto al activo.)
    if (sdlRenderer_ != r) font_.clearCache();
    sdlRenderer_ = r;
}

void GuiRenderer::initForRenderer(void* r, int fontPixels) {
    setSdlRenderer(r);
    if (r) font_.load(fontPixels);
}

Frame GuiRenderer::buildFrame(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const ChromeRequest& chrome,
    const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    return frameBuilder_.buildFrame(doc, cursor, viewport, filename, modified,
                                    chrome, selection, searchHighlight,
                                    bracketPair);
}

ChromeData GuiRenderer::bufferListChrome(
    const std::vector<std::string>& names, int selected) const {
    ChromeData data;
    data.statusBar.name = "Buffers";
    data.statusBar.estado = "SELECCIONAR";
    const int total = static_cast<int>(names.size());
    // Display-only defensivo: nunca "-1/N" aunque el caller viole la
    // precondición (0 <= selected < total). En rango válido no cambia nada.
    int pos = selected + 1;
    if (pos < 0) pos = 0;
    if (pos > total) pos = total;
    data.statusBar.right =
        std::to_string(pos) + "/" + std::to_string(total);
    return data;
}

ChromeData GuiRenderer::fileListChrome(
    const std::string& path, const MessageBarData& message,
    const std::vector<FileListItem>& items, int selected, int scroll,
    const char* estado) const {
    ChromeData data;
    data.statusBar.name =
        path.empty() ? "/" : chrome::collapseHome(path);
    data.statusBar.estado = estado;
    const int total = static_cast<int>(items.size());
    // Display-only defensivo: nunca "-1/20" ni "25/20" aunque el caller
    // viole la precondición (selected en [scroll, scroll+height)).
    // En rango válido no cambia nada.
    if (total == 0) {
        data.statusBar.right = "0/0";
    } else {
        int pos = selected - scroll + 1;
        if (pos < 1) pos = 1;
        if (pos > total) pos = total;
        data.statusBar.right =
            std::to_string(pos) + "/" + std::to_string(total);
    }
    data.message = message;
    return data;
}

namespace {

// Dibuja el chrome (StatusBar + MessageBar) y el cursor sobre el
// SDL_Renderer real. Sin SDL es no-op (el caller ya guardo last* para tests).
// El contenido queda en fondo liso (Fase 3: filas/gutter/sintaxis).
#ifdef HAVE_SDL2
void paintChromeAndCursor(SDL_Renderer* r, GuiFont& font, bool dark,
                          const Layout& layout, const ChromeData& chromeData,
                          StyleRole accent, const FrameCursor& cursor) {
    // El accent (color de la etiqueta de estado) se recibe pero aún no se
    // usa: el StatusBar se pinta hoy en un solo color (StatusBase) y el
    // MessageBar en el color de su MessageKind. El pintado por fragmentos
    // (nombre/ruta/[*]/etiqueta con su rol) llega después.
    (void)accent;
    const GuiColor bg = guichrome::background(dark);
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(r);

    const int cw = font.cellW();
    const int ch = font.cellH();
    if (cw <= 0 || ch <= 0) {
        SDL_RenderPresent(r);
        return;
    }

    const int totalWpx = layout.chrome.width * cw;
    // StatusBar: fila superior del chrome.
    if (layout.chrome.height >= 1 && layout.chrome.width > 0) {
        const GuiColor sbg = guichrome::statusBackground(dark);
        SDL_Rect srect{layout.chrome.col * cw, layout.chrome.row * ch, totalWpx,
                       ch};
        SDL_SetRenderDrawColor(r, sbg.r, sbg.g, sbg.b, sbg.a);
        SDL_RenderFillRect(r, &srect);
        if (font.ok()) {
            const std::string line =
                guichrome::statusLine(chromeData.statusBar, layout.chrome.width);
            const GuiColor fg =
                guichrome::colorFor(StyleRole::StatusBase, dark);
            void* tex = font.textTexture(r, line, fg.r, fg.g, fg.b);
            if (tex) {
                SDL_Texture* t = static_cast<SDL_Texture*>(tex);
                int tw = 0, th = 0;
                SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                SDL_Rect dst{layout.chrome.col * cw, layout.chrome.row * ch, tw,
                             th};
                SDL_RenderCopy(r, t, nullptr, &dst);
            }
        }
    }
    // MessageBar: fila inferior del chrome.
    if (layout.chrome.height >= 2 && layout.chrome.width > 0) {
        const GuiColor mbg = guichrome::messageBackground(dark);
        SDL_Rect mrect{layout.chrome.col * cw, (layout.chrome.row + 1) * ch,
                       totalWpx, ch};
        SDL_SetRenderDrawColor(r, mbg.r, mbg.g, mbg.b, mbg.a);
        SDL_RenderFillRect(r, &mrect);
        if (font.ok() && !chromeData.message.text.empty()) {
            const std::string line = guichrome::messageLine(
                chromeData.message, layout.chrome.width);
            const GuiColor fg =
                guichrome::messageColor(chromeData.message.kind, dark);
            void* tex = font.textTexture(r, line, fg.r, fg.g, fg.b);
            if (tex) {
                SDL_Texture* t = static_cast<SDL_Texture*>(tex);
                int tw = 0, th = 0;
                SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                SDL_Rect dst{layout.chrome.col * cw,
                             (layout.chrome.row + 1) * ch, tw, th};
                SDL_RenderCopy(r, t, nullptr, &dst);
            }
        }
    }

    // Cursor: el Frame ya trae la posicion resuelta (contenido o MessageBar
    // en prompts). Bar = barra fina de 2px (ok sobre el texto).
    // NOTA(Fase 3): el Block se pinta DESPUÉS del texto como rect opaco, así
    // que tapa el carácter de la celda. Hoy no es problema (el contenido aún
    // no se pinta), pero cuando llegue el pintado de filas habrá que
    // resolverlo: invertir colores, pintar el carácter con color de cursor u
    // overlay semitransparente. No tocar en este patch.
    if (cursor.visible && cursor.cell.valid()) {
        const GuiColor cc = guichrome::cursorColor(dark);
        SDL_SetRenderDrawColor(r, cc.r, cc.g, cc.b, cc.a);
        const int px = cursor.cell.col * cw;
        const int py = cursor.cell.row * ch;
        SDL_Rect crect;
        if (cursor.shape == FrameCursorShape::Bar) {
            crect = SDL_Rect{px, py, 2, ch};
        } else {
            crect = SDL_Rect{px, py, cw, ch};
        }
        SDL_RenderFillRect(r, &crect);
    }

    SDL_RenderPresent(r);
}
#endif

}  // namespace

void GuiRenderer::renderScreenDiff(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const ChromeRequest& chrome,
    Sink& sink, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    (void)sink;  // la GUI pinta en el SDL_Renderer, no en el Sink de bytes.
    Frame f = buildFrame(doc, cursor, viewport, filename, modified, chrome,
                         selection, searchHighlight, bracketPair);
    lastChrome_ = f.chrome;
    lastAccent_ = f.statusAccent;
    lastCursor_ = f.cursor;
    lastLayout_ = f.layout;
#ifdef HAVE_SDL2
    if (!sdlRenderer_) return;
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
    paintChromeAndCursor(r, font_, dark_, f.layout, f.chrome, f.statusAccent,
                         f.cursor);
#else
    // Sin SDL2: no-op (el loop nunca llega acá, main avisa antes).
#endif
}

void GuiRenderer::renderBufferList(const std::vector<std::string>& names,
                                   int selected, int width, int height,
                                   Sink& sink) {
    (void)sink;
    const Layout layout = frameBuilder_.calculateLayout(height, width);
    lastChrome_ = bufferListChrome(names, selected);
    lastAccent_ = StyleRole::AccentBuffers;
    lastLayout_ = layout;
    // Cursor en la lista (igual que TTY: fila selected, col 0).
    // Defensivo: ante args fuera de contrato no se pinta un cursor
    // arbitrario (se oculta). En rango válido es idéntico a TTY.
    const int total = static_cast<int>(names.size());
    const bool ok = total > 0 && selected >= 0 && selected < total &&
                    selected < layout.content.height;
    if (ok) {
        lastCursor_.visible = true;
        lastCursor_.shape = FrameCursorShape::Block;
        lastCursor_.cell =
            CellPos(layout.content.col, layout.content.row + selected);
    } else {
        lastCursor_.visible = false;
        lastCursor_.cell = CellPos{};
    }
#ifdef HAVE_SDL2
    if (!sdlRenderer_) return;
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
    paintChromeAndCursor(r, font_, dark_, layout, lastChrome_, lastAccent_,
                         lastCursor_);
#endif
}

void GuiRenderer::renderFileList(const std::vector<FileListItem>& items,
                                 int selected, int scroll,
                                 const std::string& path,
                                 const MessageBarData& message, int width,
                                 int height, Sink& sink) {
    (void)sink;
    const Layout layout = frameBuilder_.calculateLayout(height, width);
    lastChrome_ =
        fileListChrome(path, message, items, selected, scroll, "ABRIR ARCHIVO");
    lastAccent_ = StyleRole::AccentAbrir;
    lastLayout_ = layout;
    // Cursor en la lista (abrir), nunca en el MessageBar.
    // Defensivo: ante args fuera de contrato (selected < scroll o
    // selected-scroll >= viewport) no se pinta un cursor arbitrario
    // por encima/fuera del contenido: se oculta. En rango válido
    // es idéntico a TTY.
    const int total = static_cast<int>(items.size());
    const int row = selected - scroll;
    const bool ok = total > 0 && selected >= 0 && selected < total &&
                    scroll >= 0 && scroll <= total && row >= 0 &&
                    row < layout.content.height;
    if (ok) {
        lastCursor_.visible = true;
        lastCursor_.shape = FrameCursorShape::Block;
        lastCursor_.cell =
            CellPos(layout.content.col, layout.content.row + row);
    } else {
        lastCursor_.visible = false;
        lastCursor_.cell = CellPos{};
    }
#ifdef HAVE_SDL2
    if (!sdlRenderer_) return;
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
    paintChromeAndCursor(r, font_, dark_, layout, lastChrome_, lastAccent_,
                         lastCursor_);
#endif
}

void GuiRenderer::renderSaveAsFileList(
    const std::vector<FileListItem>& items, int selected, int scroll,
    const std::string& path, const MessageBarData& message, int width,
    int height, Sink& sink) {
    (void)sink;
    const Layout layout = frameBuilder_.calculateLayout(height, width);
    lastChrome_ =
        fileListChrome(path, message, items, selected, scroll, "GUARDAR COMO");
    lastAccent_ = StyleRole::AccentGuardar;
    lastLayout_ = layout;
    // Cursor DENTRO del input del MessageBar (igual que TTY: al final del
    // nombre, antes del sufijo decorativo via msg.cursor), nunca en la lista.
    const CellPos mbar = chrome::messageBarCursorCell(layout.chrome, message);
    if (mbar.valid()) {
        lastCursor_.visible = true;
        lastCursor_.shape = FrameCursorShape::Bar;
        lastCursor_.cell = mbar;
    } else {
        lastCursor_.visible = false;
        lastCursor_.cell = CellPos{};
    }
#ifdef HAVE_SDL2
    if (!sdlRenderer_) return;
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
    paintChromeAndCursor(r, font_, dark_, layout, lastChrome_, lastAccent_,
                         lastCursor_);
#endif
}
