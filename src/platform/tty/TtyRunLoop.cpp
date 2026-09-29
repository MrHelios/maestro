#include "platform/tty/TtyRunLoop.h"

#include <poll.h>
#include <signal.h>
#include <cerrno>
#include <chrono>
#include <unistd.h>

#include "app/Editor.h"
#include "platform/tty/Terminal.h"

TtyRunLoop::TtyRunLoop(Editor& editor) : editor_(editor) {}

void TtyRunLoop::run() {
    Terminal terminal;

    // Sincronización inicial: el composition root es dueño del tamaño.
    // Terminal lo mide, Editor lo aplica vía resize() (autónomo, sin
    // consultar backends). Vale igual para un futuro GUI.
    {
        int rows, cols;
        terminal.getWindowSize(rows, cols);
        editor_.resize(rows, cols);
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
        if (terminal.hasResized()) {
            // SIGWINCH -> resize autónomo (mismo camino que un evento GUI).
            int rows, cols;
            terminal.getWindowSize(rows, cols);
            editor_.resize(rows, cols);
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
                int rows, cols;
                terminal.getWindowSize(rows, cols);
                editor_.resize(rows, cols);
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
    write(STDOUT_FILENO, "\x1b[0m\x1b[39m\x1b[49m\x1b[?25h\x1b[2J\x1b[H\x1b[0 q", sizeof("\x1b[0m\x1b[39m\x1b[49m\x1b[?25h\x1b[2J\x1b[H\x1b[0 q") - 1);
}
