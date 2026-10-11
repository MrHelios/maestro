// Tests del límite SDL del teclado GUI (guiKeyFromSdl: SDL_Keycode/mod ->
// GuiKeyPress). Son los únicos tests que nombran SDL: necesitan HAVE_SDL2 y
// solo usan constantes (SDLK_*/KMOD_*), sin inicializar video ni ventana.
// Sin SDL2 cada caso hace SKIP, no falla.
//
// Cubren la costura que los tests del keymap no ven: Ctrl+Shift, AltGr,
// Caps/Num y keypad, más la composición adaptador -> GuiKeymap.

#include "test_framework.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

#include "platform/gui/GuiKeymap.h"
#include "platform/gui/GuiSdlMapper.h"
#include "platform/InputEvent.h"
#endif

TEST(guisdl_letters_case_and_digits) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    GuiKeyPress a = guiKeyFromSdl(SDLK_a, 0, false);
    CHECK_EQ(static_cast<int>(a.sym), static_cast<int>(GuiKeySym::KeyA));
    CHECK(!a.mods.ctrl && !a.mods.alt && !a.mods.shift && !a.repeat);
    // Mayúscula lógica ('A' == 65, sin SDLK_A dedicado): misma tecla.
    GuiKeyPress upper = guiKeyFromSdl((SDL_Keycode)'A', KMOD_SHIFT, false);
    CHECK_EQ(static_cast<int>(upper.sym), static_cast<int>(GuiKeySym::KeyA));
    CHECK(upper.mods.shift && !upper.mods.ctrl && !upper.mods.alt);
    GuiKeyPress z = guiKeyFromSdl(SDLK_z, 0, false);
    CHECK_EQ(static_cast<int>(z.sym), static_cast<int>(GuiKeySym::KeyZ));
    // Dígitos: no hay tecla lógica (van por TEXTINPUT).
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_0, 0, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
#endif
}

TEST(guisdl_control_keys) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_ESCAPE, 0, false).sym),
             static_cast<int>(GuiKeySym::Escape));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_RETURN, 0, false).sym),
             static_cast<int>(GuiKeySym::Enter));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_ENTER, 0, false).sym),
             static_cast<int>(GuiKeySym::KeyPadEnter));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_TAB, 0, false).sym),
             static_cast<int>(GuiKeySym::Tab));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_BACKSPACE, 0, false).sym),
             static_cast<int>(GuiKeySym::Backspace));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_DELETE, 0, false).sym),
             static_cast<int>(GuiKeySym::Delete));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_LEFT, 0, false).sym),
             static_cast<int>(GuiKeySym::Left));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_RIGHT, 0, false).sym),
             static_cast<int>(GuiKeySym::Right));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_UP, 0, false).sym),
             static_cast<int>(GuiKeySym::Up));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_DOWN, 0, false).sym),
             static_cast<int>(GuiKeySym::Down));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_HOME, 0, false).sym),
             static_cast<int>(GuiKeySym::Home));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_END, 0, false).sym),
             static_cast<int>(GuiKeySym::End));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_PAGEUP, 0, false).sym),
             static_cast<int>(GuiKeySym::PageUp));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_PAGEDOWN, 0, false).sym),
             static_cast<int>(GuiKeySym::PageDown));
#endif
}

TEST(guisdl_keypad_numeric_stays_unknown) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // Alcance mínimo: el KP numérico no se traduce a Home/End (secuestraría
    // dígitos con NumLock); solo KP_Enter está mapeado.
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_7, 0, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_1, KMOD_NUM, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_0, 0, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
#endif
}

TEST(guisdl_ctrl_shift_combo) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // Ctrl+Shift+K: ambos se conservan (el keymap ignora Shift y resuelve
    // Prefix por el Ctrl).
    GuiKeyPress p = guiKeyFromSdl(SDLK_k, KMOD_CTRL | KMOD_SHIFT, false);
    CHECK_EQ(static_cast<int>(p.sym), static_cast<int>(GuiKeySym::KeyK));
    CHECK(p.mods.ctrl && p.mods.shift && !p.mods.alt);
    GuiKeymap km;
    CHECK_EQ(static_cast<int>(*km.mapKey(p)), static_cast<int>(InputEventType::Prefix));
    // El flag repeat se propaga tal cual (la política vive en el keymap).
    CHECK(guiKeyFromSdl(SDLK_LEFT, 0, true).repeat);
    CHECK(!guiKeyFromSdl(SDLK_LEFT, 0, false).repeat);
#endif
}

TEST(guisdl_altgr_counts_as_alt) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // AltGr (KMOD_MODE en X11) cuenta como Alt: el KEYDOWN de la letra queda
    // sin enlace en el keymap y el carácter real llega por SDL_TEXTINPUT.
    GuiKeyPress p = guiKeyFromSdl(SDLK_a, KMOD_MODE, false);
    CHECK(p.mods.alt && !p.mods.ctrl);
    CHECK_EQ(static_cast<int>(p.sym), static_cast<int>(GuiKeySym::KeyA));
    GuiKeymap km;
    CHECK(!km.mapKey(p).has_value());
    // Igual con Alt clásico.
    CHECK(guiKeyFromSdl(SDLK_a, KMOD_ALT, false).mods.alt);
#endif
}

TEST(guisdl_caps_num_stripped) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // CapsLock/NumLock nunca llegan al keymap ni rompen coincidencias.
    GuiKeyPress p = guiKeyFromSdl(SDLK_k, KMOD_CTRL | KMOD_CAPS | KMOD_NUM, false);
    CHECK(p.mods.ctrl && !p.mods.alt && !p.mods.shift);
    GuiKeymap km;
    CHECK_EQ(static_cast<int>(*km.mapKey(p)), static_cast<int>(InputEventType::Prefix));
    GuiKeyPress nav = guiKeyFromSdl(SDLK_LEFT, KMOD_CAPS | KMOD_NUM, false);
    CHECK(!nav.mods.shift && !nav.mods.ctrl && !nav.mods.alt);
    CHECK_EQ(static_cast<int>(*km.mapKey(nav)), static_cast<int>(InputEventType::MoveLeft));
#endif
}

TEST(guisdl_plain_letter_never_maps) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // Costura completa: 'A' sin Ctrl no resuelve por KEYDOWN en ninguna
    // variante de mods (el texto va por SDL_TEXTINPUT).
    GuiKeymap km;
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'A', 0, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'A', KMOD_SHIFT, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'A', KMOD_CAPS, false)).has_value());
#endif
}
