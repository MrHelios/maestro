#include "test_framework.h"

#include "platform/gui/GuiSdlMapper.h"
#include "platform/InputEvent.h"
#include "platform/MouseEvent.h"
#include "platform/gui/GuiKeymap.h"

#define private public
#include "app/Editor.h"
#undef private

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

#define private public
#include "app/Editor.h"
#undef private
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
