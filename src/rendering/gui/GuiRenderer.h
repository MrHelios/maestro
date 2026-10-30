#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "document/Cursor.h"
#include "document/Document.h"
#include "document/Selection.h"
#include "layout/BracketMatcher.h"
#include "layout/Viewport.h"
#include "rendering/ChromeData.h"
#include "rendering/ChromeRequest.h"
#include "rendering/ListLines.h"
#include "rendering/ScreenRenderer.h"
#include "rendering/Sink.h"
#include "rendering/frame/Frame.h"
#include "rendering/frame/FrameBuilder.h"
#include "rendering/gui/GuiChrome.h"
#include "platform/gui/GuiOptions.h"
#include "rendering/gui/GuiFont.h"
#include "rendering/gui/GuiChromePixels.h"
#include "syntax/SyntaxCache.h"

// ---------------------------------------------------------------------------
// GuiRenderer: backend SDL2 del puerto neutro ScreenRenderer.
//
// Chrome: StatusBar (fila superior) + MessageBar (fila inferior) con el MISMO
// DTO y posicion de cursor que TTY (FrameBuilder + chrome::
// messageBarCursorCell). El cursor de los 4 prompts con input (Busqueda /
// IrAFila / SaveAs / Renombrar) va DENTRO del input del MessageBar
// (msg.cursor, antes de decoraciones), nunca al final ni en la lista.
// Contenido: filas del Frame (gutter + sintaxis + Selection, que fusiona
// selección y highlight de búsqueda + BracketMatch + fondo de línea actual)
// y listas modales (BufferSelector / FileBrowser / SaveAs) con la misma
// composición que TTY ("  nombre", "/" en carpetas, "~" de relleno).
// Sin SDL los render* son no-op seguros pero dejan snapshot testeable
// (lastContentRows_/lastListLines_).
// ---------------------------------------------------------------------------
// Snapshot testeable sin SDL: una fila de contenido como texto+roles.
// Es el DTO que la GUI pinta (segmentos del Frame) y lo que los tests
// afirman sin ventana: sintaxis, Selection (selección + highlight de
// búsqueda, fusionados en FrameBuilder), BracketMatch y CurrentLine.
struct GuiContentSeg {
    std::string text;
    StyleRole role = StyleRole::Default;
    bool operator==(const GuiContentSeg& o) const {
        return role == o.role && text == o.text;
    }
    bool operator!=(const GuiContentSeg& o) const { return !(*this == o); }
};
struct GuiContentRow {
    std::vector<GuiContentSeg> segs;
    bool isCurrentLine = false;
    bool operator==(const GuiContentRow& o) const {
        return isCurrentLine == o.isCurrentLine && segs == o.segs;
    }
    bool operator!=(const GuiContentRow& o) const { return !(*this == o); }
    std::string plain() const {
        std::string out;
        for (const auto& s : segs) out += s.text;
        return out;
    }
    bool hasRole(StyleRole r) const {
        for (const auto& s : segs)
            if (s.role == r) return true;
        return false;
    }
};

// Las líneas visibles de los modales son rendering::ListLine
// (ver rendering/ListLines.h: composición compartida con TTY).

// Rectángulo en píxeles de la celda del cursor (para SDL + tests sin SDL).
// `valid=false` si no hay cursor lógico (invisible o celda inválida).
struct GuiPixelRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool valid = false;
};

struct GuiScrollInfo {
    int top = 0;
    int visible = 0;
    int total = 0;
};

struct GuiScrollbarPress {
    bool handled = false;
    bool onThumb = false;
    int top = 0;
    int grabOffsetPx = 0;
};

#ifdef HAVE_SDL2
struct SDL_Renderer;
struct SDL_Texture;
#endif

class GuiTitleBar;

class GuiRenderer : public ScreenRenderer {
public:
    explicit GuiRenderer(const GuiOptions& opts = GuiOptions{});
    ~GuiRenderer() override;

    // Caché diferencial por línea (paridad conceptual con TtyDiff en TTY,
    // adaptada a SDL inmediato): cada render* compara el Frame fresco contra
    // el último pintado y repinta solo las filas que cambiaron, sin
    // SDL_RenderClear. El próximo render tras invalidateCache/toggleTheme/
    // cambio de geometría es completo. Sin SDL el pintado se salta pero la
    // comparación y las stats se calculan igual (tests sin ventana).
    void invalidateCache() override;
    bool hasCache() const { return contentHasCache_ || listHasCache_; }
    bool hasContentCache() const { return contentHasCache_; }
    bool hasListCache() const { return listHasCache_; }

