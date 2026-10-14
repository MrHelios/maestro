#include "platform/gui/GuiKeymap.h"

namespace {
// Familia navegación/edición: teclas de control que nunca producen
// SDL_TEXTINPUT en ninguna plataforma (no generan texto). Incluye Tab:
// enlazarlo es seguro por la misma razón (por defecto va sin enlazar,
// paridad TTY).
bool isNonText(GuiKeySym sym) {
    switch (sym) {
        case GuiKeySym::Escape:
        case GuiKeySym::Enter:
        case GuiKeySym::KeyPadEnter:
        case GuiKeySym::Tab:
        case GuiKeySym::Backspace:
        case GuiKeySym::Delete:
        case GuiKeySym::Left:
        case GuiKeySym::Right:
        case GuiKeySym::Up:
        case GuiKeySym::Down:
        case GuiKeySym::Home:
        case GuiKeySym::End:
        case GuiKeySym::PageUp:
        case GuiKeySym::PageDown:
            return true;
        default:
            return false;
    }
}

bool isLetter(GuiKeySym sym) {
    return sym >= GuiKeySym::KeyA && sym <= GuiKeySym::KeyZ;
}

// Normalización única de resolución: lo no-textual ignora mods (paridad
// TTY simpleEscapeForm); las letras exigen coincidencia exacta.
GuiKeyBinding normalize(GuiKeySym sym, bool ctrl, bool alt) {
    if (isNonText(sym)) return GuiKeyBinding{sym, false, false};
    return GuiKeyBinding{sym, ctrl, alt};
}

// Validez de un enlace nuevo. Invariante anti-doble-acción: KEYDOWN nunca
// puede resolver a una acción para una pulsación que además produzca
// SDL_TEXTINPUT (el commit de texto no trae identidad de tecla: suprimirlo
// de forma selectiva sería frágil ante layouts/IME). Por eso:
//   - lo no-textual se enlaza con cualquier mods (se normaliza al guardar);
//   - las letras exigen Ctrl SIN Alt (Ctrl+letra nunca genera TEXTINPUT en
//     SDL; Alt/AltGr y Ctrl+Alt —alias de AltGr en algunas plataformas—
//     SÍ pueden coincidir con commits de texto como @/|/\/{);
//   - lo demás (letras sin Ctrl, Unknown) se rechaza.
bool bindable(GuiKeySym sym, bool ctrl, bool alt) {
    if (isNonText(sym)) return true;
    if (isLetter(sym)) return ctrl && !alt;
    return false;
}
}  // namespace

// Tabla por defecto. Paridad TTY (TtyKeymap::resetDefaults + Terminal.cpp):
// flechas/Home/End/Delete/PgUp/PgDn/Enter/Backspace/Escape hacen lo mismo
// en ambos backends; Ctrl+K/S/Q/U/Y también. Diferencias GUI deliberadas:
//   - Ctrl+Z = Undo (convención GUI; en TTY es Suspend/job-control, evento
//     que la GUI nunca produce: no hay nada que suspender).
//   - Tab sin enlazar (paridad TTY: byte 9 sin enlace). Consecuencia
//     documentada: Tab no inserta tabulación en esta fase (en la mayoría de
//     plataformas Tab llega por KEYDOWN, no como SDL_TEXTINPUT).
//   - Letras sin Ctrl/Alt sin enlazar: el texto imprimible (ASCII, UTF-8,
//     AltGr como @/|/\/{} ) llega por SDL_TEXTINPUT; mapearlo en KEYDOWN
//     duplicaría la inserción.
//   - Keypad: KeyPadEnter siempre; KP_7/KP_1/KP_9/KP_3 llegan como
//     Home/End/PageUp/PageDown SOLO con NumLock apagado (el adaptador SDL
//     los traduce únicamente sin KMOD_NUM; con NumLock producen dígitos
//     por TEXTINPUT y quedan en Unknown).
void GuiKeymap::resetDefaults() {
    keys_.clear();

    // --- Navegación/edición: ignoran mods en mapKey (van sin ctrl/alt en
    // la tabla; la resolución las acepta con cualquier modificador) ---
    bindKey({GuiKeySym::Escape, false, false}, InputEventType::Escape);
    bindKey({GuiKeySym::Enter, false, false}, InputEventType::InsertNewline);
    bindKey({GuiKeySym::KeyPadEnter, false, false}, InputEventType::InsertNewline);
    bindKey({GuiKeySym::Backspace, false, false}, InputEventType::Backspace);
    bindKey({GuiKeySym::Delete, false, false}, InputEventType::Delete);
    bindKey({GuiKeySym::Left, false, false}, InputEventType::MoveLeft);
    bindKey({GuiKeySym::Right, false, false}, InputEventType::MoveRight);
    bindKey({GuiKeySym::Up, false, false}, InputEventType::MoveUp);
    bindKey({GuiKeySym::Down, false, false}, InputEventType::MoveDown);
    bindKey({GuiKeySym::Home, false, false}, InputEventType::MoveHome);
    bindKey({GuiKeySym::End, false, false}, InputEventType::MoveEnd);
    bindKey({GuiKeySym::PageUp, false, false}, InputEventType::PageUp);
    bindKey({GuiKeySym::PageDown, false, false}, InputEventType::PageDown);

    // --- Combos Ctrl (ctrl exacto, alt libre == false; shift se ignora) ---
    bindKey({GuiKeySym::KeyK, true, false}, InputEventType::Prefix);
    bindKey({GuiKeySym::KeyS, true, false}, InputEventType::Save);
    bindKey({GuiKeySym::KeyQ, true, false}, InputEventType::Quit);
    bindKey({GuiKeySym::KeyU, true, false}, InputEventType::Undo);
    bindKey({GuiKeySym::KeyY, true, false}, InputEventType::Redo);
    bindKey({GuiKeySym::KeyZ, true, false}, InputEventType::Undo);
}

GuiKeymap::GuiKeymap() { resetDefaults(); }

bool GuiKeymap::bindKey(Binding b, InputEventType type) {
    if (!bindable(b.sym, b.ctrl, b.alt)) return false;
    keys_[normalize(b.sym, b.ctrl, b.alt)] = type;
    return true;
}

void GuiKeymap::unbindKey(Binding b) {
    keys_.erase(normalize(b.sym, b.ctrl, b.alt));
}

std::optional<InputEventType> GuiKeymap::boundKey(Binding b) const {
    auto it = keys_.find(normalize(b.sym, b.ctrl, b.alt));
    return it == keys_.end() ? std::nullopt
                             : std::optional<InputEventType>(it->second);
}

bool GuiKeymap::isRepeatable(InputEventType type) const {
    switch (type) {
        case InputEventType::MoveLeft:
        case InputEventType::MoveRight:
        case InputEventType::MoveUp:
        case InputEventType::MoveDown:
        case InputEventType::MoveHome:
        case InputEventType::MoveEnd:
        case InputEventType::PageUp:
        case InputEventType::PageDown:
        case InputEventType::Backspace:
        case InputEventType::Delete:
            return true;
        default:
            return false;
    }
}

std::optional<InputEventType> GuiKeymap::mapKey(const GuiKeyPress& press) const {
    // Resolución única por tabla con la normalización de arriba: la
    // navegación acepta cualquier modificador pero consulta el enlace
    // remapeado; las letras solo resuelven con su (ctrl, alt) exacto.
    const Binding b =
        normalize(press.sym, press.mods.ctrl, press.mods.alt);
    auto it = keys_.find(b);
    if (it == keys_.end()) return std::nullopt;
    if (press.repeat && !isRepeatable(it->second)) return std::nullopt;
    return it->second;
}
