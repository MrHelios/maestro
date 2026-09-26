#pragma once

#include <map>
#include <optional>
#include <string>

#include "platform/InputEvent.h"

// ITtyKeymap — interfaz TTY-ONLY (decisión 2).
//
// El mapeo `byte de control / contenido de secuencia ESC -> InputEventType`
// es vocabulario del backend TTY, no común: una futura GuiKeymap
// (keycode + modifiers -> InputEventType) nunca podría implementar
// bindControl(unsigned char)/bindSequence(string). Por eso la interfaz vive
// en platform/tty/ y la GUI tendrá la suya propia; ambas producen el mismo
// InputEvent semántico (platform/InputEvent.h). La vieja platform/IKeymap.h
// fue eliminada: nada en platform/ depende de platform/tty/.
// El código nuevo usa ITtyKeymap.
class ITtyKeymap {
public:
    virtual ~ITtyKeymap() = default;
    virtual void bindControl(unsigned char byte, InputEventType type) = 0;
    virtual std::optional<InputEventType> control(unsigned char byte) const = 0;
    virtual void bindSequence(const std::string& contents, InputEventType type) = 0;
    virtual std::optional<InputEventType> sequence(const std::string& contents) const = 0;
    virtual void resetDefaults() = 0;
};