    // Observabilidad del diferencial (para tests/benchmarks, solo lectura):
    //   lastWasFull      -> el último render* fue completo (sin caché válida
    //                       o cambio de geometría/tema).
    //   lastRepainted    -> filas de contenido/modal repintadas en el último
    //                       render* (0 en steady sin cambios).
    //   lastSkipped      -> filas reutilizadas del caché en el último render*.
    //   lastChromeDirty  -> el chrome cambió en el último render*.
    //   Totales acumulados + frames para benchmarks GUI vs TTY.
    bool lastWasFull() const { return lastWasFull_; }
    int lastRepainted() const { return lastRepainted_; }
    int lastSkipped() const { return lastSkipped_; }
    bool lastChromeDirty() const { return lastChromeDirty_; }
    long long totalFrames() const { return totalFrames_; }
    long long totalFull() const { return totalFull_; }
    long long totalRepainted() const { return totalRepainted_; }
    long long totalSkipped() const { return totalSkipped_; }

    // Solo tests/debug: píxeles del framebuffer persistente de la ÚLTIMA
    // superficie renderizada (contenido o modal, según el último render*).
    // A diferencia de leer el backbuffer tras Present (no portable: SDL2 lo
    // declara invalidado y nada garantiza su contenido), la textura de
    // destino SÍ persiste, así que esta lectura no depende del backbuffer.
    // No altera el destino activo del renderer (lo guarda y restaura).
    // Devuelve false sin SDL/renderer, sin framebuffer (p. ej. fallback sin
    // render targets o sin ningún render previo) o si ReadPixels falla.
    bool readLastSurfacePixels(std::vector<GuiColor>& out, int& wPx,
                               int& hPx);
    // Activa/desactiva la captura del frame de ventana. Pensado para tests:
    // en producción queda en false y no hay lectura de píxeles por frame.
    void setCaptureEnabled(bool on) { captureEnabled_ = on; if (!on) lastCaptureValid_ = false; }

    // Devuelve el snapshot del último frame, capturado tras pintar el overlay
    // y ANTES de SDL_RenderPresent(). Requiere setCaptureEnabled(true) y
    // haber renderizado al menos un frame. Devuelve false si no hay snapshot.
    bool readWindowPixels(std::vector<GuiColor>& out, int& wPx,
                          int& hPx);
    bool readWindowPixels(std::vector<GuiColor>& out) {
        int w = 0, h = 0;
        return readWindowPixels(out, w, h);
    }
    void setExternalSyntaxCache(SyntaxCache* c) override {
        frameBuilder_.setExternalSyntaxCache(c);
    }
    void setWrapIndex(const softwrap::Index* idx) override {
        wrapIndex_ = idx;
    }

    // Solo tests: fuerza a fallar la adquisición de framebuffer número
    // `skips` (0-based) contada desde la próxima llamada a acquireFb.
    // `skips == 0` hace fallar la inmediata; `skips == 1` deja pasar una y
    // hace fallar la siguiente. Se consume una vez. `-1` desactiva.
    // Ejercita las rutas de recuperación (fallback sin superficie o
    // invalidación de caché) sin depender de un OOM real.
    void setFramebufAcquireFailAfterForTests(int skips);
    // Solo tests: destruye las dos superficies persistentes de contenido
    // (como si el renderer/tamaño las hubiera invalidado) para forzar su
    // recreación en el próximo render.
    void dropContentFramebuffersForTests();

    // Solo tests/observabilidad: cuántas veces se invocó el pintor único de
    // chrome (paintWindowChromeOverlays) y cuántas veces se pintó el cursor
    // Bar de la MessageBar desde el overlay. La invariante de composición es
    // exactamente 1 por frame compuesto; un píxel correcto no la demuestra
    // (el segundo pase puede tapar al primero).
    int chromePaintCalls() const { return chromePaintCalls_; }
    int messageBarCursorPaints() const { return messageBarCursorPaints_; }
    void resetChromePaintCountersForTests() const {
        chromePaintCalls_ = 0;
        messageBarCursorPaints_ = 0;
    }

