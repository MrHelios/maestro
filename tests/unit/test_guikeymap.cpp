// Tests del keymap GUI (GuiKeymap: GuiKeyPress -> InputEventType).
//
// Espejo de tests/unit/test_keymap.cpp pero en vocabulario propio GUI
// (GuiKey.h, sin SDL): ninguna prueba incluye <SDL2/SDL.h> ni usa números
// mágicos de SDL, así compilan con WITH_SDL2=0. La adaptación
// SDL_Keysym -> GuiKeyPress vive en el límite SDL (GuiRunLoop.cpp,
// guiKeyFromSdl) y se verifica por compilación con SDL2 + checklist manual.
//
// AISLAMIENTO HEADLESS (condición de aceptación): este TU es 100% puro.
//   - Compilación: cero includes de SDL2 y cero símbolos SDL en código
//     (SDL solo aparece en comentarios). Verificable con:
//     `grep "SDL" test_guikeymap.cpp | grep -v "//"` vacío.
//     Compila con WITH_SDL2=0.
//   - Ejecución: ningún TEST llama `GuiRunLoop::run()` (el único camino que
//     crea ventana e inicializa vídeo) ni `SDL_Init`; todo pasa por
//     `GuiKeyPress` directo + `Editor::handleEvent` (lógica, sin renderer).
//   - Build: entra en la suite vía `tests/unit/*.cpp` (TEST_SRC) y `make
//     test-one` lo compila solo (`Compilando solo: test_guikeymap.cpp`).
//
// Convenciones bajo prueba:
//   - Navegación/edición ignoran mods (paridad TTY simpleEscapeForm).
//   - Letras sin Ctrl/Alt NUNCA mapean (el texto viene por SDL_TEXTINPUT).
//   - Ctrl+Z = Undo en GUI (nunca Suspend: no hay job-control).
//   - Tab sin enlazar (paridad TTY); keypad mínimo (solo KeyPadEnter).
//   - Repetición: navega/borra repiten; acciones únicas no.

#include "platform/gui/GuiKeymap.h"
#include "platform/gui/IGuiKeymap.h"
#include "platform/gui/GuiRunLoop.h"
#include "platform/InputEvent.h"
#include "base/utf8.h"
#include "test_framework.h"
#include <vector>
#define private public
#include "app/Editor.h"
#undef private

namespace {

GuiKeyPress key(GuiKeySym s, bool ctrl = false, bool alt = false,
                bool shift = false, bool repeat = false) {
    GuiKeyPress p;
    p.sym = s;
    p.mods.ctrl = ctrl;
    p.mods.alt = alt;
    p.mods.shift = shift;
    p.repeat = repeat;
    return p;
}

void checkMaps(GuiKeySym s, InputEventType expected, bool ctrl = false,
               bool alt = false) {
    GuiKeymap km;
    const auto t = km.mapKey(key(s, ctrl, alt));
    CHECK(t.has_value());
    if (t) CHECK_EQ(static_cast<int>(*t), static_cast<int>(expected));
}

void checkUnmapped(GuiKeySym s, bool ctrl = false, bool alt = false,
                   bool shift = false) {
    GuiKeymap km;
    CHECK(!km.mapKey(key(s, ctrl, alt, shift)).has_value());
}

// Lleva un Editor fresco a State::Prefix vía el propio keymap
// (Ctrl+K -> Prefix), como lo haría GuiRunLoop ante SDL_KEYDOWN.
void enterPrefix(Editor& ed, GuiKeymap& km) {
    const auto t = km.mapKey(key(GuiKeySym::KeyK, true));
    CHECK(t.has_value());
    if (t) {
        InputEvent e;
        e.type = *t;
        ed.handleEvent(e);
    }
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Prefix));
}

InputEvent insertStr(const std::string& s) {
    InputEvent e;
    e.type = InputEventType::InsertChar;
    e.text = s;
    return e;
}

}  // namespace

// --- Mapeo básico: flechas, Home/End, PgUp/PgDn ---
TEST(guikeymap_nav_arrows) {
    checkMaps(GuiKeySym::Left, InputEventType::MoveLeft);
    checkMaps(GuiKeySym::Right, InputEventType::MoveRight);
    checkMaps(GuiKeySym::Up, InputEventType::MoveUp);
    checkMaps(GuiKeySym::Down, InputEventType::MoveDown);
}

TEST(guikeymap_nav_home_end) {
    checkMaps(GuiKeySym::Home, InputEventType::MoveHome);
    checkMaps(GuiKeySym::End, InputEventType::MoveEnd);
}

TEST(guikeymap_nav_pages) {
    checkMaps(GuiKeySym::PageUp, InputEventType::PageUp);
    checkMaps(GuiKeySym::PageDown, InputEventType::PageDown);
}

// --- Edición/ent control: Enter (incl. keypad), Backspace, Delete, Escape ---
TEST(guikeymap_edit_keys) {
    checkMaps(GuiKeySym::Enter, InputEventType::InsertNewline);
    checkMaps(GuiKeySym::KeyPadEnter, InputEventType::InsertNewline);
    checkMaps(GuiKeySym::Backspace, InputEventType::Backspace);
    checkMaps(GuiKeySym::Delete, InputEventType::Delete);
    checkMaps(GuiKeySym::Escape, InputEventType::Escape);
}

