#include "test_framework.h"

#include <cstdint>
#include <vector>

#include "platform/gui/GuiSdlMapper.h"
#include "platform/InputEvent.h"
#include "platform/MouseEvent.h"
#include "platform/gui/GuiKeymap.h"
#include "rendering/gui/GuiFont.h"
#include "rendering/gui/GuiRenderer.h"

#define private public
#include "app/Editor.h"
#undef private

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

TEST(gui_sdl_letters_case_and_digits) {
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

TEST(gui_sdl_control_keys) {
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

// SDL -> GuiKeyPress -> GuiKeymap -> Editor (con soft-wrap .md): 
// End llega al final exclusivo de la línea en la última fila visual.
TEST(gui_sdl_end_key_reaches_line_end_in_markdown) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // Configurar Editor con .md y soft-wrap (ancho ~12 columnas).
    Editor ed;
    ed.active().filename = "notes.md";
    ed.active().document.restore({std::string(30, 'a')});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 0;
    ed.resize(10, 19);  // gutter 7 -> textWidth 12

    // SDLK_END -> GuiKeyPress -> GuiKeymap -> InputEvent -> Editor.
    GuiKeyPress gp = guiKeyFromSdl(SDLK_END, 0, false);
    CHECK_EQ(static_cast<int>(gp.sym), static_cast<int>(GuiKeySym::End));

    GuiKeymap km;
    const auto type = km.mapKey(gp);
    CHECK(type.has_value());
    if (!type) return;

    // Posicionar cursor en la última fila visual (columna 24).
    ed.active().cursor.col = 24;
    InputEvent e;
    e.type = *type;
    ed.handleEvent(e);

    // Debe alcanzar col 30 (lineLength), no 29.
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 30);
    CHECK_EQ(ed.active().cursor.col, ed.active().document.lineLength(0));
#endif
}

// Con NumLock el KP produce dígitos por TEXTINPUT: TODO el KP numérico
// queda en Unknown para no secuestrarlos (incluidos KP_7/KP_1/KP_9/KP_3,
// que sin NumLock sí son navegación: ver guisdl_keypad_nav_sin_numlock).
// Solo KP_Enter está mapeado siempre.
TEST(gui_sdl_keypad_numeric_stays_unknown) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else    
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_7, KMOD_NUM, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_1, KMOD_NUM, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_9, KMOD_NUM, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_3, KMOD_NUM, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_0, KMOD_NUM, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
             
    // El resto del KP numérico queda Unknown con y sin NumLock.
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_0, 0, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_5, 0, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_5, KMOD_NUM, false).sym),
             static_cast<int>(GuiKeySym::Unknown));
#endif
}

// Sin NumLock (mod=0x0, el caso del reporte en ventana real: sym
// 1073741919/13/21/15) el KP navegacional SÍ se traduce: no genera
// texto y es navegación pura.
TEST(gui_sdl_keypad_nav_sin_numlock) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else    
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_7, 0, false).sym),
             static_cast<int>(GuiKeySym::Home));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_1, 0, false).sym),
             static_cast<int>(GuiKeySym::End));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_9, 0, false).sym),
             static_cast<int>(GuiKeySym::PageUp));
    CHECK_EQ(static_cast<int>(guiKeyFromSdl(SDLK_KP_3, 0, false).sym),
             static_cast<int>(GuiKeySym::PageDown));
             
    // Y resuelve en el keymap al mismo evento que las dedicadas.
    GuiKeymap km;
    CHECK_EQ(static_cast<int>(*km.mapKey(guiKeyFromSdl(SDLK_KP_9, 0, false))),
             static_cast<int>(InputEventType::PageUp));
    CHECK_EQ(static_cast<int>(*km.mapKey(guiKeyFromSdl(SDLK_KP_3, 0, false))),
             static_cast<int>(InputEventType::PageDown));
    CHECK_EQ(static_cast<int>(*km.mapKey(guiKeyFromSdl(SDLK_KP_7, 0, false))),
             static_cast<int>(InputEventType::MoveHome));
    CHECK_EQ(static_cast<int>(*km.mapKey(guiKeyFromSdl(SDLK_KP_1, 0, false))),
             static_cast<int>(InputEventType::MoveEnd));
#endif
}

TEST(gui_sdl_ctrl_shift_combo) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // Ctrl+Shift+K: ambos se conservan (el keymap trae un enlace explícito
    // por variante y resuelve Prefix; solo S distingue Shift: Save/SaveAs).
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

