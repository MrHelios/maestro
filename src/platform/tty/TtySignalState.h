#pragma once

#include <signal.h>
#include <termios.h>

// ---------------------------------------------------------------------------
// TtySignalState: ÚNICO estado compartido con los signal handlers C.
//
// REGLA: Terminal es dueño de un TtySignalState miembro (lifetime = lifetime
// del Terminal). El handler C no puede capturar `this`, así que un único
// puntero global (g_activeSignalState, en Terminal.cpp) apunta al miembro
// del Terminal vivo mientras algún modo está activo. No hay otros globales.
//
// Separación deliberada: esto lleva SOLO lo que el handler necesita
// (flags signal-safe + termios original + sigactions guardadas). El resto
// del estado (Keymap, flags de UI, pty logic) queda en Terminal y el handler
// jamás lo toca. No convertir esto en un contenedor gigante.
// ---------------------------------------------------------------------------
struct TtySignalState {
    static constexpr int kFatalCount = 9;

    struct SavedAction {
        int sig = 0;
        struct sigaction old {};
    };

    // termios original (dueño: Terminal::origTermios_). Solo se lee en el
    // handler para restaurar; se publica al entrar en raw y se nulifica al salir.
    termios* orig = nullptr;
    volatile sig_atomic_t rawActive = 0;
    volatile sig_atomic_t mouseActive = 0;
    volatile sig_atomic_t altActive = 0;
    volatile sig_atomic_t resized = 0;

    struct sigaction oldWinchAction {};
    bool winchInstalled = false;
    SavedAction savedActions[kFatalCount];
};