    // Tema claro/oscuro: conmuta `dark_` y libera el caché de texturas
    // (ahorro de memoria, no corrección: el color ya va en la clave de
    // GuiFont, ver GuiFont.h). ADEMÁS invalida el caché diferencial (los
    // fondos RGB cambian: ninguna fila cacheada sigue válida). El próximo
    // frame tras el toggle es completo (RenderClear + todo), igual que tras
    // invalidateCache().
    // Los snapshots last* solo son observabilidad para tests, nunca deciden
    // qué pintar.
    void toggleTheme() override;
    bool isDarkTheme() const override { return dark_; }

    // Frame puro del editor (mismo que TTY: geometria + chrome + cursor).
    // Publico para tests sin SDL: verifica StatusBar/MessageBar y que el
    // cursor de los prompts caiga dentro del input del MessageBar.
    // No-const a propósito (igual que TtyRenderer::buildScreen): el
    // FrameBuilder muta sus cachés internos al construir.
    Frame buildFrame(const Document& doc, const Cursor& cursor,
                     const Viewport& viewport, const std::string& filename,
                     bool modified, const ChromeRequest& chrome,
                     const std::optional<Selection>& selection = std::nullopt,
                     const std::optional<Selection>& searchHighlight = std::nullopt,
                     const std::optional<BracketPair>& bracketPair = std::nullopt,
                     const softwrap::Index* wrapIndex = nullptr);

    // ChromeData de las pantallas de listas (misma composicion que
    // TtyRenderer: nombre/estado/right + MessageBar). Publico para tests.
    ChromeData bufferListChrome(const std::vector<std::string>& names,
                                int selected) const;
    ChromeData fileListChrome(const std::string& path,
                              const MessageBarData& message,
                              const std::vector<FileListItem>& items,
                              int selected, int scroll,
                              const char* estado) const;

    // El loop GUI inyecta el SDL_Renderer* real (no-owned). Sin inyectar,
    // los render* son no-op seguros (útil en tests sin ventana).
    void setSdlRenderer(void* r);
    // Phase 2: barra de título propia. El loop reserva `topInset` píxeles
    // del drawable (la barra), reduce la grilla y el renderer desplaza el
    // framebuffer hacia abajo, pinta la MessageBar sobre esa base y dibuja
    // la barra en la franja superior antes de Present. Sin titleBar o con
    // inset 0 el comportamiento es idéntico al de las decoraciones nativas.
    void setTitleBar(GuiTitleBar* titleBar) { titleBar_ = titleBar; }
    void setContentTopInset(int px) {
        contentTopInset_ = px > 0 ? px : 0;
    }
    int contentTopInset() const { return contentTopInset_; }
    // Variante completa: inyecta renderer + abre la fuente y mide la celda.
    // Debe llamarse tras crear el SDL_Renderer y antes del primer resize.
    // `fontPixels` son PÍXELES DEL DRAWABLE (el loop ya escaló la base en
    // píxeles lógicos con guiScaledFontPixels; a escala 1 coinciden).
    // Devuelve true si la inicialización fue exitosa (fuente cargada o fallback).
    // Devuelve false si se solicitó una fuente explícita (--font) y falló al abrirla.
    bool initForRenderer(void* r, int fontPixels = 16);
    // Asegura la talla de fuente escalada por HiDPI (contrato visual-estable:
    // ver guiScaledFontPixels en GuiSdlMapper.h). Reabre SOLO si la talla
    // deseada difiere de la actual (o si no hay fuente): los resizes que no
    // cambian de escala no pagan recarga ni vacían el caché de texturas.
    // Respeta --font (loadWithPath) cuando hay ruta explícita; si no, usa
    // los candidatos del sistema. `desiredPixels <= 0` es no-op. Sin fuente
    // disponible (load falla) es no-op seguro: el loop usa el fallback
    // escalado por eje.
    void ensureFontPixels(int desiredPixels);
    int fontPixels() const { return font_.pixelSize(); }