TEST(gui_sdl_ctrl_shift_s_explicit_combo) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // Costura SDL completa de Ctrl+Shift+S: el adaptador conserva ambos
    // modificadores y el keymap los distingue (Ctrl+S = Save,
    // Ctrl+Shift+S = SaveAs directo a `buffer.guardar.como`, sin pasar por
    // Prefix ni simular teclas TTY).
    GuiKeyPress plain = guiKeyFromSdl(SDLK_s, KMOD_CTRL, false);
    CHECK_EQ(static_cast<int>(plain.sym), static_cast<int>(GuiKeySym::KeyS));
    CHECK(plain.mods.ctrl && !plain.mods.shift && !plain.mods.alt);
    
    GuiKeyPress shifted = guiKeyFromSdl(SDLK_s, KMOD_CTRL | KMOD_SHIFT, false);
    CHECK_EQ(static_cast<int>(shifted.sym), static_cast<int>(GuiKeySym::KeyS));
    CHECK(shifted.mods.ctrl && shifted.mods.shift && !shifted.mods.alt);
    
    GuiKeymap km;
    CHECK_EQ(static_cast<int>(*km.mapKey(plain)),
             static_cast<int>(InputEventType::Save));
    CHECK_EQ(static_cast<int>(*km.mapKey(shifted)),
             static_cast<int>(InputEventType::SaveAs));
             
    // Sin Ctrl no hay comando por KEYDOWN en ninguna variante (el texto va
    // por SDL_TEXTINPUT): ni S pelada ni Shift+S resuelven.
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_s, 0, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_s, KMOD_SHIFT, false)).has_value());
    
    // Con Alt tampoco (incluye Ctrl+Alt, alias de AltGr en algunas
    // plataformas), con o sin Shift.
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_s, KMOD_ALT, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_s, KMOD_CTRL | KMOD_ALT, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_s, KMOD_CTRL | KMOD_ALT | KMOD_SHIFT,false)).has_value());
    
    // El evento SaveAs llega al Editor por el comando semántico existente,
    // sin sintetizar Prefix: abre SaveAs directo desde Navegación.
    Editor ed;
    InputEvent e;
    
    e.type = *km.mapKey(shifted);
    ed.handleEvent(e);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));

    Editor direct;
    direct.executeCommand("buffer.guardar.como");
    CHECK_EQ(static_cast<int>(direct.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
#endif
}

TEST(gui_sdl_shift_uppercase_normaliza_antes_del_binding) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // representación REAL con Shift held. SDL2 entrega en keysym.sym la
    // letra afectada por el modificador en varios layouts/drivers ('S' ==
    // 83 con Shift, no SDLK_s == 115): el adaptador DEBE minorizarla a KeyS
    // ANTES de buscar el binding, o Ctrl+Shift+S caería en Unknown y el
    // atajo directo a Guardar como se perdería en esos layouts.
    GuiKeyPress upper = guiKeyFromSdl((SDL_Keycode)'S', KMOD_CTRL | KMOD_SHIFT, false);
    CHECK_EQ(static_cast<int>(upper.sym), static_cast<int>(GuiKeySym::KeyS));
    CHECK(upper.mods.ctrl && upper.mods.shift && !upper.mods.alt);
    
    GuiKeymap km;
    CHECK_EQ(static_cast<int>(*km.mapKey(upper)), static_cast<int>(InputEventType::SaveAs));
    
    // Mayúscula con solo Ctrl (driver que refleja Shift en sym pero no en
    // mod, o viceversa): misma normalización, binding sin Shift.
    GuiKeyPress upperCtrl = guiKeyFromSdl((SDL_Keycode)'S', KMOD_CTRL, false);
    CHECK_EQ(static_cast<int>(upperCtrl.sym),
             static_cast<int>(GuiKeySym::KeyS));
    CHECK_EQ(static_cast<int>(*km.mapKey(upperCtrl)),
             static_cast<int>(InputEventType::Save));
             
    // Mayúscula sin Ctrl: minoriza pero NO resuelve (el texto va por
    // SDL_TEXTINPUT, nunca por KEYDOWN).
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'S', 0, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'S', KMOD_SHIFT, false)).has_value());
    
    // ...y con Alt tampoco (Alt/AltGr de los layouts no disparan comandos).
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'S', KMOD_ALT, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'S',KMOD_CTRL | KMOD_ALT | KMOD_SHIFT, false)).has_value());
    
    // Exhaustivo: las 26 mayúsculas ('A'-'Z') con Shift held minorizan a su
    // KeyA..KeyZ, con y sin Ctrl (el binding decide después, ya en
    // minúscula lógica).
    for (int i = 0; i < 26; ++i) {
        const GuiKeySym want = static_cast<GuiKeySym>(
            static_cast<int>(GuiKeySym::KeyA) + i);
        const SDL_Keycode up = static_cast<SDL_Keycode>('A' + i);
        CHECK_EQ(static_cast<int>(guiKeyFromSdl(up, KMOD_SHIFT, false).sym),
                 static_cast<int>(want));
        CHECK_EQ(static_cast<int>(
                     guiKeyFromSdl(up, KMOD_CTRL | KMOD_SHIFT, false).sym),
                 static_cast<int>(want));
    }
#endif
}

// AltGr: comportamiento ACTUAL, no garantía universal de layouts. En
// X11 se observa KMOD_MODE y cuenta como Alt: el KEYDOWN de la letra
// queda sin enlace en el keymap y el carácter real llega por
// SDL_TEXTINPUT (que el loop acepta siempre, sin filtrar por mods).
TEST(gui_sdl_altgr_counts_as_alt) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    GuiKeyPress p = guiKeyFromSdl(SDLK_a, KMOD_MODE, false);
    CHECK(p.mods.alt && !p.mods.ctrl);
    CHECK_EQ(static_cast<int>(p.sym), static_cast<int>(GuiKeySym::KeyA));
    
    GuiKeymap km;
    CHECK(!km.mapKey(p).has_value());
    CHECK(guiKeyFromSdl(SDLK_a, KMOD_ALT, false).mods.alt);
#endif
}

