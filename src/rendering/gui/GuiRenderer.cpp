#include "rendering/gui/GuiRenderer.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "rendering/RenderUtil.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

GuiRenderer::GuiRenderer(const GuiOptions& opts) : options_(opts) {
    if (opts.theme == GuiTheme::Light) dark_ = false;
}
GuiRenderer::~GuiRenderer() { destroyFramebuf(); }

void GuiRenderer::destroyFb(FrameSurface& s) {
#ifdef HAVE_SDL2
    if (s.tex) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(s.tex));
        s.tex = nullptr;
    }
#else
    s.tex = nullptr;
#endif
    s.w = 0;
    s.h = 0;
}

void GuiRenderer::destroyFramebuf() {
    destroyFb(contentFb_[0]);
    destroyFb(contentFb_[1]);
    destroyFb(listFb_);
}

void* GuiRenderer::acquireFb(FrameSurface& s, void* sdlRenderer, int wPx,
                             int hPx, bool& fresh) {
    fresh = false;
#ifdef HAVE_SDL2
    if (!sdlRenderer || wPx <= 0 || hPx <= 0) return nullptr;
    // Hook de tests (ver setFramebufAcquireFailAfterForTests): simula OOM de
    // SDL_CreateTexture sin tocar el estado de la superficie.
    if (framebufFailAfter_ >= 0) {
        if (framebufFailAfter_ == 0) {
            framebufFailAfter_ = -1;
            return nullptr;
        }
        --framebufFailAfter_;
    }
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer);
    if (framebufTarget_ == 0)
        framebufTarget_ = SDL_RenderTargetSupported(r) ? 1 : -1;
    if (framebufTarget_ < 0) return nullptr;
    if (s.tex && (s.w != wPx || s.h != hPx)) destroyFb(s);
    if (!s.tex) {
        SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_TARGET, wPx, hPx);
        if (!t) return nullptr;
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
        s.tex = static_cast<void*>(t);
        s.w = wPx;
        s.h = hPx;
        fresh = true;
    }
    return s.tex;
#else
    (void)sdlRenderer;
    (void)wPx;
    (void)hPx;
    return nullptr;
#endif
}

void GuiRenderer::setSdlRenderer(void* r) {
    // Las texturas del caché pertenecen al renderer anterior: liberación
    // ansiosa al cambiar. (Defensa en profundidad: GuiFont también vacía
    // el caché solo si textTexture() recibe un renderer distinto al activo.)
    // El framebuffer también muere con el renderer (y se re-prueba el
    // soporte de render targets, que puede diferir entre renderers).
    // OBLIGACIÓN DE CICLO DE VIDA (ver GuiFont.h): llamar con nullptr ANTES
    // de destruir el SDL_Renderer. La detección de cambio es por identidad
    // de puntero y un renderer nuevo podría reutilizar la misma dirección.
    if (sdlRenderer_ != r) {
        font_.clearCache();
        destroyFramebuf();
        framebufTarget_ = 0;
        contentFbActive_ = 0;
    }
    sdlRenderer_ = r;
}

bool GuiRenderer::initForRenderer(void* r, int fontPixels) {
    setSdlRenderer(r);
    if (!r) return true;
    if (options_.fontPath.has_value()) {
        if (!font_.loadWithPath(fontPixels, *options_.fontPath)) {
            return false;
        }
    } else {
        font_.load(fontPixels);
    }
    return true;
}

bool GuiRenderer::readLastSurfacePixels(std::vector<GuiColor>& out, int& wPx,
                                        int& hPx) {
    out.clear();
    wPx = 0;
    hPx = 0;
#ifdef HAVE_SDL2
    if (!sdlRenderer_) return false;
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
    const FrameSurface& s =
        lastIsList_ ? listFb_ : contentFb_[contentFbActive_];
    SDL_Texture* fb = static_cast<SDL_Texture*>(s.tex);
    if (!fb || s.w <= 0 || s.h <= 0) return false;
    // Guarda/restaura el destino activo: la lectura no perturba al loop.
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    if (SDL_SetRenderTarget(r, fb) != 0) return false;
    std::vector<Uint32> raw(static_cast<size_t>(s.w) *
                            static_cast<size_t>(s.h));
    const bool ok =
        SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, raw.data(),
                             s.w * static_cast<int>(sizeof(Uint32))) == 0;
    SDL_SetRenderTarget(r, prev);
    if (!ok) return false;
    out.reserve(raw.size());
    for (Uint32 v : raw)
        out.push_back(GuiColor{static_cast<uint8_t>((v >> 16) & 0xFF),
                               static_cast<uint8_t>((v >> 8) & 0xFF),
                               static_cast<uint8_t>(v & 0xFF), 0xFF});
    wPx = s.w;
    hPx = s.h;
    return true;
#else
    return false;
#endif
}

void GuiRenderer::setFramebufAcquireFailAfterForTests(int skips) {
    framebufFailAfter_ = skips;
}

void GuiRenderer::dropContentFramebuffersForTests() {
    destroyFb(contentFb_[0]);
    destroyFb(contentFb_[1]);
    contentFbActive_ = 0;
}

void GuiRenderer::ensureFontPixels(int desiredPixels) {
    if (desiredPixels <= 0) return;
    // Sin cambio de talla y con fuente válida: no se toca nada (el reload
    // vaciaría el caché de texturas en cada resize sin necesidad).
    if (font_.ok() && font_.pixelSize() == desiredPixels) return;
    // Respeta --font: la talla escalada por HiDPI se reabre desde la ruta
    // explícita, no desde los candidatos del sistema (si no, el primer
    // resize perdería la fuente custom).
    if (options_.fontPath.has_value()) {
        font_.loadWithPath(desiredPixels, *options_.fontPath);
    } else {
        font_.load(desiredPixels);
    }
}

void GuiRenderer::toggleTheme() {
    dark_ = !dark_;
    // Solo ahorro de memoria (el color va en la clave del caché): sin esto
    // el toggle seguiría siendo correcto, solo conservaría texturas viejas
    // hasta el flush de 512. Se invalidan ADEMÁS los cachés diferenciales:
    // todos los fondos RGB cambian y ninguna fila cacheada sigue válida.
    // El próximo render* es completo. No se tocan los snapshots last*: se
    // regeneran en el próximo render* y ningún camino los usa para saltear
    // pintado (probado en gui_toggle_pantalla_quieta_repinta_todo con la
    // pantalla quieta, en editor y en lista modal).
    font_.clearCache();
    contentHasCache_ = false;
    listHasCache_ = false;
    resetIncrementalState();
}

void GuiRenderer::invalidateCache() {
    contentHasCache_ = false;
    listHasCache_ = false;
    resetIncrementalState();
}

void GuiRenderer::resetIncrementalState() const {
    contentTop_ = -1;
    contentLeft_ = -1;
    contentVpWidth_ = -1;
    contentVpHeight_ = -1;
    contentLineCount_ = -1;
    contentFilename_.clear();
    contentModified_ = false;
    contentDocVersion_ = 0;
    contentDocInstance_ = 0;
    contentCursorLine_ = -1;
    contentSelection_.reset();
    contentSearch_.reset();
    contentBracket_.reset();
    contentCursorInMsg_ = false;
}

bool GuiRenderer::sameLayout(const Layout& a, const Layout& b) {
    return a.content.row == b.content.row && a.content.col == b.content.col &&
           a.content.width == b.content.width &&
           a.content.height == b.content.height &&
           a.chrome.row == b.chrome.row && a.chrome.col == b.chrome.col &&
           a.chrome.width == b.chrome.width &&
           a.chrome.height == b.chrome.height;
}

void GuiRenderer::noteStats(bool full, int repainted, int skipped,
                            bool chromeDirty) const {
    lastWasFull_ = full;
    lastRepainted_ = repainted;
    lastSkipped_ = skipped;
    lastChromeDirty_ = chromeDirty;
    ++totalFrames_;
    if (full) ++totalFull_;
    totalRepainted_ += repainted;
    totalSkipped_ += skipped;
}

void GuiRenderer::markCursorRow(std::vector<char>& dirty, const Layout& layout,
                                const FrameCursor& c) {
    if (!c.visible || !c.cell.valid()) return;
    const int rel = c.cell.row - layout.content.row;
    if (rel < 0 || rel >= static_cast<int>(dirty.size())) return;
    // Solo filas de contenido: un cursor en el chrome (prompts con input)
    // no invalida contenido (el chrome se decide por chromeDirty).
    const int relCol = c.cell.col - layout.content.col;
    if (relCol < 0 || relCol >= layout.content.width) return;
    dirty[static_cast<size_t>(rel)] = 1;
}

void GuiRenderer::setBlinkNow(
    std::chrono::steady_clock::time_point now) {
    blinkNow_ = now;
    blinkNowSet_ = true;
}

void GuiRenderer::markCursorActivity() { cursorActivity_ = true; }