TEST(guikeymap_tab_unknown_unmapped) {
    // Tab sin enlazar por paridad TTY (decisión deliberada, no omisión:
    // en esta fase Tab no inserta tabulación en GUI).
    checkUnmapped(GuiKeySym::Tab);
    checkUnmapped(GuiKeySym::Unknown);
}

// --- Navegación/edición ignoran mods (paridad TTY simpleEscapeForm) ---
TEST(guikeymap_nav_ignores_mods) {
    GuiKeymap km;
    for (GuiKeySym s : {GuiKeySym::Left, GuiKeySym::Right, GuiKeySym::Up,
                        GuiKeySym::Down, GuiKeySym::Home, GuiKeySym::End,
                        GuiKeySym::PageUp, GuiKeySym::PageDown,
                        GuiKeySym::Backspace, GuiKeySym::Delete,
                        GuiKeySym::Enter, GuiKeySym::KeyPadEnter,
                        GuiKeySym::Escape}) {
        // Shift / Ctrl / Alt no cambian el evento (en esta fase tampoco hay
        // repetición en juego).
        CHECK(km.mapKey(key(s, false, false, true)).has_value());
        if (s == GuiKeySym::Escape) continue;  // Escape+Ctrl sigue siendo
                                              // Escape; se cubre aparte
        CHECK(km.mapKey(key(s, true)).has_value());
        CHECK(km.mapKey(key(s, false, true)).has_value());
    }
    // Escape con Ctrl sigue siendo Escape (no un prefijo ni nada).
    checkMaps(GuiKeySym::Escape, InputEventType::Escape, true);
}

// --- Combos Ctrl ---
TEST(guikeymap_ctrl_combos) {
    checkMaps(GuiKeySym::KeyK, InputEventType::Prefix, true);
    checkMaps(GuiKeySym::KeyS, InputEventType::Save, true);
    checkMaps(GuiKeySym::KeyQ, InputEventType::Quit, true);
    checkMaps(GuiKeySym::KeyU, InputEventType::Undo, true);
    checkMaps(GuiKeySym::KeyY, InputEventType::Redo, true);
    // Ctrl+Z = Undo en GUI (convención GUI; la GUI nunca produce Suspend:
    // no hay job-control que suspender).
    checkMaps(GuiKeySym::KeyZ, InputEventType::Undo, true);
}

TEST(guikeymap_ctrl_requires_ctrl) {
    // Sin Ctrl no hay combo: la letra queda sin enlace (su texto, si lo
    // hay, llegará por SDL_TEXTINPUT, nunca por KEYDOWN).
    checkUnmapped(GuiKeySym::KeyK);
    checkUnmapped(GuiKeySym::KeyS);
    checkUnmapped(GuiKeySym::KeyQ);
    checkUnmapped(GuiKeySym::KeyU);
    checkUnmapped(GuiKeySym::KeyY);
    checkUnmapped(GuiKeySym::KeyZ);
    checkUnmapped(GuiKeySym::KeyN);
    // Con Alt tampoco (incluye el caso AltGr: el KEYDOWN queda sin enlace
    // y el carácter real llega por SDL_TEXTINPUT, que se acepta siempre).
    checkUnmapped(GuiKeySym::KeyS, false, true);
    checkUnmapped(GuiKeySym::KeyK, true, true);
    // Otras letras con Ctrl no están enlazadas.
    checkUnmapped(GuiKeySym::KeyF, true);
    checkUnmapped(GuiKeySym::KeyN, true);
}

TEST(guikeymap_ctrl_shift_ignored) {
    // Shift no rompe los combos (Caps produzca o no mayúscula, el adaptador
    // SDL ya normaliza a minúscula lógica).
    checkMaps(GuiKeySym::KeyK, InputEventType::Prefix, true, false);
    GuiKeymap km;
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyK, true, false, true))),
             static_cast<int>(InputEventType::Prefix));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyS, true, false, true))),
             static_cast<int>(InputEventType::Save));
}

// KEYDOWN nunca lleva texto imprimible: sin Ctrl/Alt todo es nullopt,
// así no hay doble inserción con SDL_TEXTINPUT.
TEST(guikeymap_printable_never_maps) {
    for (int i = 0; i < 26; ++i) {
        GuiKeySym s = static_cast<GuiKeySym>(static_cast<int>(GuiKeySym::KeyA) + i);
        checkUnmapped(s);
        checkUnmapped(s, false, false, true);
    }
}

// --- Repetición ---
TEST(guikeymap_repeat_policy_table) {
    GuiKeymap km;
    CHECK(km.isRepeatable(InputEventType::MoveLeft));
    CHECK(km.isRepeatable(InputEventType::MoveRight));
    CHECK(km.isRepeatable(InputEventType::MoveUp));
    CHECK(km.isRepeatable(InputEventType::MoveDown));
    CHECK(km.isRepeatable(InputEventType::MoveHome));
    CHECK(km.isRepeatable(InputEventType::MoveEnd));
    CHECK(km.isRepeatable(InputEventType::PageUp));
    CHECK(km.isRepeatable(InputEventType::PageDown));
    CHECK(km.isRepeatable(InputEventType::Backspace));
    CHECK(km.isRepeatable(InputEventType::Delete));
    CHECK(!km.isRepeatable(InputEventType::InsertNewline));
    CHECK(!km.isRepeatable(InputEventType::Prefix));
    CHECK(!km.isRepeatable(InputEventType::Save));
    CHECK(!km.isRepeatable(InputEventType::Quit));
    CHECK(!km.isRepeatable(InputEventType::Undo));
    CHECK(!km.isRepeatable(InputEventType::Redo));
    CHECK(!km.isRepeatable(InputEventType::Escape));
}