TEST(gui_sdl_mods_combinados_sin_accidentales) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // P1 a nivel SDL (constantes KMOD_* reales, sin vídeo): los modificadores
    // combinados que SDL entrega en los layouts soportados no generan
    // comandos accidentales en letras, y no bloquean la navegación.
    // Letras: Ctrl+Alt / Alt / Alt+Shift / Ctrl+Alt+Shift nunca resuelven
    // (incluido KMOD_MODE de AltGr combinado con Ctrl).
    GuiKeymap km;
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_k, KMOD_CTRL | KMOD_ALT, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_k, KMOD_CTRL | KMOD_ALT | KMOD_SHIFT,false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_k, KMOD_ALT, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_k, KMOD_ALT | KMOD_SHIFT, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_k, KMOD_CTRL | KMOD_MODE, false)).has_value());
    
    // No-textual: los mismos modificadores combinados NO bloquean la acción
    // (paridad TTY deliberada): Ctrl+Alt+Escape sigue siendo Escape y
    // Ctrl+Alt+Left sigue siendo MoveLeft.
    CHECK_EQ(static_cast<int>(*km.mapKey(guiKeyFromSdl(SDLK_ESCAPE, KMOD_CTRL | KMOD_ALT,false))),
             static_cast<int>(InputEventType::Escape));
    CHECK_EQ(static_cast<int>(*km.mapKey(guiKeyFromSdl(SDLK_LEFT, KMOD_CTRL | KMOD_ALT,false))),
             static_cast<int>(InputEventType::MoveLeft));
    CHECK_EQ(static_cast<int>(*km.mapKey(guiKeyFromSdl(SDLK_RETURN,KMOD_CTRL | KMOD_ALT | KMOD_SHIFT,false))),
             static_cast<int>(InputEventType::InsertNewline));
#endif
}

TEST(gui_sdl_caps_num_stripped) {
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

// Costura completa: 'A' sin Ctrl no resuelve por KEYDOWN en ninguna
// variante de mods (el texto va por SDL_TEXTINPUT).
TEST(gui_sdl_plain_letter_never_maps) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    GuiKeymap km;
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'A', 0, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'A', KMOD_SHIFT, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'A', KMOD_CAPS, false)).has_value());
#endif
}

TEST(gui_sdl_mouse_buttons_left_only) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
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

TEST(gui_sdl_mouse_drag_gated_by_lmask) {
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

// y>0 arriba, y<0 abajo (el Editor ya aplica ±3 por evento; el loop
// repite según |y|). y==0 (solo horizontal) no produce evento.
TEST(gui_sdl_mouse_wheel_sign) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
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

// Solo FOCUS_LOST sintetiza release (la captura cae sola y el up puede
// ir a otra ventana). LEAVE no cierra: el arrastre fuera sigue con
// captura y el autoscroll debe continuar.
TEST(gui_sdl_mouse_focus_loss_ends_gesture) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
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

TEST(gui_sdl_mouse_focus_gain_refreshes_ime) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // Solo FOCUS_GAINED refresca el ancla IME (recálculo desde la geometría
    // actual: el rect guardado pudo quedar obsoleto en el medio). FOCUS_LOST
    // tiene su propio camino (gesto sintético) y el resto no toca el IME;
    // los tres conjuntos son disjuntos por diseño (un evento, un camino).
    CHECK(guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_FOCUS_GAINED));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_FOCUS_LOST));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_ENTER));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_LEAVE));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_RESIZED));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_SIZE_CHANGED));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_DISPLAY_CHANGED));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_MOVED));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_SHOWN));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_HIDDEN));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_MINIMIZED));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_MAXIMIZED));
    CHECK(!guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_CLOSE));
    
    // Disjunción con los otros dos conjuntos: ningún evento cae en dos.
    CHECK(!(guiFocusLossEndsGesture(SDL_WINDOWEVENT_FOCUS_GAINED) &&
            guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_FOCUS_GAINED)));
    CHECK(!(guiResizeNeedsRemeasure(SDL_WINDOWEVENT_FOCUS_GAINED) &&
            guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_FOCUS_GAINED)));
#endif
}

TEST(gui_sdl_mouse_expose_needs_repaint) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // Solo EXPOSED repinta por exposición: X11 avisa que una región quedó
    // descubierta (p. ej. al cerrarse un overlay de captura) y, sin
    // compositor, el contenido no se restaura solo. Sin esta rama el loop
    // mantiene píxeles viejos hasta FOCUS_GAINED o el próximo evento.
    CHECK(guiExposeNeedsRepaint(SDL_WINDOWEVENT_EXPOSED));
    
    // Resto de eventos con camino propio o sin repintado: no deben caer acá.
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_FOCUS_LOST));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_FOCUS_GAINED));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_RESIZED));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_SIZE_CHANGED));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_DISPLAY_CHANGED));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_MOVED));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_SHOWN));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_HIDDEN));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_MINIMIZED));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_ENTER));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_LEAVE));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_MAXIMIZED));
    CHECK(!guiExposeNeedsRepaint(SDL_WINDOWEVENT_CLOSE));
    
    // Disjunción con los otros clasificadores: EXPOSED tiene un solo camino.
    CHECK(!(guiResizeNeedsRemeasure(SDL_WINDOWEVENT_EXPOSED) ||
            guiFocusLossEndsGesture(SDL_WINDOWEVENT_EXPOSED) ||
            guiFocusRegainRefreshesIme(SDL_WINDOWEVENT_EXPOSED)));
#endif
}