    // Métricas de celda en píxeles (de la fuente; fallback canónico si no hay).
    // El loop las usa para rows/cols = píxeles/celda.
    int cellW() const { return font_.cellW(); }
    int cellH() const { return font_.cellH(); }
    bool hasFont() const { return font_.ok(); }
    const std::string& fontPath() const { return font_.path(); }
    // Observabilidad del caché de texturas (passthrough a GuiFont, ver
    // GuiFont.h): entradas vivas, tope, hits/misses/evicciones acumulados.
    // Sirve para medir el raster de texto por frame (misses) sin profiler.
    size_t fontCacheSize() const { return font_.cacheSize(); }
    size_t fontMaxCacheSize() const { return font_.maxCacheSize(); }
    unsigned long long fontCacheHits() const { return font_.cacheHits(); }
    unsigned long long fontCacheMisses() const { return font_.cacheMisses(); }
    unsigned long long fontCacheEvictions() const {
        return font_.cacheEvictions();
    }
    // Ancho en píxeles de `text` con la fuente actual (0 sin fuente).
    // Expone GuiFont::textWidth para tests (p. ej. verificar que la fuente
    // avanza un carácter ancho 2 celdas antes de afirmar su pintado).
    // string_view con copia segura a NUL-terminado dentro de GuiFont.
    int textWidthPx(std::string_view text) const {
        return font_.textWidth(text);
    }

    // Ultimo frame calculado por los render* (para tests sin SDL: sin
    // ventana no hay pixeles que afirmar, pero si DTO + cursor).
    const ChromeData& lastChrome() const { return lastChrome_; }
    StyleRole lastAccent() const { return lastAccent_; }
    FrameCursor lastCursor() const { return lastCursor_; }
    Layout lastLayout() const { return lastLayout_; }
    // Contenido testeable sin SDL:
    //   - renderScreenDiff deja las filas del Frame (texto + rol por
    //     segmento, con Selection = selección o highlight de búsqueda).
    //   - renderBufferList/FileList/SaveAs dejan las líneas visibles del
    //     modal (exactamente content.height entradas ListLine: items +
    //     relleno con filler=true; FileBrowser respeta scroll,
    //     BufferSelector desde 0). Un archivo real llamado "~" llega con
    //     filler=false aunque su texto sea idéntico al del relleno.
    //   - lastListSelected: índice seleccionado tal cual lo recibió (-1 si
    //     vacío/inválido para no afirmar un cursor oculto).
    const std::vector<GuiContentRow>& lastContentRows() const {
        return lastContentRows_;
    }
    const std::vector<ListLine>& lastListLines() const {
        return lastListLines_;
    }
    int lastListSelected() const { return lastListSelected_; }
    int lastListScroll() const { return lastListScroll_; }

    // Cursor GUI: Bloque (navegación) fijo / Barra (inserción) con parpadeo.
    // Política: shape Bar parpadea (Interacción + 4 prompts con input, que
    // ya vienen como Bar desde cursorShapeFor); shape Block siempre visible.
    // CONTRATO DE RELOJ: el propietario debe llamar setBlinkNow(now) antes
    // de CADA render; el renderer nunca lee el reloj interno para la fase.
    // Sin ninguna inyección la fase es ON. Tras la primera inyección la
    // fase deriva solo del último tiempo inyectado: si el propietario deja
    // de inyectar, el parpadeo queda congelado (bug del propietario).
    // Reutilizar el renderer en otro contexto exige seguir inyectando.
    void setBlinkNow(std::chrono::steady_clock::time_point now);
    // Marca actividad de cursor DENTRO del lote de eventos aunque la celda
    // final coincida con la del último render (AvPág + RePág, etc.). Fuerza
    // el reset del ancla de blink en el próximo render; no pinta por sí
    // solo. El loop GUI la llama una vez por lote si detectó movimiento.
    void markCursorActivity();
    // Fase del parpadeo: true = pintar cursor. Block siempre true.
    bool blinkPhaseOn() const;
    // Ms restantes hasta el próximo cambio de fase ON<->OFF, medidos desde
    // `now` contra el ancla actual. Sin valor = no hay animación pendiente
    // (cursor Block, cursor invisible, o aún sin primer render con tiempo
    // inyectado). Puro y sin SDL: el loop lo usa como deadline de su espera
    // idle (despertar en el cambio de fase, no muestrear a cadencia fija).
    // Comparte la fase con blinkPhaseOn() vía blinkPhaseForElapsedMs: un
    // cambio de constantes debe hacerse ahí, no en cada caller.
    std::optional<int> msUntilBlinkToggle(
        std::chrono::steady_clock::time_point now) const;
    // Visibilidad efectiva para pintar: visible lógico AND fase.
    bool cursorShown() const;
    // Geometría lógica del cursor (dónde se dibujaría: Bar = 2px de ancho,
    // Block = celda). Independiente de la fase de blink: es válida aunque
    // la fase esté OFF; la visibilidad efectiva para pintar la da
    // cursorShown(). No confundir con imeRectPx() (celda completa lógica
    // para la candidata IME).
    GuiPixelRect cursorPixelRect() const;
    // Ancla IME (SDL_SetTextInputRect): celda completa del cursor lógico,
    // aunque el blink lo tenga apagado (la ventana candidata sigue al
    // cursor lógico, no a la fase visible). Si no hay cursor válido
    // devuelve valid=false y el caller (GuiRunLoop::updateImeRect)
    // conserva a propósito el último rect de SDL (ver su política).
    GuiPixelRect imeRectPx() const;

