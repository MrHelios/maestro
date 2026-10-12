// Tests del límite SDL del teclado GUI (guiKeyFromSdl: SDL_Keycode/mod ->
// GuiKeyPress). Son los únicos tests que nombran SDL: necesitan HAVE_SDL2 y
// solo usan constantes (SDLK_*/KMOD_*), sin inicializar video ni ventana.
// Sin SDL2 cada caso hace SKIP, no falla.
//
// Cubren la costura que los tests del keymap no ven: Ctrl+Shift, AltGr,
// Caps/Num y keypad, más la composición adaptador -> GuiKeymap.

#include "test_framework.h"

#include "platform/gui/GuiSdlMapper.h"
#include "platform/MouseEvent.h"

TEST(guimouse_pixels_to_cell) {
    // Puro y sin SDL: división entera píxeles/celda -> CellPos 0-based.
    // (Los píxeles acá son del DRAWABLE: el loop ya escaló los puntos del
    // evento con guiScaleCoord antes de llamar.)
    CHECK((guiCellFromPixels(0, 0, 9, 18) == CellPos{0, 0}));
    CHECK((guiCellFromPixels(8, 17, 9, 18) == CellPos{0, 0}));
    CHECK((guiCellFromPixels(9, 18, 9, 18) == CellPos{1, 1}));
    CHECK((guiCellFromPixels(20, 40, 9, 18) == CellPos{2, 2}));
    // Métrica inválida o píxel negativo -> inválida, nunca {0,0} espurio.
    CHECK(!guiCellFromPixels(10, 10, 0, 18).valid());
    CHECK(!guiCellFromPixels(10, 10, 9, 0).valid());
    CHECK(!guiCellFromPixels(-1, 5, 9, 18).valid());
    CHECK(!guiCellFromPixels(5, -1, 9, 18).valid());
}

TEST(guimouse_scale_coord_hidpi) {
    // Puro y sin SDL: puntos <-> drawable. A escala 1 es identidad; a
    // escala 2 los puntos suben 2× (evento -> drawable) y bajan 2× (IME).
    CHECK_EQ(guiScaleCoord(100, 960, 960), 100);
    CHECK_EQ(guiScaleCoord(100, 960, 1920), 200);
    CHECK_EQ(guiScaleCoord(200, 1920, 960), 100);
    CHECK_EQ(guiScaleCoord(0, 960, 1920), 0);
    CHECK_EQ(guiScaleCoord(100, 200, 300), 150);  // escala fraccionaria 1.5×
    // Escala desconocida (<=0): identidad = comportamiento pre-HiDPI.
    CHECK_EQ(guiScaleCoord(100, 0, 1920), 100);
    CHECK_EQ(guiScaleCoord(100, 960, 0), 100);
    CHECK_EQ(guiScaleCoord(100, -5, 1920), 100);
    CHECK_EQ(guiScaleCoord(100, 960, -7), 100);
    // Cadena completa a 2× (ventana 960×600, drawable 1920×1200, celda
    // 9×18): el punto (19,37) es drawable (38,74) = celda (4,4). Sin el
    // escalado daría (2,2): la celda N× chica del bug HiDPI.
    const int px = guiScaleCoord(19, 960, 1920);
    const int py = guiScaleCoord(37, 600, 1200);
    CHECK_EQ(px, 38);
    CHECK_EQ(py, 74);
    const CellPos c = guiCellFromPixels(px, py, 9, 18);
    CHECK_EQ(c.col, 4);
    CHECK_EQ(c.row, 4);
}

TEST(guimouse_press_release_factories_carry_cell) {
    // Las fábricas son el camino canónico CellPos -> InputEvent del loop.
    InputEvent p = makeMousePressEvent(CellPos{3, 4});
    CHECK_EQ(static_cast<int>(p.type), static_cast<int>(InputEventType::MousePress));
    CHECK((p.cell == CellPos{3, 4}));
    InputEvent d = makeMouseDragEvent(CellPos{5, 6});
    CHECK_EQ(static_cast<int>(d.type), static_cast<int>(InputEventType::MouseDrag));
    CHECK((d.cell == CellPos{5, 6}));
    InputEvent r = makeMouseReleaseEvent(CellPos{7, 8});
    CHECK_EQ(static_cast<int>(r.type), static_cast<int>(InputEventType::MouseRelease));
    CHECK((r.cell == CellPos{7, 8}));
}

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

#include "platform/gui/GuiKeymap.h"
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

TEST(guimouse_wheel_delta_precise_first) {
    // Sin SDL (puro): preciseY manda; si es cero, fallback al entero;
    // flipped niega. Ambos cero -> 0.0.
    CHECK_EQ(guiWheelDelta(1.0f, 1, false), 1.0);
    CHECK_EQ(guiWheelDelta(-1.0f, -1, false), -1.0);
    CHECK_EQ(guiWheelDelta(0.25f, 0, false), 0.25);
    CHECK_EQ(guiWheelDelta(0.0f, 1, false), 1.0);
    CHECK_EQ(guiWheelDelta(0.0f, -2, false), -2.0);
    CHECK_EQ(guiWheelDelta(0.0f, 0, false), 0.0);
    CHECK_EQ(guiWheelDelta(1.0f, 1, true), -1.0);
    CHECK_EQ(guiWheelDelta(0.25f, 0, true), -0.25);
    CHECK_EQ(guiWheelDelta(0.0f, 1, true), -1.0);
}