TEST(gui_sdl_mouse_resize_needs_remeasure) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // RESIZED + SIZE_CHANGED + DISPLAY_CHANGED
    // re-miden el drawable y recalculan grilla + fuente. DISPLAY_CHANGED es
    // el caso 1×->2× sin cambio de tamaño en puntos (sin él, la grilla y la
    // fuente quedarían clavadas en la escala del monitor anterior).
    CHECK(guiResizeNeedsRemeasure(SDL_WINDOWEVENT_RESIZED));
    CHECK(guiResizeNeedsRemeasure(SDL_WINDOWEVENT_SIZE_CHANGED));
    CHECK(guiResizeNeedsRemeasure(SDL_WINDOWEVENT_DISPLAY_CHANGED));
    
    // El resto no re-mide (FOCUS_LOST tiene su propio camino sintético).
    CHECK(!guiResizeNeedsRemeasure(SDL_WINDOWEVENT_FOCUS_LOST));
    CHECK(!guiResizeNeedsRemeasure(SDL_WINDOWEVENT_FOCUS_GAINED));
    CHECK(!guiResizeNeedsRemeasure(SDL_WINDOWEVENT_MOVED));
    CHECK(!guiResizeNeedsRemeasure(SDL_WINDOWEVENT_ENTER));
    CHECK(!guiResizeNeedsRemeasure(SDL_WINDOWEVENT_LEAVE));
    CHECK(!guiResizeNeedsRemeasure(SDL_WINDOWEVENT_SHOWN));
    CHECK(!guiResizeNeedsRemeasure(SDL_WINDOWEVENT_HIDDEN));
    CHECK(!guiResizeNeedsRemeasure(SDL_WINDOWEVENT_MINIMIZED));
    CHECK(!guiResizeNeedsRemeasure(SDL_WINDOWEVENT_MAXIMIZED));
    CHECK(!guiResizeNeedsRemeasure(SDL_WINDOWEVENT_CLOSE));
#endif
}

TEST(gui_sdl_mouse_moved_syncs_drag_origin) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // MOVED refresca lastWinX_/lastWinY_ (vía
    // SDL_GetWindowPosition en el loop) para que un gesto manual nuevo
    // (resize de borde / arrastre de barra) arranque desde la posición real
    // de la ventana, no desde una medición vieja. Sin este evento no se
    // re-mide ni se refresca el origen: un click post-movimiento provocaría
    // un salto en el primer delta del gesto.
    CHECK(guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_MOVED));
    
    // El resto no refresca el origen (los de resize ya van por su propio
    // clasificador; los de foco/exposición IME tienen caminos propios).
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_FOCUS_LOST));
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_FOCUS_GAINED));
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_RESIZED));
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_SIZE_CHANGED));
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_DISPLAY_CHANGED));
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_ENTER));
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_LEAVE));
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_SHOWN));
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_HIDDEN));
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_MINIMIZED));
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_MAXIMIZED));
    CHECK(!guiWindowMovedSyncsDragOrigin(SDL_WINDOWEVENT_CLOSE));
#endif
}

#ifdef HAVE_SDL2
TEST(gui_sdl_window_flags_no_hidpi_elimina_flag_highdpi) {
    // Garantía A (la opción hace efecto): los flags de creación de ventana
    // salen de UN lugar (guiWindowFlags en GuiSdlMapper.h, usado por el
    // único SDL_CreateWindow del loop). Sin --no-hidpi, ALLOW_HIGHDPI
    // está presente; con --no-hidpi, ausente. La máscara se compara contra
    // las constantes SDL reales (no números sueltos).
    // La ventana nace HIDDEN (se muestra con SDL_ShowWindow tras el primer
    // frame completo, ver GuiRunLoop::run): nunca SHOWN en la creación.
    CHECK_EQ(guiWindowFlags(false),Uint32(SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE));
    CHECK((guiWindowFlags(true) & SDL_WINDOW_ALLOW_HIGHDPI) != 0);
    CHECK((guiWindowFlags(false) & SDL_WINDOW_ALLOW_HIGHDPI) == 0);
    
    // Phase 2: la barra de título propia exige una ventana sin decoraciones
    // del WM. El flag se pide explícitamente y no altera HiDPI/HIDDEN.
    CHECK_EQ(guiWindowFlags(true, true) & ~Uint32(SDL_WINDOW_BORDERLESS),
             guiWindowFlags(true));
    CHECK((guiWindowFlags(true, true) & SDL_WINDOW_BORDERLESS) != 0);
    CHECK((guiWindowFlags(false, true) & SDL_WINDOW_BORDERLESS) != 0);
    
    // El resto de la máscara no cambia con la opción: la única diferencia
    // entre ambas llamadas es el flag HiDPI.
    CHECK_EQ(guiWindowFlags(true) & ~Uint32(SDL_WINDOW_ALLOW_HIGHDPI),
             guiWindowFlags(false));
}

TEST(gui_sdl_window_flags_escala1_celulas_intactas_sin_hidpi) {
    // (separada de la A, etiquetada como cálculo): con la ventana
    // sin ALLOW_HIGHDPI el drawable coincide con la ventana, la escala de
    // guiScaledFontPixels es 1 y el cálculo de celdas sigue funcionando
    // con la misma fórmula de siempre (evaluada acá con SDL real instalado
    // pero sin tocar SDL en el cálculo: es puro).
    CHECK_EQ(guiScaledFontPixels(16, 600, 600), 16);
    
    const platform::WindowSize g = guiGridSize(960, 600, 9, 18);
    CHECK_EQ(g.cols, 106);
    CHECK_EQ(g.rows, 33);
    CHECK_EQ(g.cellW, 9);
    CHECK_EQ(g.cellH, 18);
}
#endif  // HAVE_SDL2

