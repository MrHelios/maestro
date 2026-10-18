// Tests del límite SDL del teclado GUI (guiKeyFromSdl: SDL_Keycode/mod ->
// GuiKeyPress). Son los únicos tests que nombran SDL: necesitan HAVE_SDL2 y
// solo usan constantes (SDLK_*/KMOD_*), sin inicializar video ni ventana.
// Sin SDL2 cada caso hace SKIP, no falla.
//
// AISLAMIENTO HEADLESS (condición de aceptación): dos secciones separadas.
//   SECCIÓN PURA (sin SDL, corre con WITH_SDL2=0): grilla (guiGridSize),
//   coordenadas (guiScaleCoord/guiCellFromPixels/guiCellFromPixelsBounded),
//   rueda (guiWheelDelta/guiConsumeWheelDelta), IME (guiImeRectToWindow) e
//   integración HiDPI/Editor. Está ANTES del include SDL2 y no usa ningún
//   símbolo de SDL en código: cero llamadas de init/creación de vídeo en
//   todo el TU (verificable excluyendo comentarios y guards SKIP).
//   SECCIÓN SDL (solo constantes, nunca vídeo): cada TEST empieza con
//   guard de SKIP sin SDL2; solo nombra keycodes/mods/botones por valor,
//   jamás inicializa el subsistema de vídeo ni crea ventanas.
//   Build: `tests/unit/*.cpp` (TEST_SRC); `make test-one` lo compila solo.
//
// Cubren la costura que los tests del keymap no ven: Ctrl+Shift, AltGr,
// Caps/Num y keypad, más la composición adaptador -> GuiKeymap.

#include "test_framework.h"

#include "platform/gui/GuiSdlMapper.h"
#include "platform/InputEvent.h"
#include "platform/MouseEvent.h"
#define private public
#include "app/Editor.h"
#undef private

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

TEST(guiime_rect_to_window_units) {
    // P2-IME: el rect del cursor vive en píxeles del drawable pero
    // SDL_SetTextInputRect habla en puntos de ventana. A escala 1 es
    // identidad; a 2× baja a la mitad por eje (pasarlo tal cual dejaría la
    // candidata N× desplazada/agigantada).
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
    // Medidas inválidas: identidad por eje (comportamiento pre-HiDPI).
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
    // Con NumLock el KP produce dígitos por TEXTINPUT: TODO el KP numérico
    // queda en Unknown para no secuestrarlos (incluidos KP_7/KP_1/KP_9/KP_3,
    // que sin NumLock sí son navegación: ver guisdl_keypad_nav_sin_numlock).
    // Solo KP_Enter está mapeado siempre.
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

TEST(guisdl_keypad_nav_sin_numlock) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // Sin NumLock (mod=0x0, el caso del reporte en ventana real: sym
    // 1073741919/13/21/15) el KP navegacional SÍ se traduce: no genera
    // texto y es navegación pura.
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

TEST(guisdl_ctrl_shift_combo) {
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

TEST(guisdl_ctrl_shift_s_explicit_combo) {
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
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_s, KMOD_CTRL | KMOD_ALT, false))
                .has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_s, KMOD_CTRL | KMOD_ALT | KMOD_SHIFT,
                                   false))
                .has_value());
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

TEST(guisdl_shift_uppercase_normaliza_antes_del_binding) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // P1: representación REAL con Shift held. SDL2 entrega en keysym.sym la
    // letra afectada por el modificador en varios layouts/drivers ('S' ==
    // 83 con Shift, no SDLK_s == 115): el adaptador DEBE minorizarla a KeyS
    // ANTES de buscar el binding, o Ctrl+Shift+S caería en Unknown y el
    // atajo directo a Guardar como se perdería en esos layouts.
    GuiKeyPress upper =
        guiKeyFromSdl((SDL_Keycode)'S', KMOD_CTRL | KMOD_SHIFT, false);
    CHECK_EQ(static_cast<int>(upper.sym), static_cast<int>(GuiKeySym::KeyS));
    CHECK(upper.mods.ctrl && upper.mods.shift && !upper.mods.alt);
    GuiKeymap km;
    CHECK_EQ(static_cast<int>(*km.mapKey(upper)),
             static_cast<int>(InputEventType::SaveAs));
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
    CHECK(
        !km.mapKey(guiKeyFromSdl((SDL_Keycode)'S', KMOD_SHIFT, false))
                .has_value());
    // ...y con Alt tampoco (Alt/AltGr de los layouts no disparan comandos).
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'S', KMOD_ALT, false))
                .has_value());
    CHECK(!km.mapKey(guiKeyFromSdl((SDL_Keycode)'S',
                                   KMOD_CTRL | KMOD_ALT | KMOD_SHIFT, false))
                .has_value());
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