void GuiRenderer::noteCursorForBlink(const FrameCursor& next) const {
    const bool changed =
        cursorActivity_ || !blinkAnchorSet_ ||
        next.visible != lastCursor_.visible ||
        next.shape != lastCursor_.shape || !(next.cell == lastCursor_.cell);
    cursorActivity_ = false;
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

std::pair<bool, int> GuiRenderer::blinkPhaseForElapsedMs(long elapsedMs) {
    const long phase = elapsedMs % kBlinkPeriodMs;
    const bool on = phase < kBlinkOnMs;
    int rest = on ? static_cast<int>(kBlinkOnMs - phase)
                  : static_cast<int>(kBlinkPeriodMs - phase);
    if (rest <= 0) rest = 1;
    return {on, rest};
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
    return blinkPhaseForElapsedMs(ms).first;
}

std::optional<int> GuiRenderer::msUntilBlinkToggle(
    std::chrono::steady_clock::time_point now) const {
    // Solo la barra visible se anima: el bloque es fijo y un cursor
    // apagado nunca se pinta (cursorShown() es siempre false), así que en
    // ambos casos no hay cambio de fase que esperar.
    if (lastCursor_.shape != FrameCursorShape::Bar) return std::nullopt;
    if (!lastCursor_.visible) return std::nullopt;
    if (!blinkAnchorSet_ || !blinkNowSet_) return std::nullopt;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                    blinkAnchor_)
                  .count();
    // Consulta anterior al ancla (tiempos fijos en tests): fase ON completa.
    if (ms < 0) ms = 0;
    return blinkPhaseForElapsedMs(ms).second;
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
    const std::optional<BracketPair>& bracketPair,
    const softwrap::Index* wrapIndex) {
    return frameBuilder_.buildFrame(doc, cursor, viewport, filename, modified,
                                    chrome, selection, searchHighlight,
                                    bracketPair, wrapIndex);
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

// ¿Vive este cursor como barra visible dentro del chrome? Solo esas barras
// ensucian el chrome al dibujarse/borrarse (las de contenido ensucian su
// fila vía markCursorRow; los bloques fijos se deciden por cambio de celda).
// P2: el caller la evalúa con el cursor ANTERIOR y el nuevo.
bool isChromeBar(const FrameCursor& c, const Layout& layout) {
    if (!c.visible || c.shape != FrameCursorShape::Bar || !c.cell.valid())
        return false;
    const int r0 = layout.chrome.row, r1 = r0 + layout.chrome.height;
    return c.cell.row >= r0 && c.cell.row < r1;
}

// La composición de las líneas de los modales vive en
// rendering/ListLines.h (compartida con TTY): acá solo se snapshot/pinta.

// Glifo bajo el cursor Block para inversión (paridad visual con el TTY,
// que mantiene el carácter legible sobre el bloque). Puro, sin SDL: se
// resuelve desde el Frame / líneas del modal + Layout, sin leer píxeles.
//   hasGlyph=false -> celda vacía/espacio o fuera de contenido: solo fill.
//   hasGlyph=true  -> `text` es la celda UTF-8 (1 col; 2 si ancha líder) a
//                    repintar sobre el fondo del cursor, con fg según la
//                    política de guichrome::cursorGlyphFg (inversión exacta
//                    con el fondo efectivo o fallback de contraste).
// `cellBg` es el fondo EFECTIVO de la celda (styleFor con rol + línea
// actual en contenido; estilo de lista en modales). Sin fuente el caller
// hace solo fill (modo degradado): el resolver igual devuelve glifo + bg.
struct BlockGlyph {
    std::string text;
    int widthCols = 1;
    bool hasGlyph = false;
    GuiColor cellBg{};
    bool hasCellBg = false;
};

// Celda visual encontrada por el recorrido compartido de abajo: `text`
// con sus bytes UTF-8 y `widthCols` (2 si líder ancha). `empty=true`
// significa "sin glifo que repintar, solo fill": espacio (el fill ya es el
// fondo), segunda mitad de un carácter ancho (se deja solo fill para no
// desalinear la textura de 2 celdas sobre 1; esa posición es inalcanzable
// por el cursor: Cursor::visualColumn alinea a inicio de celda, ver test
// cursor_visual_siempre_en_inicio_de_celda) o expansión de tab (defensiva,
// ver contrato de scanSegmentCells).
struct CellHit {
    std::string text;
    int widthCols = 1;
    bool empty = true;
};

// ÚNICA implementación del recorrido de celdas visuales (la usan el
// contenido por segmentos y las listas de a una línea: no duplicar).
// Escanea UN segmento visual `sv` con `absCol` (in/out: columna visual del
// inicio/fin del segmento, para encadenar piezas sin doble contabilidad de
// anchos ni chrome::colCount en la localización). Si `relCol` cae en este
// segmento describe la celda en `hit` y devuelve true (haya o no glifo);
// si no, avanza `absCol` hasta el final del segmento y devuelve false.
// Contrato de entrada: texto VISUAL (tabs ya expandidos, sin ANSI) con
// celdas nunca partidas entre piezas. En contenido lo garantiza
// FrameBuilder::buildContentRow (expandTabsInto antes de segmentar, cortes
// vía utf8::range); en modales, la composición de ListLines.h ("  nombre",
// "/", "  ~") nunca emite tabs. La rama de '\t' es solo defensiva ante
// una violación de contrato (un tab crudo): su expansión se trata como
// vacía (solo fill). Mide con utf8::cellWidth/cellLen, las mismas
// primitivas de utf8::columnOf/range del FrameBuilder y el pintor.
// Sin caché adicional a propósito: el cursor resuelve una sola celda por
// frame y el trabajo está acotado al ancho de la fila.
bool scanSegmentCells(std::string_view sv, int& absCol, int relCol,
                      CellHit& hit) {
    const int n = static_cast<int>(sv.size());
    int i = 0;
    while (i < n) {
        if (sv[i] == '\t') {
            const int w = utf8::TAB_WIDTH - (absCol % utf8::TAB_WIDTH);
            if (relCol >= absCol && relCol < absCol + w) {
                hit = CellHit{};
                return true;
            }
            absCol += w;
            ++i;
            continue;
        }
        int len = utf8::cellLen(sv, i, n);
        if (len <= 0) len = 1;
        if (i + len > n) len = n - i;
        int w = utf8::cellWidth(sv, i, n);
        if (w <= 0) w = 1;
        if (relCol >= absCol && relCol < absCol + w) {
            if (w > 1 && relCol != absCol) {
                hit = CellHit{};
                return true;
            }
            hit.text.assign(sv.data() + i, static_cast<size_t>(len));
            if (hit.text.empty() || hit.text == " ") {
                hit = CellHit{};
                return true;
            }
            hit.widthCols = (w == 2) ? 2 : 1;
            hit.empty = false;
            return true;
        }
        absCol += w;
        i += len;
    }
    return false;
}

// Extrae la celda visual en `targetCol` sobre UNA línea (vía de modales).
// Misma convención histórica: devuelve false si no hay glifo que repintar
// (más allá del texto, espacio, 2ª mitad de ancho doble o tab).
bool cellGlyphAt(const std::string& line, int targetCol, std::string& out,
                 int& outW) {
    if (targetCol < 0) return false;
    int absCol = 0;
    CellHit hit;
    if (!scanSegmentCells(line, absCol, targetCol, hit)) return false;
    if (hit.empty) return false;
    out = hit.text;
    outW = hit.widthCols;
    return true;
}

// Resuelve el glifo bajo un cursor Block en contenido del editor
// (filas del Frame: gutter + texto ya expandido + fillers CurrentLine).
// Fuera del área de contenido o más allá de las celdas -> sin glifo.
// El fondo efectivo sale de styleFor (rol + línea actual): es lo que
// alimenta la inversión exacta de cursorGlyphFg.
//
// Recorrido de celdas sobre los segmentos en orden, con la misma
// contabilidad visual que el pintor (paintFrameContent emite los segmentos
// en orden y cada celda ocupa absCol creciente), a través del núcleo
// compartido scanSegmentCells (el mismo que la vía de modales: una sola
// implementación, sin chrome::colCount en la localización).
//
// Marco de coordenadas (sin doble conteo del gutter): relCol resta el
// ORIGEN del contenido (layout.content.col) y los segmentos se recorren
// desde ese mismo origen, con el gutter como primer segmento — igual que
// el pintor (basePx = content.col). La celda ya trae el gutter incluido
// (editorCursorPos suma gutterW a la columna visual), así que restar el
// gutter además del origen desplazaría el glifo una celda. Ver test
// gui_sdl_cursor_block_gutter_no_desplaza_glifo.
//
// Contratos de FrameBuilder::buildContentRow en los que se apoya (ver
// scanSegmentCells): texto con tabs expandidos y segmentos que nunca
// parten celdas.
BlockGlyph blockGlyphInContent(const Frame& f, const FrameCursor& cursor,
                               const Layout& layout, bool dark) {
    BlockGlyph g;
    if (!cursor.visible || !cursor.cell.valid()) return g;
    const int relRow = cursor.cell.row - layout.content.row;
    const int relCol = cursor.cell.col - layout.content.col;
    if (relRow < 0 || relCol < 0) return g;
    if (relRow >= static_cast<int>(f.contentRows.size())) return g;
    if (relCol >= layout.content.width) return g;
    const StyledRow& row = f.contentRows[static_cast<size_t>(relRow)];
    int absCol = 0;
    for (const auto& seg : row.segs) {
        const GuiColor segBg =
            guichrome::styleFor(seg.role, row.isCurrentLine, dark).bg;
        const std::string_view sv(seg.text.data(), seg.text.size());
        CellHit hit;
        if (!scanSegmentCells(sv, absCol, relCol, hit)) continue;
        g.cellBg = segBg;
        g.hasCellBg = true;
        if (hit.empty) return g;  // espacio, 2ª mitad o tab: solo fill
        g.text = std::move(hit.text);
        g.widthCols = hit.widthCols;
        g.hasGlyph = true;
        return g;
    }
    return g;
}

// Resuelve el glifo bajo un cursor Block en listas modales
// (BufferSelector/FileBrowser: líneas "  nombre" + "~" de relleno).
// El cursor de lista siempre cae en col 0 (espacio) en la práctica, pero
// se resuelve genérico por si la geometría se mueve. El fondo efectivo es
// el de la fila (ListSelected si es la destacada, base si no); el relleno
// "~" no cambia el fondo, solo el fg tenue del texto.
// `selectedRow` es relativo al contenido (igual que paintListContent).
BlockGlyph blockGlyphInList(const std::vector<ListLine>& lines,
                            const FrameCursor& cursor, const Layout& layout,
                            bool dark, int selectedRow) {
    BlockGlyph g;
    if (!cursor.visible || !cursor.cell.valid()) return g;
    const int relRow = cursor.cell.row - layout.content.row;
    const int relCol = cursor.cell.col - layout.content.col;
    if (relRow < 0 || relCol < 0) return g;
    if (relRow >= static_cast<int>(lines.size())) return g;
    if (relCol >= layout.content.width) return g;
    const StyleRole role = (relRow == selectedRow) ? StyleRole::ListSelected
                                                   : StyleRole::Default;
    g.cellBg = guichrome::styleFor(role, false, dark).bg;
    g.hasCellBg = true;
    const std::string& text = lines[static_cast<size_t>(relRow)].text;
    std::string glyph;
    int w = 1;
    if (cellGlyphAt(text, relCol, glyph, w)) {
        g.text = std::move(glyph);
        g.widthCols = (w == 2) ? 2 : 1;
        g.hasGlyph = true;
    }
    return g;
}

// Resuelve el glifo bajo un cursor Block sobre el SNAPSHOT de contenido
// (GuiContentRow: texto propio + rol + línea actual), sin Frame.
// Espejo exacto de blockGlyphInContent pero sobre el DTO cacheado: mismo
// recorrido (scanSegmentCells compartido), mismo fondo efectivo
// (styleFor por segmento) y mismo marco de coordenadas (origen del
// contenido, gutter incluido como primer segmento, igual que el pintor).
// La usa el camino rápido de cursor-move, que no construye el Frame y por
// tanto no puede resolver vía blockGlyphInContent; la paridad visual con el
// camino completo se congela en tests de píxeles (SDL dummy) y de DTO.
BlockGlyph blockGlyphInSnapshot(const std::vector<GuiContentRow>& rows,
                                const Layout& layout,
                                const FrameCursor& cursor, bool dark) {
    BlockGlyph g;
    if (!cursor.visible || !cursor.cell.valid()) return g;
    const int relRow = cursor.cell.row - layout.content.row;
    const int relCol = cursor.cell.col - layout.content.col;
    if (relRow < 0 || relCol < 0) return g;
    if (relRow >= static_cast<int>(rows.size())) return g;
    if (relCol >= layout.content.width) return g;
    const GuiContentRow& row = rows[static_cast<size_t>(relRow)];
    int absCol = 0;
    for (const auto& seg : row.segs) {
        const GuiColor segBg =
            guichrome::styleFor(seg.role, row.isCurrentLine, dark).bg;
        const std::string_view sv(seg.text.data(), seg.text.size());
        CellHit hit;
        if (!scanSegmentCells(sv, absCol, relCol, hit)) continue;
        g.cellBg = segBg;
        g.hasCellBg = true;
        if (hit.empty) return g;  // espacio, 2ª mitad o tab: solo fill
        g.text = std::move(hit.text);
        g.widthCols = hit.widthCols;
        g.hasGlyph = true;
        return g;
    }
    return g;
}

// Igualdad de selecciones opcionales (el highlight es función de sus
// extremos; dos renders con la misma selección pintan lo mismo por fila).
// Selection no define operator==: se compara campo a campo.
bool sameOptSelection(const std::optional<Selection>& a,
                      const std::optional<Selection>& b) {
    if (!a.has_value() || !b.has_value())
        return a.has_value() == b.has_value();
    return a->anchor == b->anchor && a->position == b->position;
}

#ifdef HAVE_SDL2
// Pinta un texto en (px,py) con color fg; devuelve el ancho en celdas
// consumido (para avanzar la grilla monoespaciada).
// string_view: GuiFont copia a NUL-terminado antes de SDL_ttf (ver GuiFont.h).
int paintText(SDL_Renderer* r, GuiFont& font, std::string_view text,
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
    //   - 1 alloc de heap (la vista se copia a la clave NUL-terminada del
    //     caché dentro de GuiFont::textTexture),
    //   - 1 lookup en el caché de texturas (clave por texto+color),
    //   - 1 SDL_RenderCopy + 1 fill de fondo.
    // Con syntax highlighting una línea se fragmenta en muchos segmentos
    // (keyword | identifier | punctuation | string | comment | ...) y eso
    // multiplica el trabajo por frame. Interactúa además con el caché
    // acotado de GuiFont (LRU de una evicción por inserción sobre el tope):
    // más segmentos por frame = más entradas = más evicciones.
    // Al medir, empezar por: segmentos/frame, hit rate y evicciones del
    // caché, y cantidad de RenderCopy. Candidatas (no implementar a ciegas):
    // coalescer runs contiguos del mismo estilo, o cachear filas ya
    // compuestas. NOTA: no pasar (text,len) directo a SDL_ttf: el segmento
    // puede no terminar en NUL; GuiFont::textTexture ya hace la copia segura.
    const std::string_view view(text, len);
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
// Repintado parcial (caché por línea): pinta UNA fila de contenido desde el
// snapshot (texto+rol+línea actual, la misma fuente que el full). El fondo
// de la fila se limpia primero con el fondo efectivo (styleFor por
// segmento, igual que paintSegmentRow) así que no hace falta RenderClear:
// las filas limpias del backbuffer se conservan intactas entre Presents.
void paintSnapshotRow(SDL_Renderer* r, GuiFont& font, bool dark,
                      const Layout& layout, int relRow,
                      const GuiContentRow& row) {
    const int cw = font.cellW();
    const int ch = font.cellH();
    if (cw <= 0 || ch <= 0) return;
    const int py = (layout.content.row + relRow) * ch;
    const int basePx = layout.content.col * cw;
    // Fondo de la fila ANTES de los segmentos: sin el RenderClear del full,
    // una fila que se acorta (texto largo -> corto) dejaría píxeles
    // obsoletos a la derecha. Las filas CurrentLine ya traen su pad como
    // segmento; el resto muestra el fondo base (igual que el clear).
    {
        const GuiColor bg = guichrome::background(dark);
        SDL_Rect fill{basePx, py, layout.content.width * cw, ch};
        SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
        SDL_RenderFillRect(r, &fill);
    }
    int colCells = 0;
    for (const auto& seg : row.segs) {
        paintSegmentRow(r, font, dark, basePx, py, cw, ch, seg.text.data(),
                        seg.text.size(), seg.role, row.isCurrentLine,
                        colCells);
    }
}

// Una fila de modal (fondo de fila completa + texto o "~" tenue).
// Misma composición que paintListContent pero para una sola fila.
void paintSnapshotListRow(SDL_Renderer* r, GuiFont& font, bool dark,
                          const Layout& layout, int relRow, const ListLine& line,
                          bool selected) {
    const int cw = font.cellW();
    const int ch = font.cellH();
    if (cw <= 0 || ch <= 0) return;
    const StyleRole role =
        selected ? StyleRole::ListSelected : StyleRole::Default;
    const guichrome::GuiStyle st = guichrome::styleFor(role, false, dark);
    const int py = (layout.content.row + relRow) * ch;
    const int px = layout.content.col * cw;
    const std::string& text = line.text;
    const int cols = chrome::colCount(text);
    SDL_Rect bg{px, py, layout.content.width * cw, ch};
    SDL_SetRenderDrawColor(r, st.bg.r, st.bg.g, st.bg.b, st.bg.a);
    SDL_RenderFillRect(r, &bg);
    if (!line.filler) {
        paintText(r, font, text, st.fg, px, py);
    } else if (cols > 0) {
        const GuiColor mfg = guichrome::colorFor(StyleRole::Marker, dark);
        paintText(r, font, text, mfg, px, py);
    }
}

// Solo el chrome (StatusBar + MessageBar), sin cursor ni Present. Extraído
// del cuerpo de paintChromeAndCursor para el camino parcial: si el chrome
// está limpio se salta entero; si cambió se repintan sus 2 filas (barato).
void paintChromeOnly(SDL_Renderer* r, GuiFont& font, bool dark,
                     const Layout& layout, const ChromeData& chromeData,
                     StyleRole accent) {
    const int cw = font.cellW();
    const int ch = font.cellH();
    if (cw <= 0 || ch <= 0) return;
    const int totalWpx = layout.chrome.width * cw;
    if (layout.chrome.height >= 1 && layout.chrome.width > 0) {
        const GuiColor sbg = guichrome::statusBackground(dark);
        SDL_Rect srect{layout.chrome.col * cw, layout.chrome.row * ch, totalWpx,
                       ch};
        SDL_SetRenderDrawColor(r, sbg.r, sbg.g, sbg.b, sbg.a);
        SDL_RenderFillRect(r, &srect);
        if (font.ok()) {
            const std::vector<guichrome::GuiChromeSeg> segs =
                guichrome::statusSegments(chromeData.statusBar, accent,
                                          layout.chrome.width);
            int consumed = 0;
            const int baseX = layout.chrome.col * cw;
            const int y = layout.chrome.row * ch;
            for (const auto& sg : segs) {
                if (sg.text.empty()) continue;
                const int cols = chrome::colCount(sg.text);
                const bool onlySpaces =
                    sg.text.find_first_not_of(' ') == std::string::npos;
                if (!onlySpaces) {
                    const GuiColor fg =
                        guichrome::colorFor(sg.role, dark);
                    void* tex =
                        font.textTexture(r, sg.text, fg.r, fg.g, fg.b);
                    if (tex) {
                        SDL_Texture* t = static_cast<SDL_Texture*>(tex);
                        int tw = 0, th = 0;
                        SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                        SDL_Rect dst{baseX + consumed * cw, y, tw, th};
                        SDL_RenderCopy(r, t, nullptr, &dst);
                    }
                }
                consumed += cols;
            }
        }
    }
    if (layout.chrome.height >= 2 && layout.chrome.width > 0) {
        const GuiColor mbg = guichrome::messageBackground(dark);
        SDL_Rect mrect{layout.chrome.col * cw, (layout.chrome.row + 1) * ch,
                       totalWpx, ch};
        SDL_SetRenderDrawColor(r, mbg.r, mbg.g, mbg.b, mbg.a);
        SDL_RenderFillRect(r, &mrect);
        if (font.ok()) {
            const std::vector<guichrome::GuiChromeSeg> segs =
                guichrome::messageSegments(chromeData.message,
                                           layout.chrome.width);
            int consumed = 0;
            const int baseX = layout.chrome.col * cw;
            const int y = (layout.chrome.row + 1) * ch;
            for (const auto& sg : segs) {
                if (sg.text.empty()) continue;
                const int cols = chrome::colCount(sg.text);
                const bool onlySpaces =
                    sg.text.find_first_not_of(' ') == std::string::npos;
                if (!onlySpaces) {
                    const GuiColor fg =
                        guichrome::colorFor(sg.role, dark);
                    void* tex =
                        font.textTexture(r, sg.text, fg.r, fg.g, fg.b);
                    if (tex) {
                        SDL_Texture* t = static_cast<SDL_Texture*>(tex);
                        int tw = 0, th = 0;
                        SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                        SDL_Rect dst{baseX + consumed * cw, y, tw, th};
                        SDL_RenderCopy(r, t, nullptr, &dst);
                    }
                }
                consumed += cols;
            }
        }
    }
}

// Solo el cursor (bloque con glifo invertido / barra 2px), sin Present.
// Mismo código que la cola de paintChromeAndCursor.
void paintCursorOnly(SDL_Renderer* r, GuiFont& font, bool dark,
                     const Layout& layout, const FrameCursor& cursor,
                     bool cursorShown, const Frame* contentFrame,
                     const std::vector<ListLine>* listLines,
                     int listSelectedRow) {
    if (!cursorShown || !cursor.visible || !cursor.cell.valid()) return;
    const int cw = font.cellW();
    const int ch = font.cellH();
    if (cw <= 0 || ch <= 0) return;
    const GuiColor cc = guichrome::cursorColor(dark);
    const int px = cursor.cell.col * cw;
    const int py = cursor.cell.row * ch;
    if (cursor.shape == FrameCursorShape::Bar) {
        SDL_SetRenderDrawColor(r, cc.r, cc.g, cc.b, cc.a);
        SDL_Rect crect{px, py, 2, ch};
        SDL_RenderFillRect(r, &crect);
        return;
    }
    BlockGlyph glyph;
    if (contentFrame) {
        glyph = blockGlyphInContent(*contentFrame, cursor, layout, dark);
    } else if (listLines) {
        glyph = blockGlyphInList(*listLines, cursor, layout, dark,
                                 listSelectedRow);
    }
    const int wpx =
        (glyph.hasGlyph && glyph.widthCols == 2) ? 2 * cw : cw;
    SDL_SetRenderDrawColor(r, cc.r, cc.g, cc.b, cc.a);
    SDL_Rect crect{px, py, wpx, ch};
    SDL_RenderFillRect(r, &crect);
    if (glyph.hasGlyph && font.ok()) {
        const GuiColor cellBg = glyph.hasCellBg
                                    ? glyph.cellBg
                                    : guichrome::background(dark);
        const GuiColor fg = guichrome::cursorGlyphFg(cellBg, dark);
        void* tex = font.textTexture(r, glyph.text, fg.r, fg.g, fg.b);
        if (tex) {
            SDL_Texture* t = static_cast<SDL_Texture*>(tex);
            int tw = 0, th = 0;
            SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
            SDL_Rect dst{px, py, tw, th};
            SDL_RenderCopy(r, t, nullptr, &dst);
        }
    }
}

// Tamaño en píxeles del frame completo (contenido + chrome) para dimensionar
// el framebuffer persistente. false si no hay celda válida o geometría
// degenerada (el caller usa el fallback directo).
bool framePxSize(const Layout& layout, int cw, int ch, int& wPx, int& hPx) {
    if (cw <= 0 || ch <= 0) return false;
    const int rows = layout.content.height + layout.chrome.height;
    if (layout.chrome.width <= 0 || rows <= 0) return false;
    wPx = layout.chrome.width * cw;
    hPx = rows * ch;
    return true;
}

// ---- Geometría única de la MessageBar (texto y cursor Bar) ----
//
// La banda VISIBLE de la MessageBar no es solo su celda: llega al borde
// inferior de la ventana porque la división entera de guiGridSize deja una
// franja residual (winH - filas*ch, 0..ch-1 px) que visualmente es parte de
// la barra. Texto y cursor se centran en esa banda con la MISMA referencia
// vertical; una ventana múltiplo exacto de la celda no introduce
// desplazamiento (visibleH == ch => offset 0).
//
// `outH` DEBE ser la altura real del drawable: se consulta con el target de
// ventana activo (o se recibe ya calculada). NUNCA bajo el framebuffer
// seleccionado: SDL_GetRendererOutputSize devuelve el tamaño del render
// target activo, no el de la ventana.
struct MessageBarGeometry {
    int yBar = 0;      // fila superior de la MessageBar (px)
    int visibleH = 0;  // alto de la banda visible (celda + franja residual)
};

MessageBarGeometry messageBarGeometry(const Layout& layout, int outH, int ch) {
    MessageBarGeometry geo;
    if (ch <= 0 || layout.chrome.height < 2) return geo;
    geo.yBar = (layout.chrome.row + 1) * ch;
    const int gridBottom = (layout.chrome.row + layout.chrome.height) * ch;
    int restH = outH - gridBottom;
    // La franja residual (división entera de guiGridSize) mide 0..ch-1 px.
    // Un resto >= ch significa que el layout no cubre la ventana (p. ej. un
    // modal de N filas en una ventana más alta): no es una franja de la
    // barra y no debe desplazar el contenido.
    if (restH <= 0 || restH >= ch) restH = 0;
    geo.visibleH = ch + restH;
    return geo;
}

// Top del texto centrado en la banda con la tinta real (th = alto de la
// textura, medido con SDL_QueryTexture, no supuesto == ch).
int messageTextTop(const MessageBarGeometry& geo, int th) {
    const int gap = geo.visibleH - th;
    return geo.yBar + (gap > 0 ? gap / 2 : 0);
}

// Top del cursor Bar centrado en la banda con su propia geometría (alto ch).
int messageBarTop(const MessageBarGeometry& geo, int ch) {
    const int gap = geo.visibleH - ch;
    return geo.yBar + (gap > 0 ? gap / 2 : 0);
}

// Pinta la MessageBar sobre el destino de VENTANA (target por defecto):
// rellena la banda visible (celda + franja residual), dibuja el texto
// centrado y el cursor Bar con la misma referencia. Se llama DESPUÉS de
// copiar el framebuffer: así ni el texto ni el cursor quedan recortados por
// el borde inferior del framebuffer. Cambiar de camino (completo, parcial,
// cursor rápido, scroll) no cambia esta geometría.
void paintMessageBarOverlay(SDL_Renderer* r, GuiFont& font, bool dark,
                            const Layout& layout, const ChromeData& chromeData,
                            const FrameCursor& cursor, bool cursorShown,
                            int ch) {
    const int cw = font.cellW();
    if (cw <= 0 || ch <= 0 || layout.chrome.width <= 0) return;
    int outW = 0, outH = 0;
    if (SDL_GetRendererOutputSize(r, &outW, &outH) != 0) return;
    if (layout.chrome.height < 2) {
        // Ventana degenerada sin MessageBar: la franja residual sigue a la
        // única fila del chrome (StatusBar) para no dejar un borde del fondo
        // del editor. No hay texto que centrar.
        if (layout.chrome.height >= 1) {
            const int hGrid = (layout.chrome.row + layout.chrome.height) * ch;
            const int restH = outH - hGrid;
            if (restH > 0 && restH < ch) {
                const GuiColor sbg = guichrome::statusBackground(dark);
                SDL_SetRenderDrawColor(r, sbg.r, sbg.g, sbg.b, sbg.a);
                SDL_Rect rest{0, hGrid, outW, restH};
                SDL_RenderFillRect(r, &rest);
            }
        }
        return;
    }
    const MessageBarGeometry geo = messageBarGeometry(layout, outH, ch);
    const int baseX = layout.chrome.col * cw;
    const int bandW = layout.chrome.width * cw;
    const GuiColor mbg = guichrome::messageBackground(dark);
    SDL_Rect band{baseX, geo.yBar, bandW, geo.visibleH};
    SDL_SetRenderDrawColor(r, mbg.r, mbg.g, mbg.b, mbg.a);
    SDL_RenderFillRect(r, &band);
    if (font.ok()) {
        const std::vector<guichrome::GuiChromeSeg> segs =
            guichrome::messageSegments(chromeData.message,
                                       layout.chrome.width);
        int consumed = 0;
        for (const auto& sg : segs) {
            if (sg.text.empty()) continue;
            const int cols = chrome::colCount(sg.text);
            const bool onlySpaces =
                sg.text.find_first_not_of(' ') == std::string::npos;
            if (!onlySpaces) {
                const GuiColor fg = guichrome::colorFor(sg.role, dark);
                void* tex = font.textTexture(r, sg.text, fg.r, fg.g, fg.b);
                if (tex) {
                    SDL_Texture* t = static_cast<SDL_Texture*>(tex);
                    int tw = 0, th = 0;
                    SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                    const int y = messageTextTop(geo, th);
                    SDL_Rect dst{baseX + consumed * cw, y, tw, th};
                    SDL_RenderCopy(r, t, nullptr, &dst);
                }
            }
            consumed += cols;
        }
    }
    // Cursor Bar en la fila del MessageBar (prompts con input): misma
    // referencia vertical que el texto; no toca la geometría de otras filas.
    if (cursorShown && cursor.visible && cursor.cell.valid() &&
        cursor.shape == FrameCursorShape::Bar &&
        cursor.cell.row == layout.chrome.row + 1) {
        const GuiColor cc = guichrome::cursorColor(dark);
        const int px = cursor.cell.col * cw;
        const int py = messageBarTop(geo, ch);
        SDL_SetRenderDrawColor(r, cc.r, cc.g, cc.b, cc.a);
        SDL_Rect crect{px, py, 2, ch};
        SDL_RenderFillRect(r, &crect);
    }
}

// Vuelca el framebuffer a la ventana: vuelve al destino por defecto, limpia
// el backbuffer, copia la textura completa, repinta la MessageBar sobre la
// ventana (centrada, con la franja residual como parte de la barra) y
// presenta. El backbuffer se trata como invalidado en cada Present (contrato
// SDL2): nunca se lee de él, así el parcial no depende de que conserve
// píxeles. La copia es 1:1 en la esquina superior izquierda (el framebuffer
// mide la grilla, no la ventana; un stretch rompería las coordenadas de
// celda).
void presentFramebuf(SDL_Renderer* r, GuiFont& font, SDL_Texture* fb,
                     const GuiColor& bg, const Layout& layout, int ch,
                     bool dark, const ChromeData& chromeData,
                     const FrameCursor& cursor, bool cursorShown) {
    SDL_SetRenderTarget(r, nullptr);
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(r);
    int w = 0, h = 0;
    SDL_QueryTexture(fb, nullptr, nullptr, &w, &h);
    SDL_Rect dst{0, 0, w, h};
    SDL_RenderCopy(r, fb, nullptr, &dst);
    // Con el target de VENTANA activo: mide outH real y sobre-dibuja la
    // banda de la MessageBar (cubre el texto/borde que haya traído el
    // framebuffer en posición de grilla).
    paintMessageBarOverlay(r, font, dark, layout, chromeData, cursor,
                           cursorShown, ch);
    SDL_RenderPresent(r);
}

// Cursor del contenido resuelto desde el SNAPSHOT (GuiContentRow), sin Frame.
// Espejo de la rama de contenido de paintCursorOnly (Bar 2px / Block con
// glifo invertido vía blockGlyphInSnapshot): la usan los caminos rápidos,
// que no construyen el Frame. Paridad congelada en tests de píxeles SDL.
void paintContentCursorFromSnapshot(SDL_Renderer* r, GuiFont& font, bool dark,
                                    const Layout& layout,
                                    const std::vector<GuiContentRow>& rows,
                                    const FrameCursor& cursor, bool shown) {
    if (!shown || !cursor.visible || !cursor.cell.valid()) return;
    const int cw = font.cellW();
    const int ch = font.cellH();
    if (cw <= 0 || ch <= 0) return;
    const GuiColor cc = guichrome::cursorColor(dark);
    const int px = cursor.cell.col * cw;
    const int py = cursor.cell.row * ch;
    if (cursor.shape == FrameCursorShape::Bar) {
        SDL_SetRenderDrawColor(r, cc.r, cc.g, cc.b, cc.a);
        SDL_Rect crect{px, py, 2, ch};
        SDL_RenderFillRect(r, &crect);
        return;
    }
    const BlockGlyph glyph = blockGlyphInSnapshot(rows, layout, cursor, dark);
    const int wpx =
        (glyph.hasGlyph && glyph.widthCols == 2) ? 2 * cw : cw;
    SDL_SetRenderDrawColor(r, cc.r, cc.g, cc.b, cc.a);
    SDL_Rect crect{px, py, wpx, ch};
    SDL_RenderFillRect(r, &crect);
    if (glyph.hasGlyph && font.ok()) {
        const GuiColor cellBg = glyph.hasCellBg
                                    ? glyph.cellBg
                                    : guichrome::background(dark);
        const GuiColor fg = guichrome::cursorGlyphFg(cellBg, dark);
        void* tex = font.textTexture(r, glyph.text, fg.r, fg.g, fg.b);
        if (tex) {
            SDL_Texture* t = static_cast<SDL_Texture*>(tex);
            int tw = 0, th = 0;
            SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
            SDL_Rect dst{px, py, tw, th};
            SDL_RenderCopy(r, t, nullptr, &dst);
        }
    }
}
#endif

// Dibuja el chrome (StatusBar + MessageBar) y el cursor sobre el destino
// actual del SDL_Renderer (framebuffer persistente o backbuffer en el
// fallback). NO presenta: el caller compone/presenta después (P1).
// Solo existe bajo HAVE_SDL2 (sin SDL el caller ya
// guardo last* para tests y no hay nada que pintar).
#ifdef HAVE_SDL2
void paintChromeAndCursor(SDL_Renderer* r, GuiFont& font, bool dark,
                          const Layout& layout, const ChromeData& chromeData,
                          StyleRole accent, const FrameCursor& cursor,
                          bool cursorShown, const Frame* contentFrame,
                          const std::vector<ListLine>* listLines,
                          int listSelectedRow) {
    // Pintado por fragmentos: cada segmento de statusSegments/messageSegments
    // lleva su StyleRole (nombre/ruta/[*]/etiqueta con su rol, igual que
    // TtyChrome). El fondo se limpió con un fill uniforme; los segmentos de
    // solo-espacios (pads/fill) no crean textura, solo avanzan la grilla.
    const int cw = font.cellW();
    const int ch = font.cellH();
    // Sin celda válida no hay nada que pintar (el caller presenta).
    // P1: esta función ya no presenta (el Present vive en el caller tras el
    // composite del framebuffer, o en el fallback directo).
    if (cw <= 0 || ch <= 0) return;

    const int totalWpx = layout.chrome.width * cw;
    // StatusBar: fila superior del chrome.
    if (layout.chrome.height >= 1 && layout.chrome.width > 0) {
        const GuiColor sbg = guichrome::statusBackground(dark);
        SDL_Rect srect{layout.chrome.col * cw, layout.chrome.row * ch, totalWpx,
                       ch};
        SDL_SetRenderDrawColor(r, sbg.r, sbg.g, sbg.b, sbg.a);
        SDL_RenderFillRect(r, &srect);
        if (font.ok()) {
            const std::vector<guichrome::GuiChromeSeg> segs =
                guichrome::statusSegments(chromeData.statusBar, accent,
                                          layout.chrome.width);
            int consumed = 0;
            const int baseX = layout.chrome.col * cw;
            const int y = layout.chrome.row * ch;
            for (const auto& sg : segs) {
                if (sg.text.empty()) continue;
                const int cols = chrome::colCount(sg.text);
                const bool onlySpaces =
                    sg.text.find_first_not_of(' ') == std::string::npos;
                if (!onlySpaces) {
                    const GuiColor fg =
                        guichrome::colorFor(sg.role, dark);
                    void* tex =
                        font.textTexture(r, sg.text, fg.r, fg.g, fg.b);
                    if (tex) {
                        SDL_Texture* t = static_cast<SDL_Texture*>(tex);
                        int tw = 0, th = 0;
                        SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                        SDL_Rect dst{baseX + consumed * cw, y, tw, th};
                        SDL_RenderCopy(r, t, nullptr, &dst);
                    }
                }
                consumed += cols;
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
        if (font.ok()) {
            const std::vector<guichrome::GuiChromeSeg> segs =
                guichrome::messageSegments(chromeData.message,
                                           layout.chrome.width);
            int consumed = 0;
            const int baseX = layout.chrome.col * cw;
            const int y = (layout.chrome.row + 1) * ch;
            for (const auto& sg : segs) {
                if (sg.text.empty()) continue;
                const int cols = chrome::colCount(sg.text);
                const bool onlySpaces =
                    sg.text.find_first_not_of(' ') == std::string::npos;
                if (!onlySpaces) {
                    const GuiColor fg =
                        guichrome::colorFor(sg.role, dark);
                    void* tex =
                        font.textTexture(r, sg.text, fg.r, fg.g, fg.b);
                    if (tex) {
                        SDL_Texture* t = static_cast<SDL_Texture*>(tex);
                        int tw = 0, th = 0;
                        SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                        SDL_Rect dst{baseX + consumed * cw, y, tw, th};
                        SDL_RenderCopy(r, t, nullptr, &dst);
                    }
                }
                consumed += cols;
            }
        }
    }

    // Cursor: el Frame ya trae la posicion resuelta (contenido o MessageBar
    // en prompts). Bar = barra fina de 2px (ok sobre el texto).
    // Bloque = fijo (navegación); Barra = parpadea (inserción): el caller ya
    // resolvió la fase en `cursorShown` (visible lógico AND blink).
    // El Block invierte el glifo (paridad con el TTY, que lo mantiene
    // legible): fill con cursorColor + repintado de la celda con el fg de
    // la política cursorGlyphFg (inversión exacta con el fondo efectivo de
    // la celda, o fondo base como fallback si ese no contrasta: ver
    // GuiChrome.h). Sin fuente o en celda vacía/espacio se deja solo el
    // fill. En chrome o fuera de contenido/lista no hay glifo que resolver
    // y también se deja solo el fill.
    if (cursorShown && cursor.visible && cursor.cell.valid()) {
        const GuiColor cc = guichrome::cursorColor(dark);
        const int px = cursor.cell.col * cw;
        const int py = cursor.cell.row * ch;
        if (cursor.shape == FrameCursorShape::Bar) {
            SDL_SetRenderDrawColor(r, cc.r, cc.g, cc.b, cc.a);
            SDL_Rect crect{px, py, 2, ch};
            SDL_RenderFillRect(r, &crect);
        } else {
            BlockGlyph glyph;
            if (contentFrame) {
                glyph =
                    blockGlyphInContent(*contentFrame, cursor, layout, dark);
            } else if (listLines) {
                glyph = blockGlyphInList(*listLines, cursor, layout, dark,
                                         listSelectedRow);
            }
            const int wpx = (glyph.hasGlyph && glyph.widthCols == 2)
                                ? 2 * cw
                                : cw;
            SDL_SetRenderDrawColor(r, cc.r, cc.g, cc.b, cc.a);
            SDL_Rect crect{px, py, wpx, ch};
            SDL_RenderFillRect(r, &crect);
            if (glyph.hasGlyph && font.ok()) {
                const GuiColor cellBg = glyph.hasCellBg
                                            ? glyph.cellBg
                                            : guichrome::background(dark);
                const GuiColor fg =
                    guichrome::cursorGlyphFg(cellBg, dark);
                void* tex =
                    font.textTexture(r, glyph.text, fg.r, fg.g, fg.b);
                if (tex) {
                    // Sin recorte a wpx×ch a propósito: por contrato de
                    // textTexture (ver GuiFont.h) la textura mide la caja de
                    // avance (1 col → cw×ch, ancha → 2×cw×ch) y cabe en el
                    // fill; recortar solo ocultaría una fuente rota.
                    SDL_Texture* t = static_cast<SDL_Texture*>(tex);
                    int tw = 0, th = 0;
                    SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                    SDL_Rect dst{px, py, tw, th};
                    SDL_RenderCopy(r, t, nullptr, &dst);
                }
            }
        }
    }
}
#endif

}  // namespace

bool GuiRenderer::tryFastCursorMove(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const ChromeRequest& chrome,
    const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    // Precondiciones probadas (etapa ↑/↓ sin scroll). Si alguna no se cumple,
    // false SIN mutar nada: el caller cae al camino completo existente.
    // No basta con pintar menos filas después de buildFrame(): este camino
    // tampoco CONSTRUYE ni copia los segmentos de las filas que no cambiaron.
    // Con Soft Wrap una línea lógica ocupa varias filas visuales y el mapeo
    // `fila relativa = docLine - viewport.top` deja de ser válido; el camino
    // rápido no puede reconstruir las continuaciones que cambian de
    // CurrentLine. Se desactiva hasta implementar rangos por índice visual.
    if (frameBuilder_.isWrapEnabled(wrapIndex_)) return false;
    if (!contentHasCache_) return false;
    if (selection.has_value() || searchHighlight.has_value() ||
        contentSelection_.has_value() || contentSearch_.has_value()) {
        return false;  // el highlight de selección/búsqueda cubre N filas
    }
    // Etapa 2 (conservadora): cualquier bracket presente o cacheado invalida
    // el camino rápido; su overlay puede cubrir filas fuera de CurrentLine.
    if (bracketPair.has_value() || contentBracket_.has_value()) return false;
    if (chrome.cursorInMessageBar || contentCursorInMsg_) return false;
    // Idioma: si cambió, el caché de sintaxis se invalidó y hay que rebuild.
    // (Idempotente: el camino completo lo llamaría de nuevo como no-op.)
    if (frameBuilder_.updateSyntaxLanguage(filename)) return false;
    if (filename != contentFilename_) return false;
    if (doc.instanceId() != contentDocInstance_ ||
        doc.version() != contentDocVersion_) {
        return false;  // edición o cambio de buffer
    }
    if (doc.lineCount() != contentLineCount_) return false;
    // Mismo viewport.top (sin scroll; el scroll ±1 es la etapa siguiente) y
    // mismo left/ancho/alto (un scroll horizontal o resize cambia todas).
    if (viewport.top != contentTop_ || viewport.left != contentLeft_ ||
        viewport.width != contentVpWidth_ ||
        viewport.height != contentVpHeight_) {
        return false;
    }
    const FrameBuilder::EditorGeometry g =
        frameBuilder_.editorGeometry(doc, viewport);
    if (!sameLayout(g.layout, contentLayout_)) return false;
    const int cw = font_.cellW();
    const int ch = font_.cellH();
    if (cw != contentCellW_ || ch != contentCellH_ ||
        dark_ != contentDark_) {
        return false;  // tema o métricas HiDPI: frame completo
    }
    const int H = g.layout.content.height;
    if (H <= 0 ||
        static_cast<int>(contentRows_.size()) != H ||
        static_cast<int>(lastContentRows_.size()) != H) {
        return false;
    }
#ifdef HAVE_SDL2
    // Sin superficie persistente no hay parcial seguro (ver camino completo:
    // el fallback pinta TODO directo, pero acá no hay Frame completo con que
    // hacerlo). Caer al camino completo, que sí lo tiene. Sin SDL no hay
    // pintado y el camino rápido sigue valiendo (ahorro de CPU puro).
    if (sdlRenderer_ && contentFb_[contentFbActive_].tex == nullptr) {
        return false;
    }
#endif
    // Chrome barato (sin filas): si cambió se repinta aunque el contenido no.
    const FrameBuilder::ChromePayload payload =
        frameBuilder_.buildChrome(filename, modified, cursor,
                                  doc.lineCount(), chrome);
    const bool chromeSame = (payload.data == contentChrome_) &&
                            (payload.statusAccent == contentAccent_);
    const bool chromeDirty = !chromeSame;
    // Cursor como buildFrame (contenido; el caso MessageBar ya se excluyó).
    FrameCursor next;
    next.shape = chrome.cursorShape;
    CellPos pos;
    const bool inViewport =
        frameBuilder_.editorCursorPos(doc, cursor, viewport, g, pos);
    const bool visible = chrome.cursorVisibleByMode && inViewport;
    next.visible = visible;
    next.cell = visible ? pos : CellPos{};
    // Filas afectadas por CurrentLine (vieja y nueva, si están visibles).
    // El cursor siempre vive en su CurrentLine, así que su fila también queda
    // cubierta para el repintado de borrado/dibujado de abajo.
    const int gutterW = g.gutterW;
    const int textWidth =
        std::max(0, g.layout.content.width - gutterW);
    const int relOld = contentCursorLine_ - viewport.top;
    const int relNew = cursor.line - viewport.top;
    int rels[2];
    int nRels = 0;
    if (relOld >= 0 && relOld < H) rels[nRels++] = relOld;
    if (relNew >= 0 && relNew < H && relNew != relOld) rels[nRels++] = relNew;
    // Reconstruye SOLO esas filas (O(1), no O(H)): ninguna otra fila se
    // construye ni se copia. buildContentRow asegura el SyntaxCache que
    // necesite (ya válido hasta top+H por el frame anterior: no reparsea).
    const std::optional<Normalized> noSel, noSearch, noBrOpen, noBrClose;
    struct Rebuilt {
        int rel = -1;
        StyledRow styled;
        GuiContentRow snap;
        bool changed = false;
    };
    Rebuilt rb[2];
    for (int k = 0; k < nRels; ++k) {
        const int rel = rels[k];
        const int docLine = viewport.top + rel;
        rb[k].rel = rel;
        rb[k].styled = frameBuilder_.buildContentRow(
            doc, cursor, viewport, noSel, noSearch, noBrOpen, noBrClose,
            docLine, gutterW, textWidth);
        GuiContentRow gr;
        gr.isCurrentLine = rb[k].styled.isCurrentLine;
        gr.segs.reserve(rb[k].styled.segs.size());
        for (const auto& s : rb[k].styled.segs) {
            GuiContentSeg gs;
            gs.text.assign(s.text.data(), s.text.size());
            gs.role = s.role;
            gr.segs.push_back(std::move(gs));
        }
        rb[k].snap = std::move(gr);
        rb[k].changed =
            (rb[k].snap != contentRows_[static_cast<size_t>(rel)]);
    }
    std::vector<char> dirty(static_cast<size_t>(H), 0);
    for (int k = 0; k < nRels; ++k) {
        if (rb[k].changed) dirty[static_cast<size_t>(rb[k].rel)] = 1;
    }
    // Cursor: borrar la vieja + dibujar la nueva (misma regla que el camino
    // completo). Bar visible: su fila siempre (parpadeo, 1 fila/frame).
    const FrameCursor prevContent = contentCursor_;
    const bool cursorChanged =
        prevContent.visible != next.visible ||
        prevContent.shape != next.shape ||
        !(prevContent.cell == next.cell);
    if (cursorChanged) {
        markCursorRow(dirty, g.layout, prevContent);
        markCursorRow(dirty, g.layout, next);
    } else if (next.visible &&
               next.shape == FrameCursorShape::Bar &&
               next.cell.valid()) {
        markCursorRow(dirty, g.layout, next);
    }
    int repainted = 0, skipped = 0;
    for (char d : dirty) (d ? repainted : skipped) += 1;
    bool paintChrome = chromeDirty;
    if (true) {
        const bool newBar = isChromeBar(next, g.layout);
        const bool oldBar = isChromeBar(prevContent, g.layout);
        if (newBar || (oldBar && cursorChanged)) paintChrome = true;
    }
    noteStats(false, repainted, skipped, chromeDirty);
    // Publica snapshots y caché (solo las filas reconstruidas).
    lastChrome_ = payload.data;
    lastAccent_ = payload.statusAccent;
    noteCursorForBlink(next);
    lastCursor_ = next;
    lastLayout_ = g.layout;
    lastListLines_.clear();
    lastListSelected_ = -1;
    lastListScroll_ = 0;
    const bool shown = cursorShown();
    for (int k = 0; k < nRels; ++k) {
        contentRows_[static_cast<size_t>(rb[k].rel)] = rb[k].snap;
        lastContentRows_[static_cast<size_t>(rb[k].rel)] = rb[k].snap;
    }
    contentChrome_ = payload.data;
    contentAccent_ = payload.statusAccent;
    contentLayout_ = g.layout;
    contentDark_ = dark_;
    contentCellW_ = cw;
    contentCellH_ = ch;
    contentCursor_ = next;
    contentHasCache_ = true;
    lastIsList_ = false;
    contentModified_ = modified;
    contentCursorLine_ = cursor.line;
#ifdef HAVE_SDL2
    if (sdlRenderer_) {
        SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
        const GuiColor bg = guichrome::background(dark_);
        int wPx = 0, hPx = 0;
        bool fbFresh = false;
        SDL_Texture* fb = nullptr;
        if (framePxSize(g.layout, font_.cellW(), font_.cellH(), wPx, hPx)) {
            bool fr = false;
            fb = static_cast<SDL_Texture*>(acquireFb(
                contentFb_[contentFbActive_], sdlRenderer_, wPx, hPx, fr));
            fbFresh = (fb != nullptr) && fr;
        }
        // Pre-chequeado arriba: fb existe y no es fresco (misma geometría).
        // Si algo cambió bajo los pies (reserva de textura fallida), el
        // parcial no es seguro; se invalida y se deja el frame sin pintar:
        // el próximo renderFrame será completo. No se puede caer al camino
        // completo desde acá (ya se publicaron snapshots parciales válidos,
        // pero faltaría el pintado de este frame).
        if (!fb || fbFresh) {
            invalidateCache();
            return true;
        }
        SDL_SetRenderTarget(r, fb);
        for (int k = 0; k < nRels; ++k) {
            if (!dirty[static_cast<size_t>(rb[k].rel)]) continue;
            paintSnapshotRow(r, font_, dark_, g.layout, rb[k].rel,
                             contentRows_[static_cast<size_t>(rb[k].rel)]);
        }
        // Filas dirty SOLO por cursor (sin rebuild): se repintan desde el
        // snapshot cacheado (idéntico al que pintaría el camino completo).
        for (int i = 0; i < H; ++i) {
            if (!dirty[static_cast<size_t>(i)]) continue;
            bool rebuilt = false;
            for (int k = 0; k < nRels; ++k) {
                if (rb[k].rel == i) {
                    rebuilt = true;
                    break;
                }
            }
            if (!rebuilt) {
                paintSnapshotRow(r, font_, dark_, g.layout, i,
                                 contentRows_[static_cast<size_t>(i)]);
            }
        }
        if (paintChrome)
            paintChromeOnly(r, font_, dark_, g.layout, payload.data,
                            payload.statusAccent);
        // Cursor desde el snapshot (sin Frame en el camino rápido).
        paintContentCursorFromSnapshot(r, font_, dark_, g.layout,
                                       contentRows_, next, shown);
        presentFramebuf(r, font_, fb, bg, g.layout, ch, dark_, payload.data,
                        next, shown);
    }
#else
    // Sin SDL2: no-op (el loop nunca llega acá).
#endif
    return true;
}

bool GuiRenderer::tryFastScrollLine(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const ChromeRequest& chrome,
    const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    // Precondiciones (etapa scroll ±1). Si alguna falla, false SIN mutar:
    // el caller cae al camino completo. Deltas != ±1 (saltos, páginas),
    // edición, cambios visuales o geometría incompatible van por full.
    // Con Soft Wrap `viewport.top` es una fila visual, no una línea
    // documental: rotar `contentRows_` como docLine+1 y construir "la línea
    // entrante" produciría filas incorrectas. Se desactiva hasta implementar
    // rangos por índice visual.
    if (frameBuilder_.isWrapEnabled(wrapIndex_)) return false;
    if (!contentHasCache_) return false;
    const int delta = viewport.top - contentTop_;
    if (delta != 1 && delta != -1) return false;
    // Selección/búsqueda/bracket: se admiten si son IGUALES a las cacheadas
    // (el overlay es función de docLine y la rotación lo preserva); cualquier
    // cambio invalida el camino rápido.
    if (!sameOptSelection(selection, contentSelection_)) return false;
    if (!sameOptSelection(searchHighlight, contentSearch_)) return false;
    if (bracketPair.has_value() != contentBracket_.has_value()) return false;
    if (bracketPair.has_value() && *bracketPair != *contentBracket_) {
        return false;
    }
    if (chrome.cursorInMessageBar || contentCursorInMsg_) return false;
    if (frameBuilder_.updateSyntaxLanguage(filename)) return false;
    if (filename != contentFilename_) return false;
    if (doc.instanceId() != contentDocInstance_ ||
        doc.version() != contentDocVersion_) {
        return false;
    }
    if (doc.lineCount() != contentLineCount_) return false;
    if (viewport.left != contentLeft_ ||
        viewport.width != contentVpWidth_ ||
        viewport.height != contentVpHeight_) {
        return false;
    }
    const FrameBuilder::EditorGeometry g =
        frameBuilder_.editorGeometry(doc, viewport);
    if (!sameLayout(g.layout, contentLayout_)) return false;
    const int cw = font_.cellW();
    const int ch = font_.cellH();
    if (cw != contentCellW_ || ch != contentCellH_ ||
        dark_ != contentDark_) {
        return false;
    }
    const int H = g.layout.content.height;
    if (H <= 0 ||
        static_cast<int>(contentRows_.size()) != H ||
        static_cast<int>(lastContentRows_.size()) != H) {
        return false;
    }
#ifdef HAVE_SDL2
    // Igual que el camino de cursor-move: sin superficie persistente no hay
    // parcial seguro y acá no hay Frame completo con que pintar el fallback.
    if (sdlRenderer_ && contentFb_[contentFbActive_].tex == nullptr) {
        return false;
    }
#endif
    const FrameBuilder::ChromePayload payload =
        frameBuilder_.buildChrome(filename, modified, cursor,
                                  doc.lineCount(), chrome);
    const bool chromeSame = (payload.data == contentChrome_) &&
                            (payload.statusAccent == contentAccent_);
    const bool chromeDirty = !chromeSame;
    FrameCursor next;
    next.shape = chrome.cursorShape;
    CellPos pos;
    const bool inViewport =
        frameBuilder_.editorCursorPos(doc, cursor, viewport, g, pos);
    const bool visible = chrome.cursorVisibleByMode && inViewport;
    next.visible = visible;
    next.cell = visible ? pos : CellPos{};
    // Rotación de snapshots: la entrada i pasa a representar
    // docLine = viewport.top + i (el contenido es función de docLine:
    // texto, gutter, sintaxis y overlays se preservan al rotar).
    // Solo movimientos O(H) sin allocs (los strings ya son dueños).
    if (delta == 1) {
        for (int i = 0; i + 1 < H; ++i) {
            contentRows_[static_cast<size_t>(i)] =
                std::move(contentRows_[static_cast<size_t>(i + 1)]);
            lastContentRows_[static_cast<size_t>(i)] =
                std::move(lastContentRows_[static_cast<size_t>(i + 1)]);
        }
    } else {
        for (int i = H - 1; i > 0; --i) {
            contentRows_[static_cast<size_t>(i)] =
                std::move(contentRows_[static_cast<size_t>(i - 1)]);
            lastContentRows_[static_cast<size_t>(i)] =
                std::move(lastContentRows_[static_cast<size_t>(i - 1)]);
        }
    }
    const std::optional<Normalized> sel =
        selection.has_value() ? normalize(*selection) : std::nullopt;
    const std::optional<Normalized> searchSel =
        searchHighlight.has_value() ? normalize(*searchHighlight)
                                    : std::nullopt;
    std::optional<Normalized> bracketOpen, bracketClose;
    FrameBuilder::normalizeBracketPair(bracketPair, bracketOpen, bracketClose);
    const int gutterW = g.gutterW;
    const int textWidth =
        std::max(0, g.layout.content.width - gutterW);
    auto buildSnap = [&](int docLine) {
        StyledRow sr = frameBuilder_.buildContentRow(
            doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose,
            docLine, gutterW, textWidth);
        GuiContentRow gr;
        gr.isCurrentLine = sr.isCurrentLine;
        gr.segs.reserve(sr.segs.size());
        for (const auto& s : sr.segs) {
            GuiContentSeg gs;
            gs.text.assign(s.text.data(), s.text.size());
            gs.role = s.role;
            gr.segs.push_back(std::move(gs));
        }
        return gr;
    };
    // Fila entrante (la única garantizada nueva) + extras por CurrentLine
    // vieja/nueva si siguen visibles y no coinciden con la entrante.
    // Todo lo demás se reutiliza sin construir ni copiar segmentos.
    // SyntaxCache: buildContentRow asegura lo que necesite (típicamente 1
    // línea nueva; el resto ya estaba válido).
    const int enteringRel = (delta == 1) ? H - 1 : 0;
    const int enteringDocLine =
        (delta == 1) ? viewport.top + H - 1 : viewport.top;
    contentRows_[static_cast<size_t>(enteringRel)] =
        buildSnap(enteringDocLine);
    lastContentRows_[static_cast<size_t>(enteringRel)] =
        contentRows_[static_cast<size_t>(enteringRel)];
    const int lines[2] = {contentCursorLine_, cursor.line};
    // dirty físico (para el pintado con blit): entrante + extras que
    // cambiaron + filas del cursor (el blit arrastra píxeles viejos del
    // cursor: hay que repintar su borrado y su dibujado desde el snapshot).
    std::vector<char> dirty(static_cast<size_t>(std::max(0, H)), 0);
    dirty[static_cast<size_t>(enteringRel)] = 1;
    for (int li = 0; li < 2; ++li) {
        const int rel = lines[li] - viewport.top;
        if (rel < 0 || rel >= H || rel == enteringRel) continue;
        GuiContentRow gr = buildSnap(viewport.top + rel);
        if (gr != contentRows_[static_cast<size_t>(rel)]) {
            contentRows_[static_cast<size_t>(rel)] = gr;
            lastContentRows_[static_cast<size_t>(rel)] = gr;
            dirty[static_cast<size_t>(rel)] = 1;
        }
    }
    const FrameCursor prevContent = contentCursor_;
    const bool cursorChanged =
        prevContent.visible != next.visible ||
        prevContent.shape != next.shape ||
        !(prevContent.cell == next.cell);
    if (cursorChanged) {
        markCursorRow(dirty, g.layout, prevContent);
        markCursorRow(dirty, g.layout, next);
    } else if (next.visible &&
               next.shape == FrameCursorShape::Bar &&
               next.cell.valid()) {
        markCursorRow(dirty, g.layout, next);
    }
    int repainted = 0, skipped = 0;
    for (char d : dirty) (d ? repainted : skipped) += 1;
    bool paintChrome = chromeDirty;
    {
        const bool newBar = isChromeBar(next, g.layout);
        const bool oldBar = isChromeBar(prevContent, g.layout);
        if (newBar || (oldBar && cursorChanged)) paintChrome = true;
    }
    noteStats(false, repainted, skipped, chromeDirty);
    lastChrome_ = payload.data;
    lastAccent_ = payload.statusAccent;
    noteCursorForBlink(next);
    lastCursor_ = next;
    lastLayout_ = g.layout;
    lastListLines_.clear();
    lastListSelected_ = -1;
    lastListScroll_ = 0;
    const bool shown = cursorShown();
    contentChrome_ = payload.data;
    contentAccent_ = payload.statusAccent;
    contentLayout_ = g.layout;
    contentDark_ = dark_;
    contentCellW_ = cw;
    contentCellH_ = ch;
    contentCursor_ = next;
    contentHasCache_ = true;
    lastIsList_ = false;
    contentTop_ = viewport.top;
    contentModified_ = modified;
    contentCursorLine_ = cursor.line;
    contentSelection_ = selection;
    contentSearch_ = searchHighlight;
    contentBracket_ = bracketPair;
#ifdef HAVE_SDL2
    if (sdlRenderer_) {
        SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
        const GuiColor bg = guichrome::background(dark_);
        const int ccw = font_.cellW();
        const int cch = font_.cellH();
        int wPx = 0, hPx = 0;
        if (!framePxSize(g.layout, ccw, cch, wPx, hPx) || ccw <= 0 ||
            cch <= 0) {
            invalidateCache();
            return true;
        }
        // Activa (imagen vigente) y destino del ping-pong. Nunca se copia
        // una textura sobre sí misma (SDL lo deja indefinido): el blit va de
        // la activa a la inactiva y se conmuta al presentar.
        const int srcIdx = contentFbActive_;
        const int dstIdx = 1 - contentFbActive_;
        bool srcFresh = false, dstFresh = false;
        SDL_Texture* srcFb = static_cast<SDL_Texture*>(
            acquireFb(contentFb_[srcIdx], sdlRenderer_, wPx, hPx, srcFresh));
        SDL_Texture* dstFb = static_cast<SDL_Texture*>(
            acquireFb(contentFb_[dstIdx], sdlRenderer_, wPx, hPx, dstFresh));
        // Tres casos distintos de recuperación:
        //  - origen inválido o recién creado: no contiene la imagen vigente,
        //    así que no hay nada seguro de donde blitear ni repintar. Se
        //    invalida y el próximo frame será completo;
        //  - destino inválido (OOM): no se puede blitear, pero el origen SÍ
        //    es válido. Se repinta TODO el contenido desde el caché sobre el
        //    origen, se presenta y se conserva como activo (no se conmuta);
        //  - destino recién creado: su contenido es indefinido, pero el blit
        //    cubre entera la región de contenido y el chrome se repinta
        //    siempre; se puede usar el camino normal.
        const bool srcUsable = srcFb && !srcFresh;
        if (!srcUsable) {
            invalidateCache();
            return true;
        }
        if (!dstFb) {
            SDL_SetRenderTarget(r, srcFb);
            for (int i = 0; i < H; ++i) {
                paintSnapshotRow(r, font_, dark_, g.layout, i,
                                 contentRows_[static_cast<size_t>(i)]);
            }
            if (paintChrome)
                paintChromeOnly(r, font_, dark_, g.layout, payload.data,
                                payload.statusAccent);
            paintContentCursorFromSnapshot(r, font_, dark_, g.layout,
                                           contentRows_, next, shown);
            presentFramebuf(r, font_, srcFb, bg, g.layout, cch, dark_,
                            payload.data, next, shown);
            return true;
        }
        // Scroll-blit: desplaza la región de contenido ±1 fila hacia el
        // destino. El chrome NO se desplaza (posición fija): se repinta
        // siempre sobre el destino (2 filas baratas; su región en el destino
        // puede estar obsoleta y el blit no la cubre).
        SDL_SetRenderTarget(r, dstFb);
        if (H > 1) {
            const int x0 = g.layout.content.col * ccw;
            const int y0 = g.layout.content.row * cch;
            const int wRow = g.layout.content.width * ccw;
            SDL_Rect src{0, 0, 0, 0}, dst{0, 0, 0, 0};
            if (delta == 1) {
                // El contenido sube: destino[i] <- origen[i+1].
                src = SDL_Rect{x0, y0 + cch, wRow, (H - 1) * cch};
                dst = SDL_Rect{x0, y0, wRow, (H - 1) * cch};
            } else {
                // El contenido baja: destino[i] <- origen[i-1].
                src = SDL_Rect{x0, y0, wRow, (H - 1) * cch};
                dst = SDL_Rect{x0, y0 + cch, wRow, (H - 1) * cch};
            }
            SDL_RenderCopy(r, srcFb, &src, &dst);
        }
        for (int i = 0; i < H; ++i) {
            if (!dirty[static_cast<size_t>(i)]) continue;
            paintSnapshotRow(r, font_, dark_, g.layout, i,
                             contentRows_[static_cast<size_t>(i)]);
        }
        paintChromeOnly(r, font_, dark_, g.layout, payload.data,
                        payload.statusAccent);
        paintContentCursorFromSnapshot(r, font_, dark_, g.layout,
                                       contentRows_, next, shown);
        presentFramebuf(r, font_, dstFb, bg, g.layout, cch, dark_,
                        payload.data, next, shown);
        contentFbActive_ = dstIdx;
    }
#else
    // Sin SDL2: no-op (el loop nunca llega acá).
#endif
    return true;
}

void GuiRenderer::renderScreenDiff(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const ChromeRequest& chrome,
    Sink& sink, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    (void)sink;  // la GUI pinta en el SDL_Renderer, no en el Sink de bytes.

    // Camino rápido (etapa ↑/↓ sin scroll): evita buildFrame() completo si
    // solo se movió el cursor sobre el mismo viewport sin cambios. Si lo
    // resuelve, ya actualizó cachés/stats/snapshots y pintó; si no, se cae al
    // camino completo de abajo sin haber mutado nada. Los fast paths se
    // autodescartan con Soft Wrap activo (ver sus precondiciones).
    if (tryFastCursorMove(doc, cursor, viewport, filename, modified, chrome,
                          selection, searchHighlight, bracketPair)) {
        return;
    }
    // Camino rápido (etapa scroll ±1): rota snapshots y construye O(1) filas.
    // Mismas garantías de equivalencia; el repintado sigue saliendo del caché
    // hasta la etapa scroll-blit.
    if (tryFastScrollLine(doc, cursor, viewport, filename, modified, chrome,
                          selection, searchHighlight, bracketPair)) {
        return;
    }
    Frame f = buildFrame(doc, cursor, viewport, filename, modified, chrome,
                         selection, searchHighlight, bracketPair, wrapIndex_);
    const bool havePrevContent = contentHasCache_;
    const FrameCursor prevContent = contentCursor_;
    lastChrome_ = f.chrome;
    lastAccent_ = f.statusAccent;
    noteCursorForBlink(f.cursor);
    lastCursor_ = f.cursor;
    lastLayout_ = f.layout;
    std::vector<GuiContentRow> fresh;
    snapshotFrameContent(f, fresh);
    // P2-copias: `fresh` se conserva local hasta terminar el pintado; al
    // final se publica el snapshot (una copia) y se transfiere al caché por
    // movimiento, sin segunda copia profunda.
    lastListLines_.clear();
    lastListSelected_ = -1;
    lastListScroll_ = 0;
    const bool shown = cursorShown();

    // --- Diff por línea contra el caché de contenido ---
    const int contentH = static_cast<int>(fresh.size());
    const int cw = font_.cellW();
    const int ch = font_.cellH();
    const bool chromeSame = contentHasCache_ && contentChrome_ == f.chrome &&
                            contentAccent_ == f.statusAccent;
    const bool layoutSame =
        contentHasCache_ && sameLayout(contentLayout_, f.layout);
    // P2-resize: la celda física forma parte de la validez (ver header).
    const bool metricsSame = contentHasCache_ && contentCellW_ == cw &&
                             contentCellH_ == ch;
    const bool full = !contentHasCache_ || !layoutSame ||
                      contentDark_ != dark_ || !metricsSame;
    bool chromeDirty = full || !chromeSame;
    std::vector<char> dirty(static_cast<size_t>(std::max(0, contentH)), 0);
    int repainted = 0, skipped = 0;
    if (full) {
        std::fill(dirty.begin(), dirty.end(), 1);
        repainted = contentH;
    } else {
        for (int i = 0; i < contentH; ++i) {
            bool same = i < static_cast<int>(contentRows_.size()) &&
                        contentRows_[static_cast<size_t>(i)] == fresh[static_cast<size_t>(i)];
            dirty[static_cast<size_t>(i)] = same ? 0 : 1;
        }
        // Cursor: borrar la vieja + dibujar la nueva, siempre dentro de la
        // MISMA superficie (prevContent, no el cursor global que puede ser
        // del modal). Block fijo: solo si cambió; Bar (parpadeo): su fila
        // siempre (1 fila/frame en inserción vs N del full). En chrome no
        // marca contenido (lo cubre paintChrome).
        const bool cursorChanged =
            !havePrevContent || prevContent.visible != f.cursor.visible ||
            prevContent.shape != f.cursor.shape ||
            !(prevContent.cell == f.cursor.cell);
        if (cursorChanged) {
            if (havePrevContent) markCursorRow(dirty, f.layout, prevContent);
            markCursorRow(dirty, f.layout, f.cursor);
        } else if (f.cursor.visible &&
                   f.cursor.shape == FrameCursorShape::Bar &&
                   f.cursor.cell.valid()) {
            markCursorRow(dirty, f.layout, f.cursor);
        }
        for (char d : dirty) (d ? repainted : skipped) += 1;
    }
    // Blink en el chrome (prompts con input: el Bar vive en el MessageBar):
    // el chrome estructural puede estar limpio pero hay que repintarlo para
    // animar/borrar la barra. No contamina lastChromeDirty_ (estructural).
    // P2: el chrome se repinta si CAMBIÓ (chromeDirty) o si hay que
    // dibujar/borrar una barra en él, mirando AMBOS cursores. Si el anterior
    // era barra visible en el chrome y algo cambió (posición, forma o
    // visibilidad), hay que restaurar fondo+texto aunque el nuevo ya no sea
    // barra (si no, queda una barra residual). Si el nuevo sigue siendo
    // barra en el chrome se repinta cada frame para animarla.
    bool paintChrome = chromeDirty;
    if (!full) {
        const bool newBar = isChromeBar(f.cursor, f.layout);
        const bool oldBar =
            havePrevContent && isChromeBar(prevContent, f.layout);
        const bool cursorChanged =
            !havePrevContent || prevContent.visible != f.cursor.visible ||
            prevContent.shape != f.cursor.shape ||
            !(prevContent.cell == f.cursor.cell);
        if (newBar || (oldBar && cursorChanged)) paintChrome = true;
    }
    noteStats(full, repainted, skipped, chromeDirty);
    contentChrome_ = f.chrome;
    contentAccent_ = f.statusAccent;
    contentLayout_ = f.layout;
    contentDark_ = dark_;
    contentCellW_ = cw;
    contentCellH_ = ch;
    contentCursor_ = f.cursor;
    contentHasCache_ = true;
    lastIsList_ = false;
    // Estado de validación incremental (ver contentTop_): lo publica el camino
    // completo y lo consumen/rotan los caminos rápidos. Sin esto el fast path
    // nunca podría validar sus precondiciones.
    contentTop_ = viewport.top;
    contentLeft_ = viewport.left;
    contentVpWidth_ = viewport.width;
    contentVpHeight_ = viewport.height;
    contentLineCount_ = doc.lineCount();
    contentFilename_ = filename;
    contentModified_ = modified;
    contentDocVersion_ = doc.version();
    contentDocInstance_ = doc.instanceId();
    contentCursorLine_ = cursor.line;
    contentSelection_ = selection;
    contentSearch_ = searchHighlight;
    contentBracket_ = bracketPair;
    contentCursorInMsg_ = chrome.cursorInMessageBar;
#ifdef HAVE_SDL2
    if (sdlRenderer_) {
        SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
        const GuiColor bg = guichrome::background(dark_);
        // P1: el parcial pinta sobre el framebuffer persistente (las filas
        // limpias conservan sus píxeles AHÍ, no en el backbuffer). Después se
        // compone a ventana con clear+copia+Present: el backbuffer nunca se lee.
        int wPx = 0, hPx = 0;
        bool fbFresh = false;
        SDL_Texture* fb = nullptr;
        if (framePxSize(f.layout, font_.cellW(), font_.cellH(), wPx, hPx)) {
            bool fr = false;
            fb = static_cast<SDL_Texture*>(acquireFb(
                contentFb_[contentFbActive_], sdlRenderer_, wPx, hPx, fr));
            fbFresh = (fb != nullptr) && fr;
        }
        if (!fb) {
            // Fallback sin render targets: repintado COMPLETO directo en
            // ventana (el parcial no es seguro sin superficie persistente).
            SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
            SDL_RenderClear(r);
            paintFrameContent(r, font_, dark_, f.layout, f);
            paintChromeAndCursor(r, font_, dark_, f.layout, f.chrome,
                                 f.statusAccent, f.cursor, shown, &f, nullptr,
                                 -1);
            // Misma geometría de MessageBar que el camino con framebuffer
            // (target de ventana activo): sobre-dibuja la banda centrada.
            paintMessageBarOverlay(r, font_, dark_, f.layout, f.chrome,
                                   f.cursor, shown, ch);
            SDL_RenderPresent(r);
        } else {
            SDL_SetRenderTarget(r, fb);
            if (full || fbFresh) {
                SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
                SDL_RenderClear(r);
                paintFrameContent(r, font_, dark_, f.layout, f);
                paintChromeAndCursor(r, font_, dark_, f.layout, f.chrome,
                                     f.statusAccent, f.cursor, shown, &f,
                                     nullptr, -1);
            } else {
                // Parcial sobre la textura: sin RenderClear; solo filas dirty
                // + chrome si toca + cursor. Lee `fresh` (aún no movido).
                for (int i = 0; i < contentH; ++i)
                    if (dirty[static_cast<size_t>(i)])
                        paintSnapshotRow(r, font_, dark_, f.layout, i,
                                         fresh[static_cast<size_t>(i)]);
                if (paintChrome)
                    paintChromeOnly(r, font_, dark_, f.layout, f.chrome,
                                    f.statusAccent);
                paintCursorOnly(r, font_, dark_, f.layout, f.cursor, shown,
                                &f, nullptr, -1);
            }
            presentFramebuf(r, font_, fb, bg, f.layout, ch, dark_, f.chrome,
                            f.cursor, shown);
        }
    }
#else
    // Sin SDL2: no-op (el loop nunca llega acá, main avisa antes).
#endif
    // P2-copias: `fresh` sirvió al diff y al pintado; recién acá se publica
    // el snapshot de observabilidad (ÚNICA copia profunda) y se transfiere
    // al caché por movimiento. Todos los caminos lo guardan (con/sin SDL).
    lastContentRows_ = fresh;
    contentRows_ = std::move(fresh);
}

void GuiRenderer::renderListCommon(std::vector<ListLine> lines,
                                       const ChromeData& chrome,
                                       StyleRole accent, const Layout& layout,
                                       const FrameCursor& next, int selectedRow,
                                       int selSnapshot, int scrollSnapshot) {
    const bool havePrevList = listHasCache_;
    const FrameCursor prevList = listCursor_;
    lastChrome_ = chrome;
    lastAccent_ = accent;
    lastLayout_ = layout;
    lastContentRows_.clear();
    // P2-copias: `lines` (parámetro por valor) se conserva hasta terminar el
    // pintado; el snapshot y el move al caché van al final (ver abajo).
    lastListSelected_ = selSnapshot;
    lastListScroll_ = scrollSnapshot;
    noteCursorForBlink(next);
    lastCursor_ = next;
    const bool shown = cursorShown();

    const int listH = static_cast<int>(lines.size());
    const int lcw = font_.cellW();
    const int lch = font_.cellH();
    const bool chromeSame = listHasCache_ && listChrome_ == chrome &&
                            listAccent_ == accent;
    const bool layoutSame =
        listHasCache_ && sameLayout(listLayout_, layout);
    // P2-resize: igual que en el contenido (ver renderScreenDiff).
    const bool listMetricsSame = listHasCache_ && listCellW_ == lcw &&
                                 listCellH_ == lch;
    const bool full = !listHasCache_ || !layoutSame || listDark_ != dark_ ||
                      !listMetricsSame;
    bool chromeDirty = full || !chromeSame;
    std::vector<char> dirty(static_cast<size_t>(std::max(0, listH)), 0);
    int repainted = 0, skipped = 0;
    if (full) {
        std::fill(dirty.begin(), dirty.end(), 1);
        repainted = listH;
    } else {
        for (int i = 0; i < listH; ++i) {
            bool same = i < static_cast<int>(listLines_.size()) &&
                        listLines_[static_cast<size_t>(i)] == lines[static_cast<size_t>(i)];
            if (same && i != listSelectedRow_ && i != selectedRow)
                continue;  // ni texto ni destacado cambiaron
            if (same && i == listSelectedRow_ && i == selectedRow) continue;
            dirty[static_cast<size_t>(i)] = 1;
        }
        // Cursor Block fijo (abrir/buffers): borrar la vieja + dibujar la
        // nueva dentro de la MISMA superficie (prevList). Bar (SaveAs, en el
        // chrome): su animación la cubre paintChrome de abajo.
        const bool listCursorChanged =
            !havePrevList || prevList.visible != next.visible ||
            prevList.shape != next.shape || !(prevList.cell == next.cell);
        if (next.shape == FrameCursorShape::Block && listCursorChanged) {
            if (havePrevList) markCursorRow(dirty, layout, prevList);
            markCursorRow(dirty, layout, next);
        }
        for (char d : dirty) (d ? repainted : skipped) += 1;
    }
    // P2: igual que en el contenido (ver renderScreenDiff): con el cursor
    // ANTERIOR y el nuevo. Vale para SaveAs, cuya barra vive en el chrome.
    bool paintChrome = chromeDirty;
    if (!full) {
        const bool newBar = isChromeBar(next, layout);
        const bool oldBar =
            havePrevList && isChromeBar(prevList, layout);
        const bool listCursorMoved =
            !havePrevList || prevList.visible != next.visible ||
            prevList.shape != next.shape || !(prevList.cell == next.cell);
        if (newBar || (oldBar && listCursorMoved)) paintChrome = true;
    }
    noteStats(full, repainted, skipped, chromeDirty);
    listChrome_ = chrome;
    listAccent_ = accent;
    listLayout_ = layout;
    listDark_ = dark_;
    listCellW_ = lcw;
    listCellH_ = lch;
    listSelectedRow_ = selectedRow;
    listCursor_ = next;
    listHasCache_ = true;
    lastIsList_ = true;
#ifdef HAVE_SDL2
    if (sdlRenderer_) {
        SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer_);
        const GuiColor bg = guichrome::background(dark_);
        // P1: igual que el contenido (ver renderScreenDiff): el parcial pinta
        // sobre el framebuffer persistente y compone con clear+copia+Present.
        // Lee `lines` (aún no movido).
        int wPx = 0, hPx = 0;
        bool fbFresh = false;
        SDL_Texture* fb = nullptr;
        if (framePxSize(layout, font_.cellW(), font_.cellH(), wPx, hPx)) {
            bool fr = false;
            fb = static_cast<SDL_Texture*>(
                acquireFb(listFb_, sdlRenderer_, wPx, hPx, fr));
            fbFresh = (fb != nullptr) && fr;
        }
        if (!fb) {
            // Fallback sin render targets: repintado COMPLETO directo en
            // ventana (el parcial no es seguro sin superficie persistente).
            SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
            SDL_RenderClear(r);
            paintListContent(r, font_, dark_, layout, lines, selectedRow);
            paintChromeAndCursor(r, font_, dark_, layout, lastChrome_,
                                 lastAccent_, lastCursor_, shown, nullptr,
                                 &lines, selectedRow);
            paintMessageBarOverlay(r, font_, dark_, layout, lastChrome_,
                                   lastCursor_, shown, lch);
            SDL_RenderPresent(r);
        } else {
            SDL_SetRenderTarget(r, fb);
            if (full || fbFresh) {
                SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
                SDL_RenderClear(r);
                paintListContent(r, font_, dark_, layout, lines, selectedRow);
                paintChromeAndCursor(r, font_, dark_, layout, lastChrome_,
                                     lastAccent_, lastCursor_, shown, nullptr,
                                     &lines, selectedRow);
            } else {
                for (int i = 0; i < listH; ++i)
                    if (dirty[static_cast<size_t>(i)])
                        paintSnapshotListRow(r, font_, dark_, layout, i,
                                             lines[static_cast<size_t>(i)],
                                             i == selectedRow);
                if (paintChrome)
                    paintChromeOnly(r, font_, dark_, layout, lastChrome_,
                                    lastAccent_);
                paintCursorOnly(r, font_, dark_, layout, lastCursor_, shown,
                                nullptr, &lines, selectedRow);
            }
            presentFramebuf(r, font_, fb, bg, layout, lch, dark_, lastChrome_,
                            lastCursor_, shown);
        }
    }
#else
    // Sin SDL2: no-op.
#endif
    // P2-copias: `lines` sirvió al diff y al pintado; recién acá se publica
    // el snapshot de observabilidad (ÚNICA copia profunda) y se transfiere
    // al caché por movimiento. Todos los caminos lo guardan (con/sin SDL).
    lastListLines_ = lines;
    listLines_ = std::move(lines);
}

void GuiRenderer::renderBufferList(const std::vector<std::string>& names,
                                   int selected, int width, int height,
                                   Sink& sink) {
    (void)sink;
    const Layout layout = frameBuilder_.calculateLayout(height, width);
    // Cursor en la lista (igual que TTY: fila selected, col 0).
    // Defensivo: ante args fuera de contrato no se pinta un cursor
    // arbitrario (se oculta). En rango válido es idéntico a TTY.
    // El snapshot publica el mismo contrato: -1 si vacío/inválido.
    const int total = static_cast<int>(names.size());
    const bool ok = total > 0 && selected >= 0 && selected < total &&
                    selected < layout.content.height;
    const int selectedRow = ok ? selected : -1;
    FrameCursor next;
    if (ok) {
        next.visible = true;
        next.shape = FrameCursorShape::Block;
        next.cell = CellPos(layout.content.col, layout.content.row + selected);
    } else {
        next.visible = false;
        next.cell = CellPos{};
    }
    renderListCommon(buildBufferListLines(names, layout.content.height),
                     bufferListChrome(names, selected),
                     StyleRole::AccentBuffers, layout, next, selectedRow,
                     ok ? selected : -1, 0);
}

void GuiRenderer::renderFileList(const std::vector<FileListItem>& items,
                                 int selected, int scroll,
                                 const std::string& path,
                                 const MessageBarData& message, int width,
                                 int height, Sink& sink) {
    (void)sink;
    const Layout layout = frameBuilder_.calculateLayout(height, width);
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
    renderListCommon(
        buildFileListLines(items, scroll, layout.content.height),
        fileListChrome(path, message, items, selected, scroll, "ABRIR ARCHIVO"),
        StyleRole::AccentAbrir, layout, next, selectedRow, ok ? selected : -1,
        scroll);
}

void GuiRenderer::renderSaveAsFileList(
    const std::vector<FileListItem>& items, int selected, int scroll,
    const std::string& path, const MessageBarData& message, int width,
    int height, Sink& sink) {
    (void)sink;
    const Layout layout = frameBuilder_.calculateLayout(height, width);
    // SaveAs pinta la MISMA lista que abrir (solo cambian etiqueta/accent
    // y el cursor al MessageBar): el snapshot comparte constructor.
    // La fila destacada sigue siendo la navegada (selected-scroll), aunque
    // el cursor parpadee en el input: igual que TTY (fondo ListSelected).
    const int total = static_cast<int>(items.size());
    const int row = selected - scroll;
    const bool okRow = total > 0 && selected >= 0 && selected < total &&
                       scroll >= 0 && scroll <= total && row >= 0 &&
                       row < layout.content.height;
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
    renderListCommon(
        buildFileListLines(items, scroll, layout.content.height),
        fileListChrome(path, message, items, selected, scroll, "GUARDAR COMO"),
        StyleRole::AccentGuardar, layout, next, selectedRow,
        okRow ? selected : -1, scroll);
}
