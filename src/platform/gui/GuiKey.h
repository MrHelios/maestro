#pragma once

// GuiKey: vocabulario propio de teclas/modificadores del backend GUI.
//
// P1 (independencia real de SDL): este header NO incluye <SDL2/SDL.h> y no
// nombra ningún tipo ni constante SDL. Los valores son canónicos del keymap
// GUI; la adaptación `SDL_Keysym -> GuiKeyPress` vive en el límite SDL
// (GuiRunLoop.cpp, solo bajo HAVE_SDL2) y los tests usan estos nombres sin
// números mágicos de SDL ni necesidad de headers SDL.
//
// - `sym` cubre solo lo que el keymap necesita: navegación/edición y las
//   letras A-Z para combos Ctrl+*. El texto imprimible normal (incluido
//   UTF-8 y AltGr) NUNCA pasa por aquí: llega por SDL_TEXTINPUT y el loop
//   lo trocea con utf8::splitCells() en InsertChar por celda. KEYDOWN es
//   comandos/navegación/modificadores; TEXTINPUT es texto Unicode.
// - Los modificadores CapsLock/NumLock NO existen en este vocabulario: el
//   adaptador SDL los pela (KMOD_CAPS/KMOD_NUM) antes de construir el press,
//   así que nunca rompen una coincidencia.
// - Shift es SIGNIFICATIVO en letras con Ctrl (Ctrl+S vs Ctrl+Shift+S son
//   enlaces distintos: Guardar vs Guardar como) y se IGNORA en lo
//   no-textual (paridad v0.5 TTY: la selección se entra con 's', no con
//   Shift; ver Terminal.cpp simpleEscapeForm). Shift+Flecha == Flecha,
//   pero Shift+letra-Ctrl == enlace propio. Más aún: en lo no-textual
//   NINGÚN modificador extra bloquea la acción (Ctrl+Alt+Escape sigue
//   siendo Escape, Ctrl+Left sigue siendo MoveLeft): paridad TTY
//   deliberada (el TTY no distingue esas combinaciones), en contraste con
//   las letras, donde Alt siempre deja la pulsación sin enlace para que los
//   layouts con AltGr no disparen comandos accidentales.
// - AltGr: comportamiento ACTUAL (no garantía universal de layouts). En X11
//   el adaptador ve KMOD_MODE y lo cuenta como Alt: el KEYDOWN de la letra
//   queda sin enlace y el carácter (@/|/\/{ según layout) llega por
//   SDL_TEXTINPUT. Otros layouts/plataformas pueden variar; lo documentado
//   es lo observado, no un contrato con todos los teclados.
// - Alcance de keypad explícito: KeyPadEnter siempre, más KP_7/KP_1/KP_9/
//   KP_3 (Inicio/Fin/RePag/AvPag) SOLO con NumLock apagado —el adaptador
//   los traduce únicamente sin KMOD_NUM, porque con NumLock producen
//   dígitos por TEXTINPUT—. El resto del KP numérico llega como Unknown a
//   propósito para no secuestrar dígitos con NumLock activado.
enum class GuiKeySym {
    Unknown,
    Escape,
    Enter,
    KeyPadEnter,
    Tab,
    Backspace,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    KeyA,
    KeyB,
    KeyC,
    KeyD,
    KeyE,
    KeyF,
    KeyG,
    KeyH,
    KeyI,
    KeyJ,
    KeyK,
    KeyL,
    KeyM,
    KeyN,
    KeyO,
    KeyP,
    KeyQ,
    KeyR,
    KeyS,
    KeyT,
    KeyU,
    KeyV,
    KeyW,
    KeyX,
    KeyY,
    KeyZ,
};

struct GuiModifiers {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

// Pulsación normalizada: tecla lógica + modificadores (sin Caps/Num) +
// flag de repetición de SDL_KEYDOWN (repeat != 0). El consumidor decide la
// política de repetición vía IGuiKeymap::isRepeatable y el contexto:
// navegación/borrado repiten siempre; Enter solo cuando el Editor está en
// un estado donde inserta una fila (Interaccion). GuiRunLoop calcula ese
// contexto y lo pasa a mapKey, que no conoce modos.
struct GuiKeyPress {
    GuiKeySym sym = GuiKeySym::Unknown;
    GuiModifiers mods;
    bool repeat = false;
};

// Enlace remapeable (sym + ctrl/alt/shift).
// Vive aquí —y no en el keymap concreto— para que la interfaz IGuiKeymap
// pueda ofrecer bindKey/unbindKey/boundKey sin exponer el tipo concreto.
// Shift solo decide en letras con Ctrl (Ctrl+S vs Ctrl+Shift+S); en lo
// no-textual se normaliza al guardar y no distingue enlaces.
struct GuiKeyBinding {
    GuiKeySym sym = GuiKeySym::Unknown;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool operator<(const GuiKeyBinding& o) const {
        if (sym != o.sym) return sym < o.sym;
        if (ctrl != o.ctrl) return ctrl < o.ctrl;
        if (alt != o.alt) return alt < o.alt;
        return shift < o.shift;
    }
};
