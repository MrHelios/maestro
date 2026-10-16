#pragma once

#include <chrono>

#include "platform/gui/GuiKeymap.h"
#include "platform/gui/GuiOptions.h"
#include "platform/gui/IGuiKeymap.h"

class Editor;

// GuiRunLoop: dueño del loop SDL2 (ventana + fondo + cerrar; fuente SDL_ttf
// con métricas reales para el resize; chrome StatusBar + MessageBar +
// cursor en prompts; contenido del editor y listas modales).
//
// Cursor GUI: Bloque fijo (navegación) / Barra con parpadeo 530ms
// (inserción + prompts) + ancla IME (SDL_SetTextInputRect en la celda del
// cursor lógico). El parpadeo lo resuelve GuiRenderer (fase ON/OFF); el loop
// cumple su contrato de reloj inyectando el tiempo (setBlinkNow) antes de
// cada render y actualiza el rect IME después. En la rama sin eventos el
// loop espera hasta el próximo deadline real (ver computeIdleWaitMs:
// mínimo entre el timeout del Editor y el cambio de fase del blink,
// acotado por kMaxIdleWaitMs) con SDL_WaitEventTimeout, que despierta ante
// el próximo evento de entrada o al vencer la espera. Solo se renderiza
// cuando algo venció (timer del Editor o cambio de fase): los despertares
// de polling sin cambios no solicitan ningún render (ver idleOnTimeout).
// Con eventos pendientes o bloqueos el repintado puede retrasarse: el
// deadline se calcula con precisión de ms, pero la latencia real depende
// de la planificación del SO (SDL no garantiza el instante exacto).
//
// INVARIANTE DE RENDER: cada render recibe el tiempo actualizado del cursor
// (setBlinkNow) antes de pintar; los despertares que no cambian el estado
// visible ni requieren procesar un timer no solicitan otro render.
// INVARIANTE DEL WATCHER: el servicio del watcher tiene periodicidad propia
// (watcherServiceDue): la espera inactiva limita el tiempo hasta el próximo
// polling, y la actividad continua de eventos no puede posponerlo
// indefinidamente (se comprueba también dentro del drenado de eventos).
// kMaxIdleWaitMs es el máximo intervalo de servicio BUSCADO, sujeto a la
// planificación del SO, no una garantía temporal absoluta.
//
// POLÍTICA TEXT INPUT: SDL_StartTextInput() se mantiene activo durante TODO
// el run (también en navegación) para que SDL gestione la composición de
// forma continua; alternarlo por modo la cortaría al cambiar entre
// Navegación/Interacción/prompts. Un guard RAII (SdlTextInputGuard, ver
// .cpp) garantiza SDL_StopTextInput() en todos los caminos de salida.
// POLÍTICA IME INVÁLIDO: si el cursor no es válido se conserva el último
// rect de SDL (no se oculta ni se detiene la entrada, para no matar una
// composición en curso).
//
// Espejo de TtyRunLoop pero sin terminal: abre una ventana SDL2 y bombea
// SDL_PollEvent hacia la fachada neutra del Editor
// (resize/handleEvent/tick/renderFrame). Sin friend: solo usa API pública.
//
// El teclado completo vive en GuiKeymap (SDL_Keysym se adapta
// a vocabulario propio (GuiKey.h, sin SDL) en el límite SDL del .cpp -> InputEvent): flechas, Home/End,
// PgUp/PgDn, Enter (incl. keypad), Backspace/Delete, Escape, Ctrl+K/S/Q/U/
// Y/Z. El texto confirmado (incl. UTF-8 y AltGr) llega por SDL_TEXTINPUT →
// InsertChar por celda (utf8::splitCells); KEYDOWN nunca lleva texto
// imprimible (sin doble inserción). La composición provisional
// (SDL_TEXTEDITING) se descarta a propósito hasta tener superficie de
// pintado (nunca se inserta en el Document).
// El mouse vive en el mismo límite (GuiSdlMapper.h, único que nombra
// SDL_BUTTON_*/SDL_MOUSEWHEEL_*/SDL_WINDOWEVENT_FOCUS_LOST): puntos de
// evento -> píxeles de drawable (guiScaleCoord, obligatorio con
// ALLOW_HIGHDPI: a escala N los puntos son N× menores) -> CellPos 0-based
// (división por la celda escalada o fallback escalado) -> MousePress/Drag/Release
// (solo botón izquierdo; medio/derecho se ignoran, paridad TTY) y rueda ->
// ScrollUp/Down (discreta con acumulación: preciseY se junta hasta la unidad
// y cada unidad emite un evento; el Editor ya aplica ±3 líneas
// solo-viewport). El Motion solo emite drag con LMask (sin tráfico
// de hover). El resize también usa el drawable (no SDL_GetWindowSize): con
// HiDPI la grilla cols/filas = drawable/celda (guiGridSize en GuiSdlMapper.h,
// pura y testeada: fallback canónico, clamp >=1), que es el espacio donde el
// renderer dibuja (sin logical size/scale) y donde vive la celda TTF.
// CONTRATO VISUAL-ESTABLE (P1): la fuente se escala con el drawable
// (guiScaledFontPixels: 16 -> 32 a 2×) y el fallback por eje (guiScaleSize),
// así la grilla se mantiene (~106×33) en vez de duplicarse con texto diminuto.
// Anti-release-perdido en 3 capas: captura explícita en el press (el up
// fuera igual llega; el auto-capture ≥2.0.22 no se da por sentado),
// release sintético en FOCUS_LOST (la captura cae sola y el up puede ir a
// otra ventana) y oráculo SDL_GetGlobalMouseState como respaldo del tick
// (consulta al OS, no el caché del último pump). Al recuperar el foco
// (FOCUS_GAINED) se repinta y se recalcula el ancla IME desde la geometría
// actual del cursor (el rect guardado pudo quedar obsoleto en el medio;
// sin cursor válido aún rige el último conocido). El idle (tick+render al
// vencer el deadline del Editor) ya avanza el autoscroll sin mover el mouse.
class GuiRenderer;

