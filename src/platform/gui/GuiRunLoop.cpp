#include "platform/gui/GuiRunLoop.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "app/Editor.h"
#include "base/utf8.h"
#include "platform/MouseEvent.h"
#include "platform/WindowSize.h"
#include "platform/gui/GuiWindowGeometry.h"
#include "rendering/Sink.h"
#include "rendering/gui/GuiRenderer.h"
#include "rendering/gui/GuiChromePixels.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

#include "platform/gui/GuiSdlMapper.h"
#endif

int GuiRunLoop::maxBatchMsOverride_ = 0;

void GuiRunLoop::setMaxBatchMsForTests(int ms) {
    maxBatchMsOverride_ = ms;
}

GuiRunLoop::GuiRunLoop(Editor& editor, const GuiOptions& opts)
    : editor_(editor), options_(opts) {}

void GuiRunLoop::syncScrollbarInfo() {
    if (!gui_) {
        return;
    }

    const Editor::ScrollInfo si = editor_.scrollInfo();

    gui_->setScrollInfo(
        GuiScrollInfo{si.top, si.visible, si.total}
    );
}

void GuiRunLoop::idleStep(std::chrono::steady_clock::time_point now) {
    // Inyecta el tiempo de blink antes de pintar: la fase ON/OFF sale de
    // GuiRenderer (Bar parpadea, Block fijo; el ancla se resetea al mover el
    // cursor). Sin gui_ es solo tick+render (tests del Editor).
    // Sin bombeo externo a propósito: run() lo hace UNA vez al tope de cada
    // vuelta (antes de drenar SDL), así el camino idle no lo repite. Quien
    // llame idleStep() directo (tests) bombea antes vía
    // editor_.pollExternalEvents(), igual que run().
    if (gui_) gui_->setBlinkNow(now);
    syncScrollbarInfo();
    editor_.tick(now);
    editor_.renderFrame();
    if (gui_) lastBlinkPhaseOn_ = gui_->blinkPhaseOn();
}



int GuiRunLoop::computeIdleWaitMs(
    std::chrono::steady_clock::time_point now) const {
    // Mínimo explícito entre deadlines reales: los ausentes no intervienen.
    // (Un std::min ingenuo con el -1 de nextTimeoutMs taparía el deadline
    // del blink y el loop despertaría tarde.)
    int waitMs = kMaxIdleWaitMs;
    const int editorMs = editor_.nextTimeoutMs(now);
    if (editorMs >= 0) waitMs = std::min(waitMs, editorMs);
    if (gui_) {
        if (const auto blinkMs = gui_->msUntilBlinkToggle(now))
            waitMs = std::min(waitMs, *blinkMs);
    }
    return waitMs;
}

int GuiRunLoop::idleDelayMs(std::chrono::steady_clock::time_point now) const {
    return computeIdleWaitMs(now);
}

void GuiRunLoop::serviceWatcherIfDue(
    std::chrono::steady_clock::time_point now) {
    if (!watcherServiceDue(lastWatcherService_, now)) return;
    editor_.pollExternalEvents();
    lastWatcherService_ = now;
}

bool GuiRunLoop::idleOnTimeout(std::chrono::steady_clock::time_point now) {
    // Reloj de blink fresco ANTES de servir al watcher: si el
    // procesamiento externo recarga y renderiza tras bambalinas
    // (Editor::handleFileChange lo hace sincrónicamente), lo hace con la
    // fase actual en vez de un tiempo obsoleto. Inyectar sin renderar es
    // seguro (ver comentario en la rama del blink, abajo).
    if (gui_) gui_->setBlinkNow(now);
    // Periodicidad propia del watcher (ver invariante en el header).
    serviceWatcherIfDue(now);
    // Deadline vencido del Editor (mensaje/autoscroll): tick + render con
    // tiempo de blink inyectado. Se despacha sin iniciar otra espera.
    if (editor_.nextTimeoutMs(now) == 0) {
        idleStep(now);
        return true;
    }
    // Sin animación pendiente (Block/invisible/sin ancla) no hay fase que
    // seguir: se omite el render especulativo (el loop solo pinta al
    // vencer un deadline real).
    if (gui_ && gui_->msUntilBlinkToggle(now).has_value()) {
        // Cambio de fase del blink: se informa el tiempo y se compara con
        // la fase del último render. Inyectar sin renderar es seguro: el
        // ancla solo cambia durante un render (noteCursorForBlink) y la
        // consulta de deadline usa el ancla, no el tiempo inyectado.
        const bool phaseOn = gui_->blinkPhaseOn();
        if (phaseOn != lastBlinkPhaseOn_) {
            syncScrollbarInfo();
            editor_.renderFrame();
            // Registrar DESPUÉS del render: noteCursorForBlink() puede
            // haber reseteado el ancla (celda/forma/visibilidad) y la fase
            // efectiva se mide contra la nueva ancla (igual que idleStep
            // y renderWithBlink).
            lastBlinkPhaseOn_ = gui_->blinkPhaseOn();
            return true;
        }
        lastBlinkPhaseOn_ = phaseOn;
    }
    // Wake de polling sin cambios: sin render ni present.
    return false;
}

void GuiRunLoop::updateImeRect() const {
#ifdef HAVE_SDL2
    if (!gui_) return;
    const GuiPixelRect r = gui_->imeRectPx();
    // POLÍTICA IME (explícita): el rect se recalcula desde la geometría
    // actual del cursor en cada render (y al recuperar el foco, ver
    // FOCUS_GAINED abajo). Si no hay cursor válido se conserva el último
    // rect configurado en SDL (no se toca nada) como fallback aceptado:
    // SDL2 no ofrece "ocultar candidata" sin SDL_StopTextInput(), y detener
    // la entrada mataría una composición en curso; la candidata queda
    // anclada a la última celda válida hasta el próximo cursor válido.
    if (!r.valid) return;
    // UNIDADES (P2-IME): imeRectPx viene en píxeles del drawable (celda TTF ×
    // celda), pero SDL_SetTextInputRect habla en puntos de ventana (supuesto
    // documentado en guiImeRectToWindow: SDL3 lo explicita, SDL2 se infiere
    // del uso junto a eventos en puntos). Con ALLOW_HIGHDPI a escala N hay
    // que bajar N× por eje; a escala 1 es identidad. Sin medición (valores en
    // 0) se pasa tal cual (comportamiento pre-HiDPI).
    // La geometría del cursor vive en la grilla de contenido, debajo de la
    // barra propia: se desplaza al espacio del drawable completo antes de
    // convertir a puntos de ventana.
    GuiImeRectPx pr{r.x, r.y + titleBarHeightPx_, r.w, r.h};
    int x = pr.x, y = pr.y, w = pr.w, h = pr.h;
    if (sdlWin_) {
        // Sin consultas SDL acá: se reusa la medición guardada por
        // currentSize() (única lectura del drawable por frame). Si todavía
        // no hay medición, win/out quedan en 0 y la conversión es identidad.
        const GuiImeRectPx o = guiImeRectToWindow(
            pr, lastWinW_, lastWinH_, lastDrawableW_, lastDrawableH_);
        x = o.x;
        y = o.y;
        w = o.w;
        h = o.h;
    }
    SDL_Rect sr;
    sr.x = x;
    sr.y = y;
    sr.w = w;
    sr.h = h;
    SDL_SetTextInputRect(&sr);
#endif
}

#ifndef HAVE_SDL2

int GuiRunLoop::run() {
    std::fprintf(stderr,
                 "maestro: --gui requiere compilar con SDL2 "
                 "(WITH_SDL2=1, falta libsdl2-dev).\n");
    return 1;
}

#else