TEST(guikeymap_repeat_nav_repeats_actions_suppress) {
    GuiKeymap km;
    // Mantener flecha/borrado: repite (un evento por SDL_KEYDOWN con repeat).
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Left, false, false, false, true))),
             static_cast<int>(InputEventType::MoveLeft));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::PageDown, false, false, false, true))),
             static_cast<int>(InputEventType::PageDown));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Backspace, false, false, false, true))),
             static_cast<int>(InputEventType::Backspace));
    // Mantener Enter/Escape: no repite (una sola InsertNewline por
    // pulsación; la repetición al mantenerlo sería decisión futura).
    CHECK(!km.mapKey(key(GuiKeySym::Enter, false, false, false, true)).has_value());
    // Mantener Ctrl+K/S/Q/U/Y/Z/Escape: no repite (evita reabrir el prefijo
    // o ejecutar guardar/salir/deshacer en ráfaga por accidente).
    CHECK(!km.mapKey(key(GuiKeySym::KeyK, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyS, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyQ, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyU, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::Escape, false, false, false, true)).has_value());
}

// --- Remapeo y defaults ---
TEST(guikeymap_remap_and_reset) {
    GuiKeymap km;
    km.bindKey({GuiKeySym::KeyN, true, false}, InputEventType::PageDown);
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyN, true))),
             static_cast<int>(InputEventType::PageDown));
    km.unbindKey({GuiKeySym::KeyN, true, false});
    CHECK(!km.mapKey(key(GuiKeySym::KeyN, true)).has_value());
    // Romper un default y restaurarlo.
    km.bindKey({GuiKeySym::KeyK, true, false}, InputEventType::PageDown);
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyK, true))),
             static_cast<int>(InputEventType::PageDown));
    km.resetDefaults();
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyK, true))),
             static_cast<int>(InputEventType::Prefix));
}

// El remapeo no puede crear doble acción KEYDOWN + TEXTINPUT: las letras
// sin Ctrl, con Alt o Ctrl+Alt (alias de AltGr) y Unknown se rechazan en
// bindKey (false, tabla intacta). Solo letras con Ctrl sin Alt y teclas no
// textuales son enlazables.
TEST(guikeymap_bind_rejects_text_producing) {
    GuiKeymap km;
    CHECK(!km.bindKey({GuiKeySym::KeyJ, false, false}, InputEventType::MoveDown));
    // (shift no forma parte del enlace: la resolución siempre lo ignora.)
    CHECK(!km.bindKey({GuiKeySym::KeyJ, false, true}, InputEventType::MoveDown));
    CHECK(!km.bindKey({GuiKeySym::KeyJ, true, true}, InputEventType::MoveDown));
    CHECK(!km.bindKey({GuiKeySym::Unknown, false, false}, InputEventType::MoveDown));
    // Tabla intacta: la 'j' sigue sin resolver por KEYDOWN (su texto, si lo
    // hay, llega solo por SDL_TEXTINPUT como InsertChar).
    CHECK(!km.boundKey({GuiKeySym::KeyJ, false, false}).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyJ)).has_value());
    // En cambio Ctrl+letra sí es enlazable (nunca genera TEXTINPUT en SDL).
    CHECK(km.bindKey({GuiKeySym::KeyJ, true, false}, InputEventType::MoveDown));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyJ, true))),
             static_cast<int>(InputEventType::MoveDown));
    km.resetDefaults();
}

TEST(guikeymap_bind_nav_normalizes_mods) {
    GuiKeymap km;
    // Enlazar navegación con mods vale y se normaliza: resuelve igual sin
    // ellos (un solo enlace por tecla, no uno por combinación).
    CHECK(km.bindKey({GuiKeySym::Left, true, false}, InputEventType::MoveRight));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Left))),
             static_cast<int>(InputEventType::MoveRight));
    CHECK_EQ(static_cast<int>(*km.boundKey({GuiKeySym::Left, false, false})),
             static_cast<int>(InputEventType::MoveRight));
    km.resetDefaults();
}

// La navegación SÍ pasa por la tabla: remapear una flecha cambia lo que
// resuelve mapKey (incluido con mods, que se normalizan), y desatarla la
// deja sin enlace. Sin este test, un atajo codificado fuera de la tabla
// haría estos casos imposibles.
TEST(guikeymap_remap_nav_takes_effect) {
    GuiKeymap km;
    km.bindKey({GuiKeySym::Left, false, false}, InputEventType::MoveRight);
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Left))),
             static_cast<int>(InputEventType::MoveRight));
    // Con mods también: la normalización consulta el enlace remapeado.
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Left, true, false, true))),
             static_cast<int>(InputEventType::MoveRight));
    km.resetDefaults();
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Left))),
             static_cast<int>(InputEventType::MoveLeft));
}

