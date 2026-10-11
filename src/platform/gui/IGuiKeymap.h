#pragma once

#include <optional>

#include "platform/InputEvent.h"
#include "platform/gui/GuiKey.h"

// IGuiKeymap — interfaz GUI-ONLY (decisión 2 de architecture.md §3).
//
// El mapeo `tecla lógica + modificadores -> InputEventType` es vocabulario
// del backend GUI, no común: nunca reutiliza ITtyKeymap (byte de control /
// contenido de secuencia ESC) y ambas producen el mismo InputEvent
// semántico (platform/InputEvent.h). El keymap solo traduce; no conoce
// modos ni estado del Editor (anti-patrón §5): no sabe qué es Prefix,
// Navegación ni ningún comando.
class IGuiKeymap {
public:
    virtual ~IGuiKeymap() = default;
    // Traduce una pulsación normalizada (ver GuiKey.h) a evento semántico.
    // std::nullopt = tecla no enlazada (incluye: imprimible sin Ctrl/Alt,
    // cuyo texto llega por SDL_TEXTINPUT; repetición suprimida por
    // isRepeatable; Tab sin enlazar por paridad TTY).
    virtual std::optional<InputEventType> mapKey(const GuiKeyPress& press) const = 0;
    // Política de repetición (SDL_KEYDOWN con repeat != 0): solo
    // navegación (flechas, Home/End, PgUp/PgDn) y borrado (Backspace,
    // Delete) repiten; todo lo demás (Enter, Escape, Prefix, Save, Quit,
    // Undo/Redo) no. mapKey ya la aplica: con repeat == true las acciones
    // no repetibles devuelven std::nullopt. Se expone para tests y
    // documentación. Si algún día se quiere que Enter repita al mantenerlo,
    // será una decisión deliberada y probada, no un default silencioso.
    virtual bool isRepeatable(InputEventType type) const = 0;
    // Remapeo en tiempo de ejecución (misma frontera que
    // Terminal::keymapIface() -> ITtyKeymap): el Editor nunca cambia aunque
    // una misma acción venga de distintas teclas.
    //
    // bindKey devuelve false (sin tocar la tabla) si el enlace es inválido:
    // solo se enlaza lo no-textual (navegación/edición, con cualquier mods;
    // se normaliza al guardar) y letras con Ctrl sin Alt. Las letras sin
    // Ctrl, con Alt o Ctrl+Alt (alias de AltGr en algunas plataformas) y
    // Unknown se rechazan: esa pulsación puede producir SDL_TEXTINPUT y
    // aceptar el enlace ejecutaría DOBLE acción (KEYDOWN + InsertChar), ya
    // que el commit de texto no trae identidad de tecla para suprimirlo de
    // forma selectiva. boundKey/unbindKey aplican la misma normalización.
    virtual bool bindKey(GuiKeyBinding b, InputEventType type) = 0;
    virtual void unbindKey(GuiKeyBinding b) = 0;
    virtual std::optional<InputEventType> boundKey(GuiKeyBinding b) const = 0;
    virtual void resetDefaults() = 0;
};
