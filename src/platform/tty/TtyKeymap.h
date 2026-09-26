#pragma once

#include "platform/tty/Keymap.h"

// Frontier (10): TtyKeymap — implementación TTY de IKeymap.
//
// `Keymap` se conserva como nombre histórico (tests y Terminal lo usan);
// este alias es el nombre canónico del paso 10. La interfaz vive en
// platform/IKeymap.h (común); este header vive en tty (implementación).
using TtyKeymap = Keymap;