class GuiRunLoop {
public:
    GuiRunLoop(Editor& editor, const GuiOptions& opts = GuiOptions{});
    void setGuiRenderer(GuiRenderer* r) { gui_ = r; }
    // Tabla tecla -> Evento que el SDL_KEYDOWN consulta (remapeable en
    // tiempo de ejecución). Única vía pública: la interfaz GUI; el tipo
    // concreto GuiKeymap es detalle privado, nunca se expone.
    IGuiKeymap& keymapIface() { return keymap_; }
    const IGuiKeymap& keymapIface() const { return keymap_; }
    // Devuelve código de salida (0 ok, 1 sin SDL2).
    int run();

    // Camino de render idle (tick+render con tiempo de blink inyectado)
    // extraído para tests. run() lo usa vía idleOnTimeout SOLO cuando venció
    // un deadline real; los wakes de polling sin cambios no lo llaman.
    // tick(now) expira mensajes con timeout y avanza el autoscroll;
    // renderFrame repinta. Sin bombeo externo a propósito: run() sirve al
    // watcher por periodicidad propia (serviceWatcherIfDue), así el camino
    // idle no lo repite. Quien lo llame directo (tests) bombea antes vía
    // editor_.pollExternalEvents(), igual que run(). Sin SDL: solo toca
    // Editor + GuiRenderer.
    void idleStep(std::chrono::steady_clock::time_point now);
    // Cota superior de la espera idle (ms): máximo intervalo de servicio
    // BUSCADO para el polling del watcher (único consumidor real de la
    // cota: SDL no multiplexa el fd de inotify con los eventos de ventana
    // y sin cota un cambio externo en disco no despertaría al loop). No es
    // cadencia de animación ni garantía temporal absoluta. El clipboard GUI
    // es síncrono y no justifica cota alguna.
    static constexpr int kMaxIdleWaitMs = 30;
    // Espera idle hasta el próximo deadline real (ms para SDL_WaitEventTimeout):
    // mínimo entre el timeout del Editor (mensaje/autoscroll; -1 = ausente)
    // y el cambio de fase del blink (ausente = sin animación pendiente),
    // acotado por kMaxIdleWaitMs. Puro y sin SDL. Los deadlines ausentes no
    // intervienen en el mínimo (un -1 nunca tapa un deadline real).
    // PRECONDICIÓN: los deadlines vencidos (== 0) se despachan sin esperar
    // (ver idleOnTimeout); con esta precondición el resultado es > 0 en la
    // práctica (el blink nunca devuelve 0: el borde da 1).
    int computeIdleWaitMs(std::chrono::steady_clock::time_point now) const;
    // Delay del idle derivado del Editor (compatibilidad: delega en
    // computeIdleWaitMs). Los tests nuevos usan computeIdleWaitMs.
    int idleDelayMs(std::chrono::steady_clock::time_point now) const;
    // ¿Toca servir al watcher? Periodicidad propia del polling externo:
    // true si pasaron >= kMaxIdleWaitMs desde el último servicio (o si
    // nunca se sirvió). Pura y testeable; run() la comprueba al tope de
    // cada vuelta Y dentro del drenado de eventos.
    static bool watcherServiceDue(
        std::chrono::steady_clock::time_point lastService,
        std::chrono::steady_clock::time_point now) {
        return (now - lastService) >=
               std::chrono::milliseconds(kMaxIdleWaitMs);
    }
    // Maneja un wake por timeout de la espera idle, sin SDL (testeable):
    // sirve al watcher si toca, y ejecuta tick/render SOLO si venció un
    // deadline real (timer del Editor -> tick+render; cambio de fase del
    // blink -> render sin tick). La fase se registra DESPUÉS del render
    // (el render puede resetear el ancla) y la rama del blink se omite sin
    // animación pendiente. Un wake de polling sin cambios no renderiza ni
    // presenta nada. Devuelve true si renderizó. No toca el
    // rect IME (run() lo actualiza tras un render, igual que en idleStep).
    bool idleOnTimeout(std::chrono::steady_clock::time_point now);

private:
    Editor& editor_;
    GuiOptions options_;
    GuiRenderer* gui_ = nullptr;
    // Significado de cada tecla. Se consulta en cada SDL_KEYDOWN;
    // remapeable en tiempo de ejecución vía keymapIface().
    GuiKeymap keymap_;
    // Fase del blink tras el último render (para detectar el cambio de fase
    // en idleOnTimeout sin render especulativo). Init ON: sin ninguna
    // inyección la fase es ON por contrato del renderer. Se actualiza en
    // cada camino con render (idleStep y renderWithBlink en run()).
    bool lastBlinkPhaseOn_ = true;
    // Último servicio del watcher (pollExternalEvents). Reloj de la
    // periodicidad propia: epoch = "nunca servido" = debido de inmediato.
    std::chrono::steady_clock::time_point lastWatcherService_{};
    // Sirve al watcher si vence su periodicidad (actualiza el reloj).
    // Sin SDL: solo Editor + reloj.
    void serviceWatcherIfDue(std::chrono::steady_clock::time_point now);
    // Handles SDL vivos durante run() (opacos para no meter SDL en el
    // header): los necesita updateImeRect para devolver el rect a puntos de
    // ventana (SDL_SetTextInputRect habla en puntos, no en píxeles del
    // drawable). Se fijan tras crear ventana/renderer y se limpian en el
    // teardown; fuera de run() son nullptr y updateImeRect es no-op.
    void* sdlWin_ = nullptr;
    void* sdlRen_ = nullptr;
    // Posiciona la ventana candidata del IME en la celda del cursor lógico
    // (no-op sin SDL2). Si no hay cursor válido conserva el último rect de
    // SDL a propósito (ver política en el comentario de clase): la
    // candidata queda anclada a la última celda válida. Se llama después
    // de cada render.
    void updateImeRect() const;
};
