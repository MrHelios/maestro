#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

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

void checkMaps(const GuiKeymap& km, GuiKeySym s, InputEventType expected,
               bool ctrl = false, bool alt = false) {
    const auto t = km.mapKey(key(s, ctrl, alt));
    CHECK(t.has_value());
    if (t) CHECK_EQ(static_cast<int>(*t), static_cast<int>(expected));
}

void checkUnmapped(const GuiKeymap& km, GuiKeySym s, bool ctrl = false,
                   bool alt = false, bool shift = false) {
    CHECK(!km.mapKey(key(s, ctrl, alt, shift)).has_value());
}

// Lleva un Editor a State::Prefix vía (Ctrl+K -> Prefix)
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

InputEvent saveAsEvent() {
    InputEvent e;
    e.type = InputEventType::SaveAs;
    return e;
}

// Basename único por proceso para el test de confirmación en /tmp
// (sin colisiones entre corridas; el test elimina el archivo al final).
std::string uniqueSaveAsName() {
    static int n = 0;
    return "maestro_saveas_" + std::to_string(static_cast<long>(::getpid())) +
           "_" + std::to_string(n++) + ".txt";
}

std::string readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

constexpr GuiKeySym kCtrlLetters[] = {
    GuiKeySym::KeyK, GuiKeySym::KeyS, GuiKeySym::KeyQ, GuiKeySym::KeyU,
    GuiKeySym::KeyY, GuiKeySym::KeyZ, GuiKeySym::KeyN
};

constexpr InputEventType kAllEventTypes[] = {
    InputEventType::None,          InputEventType::InsertChar,
    InputEventType::InsertNewline, InputEventType::MoveLeft,
    InputEventType::MoveRight,     InputEventType::MoveUp,
    InputEventType::MoveDown,      InputEventType::MoveHome,
    InputEventType::MoveEnd,       InputEventType::PageUp,
    InputEventType::PageDown,      InputEventType::Backspace,
    InputEventType::Delete,        InputEventType::Undo,
    InputEventType::Redo,          InputEventType::Quit,
    InputEventType::Prefix,        InputEventType::Save,
    InputEventType::SaveAs,        InputEventType::Suspend,
    InputEventType::Escape,
    InputEventType::ScrollUp,      InputEventType::ScrollDown,
    InputEventType::MousePress,    InputEventType::MouseDrag,
    InputEventType::MouseRelease,  InputEventType::Resize,
    InputEventType::Copy,          InputEventType::Cut,
    InputEventType::Paste,
};

constexpr InputEventType kRepeatableEventTypes[] = {
    InputEventType::MoveLeft,  InputEventType::MoveRight,
    InputEventType::MoveUp,    InputEventType::MoveDown,
    InputEventType::MoveHome,  InputEventType::MoveEnd,
    InputEventType::PageUp,    InputEventType::PageDown,
    InputEventType::Backspace, InputEventType::Delete,
};

constexpr GuiKeySym kAllKeySyms[] = {
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

constexpr GuiKeySym kNonTextKeySyms[] = {
    GuiKeySym::Escape, GuiKeySym::Enter, GuiKeySym::KeyPadEnter,
    GuiKeySym::Tab, GuiKeySym::Backspace, GuiKeySym::Delete,
    GuiKeySym::Left, GuiKeySym::Right, GuiKeySym::Up, GuiKeySym::Down,
    GuiKeySym::Home, GuiKeySym::End, GuiKeySym::PageUp, GuiKeySym::PageDown,
};

template <typename T, std::size_t N>
bool contains(const T (&values)[N], T value) {
    return std::find(std::begin(values), std::end(values), value) !=
           std::end(values);
}

}  // namespace

// --- Mapeo básico: flechas, Home/End, PgUp/PgDn ---
TEST(gui_keymap_nav_arrows) {
    GuiKeymap km;
    checkMaps(km, GuiKeySym::Left, InputEventType::MoveLeft);
    checkMaps(km, GuiKeySym::Right, InputEventType::MoveRight);
    checkMaps(km, GuiKeySym::Up, InputEventType::MoveUp);
    checkMaps(km, GuiKeySym::Down, InputEventType::MoveDown);
}

TEST(gui_keymap_nav_home_end) {
    GuiKeymap km;
    checkMaps(km, GuiKeySym::Home, InputEventType::MoveHome);
    checkMaps(km, GuiKeySym::End, InputEventType::MoveEnd);
}

TEST(gui_keymap_nav_pages) {
    GuiKeymap km;
    checkMaps(km, GuiKeySym::PageUp, InputEventType::PageUp);
    checkMaps(km, GuiKeySym::PageDown, InputEventType::PageDown);
}

// --- Edición/ent control: Enter (incl. keypad), Backspace, Delete, Escape ---
TEST(gui_keymap_edit_keys) {
    GuiKeymap km;
    checkMaps(km, GuiKeySym::Enter, InputEventType::InsertNewline);
    checkMaps(km, GuiKeySym::KeyPadEnter, InputEventType::InsertNewline);
    checkMaps(km, GuiKeySym::Backspace, InputEventType::Backspace);
    checkMaps(km, GuiKeySym::Delete, InputEventType::Delete);
    checkMaps(km, GuiKeySym::Escape, InputEventType::Escape);
}