namespace {
// Adaptador SDL -> vocabulario propio GUI: vive en GuiSdlMapper.h (límite
// SDL testeable, único lugar que nombra SDL_Keycode/KMOD_*); acá solo se
// usa. Ver las reglas (Caps/Num pelados, AltGr como Alt, keypad mínimo) en
// ese header y en tests/unit/test_guisdlmapper.cpp.
// El cálculo de grilla rows/cols = píxeles/celda vive en GuiSdlMapper.h
// (guiGridSize, pura y testeada sin SDL): acá solo se usa.
// La celda de respaldo es la canónica (platform::kFallbackCellW/H, ver
// WindowSize.h): con HiDPI se escala por eje con guiScaleSize, ver
// currentSize/mouseCell.
// CONTRATO DE UNIDADES (fuente y geometría):
//   - `--font-size N` son PÍXELES LÓGICOS (== píxeles del drawable a escala
//     1), no puntos tipográficos (1/72in) ni píxeles físicos. Antes de cada
//     TTF_OpenFont la base se convierte a píxeles del drawable con
//     guiScaledFontPixels (eje vertical, redondeo): a 2×, 16 -> 32. Así
//     `--font-size 16` da la misma altura visual y la misma grilla en
//     cualquier monitor (contrato visual-estable).
//   - `--geometry WxH` son COORDENADAS DE VENTANA (puntos, no píxeles
//     físicos): se pasan tal cual a SDL_CreateWindow; con HiDPI el drawable
//     resulta N× mayor y grilla/fuente se derivan del drawable.
// El valor por defecto de la base es 16 px lógicos; se cambia con --font-size.

#ifdef HAVE_SDL2
// Guard RAII del estado TextInput de SDL: garantiza SDL_StopTextInput()
// en TODOS los caminos de salida de run() (retorno anticipado futuro,
// excepción, salida normal). Sin esto, un `return` nuevo tras el Start
// dejaría la entrada de texto global de SDL activada tras cerrar la
// ventana. No copiable; vive en la pila de run() desde que hay renderer
// hasta el teardown.
class SdlTextInputGuard {
public:
    SdlTextInputGuard() { SDL_StartTextInput(); }
    ~SdlTextInputGuard() { SDL_StopTextInput(); }
    SdlTextInputGuard(const SdlTextInputGuard&) = delete;
    SdlTextInputGuard& operator=(const SdlTextInputGuard&) = delete;
};

// Captura de mouse best-effort (NO fatal): SDL_CaptureMouse() devuelve 0
// en éxito y -1 si el entorno no la soporta. En Wayland el compositor
// puede denegarla (el protocolo no garantiza captura global implícita
// como X11); eso es una LIMITACIÓN DEL ENTORNO, no un error del editor:
// el press/drag/release ya entregado sigue siendo válido y la selección
// queda coherente con la última celda conocida (ver nota en el press).
// Por eso el retorno se ignora a propósito y se limpia el error de SDL
// para no contaminar el diagnóstico posterior de SDL_WaitEventTimeout
// (que distingue timeout/error por SDL_GetError).
inline void sdlCaptureMouseBestEffort(SDL_bool enabled) {
    if (SDL_CaptureMouse(enabled) != 0) SDL_ClearError();
}

// SDL_GetGlobalMouseState consulta el estado del sistema operativo (estado
// real del puntero), mientras que SDL_GetMouseState solo repite el estado que
// SDL conoce tras el último procesamiento de eventos: si un release nunca
// llegó a la cola, SDL_GetMouseState seguiría reportando el botón presionado
// y el oráculo del autoscroll no se desarmaría. Por eso se usa la consulta
// global al OS (SDL2 acepta punteros nulos para las coordenadas, que aquí no
// se necesitan). Único uso restante del estado global del mouse: el oráculo
// de setMouseButtonPressedQuery (autoscroll del tick).
inline Uint32 sdlGlobalMouseButtons() {
    return SDL_GetGlobalMouseState(nullptr, nullptr);
}




SDL_HitTestResult SDLCALL guiTitleBarHitTest(SDL_Window*,
                                             const SDL_Point* area, void* data) {
    if (!area) return SDL_HITTEST_NORMAL;
    auto* state = static_cast<GuiRunLoop::TitleBarHitState*>(data);
    if (!state || state->winW <= 0 || state->winH <= 0) {
        return SDL_HITTEST_NORMAL;
    }
    const int x = area->x;
    const int y = area->y;
    const int winW = state->winW;
    const int winH = state->winH;
    const int titleH = state->titleH;

    // 0) Scrollbar: el track ya está desplazado hacia dentro (kResizeBorderPts)
    // para no solapar con el borde de redimensionado. Snapshot atómico.
    if (GuiRunLoop::pointInScrollbarTrack(state->scrollbar.get(), x, y)) {
        return SDL_HITTEST_NORMAL;
    }

    // 1) Botones de la barra (tienen prioridad sobre cualquier borde).
    if (titleH > 0 && y < titleH && state->titleBar &&
        state->titleBar->buttonAt(x, y, winW, titleH) != GuiTitleBarButton::None) {
        return SDL_HITTEST_NORMAL;
    }
    // 2) Esquinas y bordes de redimensionado (excluyendo la franja superior
    // de la barra, que ya se gestionó con los botones).
    // kResizeBorderPts está en PUNTOS DE VENTANA. El track del scrollbar se
    // desplaza hacia dentro la misma cantidad convertida a PÍXELES DEL DRAWABLE
    // (ver renderWithBlink: resizeBorderPx = kResizeBorderPts * mfb.w / winW).
    const int border = kResizeBorderPts;
    const bool left = x >= 0 && x < border;
    const bool right = x >= winW - border && x < winW;
    const bool top = y >= 0 && y < border;
    const bool bottom = y >= winH - border && y < winH;
    // Esquinas: tienen prioridad sobre los lados.
    if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    // Lados: izquierda, derecha, fondo y TAMBIÉN borde superior (en la franja
    // de resize, antes de la zona de arrastre de la barra).
    if (left) return SDL_HITTEST_RESIZE_LEFT;
    if (right) return SDL_HITTEST_RESIZE_RIGHT;
    if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    if (top) return SDL_HITTEST_RESIZE_TOP;  // borde superior central (y < border)
    // 3) Arrastre de la barra de título (fuera de botones y fuera del borde de resize).
    if (titleH > 0 && y < titleH) {
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}
#endif
}  // namespace

// --- Cursor de redimensionado en hover (sin click) ---

void GuiRunLoop::initResizeCursors() {
    if (resizeCursorsInitialized_) return;
    cursorSizeWE_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    cursorSizeNS_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    cursorSizeNWSE_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
    cursorSizeNESW_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
    cursorArrow_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    // Fallback final si falla la creación del cursor arrow:
    // SDL_GetDefaultCursor() devuelve un cursor válido gestionado por SDL
    // (no se debe liberar con SDL_FreeCursor).
    if (!cursorArrow_) {
        cursorArrow_ = SDL_GetDefaultCursor();
        cursorArrowOwned_ = false;  // no es nuestro, no liberar
    } else {
        cursorArrowOwned_ = true;   // creado por nosotros, sí liberar
    }
    // NO asignamos cursorArrow_ como fallback en los campos direccionales:
    // eso causaría doble liberación en freeResizeCursors().
    // El fallback se resuelve en cursorForType().
    resizeCursorsInitialized_ = true;
}

void GuiRunLoop::freeResizeCursors() {
    if (cursorSizeWE_) SDL_FreeCursor(static_cast<SDL_Cursor*>(cursorSizeWE_));
    if (cursorSizeNS_) SDL_FreeCursor(static_cast<SDL_Cursor*>(cursorSizeNS_));
    if (cursorSizeNWSE_) SDL_FreeCursor(static_cast<SDL_Cursor*>(cursorSizeNWSE_));
    if (cursorSizeNESW_) SDL_FreeCursor(static_cast<SDL_Cursor*>(cursorSizeNESW_));
    // Liberar cursorArrow_ solo si lo creamos nosotros (owned).
    // Si vino de SDL_GetDefaultCursor(), no es nuestra responsabilidad liberarlo.
    if (cursorArrowOwned_ && cursorArrow_) {
        SDL_FreeCursor(static_cast<SDL_Cursor*>(cursorArrow_));
    }
    cursorSizeWE_ = cursorSizeNS_ = cursorSizeNWSE_ = cursorSizeNESW_ = cursorArrow_ = nullptr;
    currentCursor_ = nullptr;
    resizeCursorsInitialized_ = false;
    cursorArrowOwned_ = false;
}

void GuiRunLoop::setCursorIfChanged(void* cursor) {
    if (cursor != currentCursor_) {
        SDL_SetCursor(static_cast<SDL_Cursor*>(cursor));
        currentCursor_ = cursor;
    }
}

void* GuiRunLoop::cursorForType(ResizeCursorType type) const {
    void* cursor = nullptr;
    switch (type) {
        case ResizeCursorType::SizeWE:
            cursor = cursorSizeWE_;
            break;
        case ResizeCursorType::SizeNS:
            cursor = cursorSizeNS_;
            break;
        case ResizeCursorType::SizeNWSE:
            cursor = cursorSizeNWSE_;
            break;
        case ResizeCursorType::SizeNESW:
            cursor = cursorSizeNESW_;
            break;
        case ResizeCursorType::Arrow:
            break;
    }
    return cursor ? cursor : cursorArrow_;
}

void GuiRunLoop::applyWindowSize(const platform::WindowSize& size,
                                int contentTopInsetPx) {
    // Fase 2: actualización completa de la geometría del ciclo. Aplica en un
    // solo punto la medición fresca (grilla/editor + métricas de drawable +
    // inset de contenido) a los consumidores: Editor (grilla/viewport) y
    // GuiRenderer (viewport + chrome). No depende de efectos secundarios
    // previos: quien llame le pasa TODAS las métricas ya medidas.
    //
    // El renderer se actualiza PRIMERO. Si setDrawableMetrics() rechaza las
    // métricas (p. ej. drawable con dimensión 0), no se propaga el resize al
    // Editor, evitando que ambos consumidores trabajen con geometrías
    // distintas.
    if (gui_ &&
        !gui_->setDrawableMetrics(size.pixelW, size.pixelH,
                                  contentTopInsetPx)) {
        return;
    }
    editor_.resize(size);
}

int GuiRunLoop::run() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "maestro: SDL_Init falló: %s\n", SDL_GetError());
        return 1;
    }

    // ORÁCULO DE AUTOSCROLL (paridad X11MouseButtonQuery en TTY): si se
    // pierde el SDL_MOUSEBUTTONUP, el tick lo detecta vía
    // mouseButtonPressedQuery_ y desarma como un release. Es la ÚLTIMA capa
    // (respaldo), no la primera:
    //   1. Captura explícita en el press (ver SDL_MOUSEBUTTONDOWN): el up
    //      fuera de la ventana igual llega a esta ventana. Best-effort:
    //      en Wayland el compositor puede denegarla y entonces este nivel
    //      no aplica (ver sdlCaptureMouseBestEffort); el gesto sigue
    //      coherente con lo entregado dentro de la ventana y el cierre
    //      recae en los niveles 2-3.
    //   2. Release sintético en FOCUS_LOST (ver SDL_WINDOWEVENT): cubre el
    //      robo de foco, donde la captura cae sola y el up va a otro lado.
    //   3. Este oráculo: SDL_GetGlobalMouseState consulta al OS (a
    //      diferencia de SDL_GetMouseState, que solo repite el último pump
    //      y quedaría clavado en "presionado" si el release nunca entró a
    //      la cola). Límite conocido: donde el driver no pueda consultar al
    //      OS, el autoscroll por timer puede frenar antes (los pasos por
    //      evento de drag no se afectan); en el target X11 funciona.
    editor_.setMouseButtonPressedQuery([] {
        return (sdlGlobalMouseButtons() & SDL_BUTTON_LMASK) != 0;
    });

    // Único lugar que decide los flags de creación de ventana (helper puro
    // testeado en test_guisdlmapper.cpp): --no-hidpi elimina
    // SDL_WINDOW_ALLOW_HIGHDPI y el cálculo de celdas sigue intacto (sin
    // HiDPI el drawable == ventana y la escala de guiScaledFontPixels es 1).
    // Borderless: la barra de título es propia (Phase 2) y sigue el tema.
    const Uint32 windowFlags = guiWindowFlags(options_.enableHiDpi, true);

    int winW = options_.windowWidth.value_or(960);
    int winH = options_.windowHeight.value_or(600);

    SDL_Window* win = SDL_CreateWindow(
        "Maestro", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH,
        windowFlags);
    if (!win) {
        std::fprintf(stderr, "maestro: SDL_CreateWindow falló: %s\n",
                     SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* ren =
        SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        // Fallback a software si no hay aceleración.
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) {
        std::fprintf(stderr, "maestro: SDL_CreateRenderer falló: %s\n",
                     SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    // La GUI pinta directo en el SDL_Renderer; el Sink de bytes TTY no se
    // usa (NullSink de descarte, vive todo el run).
    NullSink discard;
    editor_.setSink(discard);
    // Handles SDL vivos durante run(). updateImeRect solo usa sdlWin_ para
    // decidir si convierte; las medidas vienen de currentSize().
    sdlWin_ = static_cast<void*>(win);
    if (gui_) gui_->setTitleBar(&titleBar_);
    // Pista nativa de tema ANTES del primer map (SDL_ShowWindow). La barra
    // visible es la propia (GuiTitleBar); esto solo mantiene la propiedad
    // coherente para el entorno/WM desde el arranque.
    // Abre la fuente con la base lógica del CLI (ver contrato de arriba).
    // El primer currentSize() mide el drawable y llama ensureFontPixels para
    // escalarla antes del primer render: no se consulta SDL acá, así el
    // arranque no duplica la medición de ventana/salida. A escala 1 no hay
    // recarga (la talla ya coincide); en HiDPI currentSize la reabre una vez
    // con el tamaño correcto.
    const int baseFontPx = options_.fontSize.value_or(16);
    if (gui_ &&
        !gui_->initForRenderer(static_cast<void*>(ren), baseFontPx)) {
        std::fputs(fontOpenDiagnostic(options_).c_str(), stderr);
        if (gui_) gui_->setSdlRenderer(nullptr);
        sdlWin_ = nullptr;
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    // POLÍTICA TEXT INPUT (explícita): habilitado durante TODO el run,
    // también en navegación. SDL gestiona la composición de forma continua
    // y alternar Start/Stop por modo la interrumpiría al cambiar entre
    // Navegación/Interacción/prompts. El rect se actualiza tras cada render
    // (updateImeRect) a la celda del cursor lógico. El guard garantiza el
    // Stop en todos los caminos de salida.
    SdlTextInputGuard textInput;

    // Inicializar cursors de redimensionado para hover (sin click)
    initResizeCursors();

    auto currentSize = [&] {
        // ESPACIO DRAWABLE (no puntos de ventana): con ALLOW_HIGHDPI el
        // drawable puede ser N× la ventana, y la grilla cols/filas vive en
        // el espacio donde el renderer dibuja (sin logical size/scale) y
        // donde mide la celda TTF. Con SDL_GetWindowSize a escala N la
        // grilla quedaría N× chica. Fallback a ventana si el output falla.
        // CONTRATO VISUAL-ESTABLE (P1): la fuente se escala con el drawable
        // ANTES de dividir (16 -> 32 a 2×): la celda TTF crece N× y la grilla
        // se mantiene (~106×33 en vez de duplicarse a 213×66 con texto
        // diminuto). Sin fuente, el fallback 9×18 se escala por eje igual.
        int winW = 0, winH = 0, outW = 0, outH = 0;
        int winX = 0, winY = 0;
        SDL_GetWindowPosition(win, &winX, &winY);
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_GetRendererOutputSize(ren, &outW, &outH);
        // Drawable efectivo por eje (minimizado / driver degenerado): un eje
        // en 0 cae a su eje de ventana sin descartar el otro válido; con
        // todo en 0 da 960×600 y la grilla se clampe a >=1 (nunca 0×0 al
        // Editor). Minimizar no re-mide por evento (ver
        // guiResizeNeedsRemeasure): conserva la última grilla válida o esta
        // degradada; al restaurar, SIZE_CHANGED/DISPLAY_CHANGED re-mide.
        const GuiDrawableSize fb = guiDrawableOrWindow(winW, winH, outW, outH);
        outW = fb.w;
        outH = fb.h;
        if (gui_) {
            gui_->ensureFontPixels(
                guiScaledFontPixels(baseFontPx, winH, outH));
        }
        int cw = 0, ch = 0;
        if (gui_ && gui_->hasFont()) {
            cw = gui_->cellW();
            ch = gui_->cellH();
        } else {
            cw = guiScaleSize(platform::kFallbackCellW, winW, outW);
            ch = guiScaleSize(platform::kFallbackCellH, winH, outH);
        }
        // Phase 2: la barra propia se mide en puntos lógicos (28px mínimo)
        // y se lleva al drawable, así conserva el mismo tamaño visual con
        // HiDPI. La grilla de contenido pierde exactamente esos píxeles.
        const int cellHPoints = guiScaleCoord(ch, outH, winH);
        int titleHPoints = GuiTitleBar::heightForCell(cellHPoints);
        int titleHPx = guiScaleCoord(titleHPoints, winH, outH);
        if (titleHPx >= outH) {
            titleHPoints = 0;
            titleHPx = 0;
        }
        titleBarHeightPx_ = titleHPx;
        titleBarHeightPoints_ = titleHPoints;
        hitState_.winW = winW;
        hitState_.winH = winH;
        hitState_.titleH = titleBarHeightPoints_;
        hitState_.titleBar = &titleBar_;
        const int contentH = std::max(1, outH - titleBarHeightPx_);
        // Normalización fase 2: rows/cols salen del área de contenido (post
        // barra de título); pixelW/pixelH se normalizan al DRAWABLE COMPLETO
        // (outW/outH, incl. la franja de la barra). Así WindowSize pasa a
        // representar el drawable completo y el render no debe restar el inset
        // a una altura "de contenido" que ya lo esté haciendo la grilla.
        platform::WindowSize size = guiGridSize(outW, contentH, cw, ch);
        size.pixelW = outW;
        size.pixelH = outH;
        // Misma medición para updateImeRect: el rect IME se convierte a
        // puntos con estos valores en vez de volver a consultar SDL.
        lastWinW_ = winW;
        lastWinH_ = winH;
        lastWinX_ = winX;
        lastWinY_ = winY;
        lastDrawableW_ = outW;
        lastDrawableH_ = outH;
        return size;
    };
    auto renderWithBlink = [&] {
        syncScrollbarInfo();
        if (gui_) gui_->setBlinkNow(std::chrono::steady_clock::now());
        editor_.renderFrame();
        if (gui_) lastBlinkPhaseOn_ = gui_->blinkPhaseOn();
        updateImeRect();
        // Actualizar geometría del scrollbar en PUNTOS DE VENTANA para el
        // hit-test nativo (guiTitleBarHitTest). El layout ya está fresco
        // tras renderFrame() (lastLayout_ actualizado en GuiRenderer).
        if (gui_ && gui_->scrollbarEnabled()) {
            // Reusar la medición de currentSize (única lectura del drawable
            // por frame). lastDrawable* ya incluye el fallback por eje de
            // guiDrawableOrWindow, así que no hace falta volver a consultar
            // SDL acá.
            const int winW = lastWinW_;
            const int winH = lastWinH_;
            const GuiDrawableSize mfb{lastDrawableW_, lastDrawableH_};
            // Margen de redimensionado en píxeles del drawable: kResizeBorderPts (puntos
            // de ventana) convertido a píxeles del drawable.
            const int resizeBorderPx = (kResizeBorderPts * mfb.w) / std::max(1, winW);
            const GuiPixelRect hitRect = gui_->scrollbarHitRect(gui_->lastLayout(), resizeBorderPx);
            if (hitRect.valid && hitRect.w > 0 && hitRect.h > 0) {
                // Convertir drawable px -> puntos de ventana (misma fórmula que toDrawablePoint invertida).
                GuiRunLoop::ScrollbarHitRect hr;
                hr.x = (hitRect.x * winW) / std::max(1, mfb.w);
                hr.y = (hitRect.y * winH) / std::max(1, mfb.h);
                hr.w = (hitRect.w * winW) / std::max(1, mfb.w);
                hr.h = (hitRect.h * winH) / std::max(1, mfb.h);
                hitState_.scrollbar.set(hr);
            } else {
                hitState_.scrollbar.clear();
            }
        } else {
            hitState_.scrollbar.clear();
        }
        // Cubre el toggle de tema desde cualquier origen (Ctrl+K l,
        // theme.toggle, etc.) sin acoplar Editor -> plataforma.
        };
    // LOTE DE EVENTOS: en vez de renderizar dentro de handleSdlEvent (lo
    // que hace un frame completo por cada KEYDOWN repetido), se acumulan
    // los efectos de estado y se pinta UNA vez al vaciar la cola SDL. El
    // KEYUP entra en el mismo lote: no queda atrapado detrás de renders.
    // La semántica de cada evento se preserva en orden FIFO; solo se
    // difiere el pintado. Sin tope de lote a propósito (un límite podría
    // dejar el KEYUP para después del render).
    bool renderNeeded = false;
    auto requestRender = [&] { renderNeeded = true; };
    // Barra de título propia: hit-test en puntos -> drawable y acción de
    // cada botón. Los botones NO se envían al Editor (no son celdas).
    auto titleButtonAtPoint = [&](int ptX, int ptY) {
        if (titleBarHeightPx_ <= 0) return GuiTitleBarButton::None;
        // Reusar la medición de currentSize: lastDrawable* ya incluye el
        // fallback por eje de guiDrawableOrWindow, sin consultar SDL por
        // evento de mouse.
        const int winW = lastWinW_;
        const int winH = lastWinH_;
        const int outW = lastDrawableW_;
        const int outH = lastDrawableH_;
        const int pxX = guiScaleCoord(ptX, winW, outW);
        const int pxY = guiScaleCoord(ptY, winH, outH);
        if (pxY < 0 || pxY >= titleBarHeightPx_) {
            return GuiTitleBarButton::None;
        }
        return titleBar_.buttonAt(pxX, pxY, outW, titleBarHeightPx_);
    };
    auto pointInTitleBar = [&](int ptY) {
        return titleBarHeightPoints_ > 0 && ptY >= 0 &&
               ptY < titleBarHeightPoints_;
    };
    // Fallback sin hit-test nativo: detecta el borde/corner en puntos de
    // ventana con el mismo grosor visual que usa el callback SDL. Se evalúa
    // ANTES que la barra de título para que el borde superior siga
    // redimensionando.
    // Una sola fuente para el ancho del borde (puntos de ventana).
    const int borderPts = kResizeBorderPts;
    auto manualResizeEdgeAt = [&](int ptX, int ptY) {
        if (hitTestEnabled_) return GuiWindowEdge::None;
        // Reusar la medición de currentSize; no se consulta SDL por evento.
        const int winW = lastWinW_;
        const int winH = lastWinH_;
        GuiWindowEdge edge = guiWindowEdgeAt(ptX, ptY, winW, winH, borderPts);
        // No ofrecer redimensionado por el borde superior dentro de la barra de título
        // EXCEPTO en la franja de resize (primeros kResizeBorderPts puntos).
        // Esto coincide con el hit-test nativo: Top/TopLeft/TopRight solo en y < border.
        if (ptY < titleBarHeightPoints_ &&
            ptY >= borderPts &&  // solo el resto de la barra, NO el borde de resize
            (edge == GuiWindowEdge::Top ||
             edge == GuiWindowEdge::TopLeft ||
             edge == GuiWindowEdge::TopRight)) {
            return GuiWindowEdge::None;
        }
        return edge;
    };
    auto startManualResize = [&](GuiWindowEdge edge) {
        manualResizeEdge_ = edge;
        manualTitleDrag_ = false;
        sdlCaptureMouseBestEffort(SDL_TRUE);
        // Reusa la medicion guardada por currentSize() (la unica lectura del
        // drawable por frame) en vez de consultar SDL_GetWindowPosition /
        // SDL_GetWindowSize al iniciar el gesto.
        manualResizeWinX_ = lastWinX_;
        manualResizeWinY_ = lastWinY_;
        manualResizeWinW_ = lastWinW_;
        manualResizeWinH_ = lastWinH_;
        manualResizeDragDX_ = 0;
        manualResizeDragDY_ = 0;
        // Sin query de posicion inicial: el gesto arranca desde el origen
        // guardado (lastWinX_/lastWinY_, refresheados en SDL_WINDOWEVENT_MOVED
        // y en currentSize) y los deltas relativos vienen en ev.motion.*.
    };
    auto activateTitleButton = [&](GuiTitleBarButton button) {
        switch (button) {
            case GuiTitleBarButton::Minimize:
                SDL_MinimizeWindow(win);
                break;
            case GuiTitleBarButton::Maximize:
                // El editor es dueño de su propio estado de maximizado.
                // titleBar_.maximized_ empieza en false (ventana) y el editor
                // lo actualiza al pedir maximizar/restaurar, sin depender de
                // que el gestor de ventanas confirme (o no) el cambio. Esto
                // evita desincronizaciones al cambiar de escritorio en xfwm4.
                if (titleBar_.maximized()) {
                    SDL_RestoreWindow(win);
                    titleBar_.setMaximized(false);
                } else {
                    SDL_MaximizeWindow(win);
                    titleBar_.setMaximized(true);
                }
                break;
            case GuiTitleBarButton::Close:
                // Mismo camino que SDL_QUIT: respeta buffers modificados.
                if (!editor_.requestQuit(false)) requestRender();
                break;
            case GuiTitleBarButton::None:
                break;
        }
    };
    // Actividad de cursor/viewport DENTRO del lote: un AvPág + RePág deja
    // la misma celda final, pero el cursor sí se movió y el blink debe
    // reiniciarse (ver GuiRenderer::markCursorActivity).
    Editor::CursorViewSnapshot batchView = editor_.cursorViewSnapshot();
    bool batchCursorActivity = false;
    // Puntos de evento SDL -> celda 0-based (dominio neutro CellPos): la
    // MISMA métrica que el resize (fuente escalada o fallback escalado) y el
    // MISMO espacio (drawable). Los eventos llegan en puntos de ventana: con
    // ALLOW_HIGHDPI a escala N hay que subir N× ANTES de dividir por la
    // celda (guiScaleCoord por eje); dividir puntos por celda TTF da la
    // celda N× chica. Reusa la última medición de currentSize (no consulta
    // SDL por evento): los cambios de escala sin resize llegan como
    // DISPLAY_CHANGED y re-miden antes del render siguiente. La celda se
    // ACOTA a la grilla del drawable (guiCellFromPixelsBounded con los
    // cols/rows de guiGridSize): el borde exterior queda FUERA (inválida,
    // el Editor la trata como fuera de contenido) en vez de clamparse a la
    // última columna; el pasado-EOL DENTRO de la grilla lo sigue resolviendo
    // screenToCursor a EOL. Sin offsets propios.
    // gapAsBottom: para el ARRASTRE, una posición en la franja residual se
    // trata como "debajo del contenido" (fila content.height) para que el
    // autoscroll baje. En press/release esa misma zona devuelve CellPos{}
    // (click muerto): el residuo no es una celda de contenido.
    auto mouseCell = [&](int ptX, int ptY, bool gapAsBottom = false) {
        // Misma medición que la grilla (currentSize); lastDrawable* ya trae
        // el fallback por eje de guiDrawableOrWindow, así que grilla y mouse
        // siguen en el mismo espacio también en el camino degenerado.
        const int winW = lastWinW_;
        const int winH = lastWinH_;
        const int outW = lastDrawableW_;
        const int outH = lastDrawableH_;
        int cw = 0, ch = 0;
        if (gui_ && gui_->hasFont()) {
            cw = gui_->cellW();
            ch = gui_->cellH();
        } else {
            cw = guiScaleSize(platform::kFallbackCellW, winW, outW);
            ch = guiScaleSize(platform::kFallbackCellH, winH, outH);
        }
        const int pxX = guiScaleCoord(ptX, winW, outW);
        const int pxY = guiScaleCoord(ptY, winH, outH);
        // La grilla y la celda viven debajo de la barra propia: se descuenta
        // su alto del drawable y del eje Y. Un click en la barra produce una
        // celda inválida (nunca {0,0}).
        const int pyUtil = pxY - titleBarHeightPx_;
        if (pyUtil < 0) return CellPos{}; // click en la barra de título
        
        const int contentH = std::max(1, outH - titleBarHeightPx_);
        const platform::WindowSize g =
            guiGridSize(outW, contentH, cw, ch);
        
        // Obtener layout actual para calcular geometría del chrome anclado
        const Layout layout = gui_ ? gui_->lastLayout() : Layout{};
        const guichrome::BottomChromeGeometry chromeGeom =
            guichrome::bottomChromeGeometry(contentH, ch,
                                            layout.content.height,
                                            layout.chrome.height);
        
        int row = -1;
        if (chromeGeom.anchored) {
            if (pyUtil < chromeGeom.contentBottom) {
                // Contenido normal
                row = pyUtil / ch;
            } else if (pyUtil < chromeGeom.chromeTop) {
                // Espacio residual entre contenido y chrome.
                if (gapAsBottom) {
                    const int col = std::clamp(pxX / cw, 0,
                                               std::max(0, g.cols - 1));
                    return CellPos{col, layout.content.height};
                }
                // Click muerto: sin celda válida (no es contenido).
                return CellPos{};
            } else {
                // Chrome anclado (StatusBar o MessageBar)
                const int chromeRow = (pyUtil - chromeGeom.chromeTop) / ch;
                row = layout.chrome.row + chromeRow;
            }
        } else {
            // Camino legacy: usar la conversión habitual
            row = pyUtil / ch;
        }
        
        const int col = pxX / cw;
        
        // Validar límites
        if (row < 0 || row >= g.rows || col < 0 || col >= g.cols) {
            return CellPos{};
        }
        return CellPos{col, row};
    };

    struct DrawablePoint {
        int x = 0;
        int y = 0;
    };

    auto toDrawablePoint = [&](int ptX, int ptY) {
        DrawablePoint p;
        // Reusar la medición de currentSize (lastDrawable* ya incluye el
        // fallback por eje): sin consultas SDL por evento.
        p.x = guiScaleCoord(ptX, lastWinW_, lastDrawableW_);
        p.y = guiScaleCoord(ptY, lastWinH_, lastDrawableH_);
        return p;
    };
    
    const platform::WindowSize size = currentSize();
    applyWindowSize(size, titleBarHeightPx_);
    // Hit-test nativo: la franja de la barra (salvo botones) arrastra la
    // ventana y los bordes/corners redimensionan; los botones quedan NORMAL
    // para recibir clicks. El estado se pasa como userdata al callback
    // (SDL_SetWindowHitTest sí admite userdata). Si el driver devuelve -1,
    // el fallback manual cubre tanto arrastre como redimensionado
    // (ver SDL_MOUSEBUTTONDOWN).
    hitTestEnabled_ =
        SDL_SetWindowHitTest(win, guiTitleBarHitTest, &hitState_) == 0;
    if (!hitTestEnabled_) SDL_ClearError();
    // Tema inicial ya establecido en GuiRenderer constructor
    renderWithBlink();

    // La ventana se creó OCULTA (guiWindowFlags): recién ahora se muestra,
    // con el primer frame ya compuesto. Así el usuario no ve el hueco de la
    // inicialización (SDL_CreateRenderer/GLX, apertura de la fuente TTF y
    // primer llenado de la caché de texturas), que en un WM tiling sin
    // compositor se percibía como un destello "abre/cierra/abre".
    SDL_ShowWindow(win);
    // El contenido presentado mientras la ventana estaba oculta NO se da por
    // conservado (el driver puede haberlo descartado): se fuerza un frame
    // completo sobre la ventana ya visible. La preparación previa dejó la
    // caché de texturas caliente, así que este segundo frame es barato.
    // El gestor de ventanas puede reacomodar la geometría al mostrar (i3),
    // lo que llega como RESIZED/SIZE_CHANGED y se re-mide en el handler de
    // SDL_WINDOWEVENT de abajo.
    if (gui_) gui_->invalidateCache();
    renderWithBlink();

    // Resto fraccionario de rueda sin emitir (ver SDL_MOUSEWHEEL): un táctil
    // manda fracciones de preciseY que se juntan hasta 1.0. Vive todo el
    // run; no se resetea con el tiempo (0.9 parado + 0.2 tarde igual mueve:
    // inofensivo, un solo paso de 3 líneas).
    double wheelAcc = 0.0;

    while (editor_.isRunning()) {
        // Servicio del watcher con periodicidad propia (ver invariante en el
        // header): inotify vía pollEvents(), sin fd. El clipboard SDL es
        // síncrono (no-op). Va ANTES de drenar SDL para que una recarga por
        // cambio externo quede visible en los renders de eventos de este
        // mismo frame. Solo se bombea al vencer la periodicidad, no en cada
        // vuelta incondicionalmente.
        serviceWatcherIfDue(std::chrono::steady_clock::now());
        // Camino ÚNICO de eventos: lo drenado por SDL_PollEvent y lo
        // obtenido por SDL_WaitEventTimeout se procesan acá, sin re-push.
        // (Reencolar lo esperado con SDL_PushEvent mostró livelock bajo
        // carga continua: el evento volvía a la espera sin pasar jamás por
        // el drenado. El procesado directo preserva orden FIFO sin
        // intermediarios.)
        auto handleSdlEvent = [&](SDL_Event& ev) {
            switch (ev.type) {
                case SDL_QUIT:
                    // Cierre de ventana = intento de salida (respeta
                    // buffers modificados: requestQuit(false) avisa).
                    if (!editor_.requestQuit(false)) {
                        requestRender();
                    }
                    break;
                case SDL_WINDOWEVENT:
                    if (guiResizeNeedsRemeasure(ev.window.event)) {
                        // RESIZED / SIZE_CHANGED / DISPLAY_CHANGED (decisión
                        // testeada en GuiSdlMapper.h): DISPLAY_CHANGED es el
                        // caso 1×->2× sin cambio de tamaño en puntos (la
                        // ventana lógica no se mueve pero el drawable y la
                        // escala sí). currentSize() re-consulta el drawable,
                        // re-escala la fuente y recalcula la grilla.
                        const platform::WindowSize size = currentSize();
                        applyWindowSize(size, titleBarHeightPx_);
                        requestRender();
                    } else if (guiWindowMovedSyncsDragOrigin(ev.window.event)) {
                        // SDL_WINDOWEVENT_MOVED ya trae la nueva posición en
                        // data1/data2 (doc de SDL2). Se usa directamente para
                        // evitar una consulta adicional a SDL_GetWindowPosition.
                        lastWinX_ = ev.window.data1;
                        lastWinY_ = ev.window.data2;
                    } else if (guiFocusLossEndsGesture(ev.window.event)) {
                        // Pérdida de foco con gesto en curso: la captura cae
                        // sola y el BUTTONUP puede ir a otra ventana, así que
                        // se sintetiza el release (el Editor lo ignora si no
                        // hay gesto armado; la celda no se usa en release).
                        // Sin esto, el autoscroll quedaría corriendo con el
                        // botón ya suelto hasta que el oráculo lo note.
                        manualTitleDrag_ = false;
                        manualResizeEdge_ = GuiWindowEdge::None;
                        activeTitleButton_ = GuiTitleBarButton::None;
                        titleBar_.setPressed(GuiTitleBarButton::None);
                        scrollbarDragging_ = false;
                        sdlCaptureMouseBestEffort(SDL_FALSE);
                        // Fábrica testeada (misma que el press/drag: tipo +
                        // celda en un solo punto de construcción).
                        InputEvent ie = makeMouseReleaseEvent(CellPos{});
                        editor_.handleEvent(ie);
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        requestRender();
                    } else if (ev.window.event == SDL_WINDOWEVENT_LEAVE) {
                        if (titleBar_.setHover(GuiTitleBarButton::None)) {
                            requestRender();
                        }
                        // Restaurar cursor arrow al salir de la ventana
                        setCursorIfChanged(cursorArrow_);
                    } else if (ev.window.event ==
                                   SDL_WINDOWEVENT_MAXIMIZED ||
                               ev.window.event ==
                                   SDL_WINDOWEVENT_RESTORED) {
                        // El glyph de maximizar/restaurar sigue el estado
                        // real del WM, que puede cambiar por doble click o
                        // atajos externos, no solo por nuestros botones.
                        titleBar_.setMaximized(
                            ev.window.event == SDL_WINDOWEVENT_MAXIMIZED);
                        requestRender();
                    } else if (guiFocusRegainRefreshesIme(ev.window.event)) {
                        // Foco recuperado: el rect IME de SDL puede haber
                        // quedado obsoleto (cursor, buffer, tamaño o escala
                        // cambiados en el medio), así que se repinta y se
                        // recalcula desde la geometría actual del cursor
                        // (renderWithBlink termina en updateImeRect, que usa
                        // la última medición de currentSize()). Sin cursor
                        // válido aún, rige el fallback de conservar el último
                        // conocido.
                        requestRender();
                    } else if (guiExposeNeedsRepaint(ev.window.event)) {
                        // Región descubierta (p. ej. al cerrarse el overlay de
                        // una captura): sin compositor, X11 no restaura los
                        // píxeles. Se vuelve a presentar desde el framebuffer
                        // persistente; no hace falta invalidar el contenido.
                        requestRender();
                    }
                    break;
                case SDL_KEYDOWN: {
                    // Teclado completo vía GuiKeymap: SDL_Keysym se adapta
                    // a vocabulario propio (GuiKey.h, sin SDL) en el límite
                    // SDL y el keymap traduce a InputEvent semántico.
                    // La repetición se conserva y el keymap la filtra:
                    // navegación/borrado repiten siempre; Enter solo si el
                    // estado actual inserta una fila (Interaccion). En
                    // prompts/selectores Enter repetido sigue suprimido.
                    // KEYDOWN nunca lleva texto imprimible (sin doble
                    // inserción: el texto viene por SDL_TEXTINPUT).
                    InputEvent ie;
                    bool send = false;
                    const bool enterInsertsLine =
                        editor_.cursorViewSnapshot().state == State::Interaccion;
                    const GuiKeyPress press = guiKeyFromSdl(
                        ev.key.keysym.sym, ev.key.keysym.mod,
                        ev.key.repeat != 0);
                    if (auto t = keymap_.mapKey(press, enterInsertsLine)) {
                        ie.type = *t;
                        send = true;
                    }
                    if (send) {
                        editor_.handleEvent(ie);
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        requestRender();
                    }
                    break;
                }
                case SDL_TEXTINPUT: {
                    // Texto CONFIRMADO (teclado o commit IME): fuente única
                    // del texto, se acepta SIEMPRE sin filtrar por
                    // modificadores. El evento SDL_TEXTINPUT no trae mods y
                    // los combos de control (Ctrl+S/Ctrl+Q/...) no generan
                    // texto en SDL, así que no hay doble inserción; en
                    // cambio AltGr (p.ej. @/|/\ según layout: comportamiento
                    // actual, no garantía universal) SÍ llega por aquí y un
                    // filtro global de Ctrl/Alt lo rompería. UTF-8 que
                    // puede traer varios caracteres en un solo commit.
                    // Cada celda entra como InsertChar, la operación
                    // semántica que el Editor ya usa (el handling por modo
                    // decide: inserta, completa prompts o interpreta
                    // 'i'/'s'/... en Navegación; tras Prefix, 'n'/'t'/...
                    // deciden el comando y lo desconocido cancela).
                    const char* t = ev.text.text;
                    if (t && *t) {
                        for (const std::string& cell :
                             utf8::splitCells(t)) {
                            InputEvent ie;
                            ie.type = InputEventType::InsertChar;
                            ie.text = cell;
                            editor_.handleEvent(ie);
                        }
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        requestRender();
                    }
                    break;
                }
                case SDL_TEXTEDITING: {
                    // Composición PROVISIONAL del IME: no se inserta en el
                    // Document (hacerlo duplicaría inserciones por cada
                    // actualización del candidato). Sin superficie de
                    // pintado de composición en esta fase se descarta; el
                    // rect IME ya sigue al cursor lógico y el commit llega
                    // como SDL_TEXTINPUT. Mejora futura: subrayado de
                    // composición.
                    break;
                }
                case SDL_MOUSEBUTTONDOWN: {
                    // 1) Botones de la barra (prioridad sobre bordes/arrastre).
                    if (pointInTitleBar(ev.button.y)) {
                        const GuiTitleBarButton button =
                            ev.button.button == SDL_BUTTON_LEFT
                                ? titleButtonAtPoint(ev.button.x,
                                                     ev.button.y)
                                : GuiTitleBarButton::None;
                        if (button != GuiTitleBarButton::None) {
                            activeTitleButton_ = button;
                            titleBar_.setPressed(button);
                            titleBar_.setHover(button);
                            sdlCaptureMouseBestEffort(SDL_TRUE);
                            requestRender();
                            break;
                        }
                    }
                    // 2) Hit-test del scrollbar (prioridad sobre redimensionado).
                    if (ev.button.button == SDL_BUTTON_LEFT && gui_) {
                        const DrawablePoint p = toDrawablePoint(ev.button.x, ev.button.y);
                        // resizeBorderPx = 0: el track llega hasta el borde
                        // derecho. Los últimos kResizeBorderPts px se solapan
                        // con la zona de resize pero el scrollbar tiene
                        // prioridad (se chequea primero en el hit-test).
                        const GuiScrollbarPress hit =
                            gui_->scrollbarPress(p.x, p.y, /*resizeBorderPx=*/0);

                        if (hit.handled) {
                            scrollbarDragging_ = true;
                            scrollbarGrabOffsetPx_ = hit.grabOffsetPx;

                            editor_.setViewportTop(hit.top);
                            syncScrollbarInfo();

                            sdlCaptureMouseBestEffort(SDL_TRUE);
                            requestRender();

                            break;
                        }
                    }
                    // 3) Redimensionado manual (solo si no hay hit-test nativo).
                    if (!hitTestEnabled_ && ev.button.button == SDL_BUTTON_LEFT) {
                        const GuiWindowEdge edge =
                            manualResizeEdgeAt(ev.button.x, ev.button.y);
                        if (edge != GuiWindowEdge::None) {
                            startManualResize(edge);
                            break;
                        }
                    }
                    // 4) Arrastre manual de la barra (fallback sin hit-test).
                    if (!hitTestEnabled_ &&
                        ev.button.button == SDL_BUTTON_LEFT &&
                        pointInTitleBar(ev.button.y)) {
                        manualTitleDrag_ = true;
                        sdlCaptureMouseBestEffort(SDL_TRUE);
                        // Reusa la medicion guardada por currentSize().
                        manualDragWinX_ = lastWinX_;
                        manualDragWinY_ = lastWinY_;
                        break;
                    }
                    // Click izquierdo -> MousePress en la celda (0-based).
                    // Medio/derecho -> nullopt (paridad TTY: se traducen a
                    // None, sin gesto). El Editor arma el gesto (cursor +
                    // anchor, sin rango): el drag posterior crea la
                    // selección, el release sin drag es no-op.
                    // Captura explícita best-effort (ver
                    // sdlCaptureMouseBestEffort): con captura el up/motion
                    // fuera de la ventana igual llega acá. SDL ≥2.0.22 ya
                    // hace auto-capture durante el botón, pero la explícita
                    // cubre versiones viejas y deja la intención en el
                    // código; es seguro llamarla aunque ya haya captura.
                    // Si el entorno la deniega (Wayland: SDL_CaptureMouse
                    // devuelve -1), NO es error fatal: el gesto sigue
                    // coherente con lo entregado dentro de la ventana (la
                    // selección congela su borde en la última celda válida
                    // en vez de seguir al puntero de fuera) y el cierre
                    // recae en el release entregado dentro, el sintético de
                    // FOCUS_LOST o el oráculo del tick. Ver degradado en
                    // GuiRunLoop.h y README (--gui en Wayland).
                    // Construcción por fábrica testeada (makeMousePressEvent:
                    // tipo + celda 0-based en un solo punto, igual que el
                    // drag): el gate de arriba y la fábrica son las mismas
                    // funciones que cubren los tests, no una réplica.
                    if (guiPressTypeForButton(ev.button.button).has_value()) {
                        InputEvent ie = makeMousePressEvent(
                            mouseCell(ev.button.x, ev.button.y));
                        editor_.handleEvent(ie);
                        sdlCaptureMouseBestEffort(SDL_TRUE);
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        requestRender();
                    }
                    break;
                }
                case SDL_MOUSEBUTTONUP: {
                    if (scrollbarDragging_ && ev.button.button == SDL_BUTTON_LEFT) {
                        scrollbarDragging_ = false;
                        sdlCaptureMouseBestEffort(SDL_FALSE);
                        break;
                    }
                    if (manualResizeEdge_ != GuiWindowEdge::None) {
                        manualResizeEdge_ = GuiWindowEdge::None;
                        sdlCaptureMouseBestEffort(SDL_FALSE);
                        break;
                    }
                    if (activeTitleButton_ != GuiTitleBarButton::None) {
                        const GuiTitleBarButton action = activeTitleButton_;
                        const GuiTitleBarButton released =
                            titleButtonAtPoint(ev.button.x, ev.button.y);
                        activeTitleButton_ = GuiTitleBarButton::None;
                        titleBar_.setPressed(GuiTitleBarButton::None);
                        sdlCaptureMouseBestEffort(SDL_FALSE);
                        if (released == action) activateTitleButton(action);
                        requestRender();
                        break;
                    }
                    if (manualTitleDrag_) {
                        manualTitleDrag_ = false;
                        sdlCaptureMouseBestEffort(SDL_FALSE);
                        break;
                    }
                    // Release izquierdo -> MouseRelease (cierra el gesto;
                    // sin drag previo es no-op, con drag permanece en
                    // Selección y apaga el autoscroll). Libera la captura
                    // del press en modo best-effort (también cae sola al
                    // perder foco, ver SDL_WINDOWEVENT; si nunca hubo
                    // captura —Wayland— liberar es no-op seguro). Fábrica
                    // testeada igual que el press.
                    if (guiReleaseTypeForButton(ev.button.button).has_value()) {
                        InputEvent ie = makeMouseReleaseEvent(
                            mouseCell(ev.button.x, ev.button.y));
                        editor_.handleEvent(ie);
                        sdlCaptureMouseBestEffort(SDL_FALSE);
                        const auto now = std::chrono::steady_clock::now();
                        editor_.clearExpiredMessages(now);
                        requestRender();
                    }
                    break;
                }
                case SDL_MOUSEMOTION: {
                    if (scrollbarDragging_) {
                        const bool leftHeld = guiLeftHeld(ev.motion.state);
                        const DrawablePoint p = toDrawablePoint(ev.motion.x, ev.motion.y);

                        if (!leftHeld) {
                            scrollbarDragging_ = false;
                            sdlCaptureMouseBestEffort(SDL_FALSE);
                        } else {
                            // resizeBorderPx = 0: el track llega hasta el borde
                            // derecho; el borde de resize (kResizeBorderPts = 3)
                            // se solapa pero el arrastre del scrollbar tiene
                            // prioridad sobre el resize.
                            const int top = gui_->scrollbarTopFromY(
                                p.y, scrollbarGrabOffsetPx_, /*resizeBorderPx=*/0);

                            editor_.setViewportTop(top);
                            syncScrollbarInfo();

                            requestRender();
                        }
                        break;
                    }
                    const GuiTitleBarButton hovered =
                        titleButtonAtPoint(ev.motion.x, ev.motion.y);
                    const bool hoverChanged = titleBar_.setHover(hovered);

                    // --- Cursor de redimensionado en HOVER (sin click) ---
                    // Prioridad explícita (idéntica al hit-test nativo guiTitleBarHitTest):
                    // 1. Scrollbar track -> arrow
                    // 2. Botones de la barra de título (hovered) -> arrow
                    // 3. Operaciones de arrastre activo -> no tocar cursor (deja el del drag)
                    // 4. Bordes de redimensionado -> resize cursor
                    // 5. Resto (zona arrastrable) -> arrow

                    bool inActiveDrag = scrollbarDragging_ ||
                                        manualResizeEdge_ != GuiWindowEdge::None ||
                                        manualTitleDrag_ ||
                                        activeTitleButton_ != GuiTitleBarButton::None ||
                                        guiLeftHeld(ev.motion.state);

                    const int ptX = ev.motion.x;
                    const int ptY = ev.motion.y;
                    // Reusar la medición de currentSize; no se consulta SDL
                    // por evento de motion.
                    const int winW = lastWinW_;
                    const int winH = lastWinH_;

                    // 1. Scrollbar track: cursor arrow (no redimensionable)
                    if (pointInScrollbarTrack(hitState_.scrollbar.get(), ptX, ptY)) {
                        setCursorIfChanged(cursorArrow_);
                    }
                    // 2. Botones de la barra de título: cursor arrow (clickable) - PRIORIDAD sobre bordes
                    else if (hovered != GuiTitleBarButton::None) {
                        setCursorIfChanged(cursorArrow_);
                    }
                    // 3. Si hay arrastre activo, no cambiar cursor (mantiene el del drag)
                    else if (inActiveDrag) {
                        // No hacer nada: el cursor actual es el correcto para el drag
                    }
                    // 4. Bordes de redimensionado (usar lógica pura de GuiWindowGeometry)
                    else {
                        ResizeCursorType type = hoverCursorType(
                            ptX, ptY, winW, winH, kResizeBorderPts);
                        setCursorIfChanged(cursorForType(type));
                    }

                    if (manualResizeEdge_ != GuiWindowEdge::None) {
                        if (!guiLeftHeld(ev.motion.state)) {
                            manualResizeEdge_ = GuiWindowEdge::None;
                            sdlCaptureMouseBestEffort(SDL_FALSE);
                        } else {
                            // Delta acumulado del gesto (desde el inicio del
                            // arrastre, ver startManualResize): se suman los
                            // deltas relativos del mouse y se aplica sobre el
                            // rect inicial del gesto.
                            // Se usa ev.motion.xrel / ev.motion.yrel del
                            // SDL_MOUSEMOTION en curso: SDL2 ya los calcula por
                            // evento, sin acumulacion global pendiente (que es
                            // lo que devuelve SDL_GetRelativeMouseState).
                            manualResizeDragDX_ += ev.motion.xrel;
                            manualResizeDragDY_ += ev.motion.yrel;
                            const GuiWindowRect start{
                                manualResizeWinX_, manualResizeWinY_,
                                manualResizeWinW_, manualResizeWinH_};
                            const GuiWindowRect next = guiResizeWindowRect(
                                start, manualResizeEdge_,
                                manualResizeDragDX_, manualResizeDragDY_, 160, 120);
                            if (next.x != start.x || next.y != start.y) {
                                SDL_SetWindowPosition(win, next.x, next.y);
                            }
                            if (next.w != start.w || next.h != start.h) {
                                SDL_SetWindowSize(win, next.w, next.h);
                            }
                        }
                        if (hoverChanged) requestRender();
                        break;
                    }
                    if (manualTitleDrag_) {
                        if (!guiLeftHeld(ev.motion.state)) {
                            manualTitleDrag_ = false;
                        } else {
                            // Delta acumulado del gesto: se suma el delta relativo
                            // del mouse (ev.motion.xrel / ev.motion.yrel del
                            // SDL_MOUSEMOTION en curso) a la posicion inicial del
                            // arrastre. ev.motion ya representa el movimiento
                            // relativo exacto de este evento, sin acumulacion
                            // pendiente de SDL_GetRelativeMouseState.
                            manualDragWinX_ += ev.motion.xrel;
                            manualDragWinY_ += ev.motion.yrel;
                            SDL_SetWindowPosition(
                                win, manualDragWinX_, manualDragWinY_);
                        }
                        if (hoverChanged) requestRender();
                        break;
                    }
                    if (activeTitleButton_ != GuiTitleBarButton::None) {
                        const GuiTitleBarButton pressed =
                            hovered == activeTitleButton_
                                ? activeTitleButton_
                                : GuiTitleBarButton::None;
                        if (titleBar_.pressed() != pressed) {
                            titleBar_.setPressed(pressed);
                            requestRender();
                        }
                        if (hoverChanged) requestRender();
                        break;
                    }
                    if (hoverChanged) requestRender();
                    // Arrastre con izquierdo presionado -> MouseDrag por
                    // evento (1 línea / 1 celda + autoscroll en bordes en
                    // el Editor). Sin LMask no se emite nada: el Editor ya
                    // ignora drags sin press previo, pero no se genera
                    // tráfico espurio de hover. Sin captura (Wayland cuando
                    // el compositor la deniega) el motion de fuera no llega:
                    // la selección conserva su último borde válido dentro
                    // (coherente, sin rango espurio) hasta el próximo evento
                    // dentro, el release o el cierre por tick/foco.
                    if (!guiLeftHeld(ev.motion.state)) break;
                    InputEvent ie = makeMouseDragEvent(
                        mouseCell(ev.motion.x, ev.motion.y, true));
                    editor_.handleEvent(ie);
                    const auto now = std::chrono::steady_clock::now();
                    editor_.clearExpiredMessages(now);
                    requestRender();
                    break;
                }
                case SDL_MOUSEWHEEL: {
                    // Rueda DISCRETA con acumulación (decisión explícita, ver
                    // GuiSdlMapper.h): el Editor mueve ±3 líneas por evento
                    // (grilla en celdas) y solo-viewport con cursor quieto.
                    // `preciseY` (SDL 2.0.18+) trae la fracción real: un
                    // táctil manda y==0 con fracción, y descartarlo rompería
                    // el scroll suave. Se acumula y cada unidad entera emite
                    // un ScrollUp/Down; la fracción sola nunca sale como 3
                    // líneas. Sin umbral que alcance no hay render (nada
                    // cambió; el resto espera al próximo evento). El tipo por
                    // unidad sale de guiScrollTypeForWheel (única
                    // implementación, la misma que cubren los tests).
                    const double delta = guiWheelDelta(
                        ev.wheel.preciseY, ev.wheel.y,
                        ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED);
                    const int n = guiConsumeWheelDelta(wheelAcc, delta);
                    if (n == 0) break;
                    const auto t = guiScrollTypeForWheel(n > 0 ? 1 : -1);
                    const int m = n > 0 ? n : -n;
                    for (int i = 0; i < m; ++i) {
                        InputEvent ie;
                        ie.type = *t;
                        editor_.handleEvent(ie);
                    }
                    const auto now = std::chrono::steady_clock::now();
                    editor_.clearExpiredMessages(now);
                    requestRender();
                    break;
                }
                default:
                    break;
            }
        };
        // Envuelve el handler con la detección de actividad intermedia del
        // lote (debe definirse DESPUÉS de handleSdlEvent).
        auto processEvent = [&](SDL_Event& ev) {
            handleSdlEvent(ev);
            const Editor::CursorViewSnapshot after =
                editor_.cursorViewSnapshot();
            if (after != batchView) batchCursorActivity = true;
            batchView = after;
        };
        SDL_Event ev;
        bool hadEvent = false;
        // Tope de tiempo del lote: con entrada continua la cola puede no
        // vaciarse nunca. Si ya hay un render pedido, se corta y se pinta
        // para no postergar la presentación/blink; el resto queda para el
        // próximo lote (FIFO, sin descartes).
        const auto batchStart = std::chrono::steady_clock::now();
        while (SDL_PollEvent(&ev)) {
            hadEvent = true;
            // La actividad continua de eventos no puede posponer el polling
            // del watcher: se comprueba también dentro del drenado.
            serviceWatcherIfDue(std::chrono::steady_clock::now());
            processEvent(ev);
            if (renderNeeded &&
                std::chrono::steady_clock::now() - batchStart >=
                    std::chrono::milliseconds(batchCapMs())) {
                break;
            }
        }
        if (renderNeeded) {
            // Un solo render por lote, con el estado final ya aplicado. La
            // actividad intermedia (aunque el cursor haya vuelto al origen)
            // se le informa al renderer antes de pintar.
            if (batchCursorActivity && gui_) gui_->markCursorActivity();
            batchCursorActivity = false;
            renderNeeded = false;
            renderWithBlink();
            // Volver al tope: durante el render pueden haber llegado más
            // eventos; se drenan en el próximo lote en vez de esperar.
            continue;
        }
        if (!hadEvent) {
            // Sin eventos: espera hasta el próximo deadline real (timeout
            // del Editor o cambio de fase del blink, acotado por
            // kMaxIdleWaitMs para el polling del watcher) o hasta el
            // próximo evento de entrada, lo que ocurra primero. Solo se
            // renderiza cuando algo venció (ver idleOnTimeout): los
            // despertares de polling sin cambios no tocan la GPU. Con una
            // operación bloqueante el repintado puede retrasarse (la
            // precisión del deadline es de cálculo, no garantía del SO).
            const auto now = std::chrono::steady_clock::now();
            const int waitMs = computeIdleWaitMs(now);
            if (waitMs <= 0) {
                // Deadline ya vencido: se despacha sin esperar.
                if (idleOnTimeout(now)) updateImeRect();
                continue;
            }
            SDL_ClearError();
            SDL_Event wev;
            const int rc = SDL_WaitEventTimeout(&wev, waitMs);
            if (rc == 1) {
                // Evento durante la espera: se procesa por el camino único
                // (arriba), sin re-push a la cola.
                processEvent(wev);
                continue;
            }
            // rc == 0: timeout normal O error de espera (el contrato SDL 2.0+
            // documentado en SDL_events.h devuelve 0 en ambos casos).
            // Se distingue SOLO a efectos de pacing, nunca de corrección:
            // el texto de SDL_GetError (limpiado antes de esperar, así un
            // error viejo no se confunde con este) decide únicamente si se
            // aplica un retroceso acotado. El camino de deadlines corre
            // igual en ambos casos: un error no provoca renders espurios
            // (idleOnTimeout solo pinta ante deadlines reales) ni bloquea
            // la entrada (la próxima vuelta bombea PollEvent). Nótese que
            // SDL desaconseja el texto como señal de error en general
            // (SDL_error.h); acá un falso positivo solo costaría una
            // espera de 30ms, sin efecto en corrección.
            const char* waitErr = SDL_GetError();
            if (waitErr != nullptr && waitErr[0] != '\0') {
                // Error real de espera: retroceso acotado ANTES de seguir,
                // separado del mecanismo de deadlines (no altera timers ni
                // blink), para no producir un ciclo rápido de reintentos si
                // el fallo persiste.
                SDL_ClearError();
                SDL_Delay(static_cast<Uint32>(kMaxIdleWaitMs));
            }
            if (idleOnTimeout(std::chrono::steady_clock::now())) updateImeRect();
        }
    }

    // Sin SDL_StopTextInput() manual: lo hace ~SdlTextInputGuard (RAII),
    // así cualquier retorno anticipado futuro también limpia el estado.
    freeResizeCursors();
    if (gui_) gui_->setSdlRenderer(nullptr);
    sdlWin_ = nullptr;
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

#endif
