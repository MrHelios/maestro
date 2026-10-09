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

void GuiRenderer::setBlinkNow(
    std::chrono::steady_clock::time_point now) {
    blinkNow_ = now;
    blinkNowSet_ = true;
}

void GuiRenderer::noteCursorForBlink(const FrameCursor& next) const {
    const bool changed =
        !blinkAnchorSet_ || next.visible != lastCursor_.visible ||
        next.shape != lastCursor_.shape || !(next.cell == lastCursor_.cell);
    if (!changed) return;
    // CONTRATO: el reloj lo inyecta el propietario con setBlinkNow() antes
    // de cada render; el renderer nunca consulta el reloj interno para el
    // parpadeo. Sin tiempo inyectado aún no hay ancla (la fase queda ON);
    // con tiempo inyectado el ancla es ese tiempo. Un reloj obsoleto
    // (inyectar una vez y dejar de hacerlo) congela la fase a propósito:
    // es bug del propietario, no se disimula con el reloj real.
    if (!blinkNowSet_) return;
    blinkAnchor_ = blinkNow_;
    blinkAnchorSet_ = true;
}

bool GuiRenderer::blinkPhaseOn() const {
    // Bloque (navegación y modales): fijo, sin parpadeo.
    if (lastCursor_.shape == FrameCursorShape::Block) return true;
    // Barra (inserción + prompts): parpadea 530ms ON / 530ms OFF.
    if (!blinkAnchorSet_ || !blinkNowSet_) return true;
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(blinkNow_ -
                                                              blinkAnchor_)
            .count();
    if (ms < 0) return true;
    return (ms % kBlinkPeriodMs) < kBlinkOnMs;
}

bool GuiRenderer::cursorShown() const {
    return lastCursor_.visible && blinkPhaseOn();
}

GuiPixelRect GuiRenderer::cursorPixelRect() const {
    GuiPixelRect r;
    // Geometría LÓGICA: solo depende del cursor lógico (visible + celda
    // válida), no de la fase de blink. En OFF sigue siendo válida; el
    // caller decide si pinta con cursorShown().
    if (!lastCursor_.visible || !lastCursor_.cell.valid()) return r;
    const int cw = font_.cellW();
    const int ch = font_.cellH();
    if (cw <= 0 || ch <= 0) return r;
    r.x = lastCursor_.cell.col * cw;
    r.y = lastCursor_.cell.row * ch;
    r.w = (lastCursor_.shape == FrameCursorShape::Bar) ? 2 : cw;
    r.h = ch;
    r.valid = true;
    return r;
}