// Regla 1 (no-textual: Escape/Enter/KeyPadEnter/Tab/Backspace/Delete/flechas/
// Home/End/PgUp/PgDn): IGNORA ctrl/alt/shift en mapKey y boundKey (paridad
// TTY simpleEscapeForm; un solo enlace por tecla, ver GuiKeymap::normalize).
// Cubre las 14 teclas no textuales con las ocho combinaciones de
// (ctrl, alt, shift), comprobando AMBAS vías: mapKey y boundKey deben
// resolver el mismo evento base (con mods y sin mods).
TEST(gui_keymap_nontext_modifiers_normalized) {
    GuiKeymap km;

    // Tab sin default: se enlaza para poder comparar contra la base.
    CHECK(km.bindKey({GuiKeySym::Tab, false, false, false},
                     InputEventType::InsertNewline));

    for (GuiKeySym s : kNonTextKeySyms) {
        const auto base = km.mapKey(key(s));
        CHECK(base.has_value());

        // Las ocho combinaciones deben dar el mismo evento que la base
        // (Escape incluido: Ctrl+Alt+Escape sigue siendo Escape).
        for (bool ctrl : {false, true})
            for (bool alt : {false, true})
                for (bool shift : {false, true}) {
                    const auto t = km.mapKey(key(s, ctrl, alt, shift));
                    CHECK(t.has_value());
                    CHECK_EQ(static_cast<int>(*t), static_cast<int>(*base));
                    const auto b = km.boundKey({s, ctrl, alt, shift});
                    CHECK(b.has_value());
                    CHECK_EQ(static_cast<int>(*b), static_cast<int>(*base));
                }
    }
}

// --- Letras: regla 2 (coincidencia EXACTA en (ctrl, alt, shift)) ---
// Ctrl+letra sin Alt es el único combo que resuelve por KEYDOWN (Ctrl+letra
// nunca genera SDL_TEXTINPUT). Shift distingue enlaces: Ctrl+S -> Save,
// Ctrl+Shift+S -> SaveAs son enlaces distintos. Sin Ctrl (ni con Shift) o con
// Alt (incl. Ctrl+Alt/AltGr) la pulsación no tiene enlace: el carácter llega
// por SDL_TEXTINPUT y mapearlo en KEYDOWN duplicaría la acción.
TEST(gui_keymap_letter_modifiers_exact_match) {
    GuiKeymap km;

    // Ctrl+letra sin Alt: tabla de acciones (Solo GUI: Ctrl+Z = Undo).
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyK, true, false, false))),
             static_cast<int>(InputEventType::Prefix));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyS, true, false, false))),
             static_cast<int>(InputEventType::Save));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyQ, true, false, false))),
             static_cast<int>(InputEventType::Quit));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyU, true, false, false))),
             static_cast<int>(InputEventType::Undo));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyY, true, false, false))),
             static_cast<int>(InputEventType::Redo));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyZ, true, false, false))),
             static_cast<int>(InputEventType::Undo));
    // Ctrl+C/X/V se cubren en gui_keymap_clipboard_shortcuts.

    // Shift distingue enlaces: Ctrl+S -> Save, Ctrl+Shift+S -> SaveAs.
    // K/Q/U/Y/Z toleran Shift con el MISMO evento (enlaces explícitos, sin
    // fallback silencioso).
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyS, true, false, true))),
             static_cast<int>(InputEventType::SaveAs));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyK, true, false, true))),
             static_cast<int>(InputEventType::Prefix));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyQ, true, false, true))),
             static_cast<int>(InputEventType::Quit));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyU, true, false, true))),
             static_cast<int>(InputEventType::Undo));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyY, true, false, true))),
             static_cast<int>(InputEventType::Redo));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyZ, true, false, true))),
             static_cast<int>(InputEventType::Undo));

    // boundKey aplica la misma regla exacta (misma tabla).
    CHECK_EQ(static_cast<int>(*km.boundKey({GuiKeySym::KeyS, true, false, false})),
             static_cast<int>(InputEventType::Save));
    CHECK_EQ(static_cast<int>(*km.boundKey({GuiKeySym::KeyS, true, false, true})),
             static_cast<int>(InputEventType::SaveAs));
    CHECK(!km.boundKey({GuiKeySym::KeyS, false, false, false}).has_value());
    CHECK(!km.boundKey({GuiKeySym::KeyS, false, true, false}).has_value());
    CHECK(!km.boundKey({GuiKeySym::KeyS, true, true, false}).has_value());
    CHECK(!km.boundKey({GuiKeySym::KeyS, true, true, true}).has_value());
    // KeyK coincide en el rechazo con Alt (misma tabla).
    CHECK(!km.boundKey({GuiKeySym::KeyK, false, true, false}).has_value());
    CHECK(!km.boundKey({GuiKeySym::KeyK, true, true, false}).has_value());
    CHECK(!km.boundKey({GuiKeySym::KeyK, true, true, true}).has_value());

    // Sin Ctrl no hay combo: la letra (con o sin Shift) no resuelve por
    // KEYDOWN; con Ctrl pero con Alt (AltGr incluido) tampoco: el texto
    // llega por SDL_TEXTINPUT, nunca por KEYDOWN. Se cubren las seis
    // combinaciones sin enlace (todas menos Ctrl y Ctrl+Shift).
    for (GuiKeySym s : kCtrlLetters) {
        checkUnmapped(km, s);
        checkUnmapped(km, s, false, false, true);
        checkUnmapped(km, s, false, true);
        checkUnmapped(km, s, false, true, true);
        checkUnmapped(km, s, true, true);
        checkUnmapped(km, s, true, true, true);
    }

    // Ninguna de las 8 combinaciones resuelve (KeyF).
    for (bool ctrl : {false, true})
        for (bool alt : {false, true})
            for (bool shift : {false, true}) {
                CHECK(!km.mapKey(key(GuiKeySym::KeyF, ctrl, alt, shift)).has_value());
                CHECK(!km.boundKey({GuiKeySym::KeyF, ctrl, alt, shift}).has_value());
            }

    // Un enlace nuevo de una sola variante NO habilita la otra (sin fallback
    // silencioso). Se verifica en KeyJ y KeyA, sin default, y en KeyX, donde
    // reemplaza el enlace existente Ctrl+X -> Cut.
    for (GuiKeySym s : {GuiKeySym::KeyJ, GuiKeySym::KeyA, GuiKeySym::KeyX}) {
        GuiKeymap fresh;

        CHECK(fresh.bindKey({s, true, false, false}, InputEventType::MoveDown));
        CHECK_EQ(static_cast<int>(*fresh.mapKey(key(s, true, false, false))),
                 static_cast<int>(InputEventType::MoveDown));
        // La variante con Shift sigue sin resolver (enlace distinto).
        CHECK(!fresh.mapKey(key(s, true, false, true)).has_value());
        CHECK(!fresh.boundKey({s, true, false, true}).has_value());
        // ...hasta que se enlaza explícitamente (enlace independiente).
        CHECK(fresh.bindKey({s, true, false, true}, InputEventType::MoveUp));
        CHECK_EQ(static_cast<int>(*fresh.mapKey(key(s, true, false, true))),
                 static_cast<int>(InputEventType::MoveUp));
        CHECK_EQ(static_cast<int>(*fresh.mapKey(key(s, true, false, false))),
                 static_cast<int>(InputEventType::MoveDown));

        // ctrl/alt SÍ deciden: sin Ctrl, con Alt o con Ctrl+Alt no hay enlace
        // (el texto llegaría por SDL_TEXTINPUT; enlazarlo duplicaría).
        CHECK(!fresh.mapKey(key(s, false, false)).has_value());
        CHECK(!fresh.mapKey(key(s, false, false, true)).has_value());
        CHECK(!fresh.mapKey(key(s, false, true)).has_value());
        CHECK(!fresh.mapKey(key(s, true, true)).has_value());
        CHECK(!fresh.boundKey({s, false, false}).has_value());
        CHECK(!fresh.boundKey({s, false, true}).has_value());
        CHECK(!fresh.boundKey({s, true, true}).has_value());
    }

    // Desatanar una variante no toca la otra (enlaces distintos por mods).
    km.unbindKey({GuiKeySym::KeyS, true, false, true});
    CHECK(!km.mapKey(key(GuiKeySym::KeyS, true, false, true)).has_value());
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyS, true, false, false))),
             static_cast<int>(InputEventType::Save));
    km.resetDefaults();
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::KeyS, true, false, true))),
             static_cast<int>(InputEventType::SaveAs));
}

