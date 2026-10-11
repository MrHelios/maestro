// Tests del keymap GUI (GuiKeymap: GuiKeyPress -> InputEventType).
//
// Espejo de tests/unit/test_keymap.cpp pero en vocabulario propio GUI
// (GuiKey.h, sin SDL): ninguna prueba incluye <SDL2/SDL.h> ni usa números
// mágicos de SDL, así compilan con WITH_SDL2=0. La adaptación
// SDL_Keysym -> GuiKeyPress vive en el límite SDL (GuiRunLoop.cpp,
// guiKeyFromSdl) y se verifica por compilación con SDL2 + checklist manual.
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
