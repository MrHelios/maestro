#pragma once

#include <map>
#include <optional>

#include "platform/InputEvent.h"
#include "platform/gui/GuiKey.h"
#include "platform/gui/IGuiKeymap.h"

// GuiKeymap: implementación GUI de IGuiKeymap (ver gui/IGuiKeymap.h).
//
// Resolución UNIFICADA por tabla: mapKey normaliza antes de buscar.
//   - Navegación/edición (flechas, Home/End, PgUp/PgDn, Enter, Backspace,
//     Delete, Escape, Tab): normalizan mods a (false, false, false)
//     —ignoran Ctrl/Alt/Shift en la búsqueda (paridad TTY
//     simpleEscapeForm)— pero SÍ pasan por la tabla, así que
//     bindKey/unbindKey/resetDefaults les afectan de verdad (no hay camino
//     codificado que eluda la tabla).
//   - Letras: solo con Ctrl sin Alt, con coincidencia EXACTA en Shift
//     (Ctrl+S vs Ctrl+Shift+S son enlaces distintos: Save vs SaveAs); el
//     resto de combinaciones se rechaza en bindKey (ver IGuiKeymap:
//     invariante anti-doble-acción KEYDOWN + TEXTINPUT). Sin Ctrl las
//     letras NO mapean (el texto viene por SDL_TEXTINPUT, nunca por
//     KEYDOWN: sin doble inserción).
// CONTRASTE (P1, deliberado y probado en test_guikeymap.cpp): la tolerancia
// a modificadores extra vale SOLO para lo no-textual (paridad TTY:
// Ctrl+Alt+Escape sigue siendo Escape). En letras, cualquier Alt (solo,
// con Ctrl o con Shift) deja la pulsación sin enlace, así los modificadores
// que SDL entrega en los layouts soportados (Alt/AltGr/Ctrl+Alt) jamás
// generan comandos accidentales: el carácter llega por SDL_TEXTINPUT.
//
// Remapeable en tiempo de ejecución (bindKey/unbindKey/resetDefaults) sin
// tocar el Editor: un mismo evento puede venir de distintas teclas.
// GuiKeymap: único tipo concreto; GuiRunLoop lo expone vía IGuiKeymap
// (keymapIface()), nunca como concreto.
class GuiKeymap : public IGuiKeymap {
public:
    GuiKeymap();

    std::optional<InputEventType> mapKey(
        const GuiKeyPress& press,
        bool allowRepeatedNewline = false) const override;
    bool isRepeatable(InputEventType type) const override;

    // Alias local del vocabulario compartido (GuiKey.h): la interfaz habla
    // GuiKeyBinding; el concreto lo acepta tal cual. bindKey valida y
    // normaliza (ver IGuiKeymap): false = enlace rechazado, tabla intacta.
    using Binding = GuiKeyBinding;
    bool bindKey(Binding b, InputEventType type) override;
    void unbindKey(Binding b) override;
    std::optional<InputEventType> boundKey(Binding b) const override;
    void resetDefaults() override;

private:
    std::map<Binding, InputEventType> keys_;
};