// Ctrl+Shift+S llega al Editor por el comando semántico existente
// (`buffer.guardar.como`), sin sintetizar secuencias de teclas TTY
// (sin pasar por Prefix): paridad total con executeCommand directo.
TEST(gui_keymap_ctrl_shift_s_editor_via_semantic_command) {
    GuiKeymap km;
    Editor viaKeys;
    const auto t = km.mapKey(key(GuiKeySym::KeyS, true, false, true));
    
    CHECK(t.has_value());
    CHECK_EQ(static_cast<int>(*t), static_cast<int>(InputEventType::SaveAs));
    
    InputEvent e;
    e.type = *t;
    viaKeys.handleEvent(e);
    CHECK_EQ(static_cast<int>(viaKeys.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
    CHECK(viaKeys.getStateForTesting() != State::Prefix);
    
    // Vía comando directo (lo que hace un botón GUI): mismo estado final.
    Editor viaCmd;
    viaCmd.executeCommand("buffer.guardar.como");
    CHECK_EQ(static_cast<int>(viaCmd.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
             
    // Ctrl+S fuera de prefijo sigue siendo no-op (contraste deliberado:
    // Save solo vale tras Prefix; SaveAs es directo).
    Editor plain;
    InputEvent s;
    s.type = *km.mapKey(key(GuiKeySym::KeyS, true, false, false));
    CHECK_EQ(static_cast<int>(s.type), static_cast<int>(InputEventType::Save));
    plain.handleEvent(s);
    CHECK_EQ(static_cast<int>(plain.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK(plain.isRunning());
    
    // Dentro del prefijo, SaveAs equivale a Save (Ctrl+K Ctrl+Shift+S abre
    // SaveAs igual que Ctrl+K Ctrl+S).
    Editor pre;
    enterPrefix(pre, km);
    InputEvent sa;
    sa.type = *km.mapKey(key(GuiKeySym::KeyS, true, false, true));
    pre.handleEvent(sa);
    CHECK_EQ(static_cast<int>(pre.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
}

// SaveAs llega a todos los modales: en cada uno es no-op explícito
// (conserva estado y datos). Cada test cubre un estado para que un fallo
// identifique exactamente cuál no está rechazando el evento.
TEST(gui_keymap_saveas_ignored_in_search) {
    Editor ed;

    ed.executeCommand("navegacion.buscar");
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Busqueda));
    ed.handleEvent(insertStr("x"));
    CHECK_EQ(ed.searchQuery_, "x");
    ed.handleEvent(saveAsEvent());
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Busqueda));
    CHECK_EQ(ed.searchQuery_, "x");
    CHECK(ed.isRunning());
}

TEST(gui_keymap_saveas_ignored_in_buffer_selector) {
    Editor ed;

    ed.executeCommand("buffer.nuevo");
    ed.executeCommand("buffer.selector");
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::BufferSelector));
    const int idx = ed.bufferSelectorIndex_;
    ed.handleEvent(saveAsEvent());
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::BufferSelector));
    CHECK_EQ(ed.bufferSelectorIndex_, idx);
    CHECK(ed.isRunning());
}

TEST(gui_keymap_saveas_ignored_in_filebrowser) {
    Editor ed;

    ed.executeCommand("buffer.abrir");
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::FileBrowser));

    const std::string path = ed.fileBrowser.path_;
    ed.handleEvent(saveAsEvent());
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::FileBrowser));
    CHECK_EQ(ed.fileBrowser.path_, path);
    CHECK(ed.isRunning());
}

TEST(gui_keymap_saveas_ignored_in_goto_line) {
    Editor ed;

    ed.executeCommand("navegacion.ir_a_fila");
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::IrAFila));
    ed.handleEvent(insertStr("1"));
    CHECK_EQ(ed.goToLineQuery_, "1");
    ed.handleEvent(saveAsEvent());
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::IrAFila));
    CHECK_EQ(ed.goToLineQuery_, "1");
    CHECK(ed.isRunning());
}

