#include "platform/tty/TtyRunLoop.h"

#include <climits>
#include <poll.h>
#include <signal.h>
#include <cerrno>
#include <unistd.h>

#include "app/Editor.h"
#include "layout/Gutter.h"
#include "platform/tty/Terminal.h"

TtyRunLoop::TtyRunLoop(Editor& editor) : editor_(editor) {}

void TtyRunLoop::run() {
    Terminal terminal;

    // Sincronización inicial como un Resize más: el tamaño viaja en el
    // payload del evento y el Editor lo aplica sin consultar backends.
    // Flujo limpio: terminal.getWindowSize() -> Event::Resize -> Editor.
    {
        int rows, cols;
        terminal.getWindowSize(rows, cols);
        Event init;
        init.type = EventType::Resize;
        init.resizeRows = rows;
        init.resizeCols = cols;
        editor_.handleEvent(init);
    }

    terminal.enableRawMode();
    terminal.enterAlternateScreen();
    terminal.enableMouseTracking();

    sigset_t blockMask, origMask;
    sigemptyset(&blockMask);
    sigaddset(&blockMask, SIGWINCH);
    sigprocmask(SIG_BLOCK, &blockMask, &origMask);

    {
        Buffer& b = editor_.active();
        const int totalLines = b.document.lineCount();
        const int gutterW = gutterWidth(totalLines, b.viewport.width);
        int tw = b.viewport.width - gutterW;
        if (tw < 0) tw = 0;
        b.viewport.scrollToCursor(b.cursor, b.document, tw);
        editor_.renderer_.renderScreenDiff(b.document, b.cursor, b.viewport,
                                   b.filename, b.modified, editor_.statusMessage_,
                                   editor_.state_, b.selection, editor_.searchHighlight_);
    }

    while (editor_.running_) {
        if (terminal.hasResized()) {
            // SIGWINCH -> EventType::Resize: el Editor lo maneja como
            // cualquier otro evento (Frontier 3).
            int rows, cols;
            terminal.getWindowSize(rows, cols);
            Event rs;
            rs.type = EventType::Resize;
            rs.resizeRows = rows;
            rs.resizeCols = cols;
            editor_.handleEvent(rs);
            editor_.renderFrame();
            continue;
        }

        int waitMs = -1;
        if (editor_.clipboard_ && editor_.clipboard_->hasPending()) {
            waitMs = 20;
        }
        if (editor_.statusMessage_.expiry) {
            const auto remaining = *editor_.statusMessage_.expiry -
                                   std::chrono::steady_clock::now();
            const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                remaining).count();
            int msgMs = ms <= 0 ? 0 : static_cast<int>(std::min<long>(ms, INT_MAX));
            if (waitMs < 0) waitMs = msgMs;
            else waitMs = std::min(waitMs, msgMs);
        }
        // Autoscroll temporal de seleccion por mouse: solo si hay gesto en
        // curso con el mouse fuera (no despierta periodicamente en idle
        // normal). Acota el wait para dar un paso por intervalo.
        if (editor_.mouseAutoscrollActive()) {
            const auto target =
                editor_.mouseAutoscrollLastStep_ + editor_.kMouseAutoscrollInterval;
            const auto remaining = target - std::chrono::steady_clock::now();
            const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                remaining).count();
            int tickMs = ms <= 0 ? 0 : static_cast<int>(std::min<long>(ms, INT_MAX));
            if (waitMs < 0) waitMs = tickMs;
            else waitMs = std::min(waitMs, tickMs);
        }
        if (editor_.clipboard_) editor_.clipboard_->processEvents();
        int cfd = editor_.clipboard_ ? editor_.clipboard_->fd() : -1;
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
        int wfd = editor_.watcher_ ? editor_.watcher_->fd() : -1;
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
                Event rs;
                rs.type = EventType::Resize;
                rs.resizeRows = rows;
                rs.resizeCols = cols;
                editor_.handleEvent(rs);
                editor_.renderFrame();
            }
            continue;
        }
        if (pr == 0) {
            editor_.clearExpiredActionMessage();
            editor_.tickMouseAutoscroll(std::chrono::steady_clock::now());
            editor_.renderFrame();
            continue;
        }
        bool xReady = (clipboardIdx >= 0 && (pfds[clipboardIdx].revents & POLLIN));
        bool inReady = (pfds[0].revents & POLLIN);
        bool watcherReady = (watcherIdx >= 0 && (pfds[watcherIdx].revents & POLLIN));
        if (xReady) editor_.clipboard_->processEvents();
        if (watcherReady && editor_.watcher_) {
            editor_.watcher_->pollEvents([this](const FileChangeEvent& ev) {
                editor_.handleFileChange(ev);
            });
        }
        if (inReady) {
            Event event;
            if (!terminal.readEvent(event, 0)) {
                if (xReady || watcherReady) continue;
            } else {
                editor_.handleEvent(event);
                if (!editor_.running_) break;
                editor_.clearExpiredActionMessage();
                editor_.renderFrame();
            }
        } else if (xReady || watcherReady) {
            editor_.clearExpiredActionMessage();
        }
    }

    sigprocmask(SIG_SETMASK, &origMask, nullptr);
    terminal.disableMouseTracking();
    terminal.leaveAlternateScreen();
    terminal.disableRawMode();
    write(STDOUT_FILENO, "\x1b[0m\x1b[39m\x1b[49m\x1b[?25h\x1b[2J\x1b[H\x1b[0 q", sizeof("\x1b[0m\x1b[39m\x1b[49m\x1b[?25h\x1b[2J\x1b[H\x1b[0 q") - 1);
}
