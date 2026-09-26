#pragma once

struct Event;

// A — Frontier (4): IEventSource.
//
// Abstracción de "de dónde vienen los Event". Terminal la implementa;
// tests/fakes la implementan sin TTY. El Editor común solo conoce esto
// (nunca `Terminal`, `Keymap`, `pollfd` ni `fd()`).
class IEventSource {
public:
    virtual ~IEventSource() = default;
    // Igual que Terminal::readEvent(e, timeoutMs): true si hay evento.
    virtual bool readEvent(Event& event, int timeoutMs) = 0;
};