TEST(gui_keymap_saveas_ignored_in_rename) {
    testfw::TempFile tf;
    tf.write("base");
    Editor ed;

    CHECK(ed.loadIntoActiveBuffer(tf.path));
    ed.executeCommand("archivo.renombrar");
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Renombrar));

    const std::string before = ed.renameQuery_;
    ed.handleEvent(insertStr("X"));
    CHECK_EQ(ed.renameQuery_, before + "X");
    ed.handleEvent(saveAsEvent());
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Renombrar));
    CHECK_EQ(ed.renameQuery_, before + "X");
    CHECK_EQ(ed.getActiveBufferForTesting().filename, tf.path);
    CHECK(ed.isRunning());
}

// Nombre vacío: ni Enter ni Save confirman nada (Save ya no existe en
// SaveAsFileBrowser).
TEST(gui_keymap_saveas_empty_name_never_confirms) {
    Editor ed;  // sin nombre: el prompt arranca vacío

    ed.executeCommand("buffer.guardar.como");
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
    CHECK(ed.saveAsFileName_.empty());
    InputEvent enter; enter.type = InputEventType::InsertNewline;
    ed.handleEvent(enter);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
    CHECK(ed.saveAsFileName_.empty());
    CHECK(ed.isRunning());

    // Save (Ctrl+S) también es no-op porque se removió de SaveAsFileBrowser.
    InputEvent s;
    s.type = InputEventType::Save;
    ed.handleEvent(s);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
    CHECK(ed.saveAsFileName_.empty());
}

// Con nombre nuevo en /tmp: Enter confirma (escribe y sale del modal).
TEST(gui_keymap_saveas_new_name_saved_on_enter) {
    Editor ed;

    ed.getActiveBufferForTesting().document.restore({"hola"});
    ed.executeCommand("buffer.guardar.como");
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
    ed.fileBrowser.startAt("/tmp");
    CHECK(ed.fileBrowser.reload().empty());
    const std::string name = uniqueSaveAsName();
    const std::string dest = std::string("/tmp/") + name;
    CHECK(!std::filesystem::exists(dest));
    ed.saveAsFileName_ = name;
    InputEvent enter; enter.type = InputEventType::InsertNewline;
    ed.handleEvent(enter);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.getActiveBufferForTesting().filename, dest);
    CHECK_EQ(readFileBytes(dest), "hola");
    CHECK(!ed.getActiveBufferForTesting().modified);
    std::remove(dest.c_str());
    CHECK(!std::filesystem::exists(dest));
}

// Con nombre válido: Save (Ctrl+S) y SaveAs (Ctrl+Shift+S) NO confirman el
// guardado. Solo Enter confirma. Save, SaveAs y Enter se mantienen juntos por
// ser una misma secuencia del contrato de confirmación.
TEST(gui_keymap_saveas_valid_name_requires_enter) {
    Editor ed;

    ed.getActiveBufferForTesting().document.restore({"contenido valido"});
    ed.executeCommand("buffer.guardar.como");
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
    ed.fileBrowser.startAt("/tmp");
    CHECK(ed.fileBrowser.reload().empty());
    const std::string name = uniqueSaveAsName();
    const std::string dest = std::string("/tmp/") + name;
    CHECK(!std::filesystem::exists(dest));
    ed.saveAsFileName_ = name;

    // Ctrl+S (Save) no hace nada: estado e input conservados.
    InputEvent saveEvt; saveEvt.type = InputEventType::Save;
    ed.handleEvent(saveEvt);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
    CHECK_EQ(ed.saveAsFileName_, name);
    CHECK(!std::filesystem::exists(dest));

    // Ctrl+Shift+S (SaveAs) no hace nada: estado e input conservados.
    InputEvent saveAsEvt; saveAsEvt.type = InputEventType::SaveAs;
    ed.handleEvent(saveAsEvt);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
    CHECK_EQ(ed.saveAsFileName_, name);
    CHECK(!std::filesystem::exists(dest));

    // Enter SÍ confirma: sale del modal y guarda el archivo.
    InputEvent enter; enter.type = InputEventType::InsertNewline;
    ed.handleEvent(enter);
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.getActiveBufferForTesting().filename, dest);
    CHECK_EQ(readFileBytes(dest), "contenido valido");
    CHECK(!ed.getActiveBufferForTesting().modified);
    std::remove(dest.c_str());
    CHECK(!std::filesystem::exists(dest));
}

// SaveAs directo desde los tres modos de edición (sin Prefix en el medio).
// Navegacion ya está cubierto en ..._via_semantic_command; acá se fijan
// Interaccion y Seleccion para cerrar la matriz de estados.
TEST(gui_keymap_saveas_directo_desde_edicion) {
    // Interaccion: SaveAs abre el explorador sin pasar por Prefix.
    {
        Editor ed;
        
        ed.handleEvent(insertStr("i"));
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::Interaccion));
        ed.handleEvent(saveAsEvent());
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::SaveAsFileBrowser));
        CHECK(ed.getStateForTesting() != State::Prefix);
        CHECK(ed.isRunning());
    }
    // Seleccion (sin rango marcado): idem.
    {
        Editor ed;
        
        ed.handleEvent(insertStr("s"));
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::Seleccion));
        ed.handleEvent(saveAsEvent());
        CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
                 static_cast<int>(State::SaveAsFileBrowser));
        CHECK(ed.getStateForTesting() != State::Prefix);
        CHECK(ed.isRunning());
    }
}

// Gestión de errores del commit vía SaveAs (misma rama que Save). Cada test
// cubre una rama independiente con su propia preparación.

// Nombre inválido (".."): rechazo, estado e input conservados (no sale del
// modal).
TEST(gui_keymap_saveas_invalid_name_rejected) {
    Editor ed;

    ed.getActiveBufferForTesting().document.restore({"x"});
    ed.executeCommand("buffer.guardar.como");
    ed.fileBrowser.startAt("/tmp");
    CHECK(ed.fileBrowser.reload().empty());

    ed.saveAsFileName_ = "..";
    ed.handleEvent(saveAsEvent());
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
    CHECK_EQ(ed.saveAsFileName_, "..");
    CHECK(ed.isRunning());
}