// Regresion --gui: con el emoji LITERAL "abc😀def" la fuente monoespaciada no
// tiene glifo para el emoji y SDL_ttf lo sustituye por .notdef, que avanza UNA
// celda. El renderer, en cambio, avanza la grilla DOS celdas (politica Unicode
// del emoji), asi que 'd' se pinta una columna a la izquierda de su celda y el
// cursor (que si usa columnas visuales) queda una columna a la derecha: al
// escribir se modifica la columna anterior a la que se ve.
//
// NOTE: textWidth() conserva la METRICA CRUDA de TTF_SizeUTF8 (70 para
// "abc😀def", 10 para "😀"), NO el ancho de grilla. El pintor GUI avanza
// por columnas visuales (chrome::colCount * cellW), que SÍ da 80 y 20.
// TODO(correccion-semantica): en el futuro textWidth() debería devolver
// columnOf(text) * cellW() (ancho de grilla). Mientras tanto este test
// documenta la brecha: las dos primeras CHECK_EQ documentan la metrica cruda,
// las dos ultimas verifican que el ancho de grilla es el correcto.
//
// hasEmojiGlyph() solo indica que el codepoint existe en la fuente de emoji;
// SDL_ttf puede no poder rasterizar fuentes de color (Noto Color Emoji).
// Este test verifica adicionalmente que emojiTexture() devuelve una textura
// valida cuando hay un renderer disponible (si no hay video, se skipea esa
// parte y el pintor cae a placeholder graceful).
TEST(gui_emoji_literal_advance_matches_visual_grid) {
#ifdef HAVE_SDL2
    GuiFont font;
    if (!font.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    const std::string line = "abc😀def";
    const int cw = font.cellW();
    // Ancho de grilla correcto (lo que el pintor debe respetar): 8 celdas.
    CHECK_EQ(utf8::columnOf(line, static_cast<int>(line.size())), 8);
    CHECK_EQ(utf8::columnOf("😀", static_cast<int>(sizeof("😀") - 1)), 2);
    // Metrica cruda de textWidth (TTF_SizeUTF8): .notdef avanza 1 celda,
    // no 2. Esto es el bug documentado, no se corrige hasta cambiar la
    // semantica de textWidth (ver TODO en GuiFont.h).
    CHECK_EQ(font.textWidth(line), 7 * cw);   // "abc"=3 + ".notdef"=1 + "def"=3
    CHECK_EQ(font.textWidth("😀"), 1 * cw);   // .notdef = 1 celda
    // La fuente principal no tiene glifo de emoji; la de emoji sí.
    char32_t cp = 0x1F600;
    CHECK(!font.hasGlyph(cp));
    // hasEmojiGlyph solo indica que el codepoint existe en la fuente;
    // SDL_ttf puede no poder rasterizar fuentes de color (Noto Color Emoji).
    // Verificar que realmente se puede crear una textura para el emoji
    // (si hay renderer disponible; en entornos headless sin video se skipea).
    {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) {
            SDL_Window* win = SDL_CreateWindow("emoji-test", 0, 0, 64, 64,
                                               SDL_WINDOW_HIDDEN);
            if (win) {
                SDL_Renderer* ren =
                    SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
                if (ren) {
                    void* tex = font.emojiTexture(ren, cp, 0xFF, 0xFF, 0xFF);
                    // La primera llamada debe devolver una textura valida.
                    CHECK(tex != nullptr);
                    // Mismo cp+color: el caché debe devolver la misma textura.
                    void* tex2 = font.emojiTexture(ren, cp, 0xFF, 0xFF, 0xFF);
                    if (tex != nullptr)
                        CHECK(tex2 == tex);
                    // Renderer nulo: debe devolver nullptr (defensiva).
                    CHECK(font.emojiTexture(nullptr, cp, 0xFF, 0xFF, 0xFF) ==
                          nullptr);
                    font.clearCache();
                    SDL_DestroyRenderer(ren);
                }
                SDL_DestroyWindow(win);
            }
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }
    font.unload();
#else
    SKIP("sin SDL2");
#endif
}

// Integracion: dibuja "abc😀def" en un renderer SDL usando el pintor
// productivo paintGridText() y verifica que:
//   - la 'd' empieza en la columna 5,
//   - no hay desbordamiento de texto mas alla de la columna 7,
//   - si hay una fuente de emoji disponible y SDL_ttf puede rasterizarla,
//     el emoji produce pixeles visibles en su caja de dos celdas (cols 3 y 4).
// La fuente de emoji es opcional: si no existe o SDL_ttf no puede rasterizar
// fuentes de color (p. ej. Noto Color Emoji), el pintor deja el fondo del
// emoji sin glifo (placeholder graceful) y el test sigue validando el
// avance lógico y la posicion de d.
TEST(gui_emoji_pintado_ocupa_caja_de_dos_celdas) {
#ifdef HAVE_SDL2
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        SKIP("sin SDL video");
    SDL_Window* win =
        SDL_CreateWindow("emoji-paint-test", 0, 0, 200, 40,
                         SDL_WINDOW_HIDDEN);
    if (!win) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SKIP("sin ventana SDL");
    }
    SDL_Renderer* ren =
        SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        SDL_DestroyWindow(win);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SKIP("sin renderer SDL");
    }

    GuiFont font;
    if (!font.load(16)) {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SKIP("sin fuentes del sistema para GuiFont");
    }
    const int cw = font.cellW();
    const int ch = font.cellH();
    if (cw <= 0 || ch <= 0) {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SKIP("metricas de celda invalidas");
    }

    // Fondo negro para distinguir pixeles de texto.
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);

    // Pintar "abc😀def" usando el pintor productivo paintGridText.
    // Avance por celdas visuales: 3 + 2 + 3 = 8 celdas.
    const GuiColor fg{0xFF, 0xFF, 0xFF, 0xFF};
    const int cols = paintGridTextForTests(ren, font, "abc😀def", fg, 0, 0,
                                           cw, ch);
    CHECK_EQ(cols, 8);

    SDL_RenderPresent(ren);

    // Leer pixeles del render target para verificar el pintado.
    const int fw = 200;
    const int fh = 40;
    std::vector<Uint32> pixels32(static_cast<size_t>(fw * fh));
    int pitch = fw * 4;
    if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA8888,
                             pixels32.data(), pitch) != 0) {
        // El renderer dummy puede no soportar lectura de pixeles en todos
        // los entornos: se salta la verificacion de pixeles pero se mantiene
        // la verificacion de columnas y avance.
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SKIP("SDL_RenderReadPixels no disponible en este renderer");
    }

    // SDL_GetRGBA decodifica cada pixel respetando el formato y el
    // endianess de la plataforma, evitando falsos positivos en little-endian
    // donde SDL_PIXELFORMAT_RGBA8888 no guarda los bytes en orden RGBA.
    SDL_PixelFormat* fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
    auto pixelDiffersFromBg = [&](int px, int py) -> bool {
        Uint8 r = 0, g = 0, b = 0, a = 0;
        SDL_GetRGBA(pixels32[static_cast<size_t>(py * fw + px)], fmt,
                    &r, &g, &b, &a);
        return r != 0 || g != 0 || b != 0;
    };

    // Verificar siempre: la 'd' empieza en la columna 5.
    bool dVisible = false;
    for (int py = 0; py < ch && !dVisible; ++py) {
        for (int px = 5 * cw; px < 6 * cw && px < fw; ++px) {
            if (pixelDiffersFromBg(px, py)) {
                dVisible = true;
                break;
            }
        }
    }
    CHECK(dVisible);

    // El emoji es opcional: solo exigir visibilidad si la fuente auxiliar
    // existe y se pudo crear una textura para el glifo.
    const bool emojiAvailable =
        font.hasEmojiGlyph(0x1F600);
    if (emojiAvailable) {
        void* emojiTex = font.emojiTexture(ren, 0x1F600, 0xFF, 0xFF, 0xFF);
        if (emojiTex != nullptr) {
            // Region del emoji: columnas 3 y 4 -> x en [3*cw, 5*cw).
            // Debe haber al menos un pixel distinto del fondo (emoji visible).
            bool emojiVisible = false;
            for (int py = 0; py < ch && !emojiVisible; ++py) {
                for (int px = 3 * cw; px < 5 * cw && px < fw; ++px) {
                    if (pixelDiffersFromBg(px, py)) {
                        emojiVisible = true;
                        break;
                    }
                }
            }
            CHECK(emojiVisible);
        }
        // Si emojiTexture devolvió nullptr, el pintor cae a placeholder
        // graceful y no se exige visibilidad de pixeles del emoji.
    }

    SDL_FreeFormat(fmt);
    font.clearCache();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