TEST(guimouse_wheel_accumulates_fractions) {
    // Sin SDL (puro): la rueda clásica sale directo; el táctil junta
    // fracciones hasta 1.0; opuestas se cancelan sin eventos fantasma.
    double acc = 0.0;
    CHECK_EQ(guiConsumeWheelDelta(acc, 1.0), 1);
    CHECK_EQ(acc, 0.0);
    CHECK_EQ(guiConsumeWheelDelta(acc, -1.0), -1);
    CHECK_EQ(acc, 0.0);
    CHECK_EQ(guiConsumeWheelDelta(acc, 3.0), 3);
    CHECK_EQ(acc, 0.0);
    // Fracciones exactas en binario (0.25): 3 no alcanzan, la 4ª emite una.
    CHECK_EQ(guiConsumeWheelDelta(acc, 0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, 0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, 0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, 0.25), 1);
    CHECK_EQ(acc, 0.0);
    // Cancelación: 0.5 - 0.5 = 0, ningún evento.
    CHECK_EQ(guiConsumeWheelDelta(acc, 0.5), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, -0.5), 0);
    CHECK_EQ(acc, 0.0);
    // Negativas acumulan igual: 4 × -0.25 = una unidad abajo.
    CHECK_EQ(guiConsumeWheelDelta(acc, -0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, -0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, -0.25), 0);
    CHECK_EQ(guiConsumeWheelDelta(acc, -0.25), -1);
    CHECK_EQ(acc, 0.0);
}

TEST(guimouse_sdl_buttons_left_only) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // Solo el izquierdo arma/cierra gesto (paridad TTY: resto -> None).
    auto press = guiPressTypeForButton(SDL_BUTTON_LEFT);
    CHECK(press.has_value());
    CHECK_EQ(static_cast<int>(*press), static_cast<int>(InputEventType::MousePress));
    CHECK(!guiPressTypeForButton(SDL_BUTTON_MIDDLE).has_value());
    CHECK(!guiPressTypeForButton(SDL_BUTTON_RIGHT).has_value());
    auto rel = guiReleaseTypeForButton(SDL_BUTTON_LEFT);
    CHECK(rel.has_value());
    CHECK_EQ(static_cast<int>(*rel), static_cast<int>(InputEventType::MouseRelease));
    CHECK(!guiReleaseTypeForButton(SDL_BUTTON_MIDDLE).has_value());
    CHECK(!guiReleaseTypeForButton(SDL_BUTTON_RIGHT).has_value());
#endif
}

TEST(guimouse_sdl_drag_gated_by_lmask) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    CHECK(guiLeftHeld(SDL_BUTTON_LMASK));
    CHECK(!guiLeftHeld(0));
    // Solo-LMask en un combinado también arde; otros botones solos no.
    CHECK(guiLeftHeld(SDL_BUTTON_LMASK | SDL_BUTTON_RMASK));
    CHECK(!guiLeftHeld(SDL_BUTTON_RMASK));
    CHECK(!guiLeftHeld(SDL_BUTTON_MMASK));
#endif
}

TEST(guimouse_sdl_wheel_sign) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // y>0 arriba, y<0 abajo (el Editor ya aplica ±3 por evento; el loop
    // repite según |y|). y==0 (solo horizontal) no produce evento.
    auto up = guiScrollTypeForWheel(1);
    CHECK(up.has_value());
    CHECK_EQ(static_cast<int>(*up), static_cast<int>(InputEventType::ScrollUp));
    CHECK_EQ(static_cast<int>(*guiScrollTypeForWheel(3)),
             static_cast<int>(InputEventType::ScrollUp));
    auto down = guiScrollTypeForWheel(-1);
    CHECK(down.has_value());
    CHECK_EQ(static_cast<int>(*down), static_cast<int>(InputEventType::ScrollDown));
    CHECK(!guiScrollTypeForWheel(0).has_value());
#endif
}

TEST(guimouse_sdl_focus_loss_ends_gesture) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // Solo FOCUS_LOST sintetiza release (la captura cae sola y el up puede
    // ir a otra ventana). LEAVE no cierra: el arrastre fuera sigue con
    // captura y el autoscroll debe continuar.
    CHECK(guiFocusLossEndsGesture(SDL_WINDOWEVENT_FOCUS_LOST));
    CHECK(!guiFocusLossEndsGesture(SDL_WINDOWEVENT_FOCUS_GAINED));
    CHECK(!guiFocusLossEndsGesture(SDL_WINDOWEVENT_ENTER));
    CHECK(!guiFocusLossEndsGesture(SDL_WINDOWEVENT_LEAVE));
    CHECK(!guiFocusLossEndsGesture(SDL_WINDOWEVENT_RESIZED));
    CHECK(!guiFocusLossEndsGesture(SDL_WINDOWEVENT_SIZE_CHANGED));
    CHECK(!guiFocusLossEndsGesture(SDL_WINDOWEVENT_SHOWN));
    CHECK(!guiFocusLossEndsGesture(SDL_WINDOWEVENT_HIDDEN));
    CHECK(!guiFocusLossEndsGesture(SDL_WINDOWEVENT_MINIMIZED));
    CHECK(!guiFocusLossEndsGesture(SDL_WINDOWEVENT_CLOSE));
#endif
}
