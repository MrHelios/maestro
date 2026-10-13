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