TEST(guikeymap_unbind_nav_disables_it) {
    GuiKeymap km;
    km.unbindKey({GuiKeySym::Left, false, false});
    CHECK(!km.mapKey(key(GuiKeySym::Left)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::Left, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::Left, false, false, true)).has_value());
    // El resto de la navegación sigue intacta.
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Right))),
             static_cast<int>(InputEventType::MoveRight));
    km.resetDefaults();
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Left))),
             static_cast<int>(InputEventType::MoveLeft));
}

// El remapeo es usable desde la interfaz pública (IGuiKeymap), no solo
// desde el tipo concreto: es el contrato que ofrece keymapIface().
TEST(guikeymap_iface_remap) {
    Editor ed;
    GuiRunLoop loop(ed);
    IGuiKeymap& iface = loop.keymapIface();
    iface.bindKey({GuiKeySym::Down, false, false}, InputEventType::PageDown);
    CHECK_EQ(static_cast<int>(*iface.mapKey(key(GuiKeySym::Down))),
             static_cast<int>(InputEventType::PageDown));
    iface.unbindKey({GuiKeySym::Down, false, false});
    CHECK(!iface.mapKey(key(GuiKeySym::Down)).has_value());
    iface.resetDefaults();
    CHECK_EQ(static_cast<int>(*iface.mapKey(key(GuiKeySym::Down))),
             static_cast<int>(InputEventType::MoveDown));
}

// El keymap es accesible vía la interfaz GUI del run loop (misma frontera
// que Terminal::keymapIface()), sin exponer el tipo concreto.
TEST(guikeymap_runloop_iface) {
    Editor ed;
    GuiRunLoop loop(ed);
    IGuiKeymap& iface = loop.keymapIface();
    CHECK_EQ(static_cast<int>(*iface.mapKey(key(GuiKeySym::Up))),
             static_cast<int>(InputEventType::MoveUp));
    CHECK_EQ(static_cast<int>(*iface.mapKey(key(GuiKeySym::KeyK, true))),
             static_cast<int>(InputEventType::Prefix));
}

// --- Integración Prefijo: todos los combos documentados ---
// Nota Ctrl+K f: README lo documenta como búsqueda, pero
// Editor::handlePrefixKey no lo implementa en esta fase; 'f' (como
// cualquier letra no listada) cancela el prefijo. Este test fija el
// comportamiento real; implementar Ctrl+K f o corregir la doc es
// seguimiento fuera de este patch (el keymap no inventa semántica).

TEST(guiprefix_n_new_buffer) {
    Editor ed;
    GuiKeymap km;
    const size_t before = ed.buffers.buffers_.size();
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("n"));
    CHECK_EQ(ed.buffers.buffers_.size(), before + 1);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(guiprefix_t_selector) {
    Editor ed;
    GuiKeymap km;
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("n"));  // segundo buffer para que el selector abra
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("t"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::BufferSelector));
}

TEST(guiprefix_w_close_buffer) {
    Editor ed;
    GuiKeymap km;
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("n"));
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(2));
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("w"));
    CHECK_EQ(ed.buffers.buffers_.size(), size_t(1));
    CHECK(ed.isRunning());
}

TEST(guiprefix_o_filebrowser) {
    Editor ed;
    GuiKeymap km;
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("o"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::FileBrowser));
}

TEST(guiprefix_s_save_unnamed_opens_saveas) {
    Editor ed;  // sin nombre: Ctrl+K s abre el explorador SaveAs
    GuiKeymap km;
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("s"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAsFileBrowser));
}

TEST(guiprefix_q_safe_quit) {
    Editor ed;  // sin modificados: sale directo
    GuiKeymap km;
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("q"));
    CHECK(!ed.isRunning());
}

TEST(guiprefix_b_previous_buffer) {
    Editor ed;
    GuiKeymap km;
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("n"));  // B1; previo = B0
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("b"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.isRunning());
}

TEST(guiprefix_l_theme_toggle) {
    Editor ed;
    GuiKeymap km;
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("l"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.isRunning());
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("L"));  // mayúscula también alterna
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(guiprefix_m_bracket_no_bracket) {
    Editor ed;  // documento vacío: sin bracket -> cancela con aviso
    GuiKeymap km;
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("m"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(guiprefix_r_rename_unnamed_cancels) {
    Editor ed;  // sin nombre: renombrar no aplica, vuelve al previo
    GuiKeymap km;
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("r"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.isRunning());
}

TEST(guiprefix_ctrl_s_inside_prefix_saveas) {
    Editor ed;
    GuiKeymap km;
    enterPrefix(ed, km);
    InputEvent e;
    e.type = *km.mapKey(key(GuiKeySym::KeyS, true));  // Ctrl+K Ctrl+S
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::Save));
    ed.handleEvent(e);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAsFileBrowser));
}

TEST(guiprefix_ctrl_q_inside_prefix_forced_quit) {
    Editor ed;
    GuiKeymap km;
    enterPrefix(ed, km);
    InputEvent e;
    e.type = *km.mapKey(key(GuiKeySym::KeyQ, true));  // Ctrl+K Ctrl+Q
    ed.handleEvent(e);
    CHECK(!ed.isRunning());
}