GuiPixelRect GuiRenderer::imeRectPx() const {
    GuiPixelRect r;
    if (!lastCursor_.visible || !lastCursor_.cell.valid()) return r;
    const int cw = font_.cellW();
    const int ch = font_.cellH();
    if (cw <= 0 || ch <= 0) return r;
    // Ancla IME: celda completa (la ventana candidata del IME se posiciona
    // en la celda del cursor lógico, haya o no fase visible de blink).
    r.x = lastCursor_.cell.col * cw;
    r.y = lastCursor_.cell.row * ch;
    r.w = cw;
    r.h = ch;
    r.valid = true;
    return r;
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

// Snapshot puro del Frame (sin SDL): copia texto+rol por segmento para
// tests sin ventana. Incluye Selection (selección o highlight de búsqueda,
// ya fusionados por FrameBuilder), BracketMatch, sintaxis y CurrentLine.
void snapshotFrameContent(const Frame& f,
                          std::vector<GuiContentRow>& out) {
    out.clear();
    out.reserve(f.contentRows.size());
    for (const auto& row : f.contentRows) {
        GuiContentRow gr;
        gr.isCurrentLine = row.isCurrentLine;
        gr.segs.reserve(row.segs.size());
        for (const auto& s : row.segs) {
            GuiContentSeg gs;
            gs.text.assign(s.text.data(), s.text.size());
            gs.role = s.role;
            gr.segs.push_back(std::move(gs));
        }
        out.push_back(std::move(gr));
    }
}

// La composición de las líneas de los modales vive en
// rendering/ListLines.h (compartida con TTY): acá solo se snapshot/pinta.

#ifdef HAVE_SDL2
// Pinta un texto en (px,py) con color fg; devuelve el ancho en celdas
// consumido (para avanzar la grilla monoespaciada).
int paintText(SDL_Renderer* r, GuiFont& font, const std::string& text,
              const GuiColor& fg, int px, int py) {
    if (text.empty() || !font.ok()) return chrome::colCount(text);
    void* tex = font.textTexture(r, text, fg.r, fg.g, fg.b);
    if (!tex) return chrome::colCount(text);
    SDL_Texture* t = static_cast<SDL_Texture*>(tex);
    int tw = 0, th = 0;
    SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
    SDL_Rect dst{px, py, tw, th};
    SDL_RenderCopy(r, t, nullptr, &dst);
    return chrome::colCount(text);
}

void paintSegmentRow(SDL_Renderer* r, GuiFont& font, bool dark,
                     int basePx, int basePy, int cw, int ch,
                     const char* text, size_t len, StyleRole role,
                     bool isCurrentLine, int& colCells) {
    // PERF(GUI): este es el punto caliente probable del pintado, a medir
    // cuando empiece el trabajo de performance GUI (hoy no se optimiza: la
    // GUI primero tiene que funcionar). Costo POR SEGMENTO y por frame:
    //   - 1 alloc de heap (`view` copia los bytes del segmento),
    //   - 1 lookup en el caché de texturas (clave por texto+color),
    //   - 1 SDL_RenderCopy + 1 fill de fondo.
    // Con syntax highlighting una línea se fragmenta en muchos segmentos
    // (keyword | identifier | punctuation | string | comment | ...) y eso
    // multiplica el trabajo por frame. Interactúa además con el caché
    // acotado de GuiFont (flush total a las 512 entradas, sin LRU): más
    // segmentos por frame = más entradas = más ciclos de vaciado.
    // Al medir, empezar por: segmentos/frame, hit rate y evicciones del
    // caché, y cantidad de RenderCopy. Candidatas (no implementar a ciegas):
    // string_view hasta la clave del caché, coalescer runs contiguos del
    // mismo estilo, o cachear filas ya compuestas.
    const std::string view(text, len);
    const int cols = chrome::colCount(view);
    if (cols <= 0) return;
    const guichrome::GuiStyle st = guichrome::styleFor(role, isCurrentLine, dark);
    SDL_Rect bg{basePx + colCells * cw, basePy, cols * cw, ch};
    SDL_SetRenderDrawColor(r, st.bg.r, st.bg.g, st.bg.b, st.bg.a);
    SDL_RenderFillRect(r, &bg);
    paintText(r, font, view, st.fg, bg.x, bg.y);
    colCells += cols;
}

// Contenido del editor: filas del Frame con gutter + sintaxis + Selection
// (selección y highlight de búsqueda comparten rol) + BracketMatch +
// fondo de línea actual. Sin SDL es no-op (el snapshot ya se guardó).
void paintFrameContent(SDL_Renderer* r, GuiFont& font, bool dark,
                       const Layout& layout, const Frame& f) {
    const int cw = font.cellW();
    const int ch = font.cellH();
    if (cw <= 0 || ch <= 0) return;
    for (size_t row = 0; row < f.contentRows.size(); ++row) {
        const StyledRow& fr = f.contentRows[row];
        const int py = (layout.content.row + static_cast<int>(row)) * ch;
        const int basePx = layout.content.col * cw;
        int colCells = 0;
        for (const auto& seg : fr.segs) {
            paintSegmentRow(r, font, dark, basePx, py, cw, ch,
                            seg.text.data(), seg.text.size(), seg.role,
                            fr.isCurrentLine, colCells);
        }
    }
}

// Contenido de los modales: una línea por fila visible; la seleccionada va
// con fondo ListSelected (== CurrentLine). `selectedRow` es relativo al
// contenido (selected-scroll en FileBrowser, selected en BufferSelector);
// -1 = ninguna destacada (args inválidos o lista vacía). El estilo tenue
// del relleno sale del flag filler (nunca de comparar el texto: un archivo
// real "~" tiene el mismo texto "  ~" pero filler=false).
void paintListContent(SDL_Renderer* r, GuiFont& font, bool dark,
                      const Layout& layout,
                      const std::vector<ListLine>& lines, int selectedRow) {
    const int cw = font.cellW();
    const int ch = font.cellH();
    if (cw <= 0 || ch <= 0) return;
    for (size_t row = 0; row < lines.size(); ++row) {
        const bool sel = (static_cast<int>(row) == selectedRow);
        const StyleRole role =
            sel ? StyleRole::ListSelected : StyleRole::Default;
        const guichrome::GuiStyle st = guichrome::styleFor(role, false, dark);
        const int py = (layout.content.row + static_cast<int>(row)) * ch;
        const int px = layout.content.col * cw;
        const std::string& text = lines[row].text;
        const int cols = chrome::colCount(text);
        SDL_Rect bg{px, py, layout.content.width * cw, ch};
        SDL_SetRenderDrawColor(r, st.bg.r, st.bg.g, st.bg.b, st.bg.a);
        SDL_RenderFillRect(r, &bg);
        if (!lines[row].filler) {
            paintText(r, font, text, st.fg, px, py);
        } else if (cols > 0) {
            // Relleno "~" con tono tenue (marker). El flag filler decide:
            // un archivo real "~" tiene el mismo texto pero filler=false.
            const GuiColor mfg =
                guichrome::colorFor(StyleRole::Marker, dark);
            paintText(r, font, text, mfg, px, py);
        }
    }
}
#endif

// Dibuja el chrome (StatusBar + MessageBar) y el cursor sobre el
// SDL_Renderer real. Solo existe bajo HAVE_SDL2 (sin SDL el caller ya
// guardo last* para tests y no hay nada que pintar).
#ifdef HAVE_SDL2
void paintChromeAndCursor(SDL_Renderer* r, GuiFont& font, bool dark,
                          const Layout& layout, const ChromeData& chromeData,
                          StyleRole accent, const FrameCursor& cursor,
                          bool cursorShown) {
    // El accent (color de la etiqueta de estado) se recibe pero aún no se
    // usa: el StatusBar se pinta hoy en un solo color (StatusBase) y el
    // MessageBar en el color de su MessageKind. El pintado por fragmentos
    // (nombre/ruta/[*]/etiqueta con su rol) llega después.
    (void)accent;
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
    // MessageBar: fila inferior del chrome. El texto expira por timeout en
    // el Editor (tick/clearExpiredMessages): cuando vence, el chrome llega
    // vacío y la fila queda en fondo limpio (mensaje cumplido).
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
    // Bloque = fijo (navegación); Barra = parpadea (inserción): el caller ya
    // resolvió la fase en `cursorShown` (visible lógico AND blink).
    // NOTA: el Block se pinta DESPUÉS del texto como rect opaco, así
    // que tapa el carácter de la celda. Cuando el contenido ya se pinta
    // (este patch), el Block sigue tapando el glifo: mejora futura =
    // invertir colores o overlay semitransparente. Se deja opaco por ahora
    // para paridad con el cursor en bloque del TTY.
    if (cursorShown && cursor.visible && cursor.cell.valid()) {
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
    noteCursorForBlink(f.cursor);
    lastCursor_ = f.cursor;
    lastLayout_ = f.layout;
    snapshotFrameContent(f, lastContentRows_);
    lastListLines_.clear();
    lastListSelected_ = -1;
    lastListScroll_ = 0;
    const bool shown = cursorShown();
#ifdef HAVE_SDL2
    if (!sdlRenderer_) return;
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
    const GuiColor bg = guichrome::background(dark_);
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(r);
    paintFrameContent(r, font_, dark_, f.layout, f);
    paintChromeAndCursor(r, font_, dark_, f.layout, f.chrome, f.statusAccent,
                         f.cursor, shown);
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
    lastContentRows_.clear();
    lastListLines_ = buildBufferListLines(names, layout.content.height);
    lastListScroll_ = 0;
    // Cursor en la lista (igual que TTY: fila selected, col 0).
    // Defensivo: ante args fuera de contrato no se pinta un cursor
    // arbitrario (se oculta). En rango válido es idéntico a TTY.
    // El snapshot publica el mismo contrato: -1 si vacío/inválido.
    const int total = static_cast<int>(names.size());
    const bool ok = total > 0 && selected >= 0 && selected < total &&
                    selected < layout.content.height;
    lastListSelected_ = ok ? selected : -1;
    int selectedRow = ok ? selected : -1;
    FrameCursor next;
    if (ok) {
        next.visible = true;
        next.shape = FrameCursorShape::Block;
        next.cell = CellPos(layout.content.col, layout.content.row + selected);
    } else {
        next.visible = false;
        next.cell = CellPos{};
    }
    noteCursorForBlink(next);
    lastCursor_ = next;
    const bool shownBuf = cursorShown();
#ifdef HAVE_SDL2
    if (!sdlRenderer_) return;
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
    const GuiColor bg = guichrome::background(dark_);
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(r);
    paintListContent(r, font_, dark_, layout, lastListLines_, selectedRow);
    paintChromeAndCursor(r, font_, dark_, layout, lastChrome_, lastAccent_,
                         lastCursor_, shownBuf);
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
    lastContentRows_.clear();
    lastListLines_ = buildFileListLines(items, scroll, layout.content.height);
    lastListScroll_ = scroll;
    // Cursor en la lista (abrir), nunca en el MessageBar.
    // Defensivo: ante args fuera de contrato (selected < scroll o
    // selected-scroll >= viewport) no se pinta un cursor arbitrario
    // por encima/fuera del contenido: se oculta. En rango válido
    // es idéntico a TTY. El snapshot publica el mismo contrato.
    const int total = static_cast<int>(items.size());
    const int row = selected - scroll;
    const bool ok = total > 0 && selected >= 0 && selected < total &&
                    scroll >= 0 && scroll <= total && row >= 0 &&
                    row < layout.content.height;
    lastListSelected_ = ok ? selected : -1;
    const int selectedRow = ok ? row : -1;
    FrameCursor next;
    if (ok) {
        next.visible = true;
        next.shape = FrameCursorShape::Block;
        next.cell = CellPos(layout.content.col, layout.content.row + row);
    } else {
        next.visible = false;
        next.cell = CellPos{};
    }
    noteCursorForBlink(next);
    lastCursor_ = next;
    const bool shownFile = cursorShown();
#ifdef HAVE_SDL2
    if (!sdlRenderer_) return;
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
    const GuiColor bg = guichrome::background(dark_);
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(r);
    paintListContent(r, font_, dark_, layout, lastListLines_, selectedRow);
    paintChromeAndCursor(r, font_, dark_, layout, lastChrome_, lastAccent_,
                         lastCursor_, shownFile);
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
    lastContentRows_.clear();
    // SaveAs pinta la MISMA lista que abrir (solo cambian etiqueta/accent
    // y el cursor al MessageBar): el snapshot comparte constructor.
    // La fila destacada sigue siendo la navegada (selected-scroll), aunque
    // el cursor parpadee en el input: igual que TTY (fondo ListSelected).
    lastListLines_ = buildFileListLines(items, scroll, layout.content.height);
    lastListScroll_ = scroll;
    const int total = static_cast<int>(items.size());
    const int row = selected - scroll;
    const bool okRow = total > 0 && selected >= 0 && selected < total &&
                       scroll >= 0 && scroll <= total && row >= 0 &&
                       row < layout.content.height;
    lastListSelected_ = okRow ? selected : -1;
    const int selectedRow = okRow ? row : -1;
    // Cursor DENTRO del input del MessageBar (igual que TTY: al final del
    // nombre, antes del sufijo decorativo via msg.cursor), nunca en la lista.
    const CellPos mbar = chrome::messageBarCursorCell(layout.chrome, message);
    FrameCursor next;
    if (mbar.valid()) {
        next.visible = true;
        next.shape = FrameCursorShape::Bar;
        next.cell = mbar;
    } else {
        next.visible = false;
        next.cell = CellPos{};
    }
    noteCursorForBlink(next);
    lastCursor_ = next;
    const bool shownSave = cursorShown();
#ifdef HAVE_SDL2
    if (!sdlRenderer_) return;
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
    const GuiColor bg = guichrome::background(dark_);
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(r);
    paintListContent(r, font_, dark_, layout, lastListLines_, selectedRow);
    paintChromeAndCursor(r, font_, dark_, layout, lastChrome_, lastAccent_,
                         lastCursor_, shownSave);
#endif
}
