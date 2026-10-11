#pragma once

// GuiSdlMapper — límite SDL del teclado GUI (único lugar que nombra
// tipos/constantes SDL para teclas: SDL_Keycode, KMOD_*).
//
// Convierte `SDL_Keysym -> GuiKeyPress` (vocabulario propio, GuiKey.h) para
// que GuiKeymap y sus tests nunca vean SDL. Solo existe con HAVE_SDL2; sin
// SDL este header es vacío y GuiKeymap se prueba con GuiKeyPress directo.
//
// Reglas del adaptador (testeadas en tests/unit/test_guisdlmapper.cpp):
//   - CapsLock/NumLock (KMOD_CAPS/KMOD_NUM) se pelan: nunca llegan al keymap.
//   - Shift se conserva (el keymap lo ignora en la resolución, paridad TTY).
//   - AltGr (KMOD_MODE en X11) cuenta como Alt: el KEYDOWN de la letra queda
//     sin enlace y el carácter (@/|/\/{) llega por SDL_TEXTINPUT.
//   - Letras 'A'-'Z' se normalizan a minúscula lógica (KeyA..KeyZ).
//   - Keypad mínimo: solo SDLK_KP_ENTER se traduce; el KP numérico queda
//     Unknown para no secuestrar dígitos con NumLock.
#ifdef HAVE_SDL2

#include <SDL2/SDL.h>

#include "platform/gui/GuiKey.h"

inline GuiKeyPress guiKeyFromSdl(SDL_Keycode sym, Uint16 mod, bool repeat) {
    GuiKeyPress p;
    p.repeat = repeat;
    p.mods.ctrl = (mod & KMOD_CTRL) != 0;
    p.mods.shift = (mod & KMOD_SHIFT) != 0;
    p.mods.alt = (mod & (KMOD_ALT | KMOD_MODE)) != 0;
    switch (sym) {
        case SDLK_ESCAPE: p.sym = GuiKeySym::Escape; break;
        case SDLK_RETURN: p.sym = GuiKeySym::Enter; break;
        case SDLK_KP_ENTER: p.sym = GuiKeySym::KeyPadEnter; break;
        case SDLK_TAB: p.sym = GuiKeySym::Tab; break;
        case SDLK_BACKSPACE: p.sym = GuiKeySym::Backspace; break;
        case SDLK_DELETE: p.sym = GuiKeySym::Delete; break;
        case SDLK_LEFT: p.sym = GuiKeySym::Left; break;
        case SDLK_RIGHT: p.sym = GuiKeySym::Right; break;
        case SDLK_UP: p.sym = GuiKeySym::Up; break;
        case SDLK_DOWN: p.sym = GuiKeySym::Down; break;
        case SDLK_HOME: p.sym = GuiKeySym::Home; break;
        case SDLK_END: p.sym = GuiKeySym::End; break;
        case SDLK_PAGEUP: p.sym = GuiKeySym::PageUp; break;
        case SDLK_PAGEDOWN: p.sym = GuiKeySym::PageDown; break;
        default: break;
    }
    if (p.sym == GuiKeySym::Unknown) {
        int lower = sym;
        if (lower >= 'A' && lower <= 'Z') lower += ('a' - 'A');
        if (lower >= 'a' && lower <= 'z') {
            p.sym = static_cast<GuiKeySym>(
                static_cast<int>(GuiKeySym::KeyA) + (lower - 'a'));
        }
        // NOTA keypad: SDLK_KP_1..SDLK_KP_9 (y el resto del KP numérico)
        // quedan a propósito en Unknown para no secuestrar dígitos con
        // NumLock: solo KeyPadEnter está mapeado (alcance mínimo).
    }
    return p;
}

#endif  // HAVE_SDL2