// Ctrl+S / Ctrl+Q FUERA de prefijo: el keymap los traduce (no conoce el
// estado), pero el Editor los ignora (no-op): Save solo vale tras Prefix.
TEST(guiprefix_ctrl_s_outside_prefix_noop) {
    Editor ed;
    GuiKeymap km;
    InputEvent e;
    e.type = *km.mapKey(key(GuiKeySym::KeyS, true));
    ed.handleEvent(e);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.isRunning());
}

TEST(guiprefix_ctrl_q_outside_prefix_noop) {
    Editor ed;
    GuiKeymap km;
    InputEvent e;
    e.type = *km.mapKey(key(GuiKeySym::KeyQ, true));
    ed.handleEvent(e);
    CHECK(ed.isRunning());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

// Cancelaciones del prefijo.
TEST(guiprefix_unknown_f_cancels) {
    Editor ed;
    GuiKeymap km;
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("f"));  // ver nota Ctrl+K f arriba
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.isRunning());
}

TEST(guiprefix_unknown_letter_cancels) {
    Editor ed;
    GuiKeymap km;
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("x"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(guiprefix_escape_cancels) {
    Editor ed;
    GuiKeymap km;
    enterPrefix(ed, km);
    InputEvent e;
    e.type = *km.mapKey(key(GuiKeySym::Escape));
    ed.handleEvent(e);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(guiprefix_prefix_cancels) {
    Editor ed;  // Ctrl+K dentro del prefijo lo cancela (no anida)
    GuiKeymap km;
    enterPrefix(ed, km);
    InputEvent e;
    e.type = *km.mapKey(key(GuiKeySym::KeyK, true));
    ed.handleEvent(e);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

// --- Texto: UTF-8 por celdas; Enter va por KEYDOWN, no por TEXTINPUT ---
TEST(guitext_utf8_cells_split) {
    // Commit con varios caracteres confirmados: se trocea por celda sin
    // cortar secuencias (bytes inválidos = celdas propias de 1 byte).
    const auto cells = utf8::splitCells("a\xC3\xA1\xE2\x80\x94\xF0\x9F\x98\x80");
    CHECK_EQ(cells.size(), size_t(4));
    CHECK_EQ(cells[0], "a");
    CHECK_EQ(cells[1], "\xC3\xA1");          // á
    CHECK_EQ(cells[2], "\xE2\x80\x94");      // —
    CHECK_EQ(cells[3], "\xF0\x9F\x98\x80");  // 😀
}

TEST(guitext_enter_is_keydown_not_textinput) {
    // Enter llega como InsertNewline desde KEYDOWN; no se prueba como
    // commit multilínea de TEXTINPUT (SDL no lo entrega así).
    GuiKeymap km;
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Enter))),
             static_cast<int>(InputEventType::InsertNewline));
    Editor ed;
    InputEvent e;
    e.type = InputEventType::InsertNewline;
    CHECK(ed.isRunning());
    (void)e;
}