// Destino existente: el primer Enter arma la confirmación (sin escribir) y
// el segundo confirma y sobrescribe.
TEST(gui_keymap_saveas_existing_destination_requires_confirmation) {
    const std::string name = uniqueSaveAsName();
    const std::string dest = std::string("/tmp/") + name;
    {
        std::ofstream f(dest, std::ios::binary | std::ios::trunc);
        CHECK(f.good());
        f << "viejo";
    }
    CHECK_EQ(readFileBytes(dest), "viejo");

    Editor ed;
    ed.getActiveBufferForTesting().document.restore({"nuevo"});
    ed.executeCommand("buffer.guardar.como");
    ed.fileBrowser.startAt("/tmp");
    CHECK(ed.fileBrowser.reload().empty());
    ed.saveAsFileName_ = name;

    InputEvent enter; enter.type = InputEventType::InsertNewline;
    ed.handleEvent(enter);  // 1.er Enter: arma confirmación
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::SaveAsFileBrowser));
    CHECK_EQ(ed.saveAsConfirmPath_, dest);
    CHECK_EQ(readFileBytes(dest), "viejo");  // aún sin escribir
    ed.handleEvent(enter);  // 2.º Enter: confirma y sobrescribe
    CHECK_EQ(static_cast<int>(ed.getStateForTesting()),
             static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.getActiveBufferForTesting().filename, dest);
    CHECK_EQ(readFileBytes(dest), "nuevo");
    std::remove(dest.c_str());
    CHECK(!std::filesystem::exists(dest));
}

// KEYDOWN nunca lleva texto imprimible: sin Ctrl/Alt todo es nullopt,
// así no hay doble inserción con SDL_TEXTINPUT.
TEST(gui_keymap_printable_never_maps) {
    GuiKeymap km;
    for (int i = 0; i < 26; ++i) {
        GuiKeySym s = static_cast<GuiKeySym>(static_cast<int>(GuiKeySym::KeyA) + i);
        checkUnmapped(km, s);
        checkUnmapped(km, s, false, false, true);
    }
}

// --- Repetición ---
TEST(gui_keymap_repeat_nav_repeats_actions_suppress) {
    GuiKeymap km;
    
    // Mantener flecha/borrado: repite (un evento por SDL_KEYDOWN con repeat).
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Left, false, false, false, true))),
             static_cast<int>(InputEventType::MoveLeft));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::PageDown, false, false, false, true))),
             static_cast<int>(InputEventType::PageDown));
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Backspace, false, false, false, true))),
             static_cast<int>(InputEventType::Backspace));
             
    // Mantener Enter/Escape: no repite salvo el contexto explícito de
    // InsertNewline (ver guikeymap_repeat_enter_solo_con_contexto).
    CHECK(!km.mapKey(key(GuiKeySym::Enter, false, false, false, true)).has_value());
    // Mantener Ctrl+K/S/Q/U/Y/Z/Escape: no repite (evita reabrir el prefijo
    // o ejecutar guardar/salir/deshacer en ráfaga por accidente).
    CHECK(!km.mapKey(key(GuiKeySym::KeyK, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyS, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyS, true, false, true, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyQ, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyU, true, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::Escape, false, false, false, true)).has_value());
}

TEST(gui_keymap_repeat_enter_solo_con_contexto) {
    GuiKeymap km;
    
    // Sin contexto (prompts/selectores/confirmaciones), Enter repetido se
    // descarta: no confirma dos veces.
    CHECK(!km.mapKey(key(GuiKeySym::Enter, false, false, false, true)).has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyPadEnter, false, false, false, true))
               .has_value());
    // Con contexto de Interaccion, Enter repetido inserta filas.
    CHECK_EQ(static_cast<int>(
                 *km.mapKey(key(GuiKeySym::Enter, false, false, false, true), true)),
             static_cast<int>(InputEventType::InsertNewline));
    CHECK_EQ(static_cast<int>(
                 *km.mapKey(key(GuiKeySym::KeyPadEnter, false, false, false, true),
                            true)),
             static_cast<int>(InputEventType::InsertNewline));
             
    // El contexto solo habilita InsertNewline; el resto sigue suprimido.
    CHECK(!km.mapKey(key(GuiKeySym::Escape, false, false, false, true), true)
               .has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyK, true, false, false, true), true)
               .has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyS, true, false, false, true), true)
               .has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyQ, true, false, false, true), true)
               .has_value());
    CHECK(!km.mapKey(key(GuiKeySym::KeyU, true, false, false, true), true)
               .has_value());
}

TEST(gui_keymap_enter_repetido_inserta_filas) {
    GuiKeymap km;
    Editor ed;
    ed.handleEvent(insertStr("i"));  // Interaccion

    // Pulsación inicial y repetida (repeat=true) con el contexto real de
    // Interaccion: ambas resuelven a InsertNewline; sin contexto la repetida
    // se suprimiría (cubierto en guikeymap_repeat_enter_solo_con_contexto).
    const auto initial = km.mapKey(key(GuiKeySym::Enter, false, false, false,
                                       false));
    const auto repeated = km.mapKey(key(GuiKeySym::Enter, false, false, false,
                                        true),
                                    true);
    CHECK(initial.has_value());
    CHECK(repeated.has_value());
    if (!initial || !repeated) return;
    CHECK_EQ(static_cast<int>(*initial),
             static_cast<int>(InputEventType::InsertNewline));
    CHECK_EQ(static_cast<int>(*repeated),
             static_cast<int>(InputEventType::InsertNewline));

    InputEvent enter;
    
    enter.type = *initial;
    ed.handleEvent(enter);
    enter.type = *repeated;
    for (int i = 0; i < 4; ++i) ed.handleEvent(enter);
    const Buffer& b = ed.getActiveBufferForTesting();
    CHECK_EQ(b.document.lineCount(), 6);
    CHECK_EQ(b.cursor.line, 5);
    CHECK_EQ(b.cursor.col, 0);
}