#else
    SKIP("sin SDL2");
#endif
}

// división entera píxeles/celda -> CellPos 0-based.
TEST(gui_sdl_guimouse_pixels_to_cell) {
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

// puntos <-> drawable:
// escala 1 es identidad
// escala 2 los puntos suben 2× (evento -> drawable) y bajan 2× (IME).
TEST(gui_sdl_guimouse_scale_coord_hidpi) {
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
    
    // Cadena completa a 2× 
    // (ventana 960×600, drawable 1920×1200, celda 9×18): 
    // el punto (19,37) es drawable (38,74) = celda (4,4)
    const int px = guiScaleCoord(19, 960, 1920);
    const int py = guiScaleCoord(37, 600, 1200);
    CHECK_EQ(px, 38);
    CHECK_EQ(py, 74);
    
    const CellPos c = guiCellFromPixels(px, py, 9, 18);
    CHECK_EQ(c.col, 4);
    CHECK_EQ(c.row, 4);
}

// el rect del cursor vive en píxeles del drawable pero
// SDL_SetTextInputRect habla en puntos de ventana.
TEST(gui_sdl_guiime_rect_to_window_units) {    
    const GuiImeRectPx id = guiImeRectToWindow({36, 72, 9, 18}, 960, 600,
                                               960, 600);
    CHECK_EQ(id.x, 36);
    CHECK_EQ(id.y, 72);
    CHECK_EQ(id.w, 9);
    CHECK_EQ(id.h, 18);
    
    // Celda (2,2) con celda escalada 18×36 a 2×: drawable (36,72,18,36) ->
    // ventana (18,36,9,18).
    const GuiImeRectPx half = guiImeRectToWindow({36, 72, 18, 36}, 960, 600,
                                                 1920, 1200);
    CHECK_EQ(half.x, 18);
    CHECK_EQ(half.y, 36);
    CHECK_EQ(half.w, 9);
    CHECK_EQ(half.h, 18);
    
    // Ida y vuelta por eje: ventana -> drawable -> ventana es identidad en
    // múltiplos exactos (el truncado <1px nunca cambia la celda).
    const int px = guiScaleCoord(19, 960, 1920);
    const int py = guiScaleCoord(37, 600, 1200);
    CHECK_EQ(guiScaleCoord(px, 1920, 960), 19);
    CHECK_EQ(guiScaleCoord(py, 1200, 600), 37);
    
    // Medidas inválidas: 
    // identidad por eje (comportamiento pre-HiDPI).
    // Los ejes son independientes: si solo falla el ancho, Y sigue bajando.
    const GuiImeRectPx inv = guiImeRectToWindow({36, 72, 18, 36}, 0, 600,
                                                1920, 1200);
    CHECK_EQ(inv.x, 36);
    CHECK_EQ(inv.y, 36);
    CHECK_EQ(inv.w, 18);
    CHECK_EQ(inv.h, 18);
    
    const GuiImeRectPx inv2 = guiImeRectToWindow({36, 72, 18, 36}, 960, 600,
                                                 0, 0);
    CHECK_EQ(inv2.x, 36);
    CHECK_EQ(inv2.y, 72);
    CHECK_EQ(inv2.w, 18);
    CHECK_EQ(inv2.h, 36);
}

// preciseY manda, si es cero, fallback al entero.
// flipped niega. Ambos cero -> 0.0.
TEST(gui_sdl_guimouse_wheel_delta_precise_first) {
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

// la rueda clásica sale directo; 
// el táctil junta fracciones hasta 1.0
// opuestas se cancelan sin eventos fantasma.
TEST(gui_sdl_guimouse_wheel_accumulates_fractions) {
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

// --- Headless extra (sin ventana): bordes exactos + valores grandes ---
// División PURA en un solo espacio (no es prueba HiDPI: acá píxel y celda ya
// están en el mismo espacio; el caso HiDPI real —puntos vs drawable— vive en
// gui_hidpi_integracion_mismo_espacio_headless). El borde pertenece a la celda
// siguiente y los valores grandes no desbordan.
TEST(gui_sdl_guimouse_pixels_bordes_y_grandes_headless) {
    CHECK((guiCellFromPixels(9, 18, 9, 18) == CellPos{1, 1}));
    CHECK((guiCellFromPixels(8, 17, 9, 18) == CellPos{0, 0}));
    CHECK((guiCellFromPixels(18, 36, 9, 18) == CellPos{2, 2}));
    CHECK((guiCellFromPixels(17, 35, 9, 18) == CellPos{1, 1}));
    
    // Origen siempre (0,0) con cualquier celda válida.
    CHECK((guiCellFromPixels(0, 0, 1, 1) == CellPos{0, 0}));
    CHECK((guiCellFromPixels(0, 0, 18, 36) == CellPos{0, 0}));
    
    // Valores grandes (4K): 3840/9=426, 2160/18=120, sin desborde.
    // es división pura con celda sin escalar, NO el camino HiDPI
    // real (a 2× la celda se escala a 18×36 y la grilla queda 213×60; ver
    // gui_hidpi_integracion_mismo_espacio_headless).
    const CellPos big = guiCellFromPixels(3840, 2160, 9, 18);
    CHECK_EQ(big.col, 426);
    CHECK_EQ(big.row, 120);
    CHECK(big.valid());
    
    // Celda 1x1: identidad píxel->celda.
    CHECK((guiCellFromPixels(123, 456, 1, 1) == CellPos{123, 456}));
    
    // Negativos parciales: un eje inválido invalida toda la celda.
    CHECK(!guiCellFromPixels(-1000000, 5, 9, 18).valid());
    CHECK(!guiCellFromPixels(5, -1000000, 9, 18).valid());
}

// límites de la grilla en el MISMO espacio, sin SDL ni ventana.
// Con drawable 3840×2160 y celda 9×18 la grilla es 426×120 y ocupa
// 426*9=3834 px horizontales (6 px sobran a la derecha): el borde exacto
// ENTRE celdas pertenece a la siguiente, pero el borde EXTERIOR queda fuera
// (inválida, nunca clamp a la última columna). La variante acotada es la que
// separa ambas condiciones; la pura seguiría dando col==cols "válida".
TEST(gui_sdl_bordes_grilla_limites_headless) {
    const int cw = 9, ch = 18;
    const platform::WindowSize g = guiGridSize(3840, 2160, cw, ch);
    CHECK_EQ(g.cols, 426);
    CHECK_EQ(g.rows, 120);
    
    const int gridW = 426 * 9;    // 3834 px ocupados
    const int gridH = 120 * 18;   // 2160 px ocupados (exacto en vertical)
    CHECK_EQ(gridW, 3834);
    CHECK_EQ(gridH, 2160);
    
    // x == cellW: primera celda de la segunda columna (borde inter -> sigue).
    CHECK((guiCellFromPixelsBounded(cw, 0, cw, ch, g.cols, g.rows) ==
           CellPos{1, 0}));
    CHECK((guiCellFromPixelsBounded(0, ch, cw, ch, g.cols, g.rows) ==
           CellPos{0, 1}));
    // x == gridWidth-1: último píxel de la última columna (dentro).
    
    const CellPos last =
        guiCellFromPixelsBounded(gridW - 1, gridH - 1, cw, ch, g.cols, g.rows);
    CHECK(last.valid());
    CHECK_EQ(last.col, 425);
    CHECK_EQ(last.row, 119);
    // x == gridWidth: fuera de la grilla (no clamp a la última columna).
    CHECK(!guiCellFromPixelsBounded(gridW, 0, cw, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, gridH, cw, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(gridW, gridH, cw, ch, g.cols, g.rows)
               .valid());
    // Más allá del drawable también es inválida (franja sobrante incluida).
    CHECK(!guiCellFromPixelsBounded(3839, 0, cw, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(3840, 2160, cw, ch, g.cols, g.rows)
               .valid());
    // La pura, en cambio, daría col==cols "válida": es lo que la acotada evita.
    const CellPos pure =
        guiCellFromPixels(gridW, 0, cw, ch);
    CHECK(pure.valid());
    CHECK_EQ(pure.col, 426);
}

// el exterior no se reconvierte en clic válido por el clamp.
// La acotada devuelve inválida donde la pura devolvería la última columna;
// el Editor ya trata la inválida como fuera de contenido (cancela selección,
// no arma gesto: ver guimouse_celda_invalida_no_arma_gesto_headless).
TEST(gui_sdl_borde_exterior_no_clampea_a_ultima_headless) {
    const int cw = 9, ch = 18;
    const platform::WindowSize g = guiGridSize(3840, 2160, cw, ch);
    const int gridW = 426 * 9;
    
    // Dentro: ambas vías coinciden en la última columna.
    CHECK((guiCellFromPixelsBounded(gridW - 1, 0, cw, ch, g.cols, g.rows) == CellPos{425, 0}));
    CHECK((guiCellFromPixels(gridW - 1, 0, cw, ch) == CellPos{425, 0}));
    
    // Fuera: solo la acotada es inválida; la pura "ve" col 426.
    CHECK(!guiCellFromPixelsBounded(gridW, 0, cw, ch, g.cols, g.rows).valid());
    CHECK(guiCellFromPixels(gridW, 0, cw, ch).valid());
    
    // Grilla 1×1 (drawable de 1 px, celda 9×18): la única celda cubre los
    // píxeles 0..8; el borde exterior está en 9 px, no en 1 px.
    const platform::WindowSize tiny = guiGridSize(1, 1, cw, ch);
    CHECK_EQ(tiny.cols, 1);
    CHECK_EQ(tiny.rows, 1);
    CHECK((guiCellFromPixelsBounded(0, 0, cw, ch, tiny.cols, tiny.rows) == CellPos{0, 0}));
    CHECK((guiCellFromPixelsBounded(8, 17, cw, ch, tiny.cols, tiny.rows) == CellPos{0, 0}));
    CHECK(!guiCellFromPixelsBounded(9, 0, cw, ch, tiny.cols, tiny.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, 18, cw, ch, tiny.cols, tiny.rows).valid());
               
    // Con celda de 1 px la grilla 1×1 sí mide 1 px: (1,0) ya es exterior.
    const platform::WindowSize one = guiGridSize(1, 1, 1, 1);
    CHECK_EQ(one.cols, 1);
    CHECK_EQ(one.rows, 1);
    CHECK((guiCellFromPixelsBounded(0, 0, 1, 1, one.cols, one.rows) == CellPos{0, 0}));
    CHECK(!guiCellFromPixelsBounded(1, 0, 1, 1, one.cols, one.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, 1, 1, 1, one.cols, one.rows).valid());
    
    // x<0 o y<0: inválida en ambas vías, nunca {0,0} espurio.
    CHECK(!guiCellFromPixelsBounded(-1, 0, cw, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, -1, cw, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(-1, -1, cw, ch, g.cols, g.rows).valid());
    
    // Grilla/celda degenerada: inválida (el clamp de guiGridSize a 1×1 no
    // convierte un píxel en clic: la validez la decide la acotada).
    CHECK(!guiCellFromPixelsBounded(0, 0, 0, ch, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, 0, cw, 0, g.cols, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, 0, cw, ch, 0, g.rows).valid());
    CHECK(!guiCellFromPixelsBounded(0, 0, cw, ch, g.cols, 0).valid());
}

// sin desbordes ante coordenadas muy grandes y sin validar inválidos.
// Se compara por ÍNDICES (col >= cols), nunca por anchos (cols*cellW), así
// que ni INT_MAX desborda: la división int es segura y el clamp a 1×1 jamás
// fabrica un {0,0} a partir de un píxel fuera de rango.
TEST(gui_sdl_sin_desborde_coordenadas_grandes_headless) {
    const int cw = 9, ch = 18;
    
    // División pura con valores grandes: no hay overflow ni crash.
    const CellPos big = guiCellFromPixels(1000000, 2000000, cw, ch);
    CHECK_EQ(big.col, 1000000 / 9);
    CHECK_EQ(big.row, 2000000 / 18);
    CHECK(big.valid());
    
    // Acotada con la grilla 426×120: esos mismos píxeles quedan fuera.
    const platform::WindowSize g = guiGridSize(3840, 2160, cw, ch);
    CHECK(!guiCellFromPixelsBounded(1000000, 2000000, cw, ch, g.cols, g.rows).valid());
    
    // INT_MAX con celda 1×1: índice exacto, sin overflow (col == INT_MAX).
    const CellPos maxCell = guiCellFromPixels(2147483647, 2147483647, 1, 1);
    CHECK_EQ(maxCell.col, 2147483647);
    CHECK_EQ(maxCell.row, 2147483647);
    
    // ...y con grilla finita queda fuera, no clamp a la última.
    CHECK(!guiCellFromPixelsBounded(2147483647, 0, 1, 1, 426, 120).valid());
    CHECK(!guiCellFromPixelsBounded(0, 2147483647, 1, 1, 426, 120).valid());
    
    // Grilla gigante sin multiplicar: guiGridSize no computa cols*cellW.
    const platform::WindowSize huge = guiGridSize(2147483647, 2147483647, 1, 1);
    CHECK_EQ(huge.cols, 2147483647);
    CHECK_EQ(huge.rows, 2147483647);
    
    // Escala con valores grandes: long long intermedio, resultado exacto.
    CHECK_EQ(guiScaleCoord(1000000, 960, 1920), 2000000);
    CHECK_EQ(guiScaleCoord(2000000, 1920, 960), 1000000);
}
