#pragma once

#include <map>
#include <optional>
#include <string>

#include "platform/Event.h"

// Frontier (10): IKeymap — interfaz COMÚN.
//
// Vive en platform/ (no en platform/tty/): el mapeo input crudo ->
// Evento es vocabulario común; las implementaciones (TtyKeymap en
// platform/tty/) dependen de esto, nunca al revés:
//   common → no depende de tty
//   tty → puede depender de common
//
// El Editor nunca la ve: consume Events ya traducidos. Terminal la
// expone vía keymapIface(); los tests/fakes pueden implementarla
// sin TTY.
class IKeymap {
public:
    virtual ~IKeymap() = default;
    virtual void bindControl(unsigned char byte, EventType type) = 0;
    virtual std::optional<EventType> control(unsigned char byte) const = 0;
    virtual void bindSequence(const std::string& contents, EventType type) = 0;
    virtual std::optional<EventType> sequence(const std::string& contents) const = 0;
    virtual void resetDefaults() = 0;
};