// --- Headless extra (GUI sin ventana): política de repetición exhaustiva ---
// P2-3: lista CENTRALIZADA de los 26 InputEventType + comprobación de
// cantidad. Si se agrega un valor al enum sin actualizar kAllEventTypes, el
// CHECK_EQ(size, 26) falla; si se actualiza la lista pero no se decide su
// repetibilidad, falla la partición (todo valor debe estar exactamente en un
// lado: repetible o no). Solo navegación/borrado repiten; TODO lo demás
// (incluidos eventos que el keymap nunca produce pero isRepeatable debe negar
// por contrato) no.
namespace {
const std::vector<InputEventType>& allEventTypes() {
    static const std::vector<InputEventType> v = {
        InputEventType::None,        InputEventType::InsertChar,
        InputEventType::InsertNewline, InputEventType::MoveLeft,
        InputEventType::MoveRight,   InputEventType::MoveUp,
        InputEventType::MoveDown,    InputEventType::MoveHome,
        InputEventType::MoveEnd,     InputEventType::PageUp,
        InputEventType::PageDown,    InputEventType::Backspace,
        InputEventType::Delete,      InputEventType::Undo,
        InputEventType::Redo,        InputEventType::Quit,
        InputEventType::Prefix,      InputEventType::Save,
        InputEventType::Suspend,     InputEventType::Escape,
        InputEventType::ScrollUp,    InputEventType::ScrollDown,
        InputEventType::MousePress,  InputEventType::MouseDrag,
        InputEventType::MouseRelease, InputEventType::Resize,
    };
    return v;
}
const std::vector<InputEventType>& repeatableEventTypes() {
    static const std::vector<InputEventType> v = {
        InputEventType::MoveLeft,  InputEventType::MoveRight,
        InputEventType::MoveUp,    InputEventType::MoveDown,
        InputEventType::MoveHome,  InputEventType::MoveEnd,
        InputEventType::PageUp,    InputEventType::PageDown,
        InputEventType::Backspace, InputEventType::Delete,
    };
    return v;
}
// P2-3: vocabulario completo de GuiKeySym (41 valores: 1 Unknown + 14
// no-textuales + 26 letras) + cantidad. Misma idea: agregar un sym sin
// actualizar la lista rompe el conteo; actualizarla sin decidir su familia
// rompe la partición de bindable().
const std::vector<GuiKeySym>& allKeySyms() {
    static const std::vector<GuiKeySym> v = {
        GuiKeySym::Unknown, GuiKeySym::Escape, GuiKeySym::Enter,
        GuiKeySym::KeyPadEnter, GuiKeySym::Tab, GuiKeySym::Backspace,
        GuiKeySym::Delete, GuiKeySym::Left, GuiKeySym::Right, GuiKeySym::Up,
        GuiKeySym::Down, GuiKeySym::Home, GuiKeySym::End, GuiKeySym::PageUp,
        GuiKeySym::PageDown, GuiKeySym::KeyA, GuiKeySym::KeyB, GuiKeySym::KeyC,
        GuiKeySym::KeyD, GuiKeySym::KeyE, GuiKeySym::KeyF, GuiKeySym::KeyG,
        GuiKeySym::KeyH, GuiKeySym::KeyI, GuiKeySym::KeyJ, GuiKeySym::KeyK,
        GuiKeySym::KeyL, GuiKeySym::KeyM, GuiKeySym::KeyN, GuiKeySym::KeyO,
        GuiKeySym::KeyP, GuiKeySym::KeyQ, GuiKeySym::KeyR, GuiKeySym::KeyS,
        GuiKeySym::KeyT, GuiKeySym::KeyU, GuiKeySym::KeyV, GuiKeySym::KeyW,
        GuiKeySym::KeyX, GuiKeySym::KeyY, GuiKeySym::KeyZ,
    };
    return v;
}
const std::vector<GuiKeySym>& nonTextKeySyms() {
    static const std::vector<GuiKeySym> v = {
        GuiKeySym::Escape, GuiKeySym::Enter, GuiKeySym::KeyPadEnter,
        GuiKeySym::Tab, GuiKeySym::Backspace, GuiKeySym::Delete,
        GuiKeySym::Left, GuiKeySym::Right, GuiKeySym::Up, GuiKeySym::Down,
        GuiKeySym::Home, GuiKeySym::End, GuiKeySym::PageUp, GuiKeySym::PageDown,
    };
    return v;
}
bool containsType(const std::vector<InputEventType>& v, InputEventType t) {
    for (auto x : v)
        if (x == t) return true;
    return false;
}
bool containsSym(const std::vector<GuiKeySym>& v, GuiKeySym s) {
    for (auto x : v)
        if (x == s) return true;
    return false;
}
}  // namespace

TEST(guikeymap_listas_centralizadas_cantidad_headless) {
    // Candados de cantidad: el enum no es contable por reflexión; si alguien
    // agrega un valor y no actualiza la lista central, esto falla a propósito.
    CHECK_EQ(allEventTypes().size(), size_t(26));
    CHECK_EQ(repeatableEventTypes().size(), size_t(10));
    CHECK_EQ(allKeySyms().size(), size_t(41));
    CHECK_EQ(nonTextKeySyms().size(), size_t(14));
    // Partición exacta: todo evento está en un solo lado (repetible o no).
    for (InputEventType t : allEventTypes()) {
        const bool rep = containsType(repeatableEventTypes(), t);
        int inAll = 0;
        for (auto x : allEventTypes())
            if (x == t) ++inAll;
        CHECK_EQ(inAll, 1);  // sin duplicados en la lista central
        (void)rep;
    }
    // Partición exacta de syms: Unknown(1) + no-textuales(14) + letras(26).
    int nUnknown = 0, nNonText = 0, nLetters = 0;
    for (GuiKeySym s : allKeySyms()) {
        if (s == GuiKeySym::Unknown) ++nUnknown;
        else if (containsSym(nonTextKeySyms(), s)) ++nNonText;
        else if (s >= GuiKeySym::KeyA && s <= GuiKeySym::KeyZ) ++nLetters;
        else CHECK(false);  // sym sin familia: decidir regla antes de seguir
    }
    CHECK_EQ(nUnknown, 1);
    CHECK_EQ(nNonText, 14);
    CHECK_EQ(nLetters, 26);
}

TEST(guikeymap_no_repeat_exhaustivo_headless) {
    GuiKeymap km;
    // Partición verificada contra la lista central: cada uno de los 26 tipos
    // repite exactamente si está en repeatableEventTypes(). Un valor nuevo
    // del enum sin clasificar rompe el conteo de la lista (test anterior) o
    // cae aquí como no-clasificado.
    CHECK_EQ(allEventTypes().size(), size_t(26));
    for (InputEventType t : allEventTypes()) {
        CHECK(km.isRepeatable(t) == containsType(repeatableEventTypes(), t));
    }
    // Negados por contrato (default): ni repiten ni deben empezar a hacerlo
    // en silencio si alguien enlaza una letra a ellos.
    for (InputEventType t : allEventTypes()) {
        if (!containsType(repeatableEventTypes(), t)) CHECK(!km.isRepeatable(t));
    }
    // mapKey con repeat suprime aunque el enlace exista (evita ráfagas de
    // guardar/salir/deshacer al mantener la tecla).
    CHECK(!km.mapKey(key(GuiKeySym::KeyU, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyZ, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyY, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyQ, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyPadEnter, false, false, false, true))
               .has_value());
    CHECK(!km.mapKey(key(GuiKeySym::Unknown, false, false, false, true))
               .has_value());
    // ...y los repetibles sí pasan con repeat ( Delete/Home/End incluidos ).
    CHECK(km.mapKey(key(GuiKeySym::Delete, false, false, false, true)).has_value());
    CHECK(km.mapKey(key(GuiKeySym::Home, false, false, false, true)).has_value());
    CHECK(km.mapKey(key(GuiKeySym::End, false, false, false, true)).has_value());
}

