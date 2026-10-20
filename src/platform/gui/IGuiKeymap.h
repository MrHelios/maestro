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
    // KEYDOWN es comandos/navegación/modificadores; el texto Unicode va
    // por SDL_TEXTINPUT (InsertChar por celda), nunca por aquí.
    // std::nullopt = tecla no enlazada (incluye: imprimible sin Ctrl, cuyo
    // texto llega por SDL_TEXTINPUT; repetición suprimida por
    // isRepeatable/contexto; Tab sin enlazar por paridad TTY).
    // `allowRepeatedNewline` es contexto de política, no estado: GuiRunLoop
    // lo pone en true solo si Enter inserta una fila en el estado actual
    // (Interaccion). El keymap no conoce modos.
    virtual std::optional<InputEventType> mapKey(
        const GuiKeyPress& press,
        bool allowRepeatedNewline = false) const = 0;
    // Política base de repetición (SDL_KEYDOWN con repeat != 0): solo
    // navegación (flechas, Home/End, PgUp/PgDn) y borrado (Backspace,
    // Delete) repiten; todo lo demás (Enter, Escape, Prefix, Save, SaveAs,
    // Quit, Undo/Redo) no. mapKey la aplica: con repeat == true las
    // acciones no repetibles devuelven std::nullopt, salvo InsertNewline
    // cuando allowRepeatedNewline indica que el estado lo permite. Se
    // expone para tests y documentación.
    virtual bool isRepeatable(InputEventType type) const = 0;
    // Remapeo en tiempo de ejecución (misma frontera que
    // Terminal::keymapIface() -> ITtyKeymap): el Editor nunca cambia aunque
    // una misma acción venga de distintas teclas.
    //
    // bindKey devuelve false (sin tocar la tabla) si el enlace es inválido:
    // solo se enlaza lo no-textual (navegación/edición, con cualquier mods;
    // se normaliza al guardar, shift incluido: un modificador adicional
    // nunca bloquea esas acciones, por paridad TTY deliberada) y letras con
    // Ctrl sin Alt (con o sin Shift: Ctrl+S y Ctrl+Shift+S son enlaces
    // DISTINTOS; con Alt nunca, para que Alt/AltGr de los layouts no dispare
    // comandos accidentales).
    // Las letras sin Ctrl, con Alt o Ctrl+Alt (alias de AltGr en algunas
    // plataformas) y Unknown se rechazan: esa pulsación puede producir
    // SDL_TEXTINPUT y aceptar el enlace ejecutaría DOBLE acción (KEYDOWN +
    // InsertChar), ya que el commit de texto no trae identidad de tecla
    // para suprimirlo de forma selectiva. boundKey/unbindKey aplican la
    // misma normalización.
    virtual bool bindKey(GuiKeyBinding b, InputEventType type) = 0;
    virtual void unbindKey(GuiKeyBinding b) = 0;
    virtual std::optional<InputEventType> boundKey(GuiKeyBinding b) const = 0;
    virtual void resetDefaults() = 0;
};