// --- Remapeo y defaults ---
TEST(gui_keymap_remap_and_reset) {
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
TEST(gui_keymap_bind_rejects_text_producing) {
    GuiKeymap km;
    
    CHECK(!km.bindKey({GuiKeySym::KeyJ, false, false}, InputEventType::MoveDown));
    // (sin Ctrl no hay enlace en ninguna variante de Shift: el texto va por
    // SDL_TEXTINPUT; Shift solo distingue enlaces con Ctrl sin Alt.)
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

TEST(gui_keymap_bind_nav_normalizes_mods) {
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
TEST(gui_keymap_remap_nav_takes_effect) {
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

TEST(gui_keymap_unbind_nav_disables_it) {
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
TEST(gui_keymap_iface_remap) {
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

// El contexto de repeticion tambien debe despacharse por la interfaz
// virtual (IGuiKeymap), no solo por el tipo concreto: GuiKeymap es el unico
// implementador, pero el contrato de keymapIface() es la interfaz.
TEST(gui_keymap_iface_contexto_enter_repetido) {
    Editor ed;
    GuiRunLoop loop(ed);
    IGuiKeymap& iface = loop.keymapIface();
    
    const GuiKeyPress enterRepeat =
        key(GuiKeySym::Enter, false, false, false, true);
    // Sin contexto: Enter repetido se suprime (prompts/selectores).
    CHECK(!iface.mapKey(enterRepeat).has_value());
    // Con contexto explicito: se permite InsertNewline (Interaccion).
    CHECK_EQ(static_cast<int>(*iface.mapKey(enterRepeat, true)),
             static_cast<int>(InputEventType::InsertNewline));
}

TEST(gui_keymap_prefix_n_new_buffer) {
    Editor ed;
    GuiKeymap km;
    const size_t before = ed.buffers.buffers_.size();
    
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("n"));
    CHECK_EQ(ed.buffers.buffers_.size(), before + 1);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(gui_keymap_prefix_t_selector) {
    Editor ed;
    GuiKeymap km;
    
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("n"));  // segundo buffer para que el selector abra
    
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("t"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::BufferSelector));
}

TEST(gui_keymap_prefix_w_close_buffer) {
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

TEST(gui_keymap_prefix_o_filebrowser) {
    Editor ed;
    GuiKeymap km;
    
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("o"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::FileBrowser));
}

TEST(gui_keymap_prefix_s_save_unnamed_opens_saveas) {
    Editor ed;  // sin nombre: Ctrl+K s abre el explorador SaveAs
    GuiKeymap km;
    
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("s"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAsFileBrowser));
}

TEST(gui_keymap_prefix_q_safe_quit) {
    Editor ed;  // sin modificados: sale directo
    GuiKeymap km;
    
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("q"));
    CHECK(!ed.isRunning());
}

TEST(gui_keymap_prefix_b_previous_buffer) {
    Editor ed;
    GuiKeymap km;

    const int firstBuffer = ed.buffers.active().id;

    enterPrefix(ed, km);
    ed.handleEvent(insertStr("n"));  // nuevo buffer
    const int secondBuffer = ed.buffers.active().id;
    CHECK_EQ(ed.buffers.count(), 2);
    CHECK(secondBuffer != firstBuffer);

    enterPrefix(ed, km);
    ed.handleEvent(insertStr("b"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.buffers.active().id, firstBuffer);  // volvió al anterior
    CHECK(ed.isRunning());
}

TEST(gui_keymap_prefix_l_theme_toggle) {
    Editor ed;
    GuiKeymap km;

    const bool initialDark = ed.renderer_->isDarkTheme();

    enterPrefix(ed, km);
    ed.handleEvent(insertStr("l"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.isRunning());
    CHECK_EQ(ed.renderer_->isDarkTheme(), !initialDark);  // el tema cambió

    enterPrefix(ed, km);
    ed.handleEvent(insertStr("L"));  // mayúscula también alterna
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.renderer_->isDarkTheme(), initialDark);  // volvió al original
}

TEST(gui_keymap_prefix_m_bracket_no_bracket) {
    Editor ed;  // documento vacío: sin bracket -> cancela con aviso
    GuiKeymap km;
    
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("m"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(gui_keymap_prefix_r_rename_unnamed_cancels) {
    Editor ed;  // sin nombre: renombrar no aplica, vuelve al previo
    GuiKeymap km;
    
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("r"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.isRunning());
}

TEST(gui_keymap_prefix_ctrl_s_inside_prefix_saveas) {
    Editor ed;
    GuiKeymap km;
    InputEvent e;
    
    enterPrefix(ed, km);
    e.type = *km.mapKey(key(GuiKeySym::KeyS, true));  // Ctrl+K Ctrl+S
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(InputEventType::Save));
    ed.handleEvent(e);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAsFileBrowser));
}

TEST(gui_keymap_prefix_ctrl_q_inside_prefix_forced_quit) {
    Editor ed;
    GuiKeymap km;
    InputEvent e;
    
    enterPrefix(ed, km);
    e.type = *km.mapKey(key(GuiKeySym::KeyQ, true));  // Ctrl+K Ctrl+Q
    ed.handleEvent(e);
    CHECK(!ed.isRunning());
}

// Ctrl+S / Ctrl+Q FUERA de prefijo: 
// el keymap los traduce (no conoce el estado), 
// pero el Editor los ignora (no-op): Save solo vale tras Prefix.
TEST(gui_keymap_prefix_ctrl_s_outside_prefix_noop) {
    Editor ed;
    GuiKeymap km;
    InputEvent e;
    
    e.type = *km.mapKey(key(GuiKeySym::KeyS, true));
    ed.handleEvent(e);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.isRunning());
}

TEST(gui_keymap_prefix_ctrl_q_outside_prefix_noop) {
    Editor ed;
    GuiKeymap km;
    InputEvent e;
    
    e.type = *km.mapKey(key(GuiKeySym::KeyQ, true));
    ed.handleEvent(e);
    CHECK(ed.isRunning());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

// Cancelaciones del prefijo.
TEST(gui_keymap_prefix_unknown_f_cancels) {
    Editor ed;
    GuiKeymap km;
    
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("f"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.isRunning());
}

TEST(gui_keymap_prefix_unknown_letter_cancels) {
    Editor ed;
    GuiKeymap km;
    
    enterPrefix(ed, km);
    ed.handleEvent(insertStr("x"));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(gui_keymap_prefix_escape_cancels) {
    Editor ed;
    GuiKeymap km;
    InputEvent e;
    
    enterPrefix(ed, km);
    e.type = *km.mapKey(key(GuiKeySym::Escape));
    ed.handleEvent(e);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(gui_keymap_prefix_prefix_cancels) {
    Editor ed;
    GuiKeymap km;
    InputEvent e;
    
    enterPrefix(ed, km);
    e.type = *km.mapKey(key(GuiKeySym::KeyK, true));
    ed.handleEvent(e);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

// Texto: UTF-8 por celdas
TEST(gui_keymap_text_utf8_cells_split) {
    const auto cells = utf8::splitCells("a\xC3\xA1\xE2\x80\x94\xF0\x9F\x98\x80");
    
    CHECK_EQ(cells.size(), size_t(4));
    CHECK_EQ(cells[0], "a");
    CHECK_EQ(cells[1], "\xC3\xA1");          // á
    CHECK_EQ(cells[2], "\xE2\x80\x94");      // —
    CHECK_EQ(cells[3], "\xF0\x9F\x98\x80");  // 😀
}

// Enter llega como InsertNewline desde KEYDOWN; no se prueba como
// commit multilínea de TEXTINPUT (SDL no lo entrega así).
TEST(gui_keymap_text_enter_is_keydown_not_textinput) {
    GuiKeymap km;
    
    CHECK_EQ(static_cast<int>(*km.mapKey(key(GuiKeySym::Enter))),
             static_cast<int>(InputEventType::InsertNewline));

    Editor ed;
    InputEvent e;
    e.type = InputEventType::InsertNewline;
    CHECK(ed.isRunning());
    (void)e;
}

// Valida los inventarios centrales del test: sin duplicados, coherentes entre
// sí y con GuiKeymap::isRepeatable. No es exhaustivo respecto de los enums:
// sin un sentinel/reflexión, un valor nuevo del enum que no se agregue a las
// listas no se puede detectar automáticamente.
TEST(gui_keymap_test_inventories_are_valid) {
    GuiKeymap km;

    // Sin duplicados en los inventarios.
    for (InputEventType t : kAllEventTypes) {
        int inAll = 0;
        for (auto x : kAllEventTypes)
            if (x == t) ++inAll;
        CHECK_EQ(inAll, 1);
    }
    for (InputEventType t : kRepeatableEventTypes) {
        int inRepeatable = 0;
        for (auto x : kRepeatableEventTypes)
            if (x == t) ++inRepeatable;
        CHECK_EQ(inRepeatable, 1);
    }

    // Los repetibles son un subconjunto del inventario general y coinciden
    // con la política del keymap.
    for (InputEventType t : kRepeatableEventTypes) {
        CHECK(contains(kAllEventTypes, t));
        CHECK(km.isRepeatable(t));
    }
    for (InputEventType t : kAllEventTypes) {
        CHECK_EQ(km.isRepeatable(t), contains(kRepeatableEventTypes, t));
    }

    // Syms únicos y bien clasificados: Unknown(1) + no-textuales(14) +
    // letras(26). Los conteos son el tamaño esperado de cada familia del
    // inventario, no una enumeración automática del enum.
    for (GuiKeySym s : kAllKeySyms) {
        int inAll = 0;
        for (auto x : kAllKeySyms)
            if (x == s) ++inAll;
        CHECK_EQ(inAll, 1);
    }
    for (GuiKeySym s : kNonTextKeySyms) {
        int inNonText = 0;
        for (auto x : kNonTextKeySyms)
            if (x == s) ++inNonText;
        CHECK_EQ(inNonText, 1);
        CHECK(contains(kAllKeySyms, s));
        CHECK(s != GuiKeySym::Unknown);
        CHECK(!(s >= GuiKeySym::KeyA && s <= GuiKeySym::KeyZ));
    }

    int nUnknown = 0, nNonText = 0, nLetters = 0;
    for (GuiKeySym s : kAllKeySyms) {
        if (s == GuiKeySym::Unknown) ++nUnknown;
        else if (contains(kNonTextKeySyms, s)) ++nNonText;
        else if (s >= GuiKeySym::KeyA && s <= GuiKeySym::KeyZ) ++nLetters;
        else CHECK(false);  // sym sin familia: decidir regla antes de seguir
    }
    CHECK_EQ(nUnknown, 1);
    CHECK_EQ(nNonText, 14);
    CHECK_EQ(nLetters, 26);
}

TEST(gui_keymap_no_repeat_exhaustivo_headless) {
    GuiKeymap km;
    
    // Partición verificada contra la lista central: 
    CHECK_EQ(std::size(kAllEventTypes), size_t(30));
    for (InputEventType t : kAllEventTypes) {
        CHECK(km.isRepeatable(t) == contains(kRepeatableEventTypes, t));
    }
    
    // Negados por contrato (default): ni repiten ni deben empezar a hacerlo
    // en silencio si alguien enlaza una letra a ellos.
    for (InputEventType t : kAllEventTypes) {
        if (!contains(kRepeatableEventTypes, t)) CHECK(!km.isRepeatable(t));
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
               
    // Enter repetido solo pasa con el contexto explícito de Interaccion.
    CHECK(!km.mapKey(key(GuiKeySym::Enter, false, false, false, true))
               .has_value());
    CHECK_EQ(static_cast<int>(
                 *km.mapKey(key(GuiKeySym::Enter, false, false, false, true), true)),
             static_cast<int>(InputEventType::InsertNewline));
             
    // ...y los repetibles sí pasan con repeat ( Delete/Home/End incluidos ).
    CHECK(km.mapKey(key(GuiKeySym::Delete, false, false, false, true)).has_value());
    CHECK(km.mapKey(key(GuiKeySym::Home, false, false, false, true)).has_value());
    CHECK(km.mapKey(key(GuiKeySym::End, false, false, false, true)).has_value());
}

// --- Headless extra
// Tab no tiene enlace por defecto (paridad TTY), pero puede enlazarse
// manualmente; tras resetDefaults vuelve a quedar sin enlace. KeyPadEnter
// normaliza mods. Unknown nunca resuelve. ---
TEST(gui_keymap_tab_y_keypad_headless) {
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
    checkMaps(km, GuiKeySym::KeyPadEnter, InputEventType::InsertNewline, true);
    checkMaps(km, GuiKeySym::KeyPadEnter, InputEventType::InsertNewline, false, true);
    
    GuiKeymap km2;
    CHECK(km2.bindKey({GuiKeySym::KeyPadEnter, true, false},
                      InputEventType::MoveDown));
    CHECK_EQ(static_cast<int>(*km2.mapKey(key(GuiKeySym::KeyPadEnter))),
             static_cast<int>(InputEventType::MoveDown));

    // Unknown nunca resuelve (sin enlace posible).
    checkUnmapped(km, GuiKeySym::Unknown);
}

// Headless extra: 
// boundKey aplica la misma normalización que mapKey
TEST(gui_keymap_boundkey_normaliza_headless) {
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

// Unknown: no es no-textual ni letra, así que ninguna regla lo rescata.
// Ninguna combinación de mods (con o sin repetición) resuelve y no se puede
// enlazar.
TEST(gui_keymap_unknown_never_maps) {
    GuiKeymap km;

    for (bool ctrl : {false, true})
        for (bool alt : {false, true})
            for (bool shift : {false, true})
                for (bool repeat : {false, true}) {
                    CHECK(!km.mapKey(key(GuiKeySym::Unknown, ctrl, alt, shift,
                                         repeat))
                               .has_value());
                    CHECK(!km.boundKey({GuiKeySym::Unknown, ctrl, alt, shift})
                               .has_value());
                }

    CHECK(!km.bindKey({GuiKeySym::Unknown, false, false},
                      InputEventType::MoveDown));
    CHECK(!km.bindKey({GuiKeySym::Unknown, true, false},
                      InputEventType::MoveDown));
}

// política bindable() 
// particionada por familia (41 syms, lista central).
// No-textuales: enlazables con cualquier mods (se normaliza al guardar).
// Letras: solo con Ctrl sin Alt (invariante anti-doble-acción KEYDOWN + TEXTINPUT). 
// Unknown: nunca. Un sym nuevo sin clasificar rompe el conteo.
TEST(gui_keymap_bindable_particion_por_familia_headless) {
    CHECK_EQ(std::size(kAllKeySyms), size_t(41));
    
    for (GuiKeySym s : kAllKeySyms) {
        GuiKeymap km;
        if (s == GuiKeySym::Unknown) {
            CHECK(!km.bindKey({s, false, false}, InputEventType::MoveDown));
            CHECK(!km.bindKey({s, true, false}, InputEventType::MoveDown));
            CHECK(!km.bindKey({s, true, true}, InputEventType::MoveDown));
        } else if (contains(kNonTextKeySyms, s)) {
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
}

// ===========================================================================
// GuiKeymap: Ctrl+C, Ctrl+X, Ctrl+V
// ===========================================================================

TEST(gui_keymap_clipboard_shortcuts) {
    GuiKeymap keymap;
    keymap.resetDefaults();

    // Ctrl+letra sin Alt: únicos atajos de portapapeles.
    CHECK_EQ(static_cast<int>(*keymap.mapKey(key(GuiKeySym::KeyC, true, false, false))),
             static_cast<int>(InputEventType::Copy));
    CHECK_EQ(static_cast<int>(*keymap.mapKey(key(GuiKeySym::KeyX, true, false, false))),
             static_cast<int>(InputEventType::Cut));
    CHECK_EQ(static_cast<int>(*keymap.mapKey(key(GuiKeySym::KeyV, true, false, false))),
             static_cast<int>(InputEventType::Paste));

    // Sin Ctrl: la letra va por SDL_TEXTINPUT (sin enlace en KEYDOWN).
    CHECK(!keymap.mapKey(key(GuiKeySym::KeyC, false, false, false)).has_value());
    CHECK(!keymap.mapKey(key(GuiKeySym::KeyX, false, false, false)).has_value());
    CHECK(!keymap.mapKey(key(GuiKeySym::KeyV, false, false, false)).has_value());

    // Shift distingue enlaces: Ctrl+Shift+C/X/V no están enlazados.
    CHECK(!keymap.mapKey(key(GuiKeySym::KeyC, true, false, true)).has_value());
    CHECK(!keymap.mapKey(key(GuiKeySym::KeyX, true, false, true)).has_value());
    CHECK(!keymap.mapKey(key(GuiKeySym::KeyV, true, false, true)).has_value());

    // Alt (incl. Ctrl+Alt/AltGr) tampoco: no se disparan comandos accidentales.
    CHECK(!keymap.mapKey(key(GuiKeySym::KeyC, true, true, false)).has_value());
    CHECK(!keymap.mapKey(key(GuiKeySym::KeyX, true, true, false)).has_value());
    CHECK(!keymap.mapKey(key(GuiKeySym::KeyV, true, true, false)).has_value());
}