// --- Headless extra: Tab enlazable + KeyPadEnter normaliza mods + Unknown ---
// Tab va sin enlazar por defecto (paridad TTY) pero ES enlazable (no genera
// TEXTINPUT en ninguna plataforma); KeyPadEnter es no-textual (normaliza).
TEST(guikeymap_tab_y_keypad_headless) {
    GuiKeymap km;
    CHECK(!km.mapKey(key(GuiKeySym::Tab)).has_value());
    CHECK(km.bindKey({GuiKeySym::Tab, false, false}, InputEventType::InsertNewline));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Tab))),
             static_cast<int>(InputEventType::InsertNewline));
    // Con mods también resuelve (normalización no-textual), igual que flechas.
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Tab, true, true, true))),
             static_cast<int>(InputEventType::InsertNewline));
    km.resetDefaults();
    CHECK(!km.mapKey(key(GuiKeySym::Tab)).has_value());
    // KeyPadEnter con Ctrl/Alt/Shift sigue siendo InsertNewline por defecto.
    checkMaps(GuiKeySym::KeyPadEnter, InputEventType::InsertNewline, true);
    checkMaps(GuiKeySym::KeyPadEnter, InputEventType::InsertNewline, false, true);
    GuiKeymap km2;
    CHECK(km2.bindKey({GuiKeySym::KeyPadEnter, true, false},
                      InputEventType::MoveDown));
    CHECK_EQ(static_cast<int>(*km2.mapKey(key(GuiKeySym::KeyPadEnter))),
             static_cast<int>(InputEventType::MoveDown));
    // Unknown nunca resuelve, con o sin mods/repeat, ni se puede enlazar.
    GuiKeymap km3;
    CHECK(!km3.mapKey(key(GuiKeySym::Unknown, true, true, true, true)).has_value());
    CHECK(!km3.bindKey({GuiKeySym::Unknown, false, false},
                       InputEventType::MoveDown));
}

// --- Headless extra: boundKey aplica la misma normalización que mapKey ---
TEST(guikeymap_boundkey_normaliza_headless) {
    GuiKeymap km;
    // No-textual: boundKey ignora mods igual que mapKey (un solo enlace).
    CHECK_EQ(static_cast<int>(*km.boundKey({GuiKeySym::Left, false, false})),
             static_cast<int>(InputEventType::MoveLeft));
    CHECK_EQ(static_cast<int>(*km.boundKey({GuiKeySym::Left, true, true})),
             static_cast<int>(InputEventType::MoveLeft));
    // Letras: coincidencia exacta (Ctrl sin Alt); con Alt no hay enlace.
    CHECK_EQ(static_cast<int>(*km.boundKey({GuiKeySym::KeyK, true, false})),
             static_cast<int>(InputEventType::Prefix));
    CHECK(!km.boundKey({GuiKeySym::KeyK, true, true}).has_value());
    CHECK(!km.boundKey({GuiKeySym::KeyK, false, false}).has_value());
    // Tras unbind, mapKey y boundKey caen juntos (misma tabla).
    km.unbindKey({GuiKeySym::KeyK, true, false});
    CHECK(!km.boundKey({GuiKeySym::KeyK, true, false}).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyK, true)).has_value());
    km.resetDefaults();
    CHECK_EQ(static_cast<int>(*km.boundKey({GuiKeySym::KeyK, true, false})),
             static_cast<int>(InputEventType::Prefix));
}