    void renderScreenDiff(const Document& doc,
                          const Cursor& cursor,
                          const Viewport& viewport,
                          const std::string& filename,
                          bool modified,
                          const ChromeRequest& chrome,
                          Sink& sink,
                          const std::optional<Selection>& selection,
                          const std::optional<Selection>& searchHighlight,
                          const std::optional<BracketPair>& bracketPair) override;

    void renderBufferList(const std::vector<std::string>& names,
                          int selected,
                          int width,
                          int height,
                          Sink& sink) override;

    void renderFileList(const std::vector<FileListItem>& items,
                        int selected,
                        int scroll,
                        const std::string& path,
                        const MessageBarData& message,
                        int width,
                        int height,
                        Sink& sink) override;

    void renderSaveAsFileList(const std::vector<FileListItem>& items,
                              int selected,
                              int scroll,
                              const std::string& path,
                              const MessageBarData& message,
                              int width,
                              int height,
                              Sink& sink) override;

    void setScrollInfo(GuiScrollInfo info);
    GuiScrollbarPress scrollbarPress(int pxX, int pxY);
    int scrollbarTopFromY(int pxY, int grabOffsetPx) const;
    bool scrollbarEnabled() const;
    const GuiScrollInfo& scrollInfo() const { return scrollInfo_; }
    // Debug: geometría del scrollbar para diagnóstico
    // `resizeBorderPx`: margen de redimensionado en píxeles del drawable
// (a HiDPI 2×, 8 puntos = 16 px). 0 = sin margen (tests sin SDL).
GuiPixelRect scrollbarTrackRect(const Layout& layout, int resizeBorderPx = 0) const;
    GuiPixelRect scrollbarThumbRect(const Layout& layout) const;
    int scrollbarThumbHeightPx(int trackH) const;
    int scrollbarMaxTop() const;

private:
    GuiOptions options_;
    GuiScrollInfo scrollInfo_;
    bool dark_ = true;
    FrameBuilder frameBuilder_;
    // SDL_Renderer* opaco (solo se toca en el .cpp bajo HAVE_SDL2).
    void* sdlRenderer_ = nullptr;
    GuiFont font_;
    // Barra de título propia (no-owned) y alto reservado en píxeles del
    // drawable. Ver setTitleBar/setContentTopInset.
    GuiTitleBar* titleBar_ = nullptr;
    int contentTopInset_ = 0;

    // Ultimo chrome/cursor calculados (para inspeccion en tests sin SDL y
    // para no recalcular en el pintado cuando hay SDL).
    mutable ChromeData lastChrome_;
    mutable StyleRole lastAccent_ = StyleRole::StatusAccentDefault;
    mutable FrameCursor lastCursor_;
    mutable Layout lastLayout_;
    // Snapshots puros (ver getters): se rellenan SIEMPRE, con o sin SDL.
    mutable std::vector<GuiContentRow> lastContentRows_;
    mutable std::vector<ListLine> lastListLines_;
    mutable int lastListSelected_ = -1;
    mutable int lastListScroll_ = 0;
    // Parpadeo del cursor (solo Bar): ancla = último cambio de celda/forma
    // medido en el tiempo inyectado por el propietario (ver contrato en
    // setBlinkNow). Sin inyección no hay ancla y la fase es ON.
    mutable std::chrono::steady_clock::time_point blinkAnchor_{};
    mutable std::chrono::steady_clock::time_point blinkNow_{};
    mutable bool blinkAnchorSet_ = false;
    mutable bool blinkNowSet_ = false;
    // Actividad intermedia del lote pendiente de consumir por
    // noteCursorForBlink (ver markCursorActivity).
    mutable bool cursorActivity_ = false;
    static constexpr int kBlinkOnMs = 530;
    static constexpr int kBlinkPeriodMs = 1060;
    void noteCursorForBlink(const FrameCursor& next) const;
    // Lógica de fase compartida por blinkPhaseOn() y msUntilBlinkToggle():
    // dado el tiempo transcurrido (>= 0) desde el ancla, devuelve
    // {faseEncendida, ms restantes hasta el próximo cambio de fase}.
    // El resto es 1..530: el borde exacto (rest == 0) devuelve 1 para que
    // un deadline ya vencido no genere una espera de 0 ms (busy-spin); el
    // borde se resuelve en el render inmediato.
    static std::pair<bool, int> blinkPhaseForElapsedMs(long elapsedMs);

