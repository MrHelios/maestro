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
// TTY simpleEscapeForm, shift incluido); las letras exigen coincidencia
// exacta en (ctrl, alt, shift): Ctrl+S y Ctrl+Shift+S son enlaces
// distintos y deben probarse por separado.
//
// CONTRASTE DELIBERADO (P1): un modificador adicional NUNCA bloquea la
// acción no-textual (Ctrl+Alt+Escape sigue siendo Escape, Ctrl+Left sigue
// siendo MoveLeft): el TTY no distingue esas combinaciones y la GUI las
// acepta igual por paridad. En letras, en cambio, la coincidencia es
// exacta y Alt siempre rechaza: los layouts entregan Ctrl+Alt/AltGr que
// además producen SDL_TEXTINPUT, y resolverlos como comando sería una
// acción accidental (doble efecto KEYDOWN + texto).
GuiKeyBinding normalize(GuiKeySym sym, bool ctrl, bool alt, bool shift) {
    if (isNonText(sym)) return GuiKeyBinding{sym, false, false, false};
    return GuiKeyBinding{sym, ctrl, alt, shift};
}

// Validez de un enlace nuevo. Invariante anti-doble-acción: KEYDOWN nunca
// puede resolver a una acción para una pulsación que además produzca
// SDL_TEXTINPUT (el commit de texto no trae identidad de tecla: suprimirlo
// de forma selectiva sería frágil ante layouts/IME). Por eso:
//   - lo no-textual se enlaza con cualquier mods (se normaliza al guardar);
//   - las letras exigen Ctrl SIN Alt, con o sin Shift (Ctrl+letra nunca
//     genera TEXTINPUT en SDL; Alt/AltGr y Ctrl+Alt —alias de AltGr en
//     algunas plataformas— SÍ pueden coincidir con commits de texto como
//     @/|/\/{, cuyo comportamiento actual se documenta sin garantizar
//     todos los layouts);
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
//   - Ctrl+Shift+S = SaveAs directo (`buffer.guardar.como` sin prefijo;
//     en TTY no existe: el byte de control no distingue Shift).
//   - Tab sin enlazar (paridad TTY: byte 9 sin enlace). Consecuencia
//     documentada: Tab no inserta tabulación en esta fase (en la mayoría de
//     plataformas Tab llega por KEYDOWN, no como SDL_TEXTINPUT).
//   - Letras sin Ctrl sin enlazar: el texto imprimible (ASCII, UTF-8,
//     AltGr como @/|/\/{} según layout —comportamiento actual, no garantía
//     universal—) llega por SDL_TEXTINPUT; mapearlo en KEYDOWN duplicaría
//     la inserción.
//   - Keypad: KeyPadEnter siempre; KP_7/KP_1/KP_9/KP_3 llegan como
//     Home/End/PageUp/PageDown SOLO con NumLock apagado (el adaptador SDL
//     los traduce únicamente sin KMOD_NUM; con NumLock producen dígitos
//     por TEXTINPUT y quedan en Unknown).
void GuiKeymap::resetDefaults() {
    keys_.clear();

    // --- Navegación/edición: ignoran mods en mapKey (van sin
    // ctrl/alt/shift en la tabla; la resolución las acepta con cualquier
    // modificador) ---
    bindKey({GuiKeySym::Escape, false, false, false}, InputEventType::Escape);
    bindKey({GuiKeySym::Enter, false, false, false}, InputEventType::InsertNewline);
    bindKey({GuiKeySym::KeyPadEnter, false, false, false}, InputEventType::InsertNewline);
    bindKey({GuiKeySym::Backspace, false, false, false}, InputEventType::Backspace);
    bindKey({GuiKeySym::Delete, false, false, false}, InputEventType::Delete);
    bindKey({GuiKeySym::Left, false, false, false}, InputEventType::MoveLeft);
    bindKey({GuiKeySym::Right, false, false, false}, InputEventType::MoveRight);
    bindKey({GuiKeySym::Up, false, false, false}, InputEventType::MoveUp);
    bindKey({GuiKeySym::Down, false, false, false}, InputEventType::MoveDown);
    bindKey({GuiKeySym::Home, false, false, false}, InputEventType::MoveHome);
    bindKey({GuiKeySym::End, false, false, false}, InputEventType::MoveEnd);
    bindKey({GuiKeySym::PageUp, false, false, false}, InputEventType::PageUp);
    bindKey({GuiKeySym::PageDown, false, false, false}, InputEventType::PageDown);

    // --- Combos Ctrl: (ctrl, alt) exactos; shift distingue enlaces ---
    // Ctrl+Shift+S es Guardar como (SaveAs directo); el resto conserva la
    // tolerancia histórica a Shift con un enlace explícito por variante,
    // para que ninguna combinación quede librada a un fallback silencioso.
    bindKey({GuiKeySym::KeyK, true, false, false}, InputEventType::Prefix);
    bindKey({GuiKeySym::KeyK, true, false, true}, InputEventType::Prefix);
    bindKey({GuiKeySym::KeyS, true, false, false}, InputEventType::Save);
    bindKey({GuiKeySym::KeyS, true, false, true}, InputEventType::SaveAs);
    bindKey({GuiKeySym::KeyQ, true, false, false}, InputEventType::Quit);
    bindKey({GuiKeySym::KeyQ, true, false, true}, InputEventType::Quit);
    bindKey({GuiKeySym::KeyU, true, false, false}, InputEventType::Undo);
    bindKey({GuiKeySym::KeyU, true, false, true}, InputEventType::Undo);
    bindKey({GuiKeySym::KeyY, true, false, false}, InputEventType::Redo);
    bindKey({GuiKeySym::KeyY, true, false, true}, InputEventType::Redo);
    bindKey({GuiKeySym::KeyZ, true, false, false}, InputEventType::Undo);
    bindKey({GuiKeySym::KeyZ, true, false, true}, InputEventType::Undo);
}

GuiKeymap::GuiKeymap() { resetDefaults(); }

bool GuiKeymap::bindKey(Binding b, InputEventType type) {
    if (!bindable(b.sym, b.ctrl, b.alt)) return false;
    keys_[normalize(b.sym, b.ctrl, b.alt, b.shift)] = type;
    return true;
}

void GuiKeymap::unbindKey(Binding b) {
    keys_.erase(normalize(b.sym, b.ctrl, b.alt, b.shift));
}

std::optional<InputEventType> GuiKeymap::boundKey(Binding b) const {
    auto it = keys_.find(normalize(b.sym, b.ctrl, b.alt, b.shift));
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

std::optional<InputEventType> GuiKeymap::mapKey(
    const GuiKeyPress& press, bool allowRepeatedNewline) const {
    // Resolución única por tabla con la normalización de arriba: la
    // navegación acepta cualquier modificador pero consulta el enlace
    // remapeado; las letras solo resuelven con su (ctrl, alt, shift)
    // exacto (Ctrl+S != Ctrl+Shift+S, sin fallback silencioso).
    const Binding b = normalize(press.sym, press.mods.ctrl, press.mods.alt,
                                press.mods.shift);
    auto it = keys_.find(b);
    if (it == keys_.end()) return std::nullopt;
    if (press.repeat && !isRepeatable(it->second) &&
        !(allowRepeatedNewline && it->second == InputEventType::InsertNewline))
        return std::nullopt;
    return it->second;
}