TEST(guisdl_altgr_counts_as_alt) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // AltGr: comportamiento ACTUAL, no garantía universal de layouts. En
    // X11 se observa KMOD_MODE y cuenta como Alt: el KEYDOWN de la letra
    // queda sin enlace en el keymap y el carácter real llega por
    // SDL_TEXTINPUT (que el loop acepta siempre, sin filtrar por mods).
    GuiKeyPress p = guiKeyFromSdl(SDLK_a, KMOD_MODE, false);
    CHECK(p.mods.alt && !p.mods.ctrl);
    CHECK_EQ(static_cast<int>(p.sym), static_cast<int>(GuiKeySym::KeyA));
    GuiKeymap km;
    CHECK(!km.mapKey(p).has_value());
    // Igual con Alt clásico.
    CHECK(guiKeyFromSdl(SDLK_a, KMOD_ALT, false).mods.alt);
#endif
}

TEST(guisdl_mods_combinados_sin_accidentales) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // P1 a nivel SDL (constantes KMOD_* reales, sin vídeo): los modificadores
    // combinados que SDL entrega en los layouts soportados no generan
    // comandos accidentales en letras, y no bloquean la navegación.
    // Letras: Ctrl+Alt / Alt / Alt+Shift / Ctrl+Alt+Shift nunca resuelven
    // (incluido KMOD_MODE de AltGr combinado con Ctrl).
    GuiKeymap km;
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_k, KMOD_CTRL | KMOD_ALT, false))
                .has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_k, KMOD_CTRL | KMOD_ALT | KMOD_SHIFT,
                                   false))
                .has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_k, KMOD_ALT, false)).has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_k, KMOD_ALT | KMOD_SHIFT, false))
                .has_value());
    CHECK(!km.mapKey(guiKeyFromSdl(SDLK_k, KMOD_CTRL | KMOD_MODE, false))
                .has_value());
    // No-textual: los mismos modificadores combinados NO bloquean la acción
    // (paridad TTY deliberada): Ctrl+Alt+Escape sigue siendo Escape y
    // Ctrl+Alt+Left sigue siendo MoveLeft.
    CHECK_EQ(static_cast<int>(
                 *km.mapKey(guiKeyFromSdl(SDLK_ESCAPE, KMOD_CTRL | KMOD_ALT,
                                          false))),
             static_cast<int>(InputEventType::Escape));
    CHECK_EQ(static_cast<int>(
                 *km.mapKey(guiKeyFromSdl(SDLK_LEFT, KMOD_CTRL | KMOD_ALT,
                                          false))),
             static_cast<int>(InputEventType::MoveLeft));
    CHECK_EQ(static_cast<int>(
                 *km.mapKey(guiKeyFromSdl(SDLK_RETURN,
                                          KMOD_CTRL | KMOD_ALT | KMOD_SHIFT,
                                          false))),
             static_cast<int>(InputEventType::InsertNewline));
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

