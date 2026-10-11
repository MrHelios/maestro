#pragma once

#include <chrono>

#include "platform/gui/GuiKeymap.h"
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
// loop duerme como máximo 30ms antes de reinyectar y repintar, lo que en
// la práctica anima el blink sin deadline propio; no hay temporizador
// independiente y con eventos pendientes o bloqueos el repintado puede
// retrasarse.
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
// El teclado completo vive en GuiKeymap (SDL_Keycode/SDL_Keymod ->
// GuiKeyPress en el límite SDL del .cpp -> InputEvent): flechas, Home/End,
// PgUp/PgDn, Enter (incl. keypad), Backspace/Delete, Escape, Ctrl+K/S/Q/U/
// Y/Z. El texto confirmado (incl. UTF-8 y AltGr) llega por SDL_TEXTINPUT →
// InsertChar por celda (utf8::splitCells); KEYDOWN nunca lleva texto
// imprimible (sin doble inserción). La composición provisional
// (SDL_TEXTEDITING) se descarta a propósito hasta tener superficie de
// pintado (nunca se inserta en el Document).
class GuiRenderer;

class GuiRunLoop {
public:
    explicit GuiRunLoop(Editor& editor);
    void setGuiRenderer(GuiRenderer* r) { gui_ = r; }
    // Tabla tecla -> Evento que el SDL_KEYDOWN consulta (remapeable en
    // tiempo de ejecución). Única vía pública: la interfaz GUI; el tipo
    // concreto GuiKeymap es detalle privado, nunca se expone.
    IGuiKeymap& keymapIface() { return keymap_; }
    const IGuiKeymap& keymapIface() const { return keymap_; }
    // Devuelve código de salida (0 ok, 1 sin SDL2).
    int run();

    // Camino idle (vuelta sin eventos) extraído para tests: EXACTAMENTE lo
    // que run() hace sin eventos, menos el SDL_Delay (propio del loop real
    // con ventana). tick(now) expira mensajes con timeout y avanza el
    // autoscroll; renderFrame repinta. Sin SDL: solo toca Editor + GuiRenderer.
    void idleStep(std::chrono::steady_clock::time_point now);
    // Delay del idle derivado del Editor (clamp 0..30ms; indefinido → 30).
    // Puro y sin SDL: la misma fórmula que run() pasa a SDL_Delay. Solo
    // acota la pausa de la rama sin eventos; no garantiza por sí misma la
    // cadencia de repintado.
    int idleDelayMs(std::chrono::steady_clock::time_point now) const;

private:
    Editor& editor_;
    GuiRenderer* gui_ = nullptr;
    // Significado de cada tecla. Se consulta en cada SDL_KEYDOWN;
    // remapeable en tiempo de ejecución vía keymapIface().
    GuiKeymap keymap_;
    // Posiciona la ventana candidata del IME en la celda del cursor lógico
    // (no-op sin SDL2). Si no hay cursor válido conserva el último rect de
    // SDL a propósito (ver política en el comentario de clase): la
    // candidata queda anclada a la última celda válida. Se llama después
    // de cada render.
    void updateImeRect() const;
};
