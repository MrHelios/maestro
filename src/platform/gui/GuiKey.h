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
//   lo trocea con utf8::splitCells() en InsertChar por celda.
// - Los modificadores CapsLock/NumLock NO existen en este vocabulario: el
//   adaptador SDL los pela (KMOD_CAPS/KMOD_NUM) antes de construir el press,
//   así que nunca rompen una coincidencia.
// - Shift se conserva en el struct pero se IGNORA en la resolución (paridad
//   v0.5 TTY: la selección se entra con 's', no con Shift; ver
//   Terminal.cpp simpleEscapeForm). Shift+Flecha == Flecha.
// - Alcance de keypad mínimo y explícito: solo KeyPadEnter está mapeado.
//   Los KP numéricos (KP_7/KP_1/...) llegan como Unknown a propósito para
//   no secuestrar dígitos con NumLock activado.
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
// política de repetición vía IGuiKeymap::isRepeatable (GuiRunLoop la aplica
// dentro de GuiKeymap::mapKey: las acciones no repetibles devuelven
// std::nullopt cuando repeat == true).
struct GuiKeyPress {
    GuiKeySym sym = GuiKeySym::Unknown;
    GuiModifiers mods;
    bool repeat = false;
};

// Enlace remapeable (sym + ctrl/alt; shift se ignora en la resolución).
// Vive aquí —y no en el keymap concreto— para que la interfaz IGuiKeymap
// pueda ofrecer bindKey/unbindKey/boundKey sin exponer el tipo concreto.
struct GuiKeyBinding {
    GuiKeySym sym = GuiKeySym::Unknown;
    bool ctrl = false;
    bool alt = false;
    bool operator<(const GuiKeyBinding& o) const {
        if (sym != o.sym) return sym < o.sym;
        if (ctrl != o.ctrl) return ctrl < o.ctrl;
        return alt < o.alt;
    }
};