TEST(guimouse_sdl_focus_gain_refreshes_ime) {
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

TEST(guimouse_sdl_resize_needs_remeasure) {
#ifndef HAVE_SDL2
    SKIP("sin SDL2");
#else
    // P2 (cambio de monitor): RESIZED + SIZE_CHANGED + DISPLAY_CHANGED
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

TEST(guigrid_size_scale_1x) {
    // Sin SDL (pura): drawable 960x600, celda real 9x18 -> 106x33.
    // Es el camino del loop tras SDL_GetRendererOutputSize.
    const platform::WindowSize s = guiGridSize(960, 600, 9, 18);
    CHECK_EQ(s.cols, 106);
    CHECK_EQ(s.rows, 33);
    CHECK_EQ(s.cellW, 9);
    CHECK_EQ(s.cellH, 18);
    CHECK_EQ(s.pixelW, 960);
    CHECK_EQ(s.pixelH, 600);
}

TEST(guigrid_size_hidpi_2x_uses_drawable) {
    // HiDPI 2x, división FÍSICA: drawable 1920x1200 / celda sin escalar 9x18
    // = 213x66 (frente a 106x33 con drawable 960x600). Es lo que daría NO
    // escalar la fuente: grilla duplicada con texto diminuto (bug P1).
    // El contrato visual-estable escala la celda antes de dividir (ver
    // guigrid_stable_visual_size_hidpi): este test solo congela la división.
    const platform::WindowSize s = guiGridSize(1920, 1200, 9, 18);
    CHECK_EQ(s.cols, 213);
    CHECK_EQ(s.rows, 66);
    // Cadena completa evento->celda a 2x (paridad con guimouse_scale_coord):
    // punto (19,37) -> drawable (38,74) -> celda (4,4).
    const int px = guiScaleCoord(19, 960, 1920);
    const int py = guiScaleCoord(37, 600, 1200);
    const CellPos c = guiCellFromPixels(px, py, s.cellW, s.cellH);
    CHECK_EQ(c.col, 4);
    CHECK_EQ(c.row, 4);
    // Sin escalar (bug HiDPI: puntos / celda) daría (2,2).
    const CellPos wrong = guiCellFromPixels(19, 37, s.cellW, s.cellH);
    CHECK_EQ(wrong.col, 2);
    CHECK_EQ(wrong.row, 2);
}

TEST(guigrid_size_fallback_and_clamp) {
    // Sin fuente (celda <=0): fallback canónico, igual que GuiFont y el loop.
    const platform::WindowSize f = guiGridSize(960, 600, 0, 0);
    CHECK_EQ(f.cellW, platform::kFallbackCellW);
    CHECK_EQ(f.cellH, platform::kFallbackCellH);
    CHECK_EQ(f.cols, 106);
    CHECK_EQ(f.rows, 33);
    const platform::WindowSize f2 = guiGridSize(960, 600, -3, -5);
    CHECK_EQ(f2.cellW, platform::kFallbackCellW);
    CHECK_EQ(f2.cellH, platform::kFallbackCellH);
    // Ventana minimizada / drawable diminuto: nunca 0x0 al Editor.
    const platform::WindowSize m = guiGridSize(1, 1, 9, 18);
    CHECK_EQ(m.cols, 1);
    CHECK_EQ(m.rows, 1);
    const platform::WindowSize z = guiGridSize(0, 0, 9, 18);
    CHECK_EQ(z.cols, 1);
    CHECK_EQ(z.rows, 1);
    // Fraccionaria 1.5x: 300px / 9 = 33 cols (truncado, sin redondeo).
    const platform::WindowSize fr = guiGridSize(300, 300, 9, 18);
    CHECK_EQ(fr.cols, 33);
    CHECK_EQ(fr.rows, 16);
}

TEST(guigrid_size_negative_pixels_normalized) {
    // P3: sin precondiciones; píxeles negativos -> 0 (desconocido, igual que
    // Editor::resize), nunca negativos guardados. La grilla se clampea a 1x1.
    const platform::WindowSize s = guiGridSize(-100, -50, 9, 18);
    CHECK_EQ(s.pixelW, 0);
    CHECK_EQ(s.pixelH, 0);
    CHECK_EQ(s.cols, 1);
    CHECK_EQ(s.rows, 1);
    // Mixto: un eje negativo no contamina al otro.
    const platform::WindowSize m = guiGridSize(-100, 600, 9, 18);
    CHECK_EQ(m.pixelW, 0);
    CHECK_EQ(m.pixelH, 600);
    CHECK_EQ(m.cols, 1);
    CHECK_EQ(m.rows, 33);
    // Negativos en todo: celdas al fallback, píxeles a 0, grilla 1x1.
    const platform::WindowSize a = guiGridSize(-100, -50, -9, -18);
    CHECK_EQ(a.pixelW, 0);
    CHECK_EQ(a.pixelH, 0);
    CHECK_EQ(a.cellW, platform::kFallbackCellW);
    CHECK_EQ(a.cellH, platform::kFallbackCellH);
    CHECK_EQ(a.cols, 1);
    CHECK_EQ(a.rows, 1);
}

TEST(guiscale_size_font_and_fallback) {
    // Puro y sin SDL: escalado con redondeo entre espacios.
    // Fuente: 16 a 1x queda 16; a 2x (600pts->1200px) pasa a 32.
    CHECK_EQ(guiScaledFontPixels(16, 600, 600), 16);
    CHECK_EQ(guiScaledFontPixels(16, 600, 1200), 32);
    CHECK_EQ(guiScaledFontPixels(16, 960, 960), 16);
    // Fraccionaria 1.5x: 16 -> 24 (redondeo, no truncado).
    CHECK_EQ(guiScaledFontPixels(16, 200, 300), 24);
    // Escala desconocida (<=0): identidad = comportamiento pre-HiDPI.
    CHECK_EQ(guiScaledFontPixels(16, 0, 1200), 16);
    CHECK_EQ(guiScaledFontPixels(16, 600, 0), 16);
    CHECK_EQ(guiScaledFontPixels(16, -5, 1200), 16);
    // Fallback por eje: 9x18 a 2x -> 18x36 (misma proporción que la fuente).
    CHECK_EQ(guiScaleSize(9, 960, 1920), 18);
    CHECK_EQ(guiScaleSize(18, 600, 1200), 36);
    CHECK_EQ(guiScaleSize(9, 960, 960), 9);
    CHECK_EQ(guiScaleSize(18, 600, 600), 18);
    // Base inválida o escala desconocida: identidad.
    CHECK_EQ(guiScaleSize(0, 960, 1920), 0);
    CHECK_EQ(guiScaleSize(9, 0, 1920), 9);
    CHECK_EQ(guiScaleSize(9, 960, 0), 9);
}

TEST(guigrid_stable_visual_size_hidpi) {
    // Contrato visual-estable (P1): a 2x la celda SE ESCALA antes de dividir
    // y la grilla se mantiene (~106x33) en vez de duplicarse (213x66).
    // Ventana 960x600 pts, drawable 1920x1200 px, base 16px/9x18:
    const int fontPx = guiScaledFontPixels(16, 600, 1200);
    CHECK_EQ(fontPx, 32);
    const int cw = guiScaleSize(9, 960, 1920);
    const int ch = guiScaleSize(18, 600, 1200);
    CHECK_EQ(cw, 18);
    CHECK_EQ(ch, 36);
    const platform::WindowSize s2 = guiGridSize(1920, 1200, cw, ch);
    const platform::WindowSize s1 = guiGridSize(960, 600, 9, 18);
    CHECK_EQ(s2.cols, s1.cols);
    CHECK_EQ(s2.rows, s1.rows);
    CHECK_EQ(s2.cols, 106);
    CHECK_EQ(s2.rows, 33);
    // El mouse sigue la misma celda escalada: punto (19,37) -> drawable
    // (38,74) -> celda (2,2) con celda 18x36 (era (4,4) con 9x18 sin
    // escalar: el índice acompaña al tamaño visual, no al físico).
    const int px = guiScaleCoord(19, 960, 1920);
    const int py = guiScaleCoord(37, 600, 1200);
    const CellPos c = guiCellFromPixels(px, py, cw, ch);
    CHECK_EQ(c.col, 2);
    CHECK_EQ(c.row, 2);
}

// CONTRATO DE UNIDADES CLI (puro y sin SDL): `--font-size N` son píxeles
// lógicos (== píxeles del drawable a escala 1; no puntos tipográficos de
// 1/72in) y la talla que llega a TTF_OpenFont es guiScaledFontPixels(N,
// winH, outH) (eje vertical, redondeo). Fija que el mismo N da la misma
// altura visual en cualquier monitor: el loop escala tanto al abrir (init)
// como en cada resize.
TEST(gui_contrato_fontsize_pixeles_logicos) {
    // A escala 1 la base llega intacta a TTF_OpenFont.
    CHECK_EQ(guiScaledFontPixels(16, 600, 600), 16);
    // A escala 2 la talla duplica (misma altura visual, misma grilla).
    CHECK_EQ(guiScaledFontPixels(16, 600, 1200), 32);
    // Base custom del CLI (--font-size 18) escala igual.
    CHECK_EQ(guiScaledFontPixels(18, 600, 1200), 36);
    // Escala fraccionaria 1.5× con redondeo, no truncado (16 -> 24).
    CHECK_EQ(guiScaledFontPixels(16, 600, 900), 24);
    // Ventana/salida degenerada (minimizada, 0): identidad segura.
    CHECK_EQ(guiScaledFontPixels(16, 0, 1200), 16);
    CHECK_EQ(guiScaledFontPixels(16, 600, 0), 16);
}

// CONTRATO DE UNIDADES CLI (puro y sin SDL): `--geometry WxH` son
// coordenadas de ventana (puntos, no píxeles físicos). La misma ventana
// lógica en dos escalas da la misma grilla visual: si la geometría fueran
// píxeles físicos, a 2× la ventana lógica sería la mitad y la grilla
// cambiaría (53×16 en vez de 106×33).
TEST(gui_contrato_geometry_coords_ventana) {
    // Ventana lógica 1200×800 (p. ej. --geometry 1200x800) a escala 1.
    const int winW = 1200, winH = 800;
    const int cw1 = guiScaleSize(9, winW, winW);
    const int ch1 = guiScaleSize(18, winH, winH);
    const platform::WindowSize g1 = guiGridSize(winW, winH, cw1, ch1);
    CHECK_EQ(g1.cols, 133);
    CHECK_EQ(g1.rows, 44);
    // Misma ventana lógica a escala 2 (drawable 2400×1600): misma grilla.
    const int outW = 2400, outH = 1600;
    const int cw2 = guiScaleSize(9, winW, outW);
    const int ch2 = guiScaleSize(18, winH, outH);
    const platform::WindowSize g2 = guiGridSize(outW, outH, cw2, ch2);
    CHECK_EQ(g2.cols, g1.cols);
    CHECK_EQ(g2.rows, g1.rows);
    // Y la talla de fuente de esa geometría escala en la misma razón.
    CHECK_EQ(guiScaledFontPixels(16, winH, outH), 32);
}

// P2-transición: el drawable efectivo ante medidas degeneradas (minimizado
// o driver que reporta un eje en 0). El fallback es POR EJE: un eje válido
// nunca se descarta porque el otro falle (el fallback conjunto lo haría).
TEST(gui_drawable_fallback_por_eje) {
    // Medidas sanas: passthrough intacto.
    GuiDrawableSize ok = guiDrawableOrWindow(960, 600, 1920, 1200);
    CHECK_EQ(ok.w, 1920);
    CHECK_EQ(ok.h, 1200);
    // Un eje degenerado: el otro se conserva (no cae a la ventana).
    GuiDrawableSize mixW = guiDrawableOrWindow(960, 600, 0, 1200);
    CHECK_EQ(mixW.w, 960);
    CHECK_EQ(mixW.h, 1200);
    GuiDrawableSize mixH = guiDrawableOrWindow(960, 600, 1920, 0);
    CHECK_EQ(mixH.w, 1920);
    CHECK_EQ(mixH.h, 600);
    // Todo degenerado (minimizada sin medidas): 960×600, escala 1:1.
    GuiDrawableSize all = guiDrawableOrWindow(0, 0, 0, 0);
    CHECK_EQ(all.w, 960);
    CHECK_EQ(all.h, 600);
    // Negativos: igual que 0 (desconocido, nunca se guarda tal cual).
    GuiDrawableSize neg = guiDrawableOrWindow(-10, 600, -5, 1200);
    CHECK_EQ(neg.w, 960);
    CHECK_EQ(neg.h, 1200);
    // La cadena del loop sobre el fallback nunca da 0×0 al Editor: grilla
    // mínima 1×1 y fuente en identidad.
    const platform::WindowSize g = guiGridSize(all.w, all.h, 9, 18);
    CHECK_EQ(g.cols, 106);
    CHECK_EQ(g.rows, 33);
    CHECK_EQ(guiScaledFontPixels(16, 0, all.h), 16);
}

// P2-transición: cambio de monitor ida y vuelta (1× -> 2× -> 1×) con la
// MISMA ventana lógica. Replica la cadena del loop (fallback drawable +
// fuente + grilla + mouse) en cada etapa: la talla sigue a la escala
// (16->32->16), la grilla no se mueve y el mismo clic lógico cae en la
// misma celda en las tres etapas.
TEST(gui_hidpi_transicion_monitor_ida_y_vuelta) {
    const int winW = 960, winH = 600;
    // Etapa 1×: ventana == drawable.
    GuiDrawableSize d1 = guiDrawableOrWindow(winW, winH, 960, 600);
    const int f1 = guiScaledFontPixels(16, winH, d1.h);
    CHECK_EQ(f1, 16);
    const platform::WindowSize g1 = guiGridSize(d1.w, d1.h, 9, 18);
    CHECK_EQ(g1.cols, 106);
    CHECK_EQ(g1.rows, 33);
    // Etapa 2× (DISPLAY_CHANGED sin resize en puntos): drawable duplica.
    GuiDrawableSize d2 = guiDrawableOrWindow(winW, winH, 1920, 1200);
    const int f2 = guiScaledFontPixels(16, winH, d2.h);
    CHECK_EQ(f2, 32);
    const int cw2 = guiScaleSize(9, winW, d2.w);
    const int ch2 = guiScaleSize(18, winH, d2.h);
    const platform::WindowSize g2 = guiGridSize(d2.w, d2.h, cw2, ch2);
    CHECK_EQ(g2.cols, g1.cols);
    CHECK_EQ(g2.rows, g1.rows);
    // Vuelta a 1×: la talla baja sola (ensureFontPixels recarga por cambio)
    // y la grilla sigue estable.
    GuiDrawableSize d3 = guiDrawableOrWindow(winW, winH, 960, 600);
    const int f3 = guiScaledFontPixels(16, winH, d3.h);
    CHECK_EQ(f3, 16);
    const platform::WindowSize g3 = guiGridSize(d3.w, d3.h, 9, 18);
    CHECK_EQ(g3.cols, g1.cols);
    CHECK_EQ(g3.rows, g1.rows);
    // Mismo clic lógico (19,37) pts en las tres etapas -> misma celda.
    const CellPos c1 = guiCellFromPixels(guiScaleCoord(19, winW, d1.w),
                                         guiScaleCoord(37, winH, d1.h), 9, 18);
    const CellPos c2 = guiCellFromPixels(guiScaleCoord(19, winW, d2.w),
                                         guiScaleCoord(37, winH, d2.h), cw2,
                                         ch2);
    const CellPos c3 = guiCellFromPixels(guiScaleCoord(19, winW, d3.w),
                                         guiScaleCoord(37, winH, d3.h), 9, 18);
    CHECK(c2 == c1);
    CHECK(c3 == c1);
}

// P2-transición: escala NO uniforme (X a 2×, Y a 1.5×; fuera de lo que SDL
// produce, donde el factor es único). Cada eje usa su propia razón en
// coords, fallback y grilla; la fuente sigue el eje VERTICAL (la talla es
// altura) y el raster escala uniforme con ella.
TEST(gui_hidpi_escala_no_uniforme_por_eje) {
    const int winW = 960, winH = 600;
    const int outW = 1920, outH = 900;
    CHECK_EQ(guiScaleCoord(100, winW, outW), 200);
    CHECK_EQ(guiScaleCoord(100, winH, outH), 150);
    CHECK_EQ(guiScaledFontPixels(16, winH, outH), 24);
    CHECK_EQ(guiScaleSize(9, winW, outW), 18);
    CHECK_EQ(guiScaleSize(18, winH, outH), 27);
    const platform::WindowSize g = guiGridSize(outW, outH, 18, 27);
    CHECK_EQ(g.cols, 106);
    CHECK_EQ(g.rows, 33);
}

// --- Headless extra (sin ventana): bordes exactos + valores grandes ---
// División PURA en un solo espacio (no es prueba HiDPI: acá píxel y celda ya
// están en el mismo espacio; el caso HiDPI real —puntos vs drawable— vive en
// gui_hidpi_integracion_mismo_espacio_headless). El borde pertenece a la celda
// siguiente y los valores grandes no desbordan.
TEST(guimouse_pixels_bordes_y_grandes_headless) {
    // Borde exacto: 9px con celda 9 -> col 1 (no 0); último px de la celda
    // anterior sigue en 0.
    CHECK((guiCellFromPixels(9, 18, 9, 18) == CellPos{1, 1}));
    CHECK((guiCellFromPixels(8, 17, 9, 18) == CellPos{0, 0}));
    CHECK((guiCellFromPixels(18, 36, 9, 18) == CellPos{2, 2}));
    CHECK((guiCellFromPixels(17, 35, 9, 18) == CellPos{1, 1}));
    // Origen siempre (0,0) con cualquier celda válida.
    CHECK((guiCellFromPixels(0, 0, 1, 1) == CellPos{0, 0}));
    CHECK((guiCellFromPixels(0, 0, 18, 36) == CellPos{0, 0}));
    // Valores grandes (4K): 3840/9=426, 2160/18=120, sin desborde.
    // NOTA P2-1: es división pura con celda sin escalar, NO el camino HiDPI
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

// --- Headless extra: grilla grande (4K) + drawable diminuto ---
// División PURA (mismo espacio en ambos lados). El camino HiDPI real con
// celda escalada vive en gui_hidpi_integracion_mismo_espacio_headless.
TEST(guigrid_grande_y_minimo_headless) {
    const platform::WindowSize k4 =
        guiGridSize(3840, 2160, 9, 18);
    CHECK_EQ(k4.cols, 426);
    CHECK_EQ(k4.rows, 120);
    CHECK_EQ(k4.cellW, 9);
    CHECK_EQ(k4.cellH, 18);
    // 1px de drawable: clamp a 1x1, nunca 0x0 al Editor::resize.
    const platform::WindowSize tiny = guiGridSize(1, 1, 18, 36);
    CHECK_EQ(tiny.cols, 1);
    CHECK_EQ(tiny.rows, 1);
    // Celda gigante (ventana menor que una celda): también 1x1.
    const platform::WindowSize huge = guiGridSize(100, 100, 1000, 1000);
    CHECK_EQ(huge.cols, 1);
    CHECK_EQ(huge.rows, 1);
}

// --- Headless extra: celda inválida -> Editor no arma gesto ---
// El píxel fuera de métrica da CellPos inválida (nunca {0,0} espurio) y el
// Editor la trata como fuera de contenido: cancela selección si la había y
// no arma gesto (el drag posterior sin press se ignora).
TEST(guimouse_celda_invalida_no_arma_gesto_headless) {
    // Fábrica: la celda inválida viaja tal cual (release sintético de
    // FOCUS_LOST usa este mismo camino con CellPos{}).
    InputEvent pressInv = makeMousePressEvent(CellPos{});
    CHECK(!pressInv.cell.valid());
    CHECK_EQ(static_cast<int>(pressInv.type),
             static_cast<int>(InputEventType::MousePress));
    // Editor: press inválido desde Navegación no mueve el cursor ni arma.
    {
        Editor ed;
        ed.getActiveBufferForTesting().document.restore({"hola", "mundo"});
        InputEvent press = makeMousePressEvent(CellPos{});
        ed.handleEvent(press);
        CHECK_EQ(static_cast<int>(ed.state_),
                 static_cast<int>(State::Navegacion));
        CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
        CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 0);
        // Drag sin press previo: ignorado (sin selección, sin crash).
        InputEvent drag = makeMouseDragEvent(CellPos{5, 0});
        ed.handleEvent(drag);
        CHECK(!ed.hasSelection());
        CHECK_EQ(static_cast<int>(ed.state_),
                 static_cast<int>(State::Navegacion));
    }
    // Editor: press inválido desde Selección cancela a Navegación (igual que
    // click en el chrome), sin dejar gesto armado.
    {
        Editor ed;
        ed.getActiveBufferForTesting().document.restore({"hola mundo"});
        InputEvent s;
        s.type = InputEventType::InsertChar;
        s.text = "s";
        ed.handleEvent(s);
        CHECK_EQ(static_cast<int>(ed.state_),
                 static_cast<int>(State::Seleccion));
        ed.handleEvent(makeMousePressEvent(CellPos{}));
        CHECK_EQ(static_cast<int>(ed.state_),
                 static_cast<int>(State::Navegacion));
        CHECK(!ed.hasSelection());
        ed.handleEvent(makeMouseDragEvent(CellPos{5, 0}));
        CHECK(!ed.hasSelection());
    }
}

// P2-1: integración HiDPI en el MISMO espacio, sin SDL ni ventana.
// Replica EXACTAMENTE la cadena del loop (GuiRunLoop::currentSize +
// mouseCell) solo con helpers puros: la grilla divide píxeles REALES del
// drawable por celda FÍSICA escalada, y el evento en PUNTOS de ventana sube
// a drawable antes de dividirse por esa misma celda. Si un lado quedara sin
// escalar, los tests puros de división seguirían pasando pero el clic real
// caería en otra celda (el bug que este test caza: ambas variantes rotas dan
// una celda distinta de la estable).
TEST(gui_hidpi_integracion_mismo_espacio_headless) {
    // Marco 1×: ventana 960×600 pts == drawable 960×600 px, celda 9×18.
    const int winW = 960, winH = 600;
    const int out1W = 960, out1H = 600;
    const int cw1 = guiScaleSize(9, winW, out1W);
    const int ch1 = guiScaleSize(18, winH, out1H);
    CHECK_EQ(cw1, 9);
    CHECK_EQ(ch1, 18);
    const platform::WindowSize g1 = guiGridSize(out1W, out1H, cw1, ch1);
    CHECK_EQ(g1.cols, 106);
    CHECK_EQ(g1.rows, 33);
    // Marco 2×: misma ventana lógica, drawable 1920×1200, celda 18×36.
    const int out2W = 1920, out2H = 1200;
    const int cw2 = guiScaleSize(9, winW, out2W);
    const int ch2 = guiScaleSize(18, winH, out2H);
    CHECK_EQ(cw2, 18);
    CHECK_EQ(ch2, 36);
    const platform::WindowSize g2 = guiGridSize(out2W, out2H, cw2, ch2);
    // La grilla usa el DRAWABLE: con puntos de ventana daría 53×16.
    const platform::WindowSize wrongGrid = guiGridSize(winW, winH, cw2, ch2);
    CHECK_EQ(wrongGrid.cols, 53);
    CHECK_EQ(wrongGrid.rows, 16);
    CHECK(g2.cols == g1.cols);
    CHECK(g2.rows == g1.rows);
    // Mismo clic lógico (19,37) pts en ambos marcos -> misma celda visual.
    const CellPos c1 = guiCellFromPixels(
        guiScaleCoord(19, winW, out1W), guiScaleCoord(37, winH, out1H), cw1,
        ch1);
    const CellPos c2 = guiCellFromPixels(
        guiScaleCoord(19, winW, out2W), guiScaleCoord(37, winH, out2H), cw2,
        ch2);
    CHECK_EQ(c1.col, 2);
    CHECK_EQ(c1.row, 2);
    CHECK(c2 == c1);
    // Sin escalar el evento (puntos / celda física): celda chica (1,1).
    const CellPos noEventScale = guiCellFromPixels(19, 37, cw2, ch2);
    CHECK_EQ(noEventScale.col, 1);
    CHECK_EQ(noEventScale.row, 1);
    CHECK(!(noEventScale == c1));
    // Sin escalar la celda (drawable / celda base): celda grande (4,4).
    const int px2 = guiScaleCoord(19, winW, out2W);
    const int py2 = guiScaleCoord(37, winH, out2H);
    const CellPos noCellScale = guiCellFromPixels(px2, py2, 9, 18);
    CHECK_EQ(noCellScale.col, 4);
    CHECK_EQ(noCellScale.row, 4);
    CHECK(!(noCellScale == c1));
    // 4K real: ventana 1920×1080 pts, drawable 3840×2160, celda 18×36 ->
    // 213×60, igual que 1× (1920×1080 / 9×18). Sin escalar daría 426×120.
    const int w4 = 1920, h4 = 1080, o4W = 3840, o4H = 2160;
    const int cw4 = guiScaleSize(9, w4, o4W);
    const int ch4 = guiScaleSize(18, h4, o4H);
    CHECK_EQ(cw4, 18);
    CHECK_EQ(ch4, 36);
    const platform::WindowSize g4 = guiGridSize(o4W, o4H, cw4, ch4);
    const platform::WindowSize g4base = guiGridSize(w4, h4, 9, 18);
    CHECK_EQ(g4.cols, g4base.cols);
    CHECK_EQ(g4.rows, g4base.rows);
    CHECK_EQ(g4.cols, 213);
    CHECK_EQ(g4.rows, 60);
}

// P2-2: límites de la grilla en el MISMO espacio, sin SDL ni ventana.
// Con drawable 3840×2160 y celda 9×18 la grilla es 426×120 y ocupa
// 426*9=3834 px horizontales (6 px sobran a la derecha): el borde exacto
// ENTRE celdas pertenece a la siguiente, pero el borde EXTERIOR queda fuera
// (inválida, nunca clamp a la última columna). La variante acotada es la que
// separa ambas condiciones; la pura seguiría dando col==cols "válida".
TEST(gui_bordes_grilla_limites_headless) {
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

// P2-2: el exterior no se reconvierte en clic válido por el clamp.
// La acotada devuelve inválida donde la pura devolvería la última columna;
// el Editor ya trata la inválida como fuera de contenido (cancela selección,
// no arma gesto: ver guimouse_celda_invalida_no_arma_gesto_headless).
TEST(gui_borde_exterior_no_clampea_a_ultima_headless) {
    const int cw = 9, ch = 18;
    const platform::WindowSize g = guiGridSize(3840, 2160, cw, ch);
    const int gridW = 426 * 9;
    // Dentro: ambas vías coinciden en la última columna.
    CHECK((guiCellFromPixelsBounded(gridW - 1, 0, cw, ch, g.cols, g.rows) ==
           CellPos{425, 0}));
    CHECK((guiCellFromPixels(gridW - 1, 0, cw, ch) == CellPos{425, 0}));
    // Fuera: solo la acotada es inválida; la pura "ve" col 426.
    CHECK(!guiCellFromPixelsBounded(gridW, 0, cw, ch, g.cols, g.rows).valid());
    CHECK(guiCellFromPixels(gridW, 0, cw, ch).valid());
    // Grilla 1×1 (drawable de 1 px, celda 9×18): la única celda cubre los
    // píxeles 0..8; el borde exterior está en 9 px, no en 1 px.
    const platform::WindowSize tiny = guiGridSize(1, 1, cw, ch);
    CHECK_EQ(tiny.cols, 1);
    CHECK_EQ(tiny.rows, 1);
    CHECK((guiCellFromPixelsBounded(0, 0, cw, ch, tiny.cols, tiny.rows) ==
           CellPos{0, 0}));
    CHECK((guiCellFromPixelsBounded(8, 17, cw, ch, tiny.cols, tiny.rows) ==
           CellPos{0, 0}));
    CHECK(!guiCellFromPixelsBounded(9, 0, cw, ch, tiny.cols, tiny.rows)
               .valid());
    CHECK(!guiCellFromPixelsBounded(0, 18, cw, ch, tiny.cols, tiny.rows)
               .valid());
    // Con celda de 1 px la grilla 1×1 sí mide 1 px: (1,0) ya es exterior.
    const platform::WindowSize one = guiGridSize(1, 1, 1, 1);
    CHECK_EQ(one.cols, 1);
    CHECK_EQ(one.rows, 1);
    CHECK((guiCellFromPixelsBounded(0, 0, 1, 1, one.cols, one.rows) ==
           CellPos{0, 0}));
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

// P2-2: sin desbordes ante coordenadas muy grandes y sin validar inválidos.
// Se compara por ÍNDICES (col >= cols), nunca por anchos (cols*cellW), así
// que ni INT_MAX desborda: la división int es segura y el clamp a 1×1 jamás
// fabrica un {0,0} a partir de un píxel fuera de rango.
TEST(gui_sin_desborde_coordenadas_grandes_headless) {
    const int cw = 9, ch = 18;
    // División pura con valores grandes: no hay overflow ni crash.
    const CellPos big = guiCellFromPixels(1000000, 2000000, cw, ch);
    CHECK_EQ(big.col, 1000000 / 9);
    CHECK_EQ(big.row, 2000000 / 18);
    CHECK(big.valid());
    // Acotada con la grilla 426×120: esos mismos píxeles quedan fuera.
    const platform::WindowSize g = guiGridSize(3840, 2160, cw, ch);
    CHECK(!guiCellFromPixelsBounded(1000000, 2000000, cw, ch, g.cols, g.rows)
               .valid());
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

#ifdef HAVE_SDL2
TEST(guiwindowflags_no_hidpi_elimina_flag_highdpi) {
    // Garantía A (la opción hace efecto): los flags de creación de ventana
    // salen de UN lugar (guiWindowFlags en GuiSdlMapper.h, usado por el
    // único SDL_CreateWindow del loop). Sin --no-hidpi, ALLOW_HIGHDPI
    // está presente; con --no-hidpi, ausente. La máscara se compara contra
    // las constantes SDL reales (no números sueltos).
    // La ventana nace HIDDEN (se muestra con SDL_ShowWindow tras el primer
    // frame completo, ver GuiRunLoop::run): nunca SHOWN en la creación.
    CHECK_EQ(guiWindowFlags(false),
             Uint32(SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE));
    CHECK((guiWindowFlags(true) & SDL_WINDOW_ALLOW_HIGHDPI) != 0);
    CHECK((guiWindowFlags(false) & SDL_WINDOW_ALLOW_HIGHDPI) == 0);
    // El resto de la máscara no cambia con la opción: la única diferencia
    // entre ambas llamadas es el flag HiDPI.
    CHECK_EQ(guiWindowFlags(true) & ~Uint32(SDL_WINDOW_ALLOW_HIGHDPI),
             guiWindowFlags(false));
}

TEST(guiwindowflags_escala1_celulas_intactas_sin_hidpi) {
    // Garantía B (separada de la A, etiquetada como cálculo): con la ventana
    // sin ALLOW_HIGHDPI el drawable coincide con la ventana, la escala de
    // guiScaledFontPixels es 1 y el cálculo de celdas sigue funcionando
    // con la misma fórmula de siempre (evaluada acá con SDL real instalado
    // pero sin tocar SDL en el cálculo: es puro).
    CHECK_EQ(guiScaledFontPixels(16, 600, 600), 16);
    const platform::WindowSize g =
        guiGridSize(960, 600, 9, 18);
    CHECK_EQ(g.cols, 106);
    CHECK_EQ(g.rows, 33);
    CHECK_EQ(g.cellW, 9);
    CHECK_EQ(g.cellH, 18);
}
#endif  // HAVE_SDL2
