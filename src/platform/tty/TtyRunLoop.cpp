#include "platform/tty/TtyRunLoop.h"

#include <poll.h>
#include <signal.h>
#include <cerrno>
#include <chrono>
#include <unistd.h>

#include "app/Editor.h"
#include "platform/tty/Terminal.h"
#include "platform/WindowSize.h"

namespace {
// Fuente única del camino medir -> aplicar: los 3 sitios de run() lo usan
// (init, SIGWINCH, EINTR). Solo mide y aplica; cada llamada conserva su
// renderFrame/continue propio (el init no repinta ahí, los otros dos sí).
void syncWindowSize(Terminal& terminal, Editor& editor) {
    int rows, cols, pixelW, pixelH;
    terminal.getWindowSize(rows, cols, pixelW, pixelH);
    editor.resize(platform::WindowSize{rows, cols, 0, 0, pixelW, pixelH});
}
}

TtyRunLoop::TtyRunLoop(Editor& editor) : editor_(editor) {}

void TtyRunLoop::run() {
    Terminal terminal;

    // Sincronización inicial: el composition root es dueño del tamaño.
    // Terminal lo mide, Editor lo aplica vía resize() (autónomo, sin
    // consultar backends). Vale igual para un futuro GUI.
    {
        syncWindowSize(terminal, editor_);
    }

    terminal.enableRawMode();
    terminal.enterAlternateScreen();
    terminal.enableMouseTracking();

    sigset_t blockMask, origMask;
    sigemptyset(&blockMask);
    sigaddset(&blockMask, SIGWINCH);
    sigprocmask(SIG_BLOCK, &blockMask, &origMask);

    // Primer frame vía fachada pública (scroll + brackets + diff internos).
    editor_.renderFrame();

    while (editor_.isRunning()) {
        // Vuelta de SIGCONT: la pantalla física se perdió (re-entrada a
        // alt screen sobre buffer limpio) aunque el tamaño sea el mismo.
        // Invalidar el diff ANTES de cualquier render de este giro,
        // incluido el camino EINTR de abajo (CONT interrumpe el ppoll).
        const bool resumed = terminal.hasResumed();
        if (resumed) editor_.invalidateScreen();

        if (terminal.hasResized()) {
            // SIGWINCH -> resize autónomo (mismo camino que un evento GUI).
            syncWindowSize(terminal, editor_);
            editor_.renderFrame();
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        int waitMs = editor_.nextTimeoutMs(now);
        // Pre-ppoll SOLO clipboard (heartbeat INCR, sin estado visible):
        // el watcher va por readiness (abajo) porque handleFileChange
        // puede recargar el documento y el ppoll bloquearía con la
        // pantalla vieja.
        editor_.processClipboardEvents();
        int cfd = editor_.clipboardFd();
        struct pollfd pfds[3];
        pfds[0].fd = STDIN_FILENO;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        int nfds = 1;
        int clipboardIdx = -1;
        if (cfd >= 0) {
            clipboardIdx = nfds;
            pfds[nfds].fd = cfd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
        int watcherIdx = -1;
        int wfd = editor_.watcherFd();
        if (wfd >= 0) {
            watcherIdx = nfds;
            pfds[nfds].fd = wfd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
        struct timespec ts;
        struct timespec* tsp = nullptr;
        if (waitMs >= 0) {
            ts.tv_sec = waitMs / 1000;
            ts.tv_nsec = (waitMs % 1000) * 1000000L;
            tsp = &ts;
        }
        int pr = ppoll(pfds, nfds, tsp, &origMask);
        if (pr < 0) {
            if (errno == EINTR && terminal.hasResized()) {
                syncWindowSize(terminal, editor_);
                editor_.renderFrame();
            } else if (resumed) {
                // Reanudación sin resize pendiente: igual hay que repintar
                // (el diff quedó invalidado arriba y el camino de resize
                // no corrió). Hoy CONT siempre marca resized, así que es
                // solo robustez ante un futuro desacople de flags.
                editor_.renderFrame();
            }
            continue;
        }
        if (pr == 0) {
            const auto now = std::chrono::steady_clock::now();
            editor_.tick(now);
            editor_.renderFrame();
            continue;
        }
        bool xReady = (clipboardIdx >= 0 && (pfds[clipboardIdx].revents & POLLIN));
        bool inReady = (pfds[0].revents & POLLIN);
        bool watcherReady = (watcherIdx >= 0 && (pfds[watcherIdx].revents & POLLIN));
        if (xReady || watcherReady) editor_.pollExternalEvents();
        if (inReady) {
            InputEvent event;
            if (!terminal.readEvent(event, 0)) {
                if (xReady || watcherReady) continue;
            } else {
                const auto now = std::chrono::steady_clock::now();
                editor_.handleEvent(event);
                if (!editor_.isRunning()) break;
                if (editor_.consumeSuspendRequest()) {
                    // Ctrl+Z: suspender el proceso. El handler de SIGTSTP
                    // ya instalado restaura la terminal y detiene con
                    // SIGSTOP; al reanudar (fg), el handler de CONT
                    // recompone los modos y marca resize, así que el
                    // próximo giro re-renderiza (sin render intermedio).
                    // Sin handlers instalados (raw fallido) equivale a un
                    // kill -TSTP externo.
                    raise(SIGTSTP);
                    continue;
                }
                // Solo expiración: el autoscroll avanza únicamente en el
                // timeout (evita un paso extra + consulta al oráculo tras
                // cada evento).
                editor_.clearExpiredMessages(now);
                editor_.renderFrame();
            }
        } else if (xReady || watcherReady) {
            // Sin autoscroll acá: no hay render después y movería la
            // selección con la pantalla desactualizada.
            const auto now = std::chrono::steady_clock::now();
            editor_.clearExpiredMessages(now);
        }
    }

    sigprocmask(SIG_SETMASK, &origMask, nullptr);
    terminal.disableMouseTracking();
    terminal.leaveAlternateScreen();
    terminal.disableRawMode();
    write(STDOUT_FILENO, "\x1b[0m\x1b[39m\x1b[49m\x1b[?25h\x1b[2J\x1b[H", sizeof("\x1b[0m\x1b[39m\x1b[49m\x1b[?25h\x1b[2J\x1b[H") - 1);
    write(STDOUT_FILENO, kCursorDefault, sizeof(kCursorDefault) - 1);
}