    // --- Caché diferencial por línea (ver invalidateCache) ---
    // Contenido del editor y modales tienen cachés SEPARADOS: el Editor
    // intercala editor <-> modal y una sola entrada haría thrash (cada
    // cambio de pantalla invalidaría a la otra). Cada caché guarda lo que
    // decide el repintado: filas + chrome + accent + geometría + tema.
    // La igualdad de filas es estructural (texto+rol+línea actual): el DTO
    // solo lleva estado visual, igual que ChromeData::operator== en TTY.
    // NOTA DE SCROLL (honesta): sin blit GPU, un scroll vertical debe
    // repintar las N filas físicas (los píxeles no se desplazan solos).
    // Con render targets, el scroll ±1 usa scroll-blit (ping-pong entre las
    // dos superficies de contenido: copia desplazada + filas dirty), con lo
    // que el repintado también es O(1). Sin render targets cae al repintado
    // desde caché (todas las filas, con hits de texturas). La CONSTRUCCIÓN
    // es O(1) en ambos casos (tryFastScrollLine). TTY evita además el
    // repintado con scroll de región (TtyScroll.h).
    mutable bool contentHasCache_ = false;
    mutable std::vector<GuiContentRow> contentRows_;
    mutable ChromeData contentChrome_;
    mutable StyleRole contentAccent_ = StyleRole::StatusAccentDefault;
    mutable Layout contentLayout_{};
    mutable bool contentDark_ = true;
    // Métricas físicas (píxeles por celda) con las que se pintó el caché.
    // P2-resize: la validez NO es solo la grilla en celdas (sameLayout). Un
    // resize o cambio de escala HiDPI puede dejar las mismas filas/columnas
    // con otra superficie en píxeles; Editor::resize no invalida a propósito
    // (conserva el fast path TTY al mismo tamaño) y el loop solo re-escala
    // la fuente, así que el renderer lo detecta solo: si la celda cambió, el
    // frame es completo aunque la grilla coincida. (El framebuffer también
    // se recrea por tamaño, pero la decisión del diff debe ser explícita.)
    mutable int contentCellW_ = 0;
    mutable int contentCellH_ = 0;
    mutable bool listHasCache_ = false;
    mutable std::vector<ListLine> listLines_;
    mutable ChromeData listChrome_;
    mutable StyleRole listAccent_ = StyleRole::StatusAccentDefault;
    mutable Layout listLayout_{};
    mutable bool listDark_ = true;
    mutable int listCellW_ = 0;
    mutable int listCellH_ = 0;
    mutable int listSelectedRow_ = -1;
    // Cursor por superficie (contenido vs modal): el cursor viejo solo
    // invalida filas si pertenece a la MISMA superficie (al volver de un
    // modal al editor, el cursor del modal nunca se pintó sobre el contenido
    // y remarcar su fila sería un repintado espurio). lastCursor_ sigue
    // siendo el último global (snapshots + blink + pintado).
    mutable FrameCursor contentCursor_;
    mutable FrameCursor listCursor_;
    mutable bool lastIsList_ = false;
    // --- Estado para caminos incrementales (cursor-move sin scroll, scroll ±1) ---
    // Permite validar precondiciones sin construir el Frame: el caché de filas
    // solo es reutilizable si el documento, el viewport, el lenguaje y los
    // efectos visuales (selección/búsqueda/bracket) son compatibles.
    // Invariante (etapa scroll): contentRows_[i] representa el docLine
    // contentTop_ + i. Se publica en el camino completo y se rota en scroll ±1.
    mutable int contentTop_ = -1;
    mutable int contentLeft_ = -1;
    mutable int contentVpWidth_ = -1;
    mutable int contentVpHeight_ = -1;
    mutable int contentLineCount_ = -1;
    mutable std::string contentFilename_;
    mutable bool contentModified_ = false;
    mutable uint64_t contentDocVersion_ = 0;
    mutable uint64_t contentDocInstance_ = 0;
    mutable int contentCursorLine_ = -1;  // línea doc del cursor al cachear
    mutable std::optional<Selection> contentSelection_;
    mutable std::optional<Selection> contentSearch_;
    mutable std::optional<BracketPair> contentBracket_;
    mutable bool contentCursorInMsg_ = false;
    // Stats del diferencial (ver getters públicos).
    mutable bool lastWasFull_ = true;
    mutable int lastRepainted_ = 0;
    mutable int lastSkipped_ = 0;
    mutable bool lastChromeDirty_ = true;
    mutable guichrome::ChromePaintGeometry lastChromeGeometry_;
    mutable int chromePaintCalls_ = 0;
    mutable int messageBarCursorPaints_ = 0;
    // Captura del último frame de ventana (solo tests, desactivada por defecto).
    bool captureEnabled_ = false;
    bool lastCaptureValid_ = false;
    std::vector<GuiColor> lastCapture_;
    int lastCaptureW_ = 0;
    int lastCaptureH_ = 0;
    mutable long long totalFrames_ = 0;
    mutable long long totalFull_ = 0;
    mutable long long totalRepainted_ = 0;
    mutable long long totalSkipped_ = 0;
    static bool sameLayout(const Layout& a, const Layout& b);
    void noteStats(bool full, int repainted, int skipped,
                   bool chromeDirty) const;
    // relRow: fila relativa al contenido; fuera de rango = no marca.
    static void markCursorRow(std::vector<char>& dirty, const Layout& layout,
                              const FrameCursor& c);
    // Limpia el estado de validación incremental (ver contentTop_ y ss.).
    // Se llama al invalidar (el flag contentHasCache_=false ya bloquea el
    // camino rápido; esto solo evita que queden valores obsoletos).
    void resetIncrementalState() const;
    // Camino rápido: movimiento de cursor sin scroll ni cambios (etapa ↑/↓).
    // Evita buildFrame() completo (todas las filas + snapshot total):
    // reconstruye solo las filas afectadas por CurrentLine y resuelve el
    // cursor/chrome como buildFrame. Devuelve true si resolvió el frame
    // (actualizó cachés, stats, snapshots y pintó); false para caer al camino
    // completo existente. Nunca altera el backend TTY (método solo GUI).
    bool tryFastCursorMove(const Document& doc, const Cursor& cursor,
                           const Viewport& viewport,
                           const std::string& filename, bool modified,
                           const ChromeRequest& chrome,
                           const std::optional<Selection>& selection,
                           const std::optional<Selection>& searchHighlight,
                           const std::optional<BracketPair>& bracketPair);
    // Camino rápido: scroll vertical de una línea (etapa scroll ±1).
    // Rota los snapshots cacheados (la fila de docLine L sigue representando
    // a L: solo cambia su índice) y construye únicamente la fila entrante +
    // las extras por CurrentLine; no reconstruye las filas que siguen
    // visibles. El repintado usa scroll-blit GPU (ping-pong, ver contentFb_)
    // cuando hay render targets; sin ellos cae al repintado desde caché.
    // Mismas garantías de equivalencia visual que el camino completo;
    // false = fallback.
    bool tryFastScrollLine(const Document& doc, const Cursor& cursor,
                           const Viewport& viewport,
                           const std::string& filename, bool modified,
                           const ChromeRequest& chrome,
                           const std::optional<Selection>& selection,
                           const std::optional<Selection>& searchHighlight,
                           const std::optional<BracketPair>& bracketPair);
    // Camino común de los 3 modales: snapshots + diff por línea + pintado
    // full/parcial. `lines` ya viene compuesto (ListLines.h), `next` es el
    // cursor resuelto por el caller, `selectedRow` la fila destacada
    // relativa (-1 = ninguna), `selSnapshot/scrollSnapshot` lo que publica
    // el snapshot (lastListSelected_/lastListScroll_).
    void renderListCommon(std::vector<ListLine> lines, const ChromeData& chrome,
                          StyleRole accent, const Layout& layout,
                          const FrameCursor& next, int selectedRow,
                          int selSnapshot, int scrollSnapshot);
    // Calcula y almacena la geometría de pintado del chrome inferior para
    // ambos modos (anclado y legacy). Única fuente de verdad para el overlay.
    void updateChromeGeometry(const Layout& layout, int outW, int outH);

