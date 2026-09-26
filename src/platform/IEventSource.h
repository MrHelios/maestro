#pragma once

#include "platform/InputEvent.h"

// A — Frontier (4): IEventSource.
//
// Abstracción de "de dónde vienen los InputEvent". Terminal la implementa;
// tests/fakes la implementan sin TTY. El Editor común solo conoce esto
// (nunca `Terminal`, `Keymap`, `pollfd` ni `fd()`).
class IEventSource {
public:
    virtual ~IEventSource() = default;
    // Igual que Terminal::readEvent(e, timeoutMs): true si hay evento.
    virtual bool readEvent(InputEvent& event, int timeoutMs) = 0;
};
