#pragma once

#include "platform/tty/Keymap.h"

// Frontier (10): TtyKeymap — implementación TTY de ITtyKeymap.
//
// `Keymap` se conserva como nombre histórico (tests y Terminal lo usan);
// este alias es el nombre canónico del paso 10. La interfaz vive en
// platform/tty/ITtyKeymap.h (TTY-only); la GUI tendrá la suya propia.
using TtyKeymap = Keymap;