    // --- Framebuffers persistentes para el repintado parcial (P1) ---
    // El camino parcial NO puede apoyarse en que el backbuffer conserve sus
    // píxeles entre Presents: el contrato SDL2 lo declara invalidado tras
    // cada SDL_RenderPresent (funciona con el renderer software de los tests
    // pero puede fallar con otro backend/driver). Por eso el parcial pinta
    // sobre texturas de destino (SDL_TEXTUREACCESS_TARGET), que SÍ
    // persisten; después se vuelve al destino de ventana, se limpia el
    // backbuffer, se copia la textura completa y se presenta. El backbuffer
    // nunca se lee, solo se escribe: el ahorro de las filas sucias se
    // mantiene sin depender de comportamiento accidental.
    // Hay UNA textura por superficie lógica (contenido y modal): con una sola
    // compartida, volver del modal al editor con caché válida y 0 filas
    // dirty presentaría la imagen del modal (las cachés separadas guardan
    // snapshots lógicos, no imágenes). Cada superficie compone la suya y el
    // cambio de pantalla no repinta nada.
    // Si el renderer no soporta render targets (verificado una vez por
    // renderer con SDL_RenderTargetSupported) o la textura no se puede
    // crear, el pintado cae al repintado completo directo en ventana.
    // La textura se recrea al cambiar de renderer o de tamaño en píxeles
    // (fresh=true: contenido indefinido, el caller pinta todo).
    struct FrameSurface {
        void* tex = nullptr;  // SDL_Texture* opaco (solo .cpp + HAVE_SDL2)
        int w = 0;
        int h = 0;
    };
    // Contenido con ping-pong para el scroll-blit (etapa scroll-blit):
    // contentFb_[contentFbActive_] guarda la imagen vigente; el blit copia la
    // región de contenido desplazada a la otra superficie (nunca una textura
    // sobre sí misma: SDL lo deja indefinido), pinta encima entrante + dirty
    // + chrome + cursor, presenta y conmuta el índice. La superficie inactiva
    // puede estar obsoleta: el blit siempre la regenera desde la activa (la
    // región de contenido queda cubierta entera y el chrome se repinta
    // siempre sobre el target). Sin render targets ambas quedan nulas y todo
    // cae al repintado completo directo (ver acquireFb/fallbacks).
    FrameSurface contentFb_[2];
    int contentFbActive_ = 0;
    FrameSurface listFb_;
    int framebufTarget_ = 0;  // 0 = sin probar, 1 = soportado, -1 = no
    // Ver setFramebufAcquireFailAfterForTests: -1 desactiva.
    int framebufFailAfter_ = -1;
    void destroyFb(FrameSurface& s);
    void destroyFramebuf();
    void* acquireFb(FrameSurface& s, void* sdlRenderer, int wPx, int hPx,
                     bool& fresh);
    const softwrap::Index* wrapIndex_ = nullptr;