// P2-3: DOS reglas de normalización, fijadas por separado (no generalizar).
// Regla 1 (no-textual: Escape/Enter/KeyPadEnter/Tab/Backspace/Delete/flechas/
// Home/End/PgUp/PgDn): IGNORA ctrl/alt/shift en mapKey y boundKey (paridad
// TTY simpleEscapeForm; un solo enlace por tecla). Regla 2 (textual: letras
// A-Z): exige coincidencia EXACTA en (ctrl,alt) —Ctrl sin Alt— y solo ignora
// shift (el Binding ni lo nombra). Unknown nunca resuelve ni se enlaza.
TEST(guikeymap_normalizacion_dos_reglas_headless) {
    GuiKeymap km;
    // Tab no trae default (paridad TTY) pero sigue la regla 1 una vez
    // enlazado: se enlaza acá para que las 14 resuelvan y la uniformidad sea
    // comparable (sin enlace, la regla 1 para Tab es "las 8 dan nullopt").
    CHECK(km.bindKey({GuiKeySym::Tab, false, false},
                     InputEventType::InsertNewline));
    // Regla 1: las 14 no-textuales resuelven igual con las 8 combinaciones de
    // mods (misma tabla, mismo evento que sin mods).
    for (GuiKeySym s : nonTextKeySyms()) {
        auto base = km.mapKey(key(s));
        CHECK(base.has_value());
        for (bool ctrl : {false, true})
            for (bool alt : {false, true})
                for (bool shift : {false, true}) {
                    auto t = km.mapKey(key(s, ctrl, alt, shift));
                    CHECK(t.has_value());
                    CHECK_EQ(static_cast<int>(*t), static_cast<int>(*base));
                    auto b = km.boundKey({s, ctrl, alt});
                    CHECK(b.has_value());
                    CHECK_EQ(static_cast<int>(*b), static_cast<int>(*base));
                }
    }
    // Regla 2: las letras exigen (Ctrl=true, Alt=false); shift se ignora pero
    // ctrl/alt no se normalizan. Se verifica sobre una enlazada (KeyK=Prefix),
    // una enlazable sin default (KeyJ) y los bordes del rango (KeyA/KeyZ).
    for (GuiKeySym s :
         {GuiKeySym::KeyK, GuiKeySym::KeyJ, GuiKeySym::KeyA, GuiKeySym::KeyZ}) {
        GuiKeymap fresh;
        if (s != GuiKeySym::KeyK) {
            // KeyJ/A/Z sin default: se enlazan para probar la regla exacta.
            CHECK(fresh.bindKey({s, true, false}, InputEventType::MoveDown));
        }
        const auto base = fresh.mapKey(key(s, true, false));
        CHECK(base.has_value());
        // Shift no rompe (se ignora en ambas reglas).
        CHECK_EQ(static_cast<int>(*fresh.mapKey(key(s, true, false, true))),
                 static_cast<int>(*base));
        CHECK_EQ(static_cast<int>(*fresh.boundKey({s, true, false})),
                 static_cast<int>(*base));
        // Pero ctrl/alt SÍ deciden: sin Ctrl, con Alt o con Ctrl+Alt no hay
        // enlace (el texto llegaría por SDL_TEXTINPUT; enlazarlo duplicaría).
        CHECK(!fresh.mapKey(key(s, false, false)).has_value());
        CHECK(!fresh.mapKey(key(s, false, true)).has_value());
        CHECK(!fresh.mapKey(key(s, true, true)).has_value());
        CHECK(!fresh.mapKey(key(s, false, false, true)).has_value());
        CHECK(!fresh.boundKey({s, false, false}).has_value());
        CHECK(!fresh.boundKey({s, false, true}).has_value());
        CHECK(!fresh.boundKey({s, true, true}).has_value());
    }
    // Unknown: ninguna regla lo rescata (sin enlace posible).
    for (bool ctrl : {false, true})
        for (bool alt : {false, true})
            for (bool shift : {false, true})
                for (bool repeat : {false, true}) {
                    CHECK(!km.mapKey(key(GuiKeySym::Unknown, ctrl, alt, shift,
                                         repeat))
                               .has_value());
                    CHECK(!km.boundKey({GuiKeySym::Unknown, ctrl, alt})
                               .has_value());
                }
    GuiKeymap m2;
    CHECK(!m2.bindKey({GuiKeySym::Unknown, false, false},
                      InputEventType::MoveDown));
    CHECK(!m2.bindKey({GuiKeySym::Unknown, true, false},
                      InputEventType::MoveDown));
}

// P2-3: política bindable() particionada por familia (41 syms, lista central).
// No-textuales: enlazables con cualquier mods (se normaliza al guardar).
// Letras: solo con Ctrl sin Alt (invariante anti-doble-acción KEYDOWN +
// TEXTINPUT). Unknown: nunca. Un sym nuevo sin clasificar rompe el conteo.
TEST(guikeymap_bindable_particion_por_familia_headless) {
    CHECK_EQ(allKeySyms().size(), size_t(41));
    for (GuiKeySym s : allKeySyms()) {
        GuiKeymap km;
        if (s == GuiKeySym::Unknown) {
            CHECK(!km.bindKey({s, false, false}, InputEventType::MoveDown));
            CHECK(!km.bindKey({s, true, false}, InputEventType::MoveDown));
            CHECK(!km.bindKey({s, true, true}, InputEventType::MoveDown));
        } else if (containsSym(nonTextKeySyms(), s)) {
            CHECK(km.bindKey({s, false, false}, InputEventType::MoveDown));
            CHECK(km.bindKey({s, true, true}, InputEventType::MoveDown));
        } else {
            CHECK(s >= GuiKeySym::KeyA && s <= GuiKeySym::KeyZ);
            CHECK(!km.bindKey({s, false, false}, InputEventType::MoveDown));
            CHECK(!km.bindKey({s, false, true}, InputEventType::MoveDown));
            CHECK(!km.bindKey({s, true, true}, InputEventType::MoveDown));
            CHECK(km.bindKey({s, true, false}, InputEventType::MoveDown));
        }
    }
    // Tab, caso que motiva el test: sin default pero enlazable (no genera
    // TEXTINPUT); tras reset vuelve a sin-enlazar.
    GuiKeymap km;
    CHECK(!km.mapKey(key(GuiKeySym::Tab)).has_value());
    CHECK(km.bindKey({GuiKeySym::Tab, false, false},
                     InputEventType::InsertNewline));
    km.resetDefaults();
    CHECK(!km.mapKey(key(GuiKeySym::Tab)).has_value());
}