    #ifdef HAVE_SDL2
    // Captura el destino de ventana tras el último draw del overlay y antes
    // del Present. Solo se usa cuando captureEnabled_ está activo (tests).
    void captureBackbuffer_(SDL_Renderer* r);
    void presentFramebuf(SDL_Renderer* r, SDL_Texture* fb,
                         const GuiColor& bg, const Layout& layout, int ch,
                         const ChromeData& chromeData,
                         const FrameCursor& cursor, bool cursorShown);
    // Composición final del fallback sin render targets: clear + contenido +
    // cursor + (scrollbar) + chrome + Present. Es el único otro punto de
    // composición además de presentFramebuf, y también pinta el chrome una
    // sola vez. `withScrollbar` preserva la diferencia histórica entre
    // contenido (con scrollbar) y listas (sin scrollbar).
    void presentWindow(SDL_Renderer* r, const GuiColor& bg,
                       const Layout& layout, int ch,
                       const ChromeData& chromeData, StyleRole accent,
                       const FrameCursor& cursor, bool cursorShown,
                       const Frame* contentFrame,
                       const std::vector<ListLine>* listLines,
                       int listSelectedRow, bool withScrollbar);

    void paintVerticalScrollbar(SDL_Renderer* r, const Layout& layout);
#endif
};
