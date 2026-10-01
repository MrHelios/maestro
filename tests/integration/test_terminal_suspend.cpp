// Regresión: el handler de SIGTSTP debe sobrevivir al ciclo suspender/reanudar.
//
// Diseño fork+pty: el hijo abre el esclavo como stdin, activa raw mode
// (instala suspendSignalHandler/continueSignalHandler) y se bloquea
// leyendo un pipe. El padre le envía SIGTSTP/SIGCONT con kill() y observa
// el termios del pty:
//
//   parada  -> terminal "cocida" (ECHO|ICANON|ISIG restaurados)
//   resume  -> raw de nuevo
//
// La SEGUNDA parada es la regresión: sin el rearmado en
// continueSignalHandler, el segundo SIGTSTP cae en SIG_DFL y detiene al
// hijo sin restaurar la terminal (quedaría en raw).
//
// El hijo sale con _exit() (nunca exit()): así se congela el estado
// observable y se evita que atexit/dtor restauren la terminal y le ganen
// la carrera a las comprobaciones del padre.
//
// Sin pty disponible (contenedores mínimos) el test se saltea.

#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

#include "test_framework.h"
#include "rendering/Sink.h"
#include "platform/tty/Terminal.h"
#include "platform/tty/TtyRunLoop.h"
#include "rendering/tty/TtySink.h"
#include "app/Editor.h"

namespace {

using Clock = std::chrono::steady_clock;

bool waitReadable(int fd, int timeoutMs) {
    struct pollfd pfd{fd, POLLIN, 0};
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        const int r = ::poll(&pfd, 1, 50);
        if (r > 0) return true;
        if (r < 0 && errno != EINTR) return false;
    }
    return false;
}

bool writeByteRetry(int fd, char b) {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < deadline) {
        const ssize_t n = ::write(fd, &b, 1);
        if (n == 1) return true;
        if (n < 0 && errno != EINTR) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// true si waitpid reporta al hijo detenido antes del timeout.
bool waitStopped(pid_t pid, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        int status = 0;
        const pid_t r = ::waitpid(pid, &status, WUNTRACED | WNOHANG);
        if (r == pid) return WIFSTOPPED(status) != 0;
        if (r < 0) return false;  // salió o error: no hay parada que ver
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// true si waitpid reporta al hijo continuado antes del timeout.
bool waitContinued(pid_t pid, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        int status = 0;
        const pid_t r = ::waitpid(pid, &status, WUNTRACED | WCONTINUED | WNOHANG);
        if (r == pid) return WIFCONTINUED(status) != 0;
        if (r < 0) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// true si el hijo termina (exit) antes del timeout; deja el código en `code`.
bool waitExited(pid_t pid, int* code, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        int status = 0;
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid && WIFEXITED(status)) {
            *code = WEXITSTATUS(status);
            return true;
        }
        if (r == pid || r < 0) return false;  // señal o error
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// true si el termios del master llega al estado esperado antes del timeout:
// expectCooked=true exige ECHO|ICANON|ISIG (restaurado por el handler);
// false exige los tres apagados (raw reaplicado al reanudar).
bool termiosIs(int masterFd, bool expectCooked, int timeoutMs) {    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        struct termios t{};
        if (::tcgetattr(masterFd, &t) == 0) {
            const bool cooked =
                (t.c_lflag & (ECHO | ICANON | ISIG)) == (ECHO | ICANON | ISIG);
            if (cooked == expectCooked) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// Tamaño actual del archivo (para medir cuánto pintó el hijo).
size_t fileSize(int fd) {
    struct stat st{};
    if (::fstat(fd, &st) != 0) return 0;
    return static_cast<size_t>(st.st_size);
}

// true si el archivo crece respecto a `base` antes del timeout; deja el
// tamaño final en `out`.
bool pollFileGrowth(int fd, size_t base, size_t* out, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        const size_t cur = fileSize(fd);
        if (cur > base) {
            *out = cur;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    *out = fileSize(fd);
    return *out > base;
}

// Reaper: si el test falla a mitad de camino, no dejar al hijo colgado.
struct ChildReaper {
    pid_t pid;
    bool active = true;
    explicit ChildReaper(pid_t p) : pid(p) {}
    void dismiss() { active = false; }
    ~ChildReaper() {
        if (!active) return;
        ::kill(pid, SIGKILL);
        int st = 0;
        while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    }
};

// Cuerpo del hijo. No retorna: termina con _exit() para congelar el estado
// observable (exit() correría atexit/dtor y restauraría la terminal).
[[noreturn]] void childMain(const char* slavePath, int readyFd, int exitFd) {
    if (::setsid() < 0) ::_exit(41);
    const int slave = ::open(slavePath, O_RDWR | O_NOCTTY);
    if (slave < 0) ::_exit(42);
    ::dup2(slave, STDIN_FILENO);
    const int devnull = ::open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        ::dup2(devnull, STDOUT_FILENO);
        ::dup2(devnull, STDERR_FILENO);
        ::close(devnull);
    }
    ::close(slave);

    Terminal term;
    term.enableRawMode();
    if (!term.isRawActiveForTest()) ::_exit(43);

    if (!writeByteRetry(readyFd, 'R')) ::_exit(44);
    ::close(readyFd);

    // Bloqueo hasta la orden de salida. TSTP/CONT interrumpen el read
    // (EINTR) y se reintenta: el hijo nunca sale por señales.
    char b = 0;
    for (;;) {
        const ssize_t n = ::read(exitFd, &b, 1);
        if (n == 1) ::_exit(0);
        if (n == 0) ::_exit(45);  // el padre cerró el pipe
        if (errno != EINTR) ::_exit(46);
    }
}

}  // namespace

TEST(terminal_suspend_handler_survives_resume) {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) SKIP("sin pty disponible (posix_openpt)");
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        ::close(master);
        SKIP("sin pty disponible (grantpt/unlockpt)");
    }
    const char* slavePath = ::ptsname(master);
    if (!slavePath) {
        ::close(master);
        SKIP("sin pty disponible (ptsname)");
    }
    const std::string slave = slavePath;

    int readyPipe[2] = {-1, -1}, exitPipe[2] = {-1, -1};
    if (::pipe(readyPipe) != 0 || ::pipe(exitPipe) != 0) {
        ::close(master);
        ::close(readyPipe[0]); ::close(readyPipe[1]);
        ::close(exitPipe[0]); ::close(exitPipe[1]);
        SKIP("sin pipes disponibles");
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(master);
        ::close(readyPipe[0]); ::close(readyPipe[1]);
        ::close(exitPipe[0]); ::close(exitPipe[1]);
        SKIP("fork no disponible");
    }
    if (pid == 0) {
        ::close(master);
        ::close(readyPipe[0]);
        ::close(exitPipe[1]);
        childMain(slave.c_str(), readyPipe[1], exitPipe[0]);
    }
    ::close(readyPipe[1]);
    ::close(exitPipe[0]);
    ChildReaper reaper(pid);

    // 1. El hijo instaló handlers y está bloqueado.
    CHECK(waitReadable(readyPipe[0], 5000));
    char tmp = 0;
    CHECK(::read(readyPipe[0], &tmp, 1) == 1);
    ::close(readyPipe[0]);

    // 2. Primer ciclo: TSTP detiene con la terminal restaurada (cocida).
    CHECK(::kill(pid, SIGTSTP) == 0);
    CHECK(waitStopped(pid, 5000));
    CHECK(termiosIs(master, true, 2000));

    // 3. Reanudar: CONT vuelve a raw.
    CHECK(::kill(pid, SIGCONT) == 0);
    CHECK(waitContinued(pid, 5000));
    CHECK(termiosIs(master, false, 2000));

    // 4. Segundo ciclo (la regresión): TSTP debe detener con la terminal
    //    restaurada otra vez. Sin el rearmado en continueSignalHandler, la
    //    disposición sigue en SIG_DFL y no hay limpieza: según el entorno
    //    el hijo detiene con la terminal en raw (grupo no huérfano) o la
    //    señal se descarta y ni siquiera detiene (grupo huérfano, como el
    //    hijo con setsid de este test). Ambos fallan acá.
    CHECK(::kill(pid, SIGTSTP) == 0);
    CHECK(waitStopped(pid, 5000));
    CHECK(termiosIs(master, true, 2000));

    // 5. Salida limpia del hijo.
    CHECK(::kill(pid, SIGCONT) == 0);
    CHECK(waitContinued(pid, 5000));
    CHECK(writeByteRetry(exitPipe[1], 'X'));
    int code = -1;
    CHECK(waitExited(pid, &code, 5000));
    CHECK_EQ(code, 0);
    reaper.dismiss();

    ::close(master);
    ::close(exitPipe[1]);
}

// Camino completo de Ctrl+Z con el loop real: el byte 0x1A (ISIG está
// apagado en raw, así que NO llega como señal) viaja keymap -> Suspend ->
// petición al Editor -> el loop se auto-envía SIGTSTP. Se observa parada
// con terminal restaurada, `fg` que retoma en raw, y salida limpia con
// Ctrl+K q (el buffer inicial no está modificado, así que sale directo).
// Corre el loop real sobre el esclavo ya duplicado en stdin/stdout.
// No retorna: _exit() congela el estado (exit() correría atexit/dtor).
[[noreturn]] void runLoopOnSlave() {
    TtySink sink;
    Editor editor;
    editor.setSink(sink);
    TtyRunLoop loop(editor);
    loop.run();
    ::_exit(0);
}

[[noreturn]] void loopChildMain(const char* slavePath) {
    if (::setsid() < 0) ::_exit(41);
    const int slave = ::open(slavePath, O_RDWR | O_NOCTTY);
    if (slave < 0) ::_exit(42);
    ::dup2(slave, STDIN_FILENO);
    const int devnull = ::open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        ::dup2(devnull, STDOUT_FILENO);
        ::dup2(devnull, STDERR_FILENO);
        ::close(devnull);
    }
    ::close(slave);

    runLoopOnSlave();
}

// Variante con stdout a un archivo (para medir cuánto repinta): el padre
// lo observa con fstat sin perturbar el offset compartido.
[[noreturn]] void loopChildFileMain(const char* slavePath, int outFd) {
    if (::setsid() < 0) ::_exit(41);
    const int slave = ::open(slavePath, O_RDWR | O_NOCTTY);
    if (slave < 0) ::_exit(42);
    ::dup2(slave, STDIN_FILENO);
    ::dup2(outFd, STDOUT_FILENO);
    ::dup2(outFd, STDERR_FILENO);
    ::close(slave);

    runLoopOnSlave();
}

TEST(terminal_ctrl_z_suspends_loop_and_fg_resumes) {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) SKIP("sin pty disponible (posix_openpt)");
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        ::close(master);
        SKIP("sin pty disponible (grantpt/unlockpt)");
    }
    const char* slavePath = ::ptsname(master);
    if (!slavePath) {
        ::close(master);
        SKIP("sin pty disponible (ptsname)");
    }
    const std::string slave = slavePath;

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(master);
        SKIP("fork no disponible");
    }
    if (pid == 0) loopChildMain(slave.c_str());
    ChildReaper reaper(pid);

    // El loop está corriendo cuando el esclavo entra en raw. El pty
    // bufea la entrada, así que no hay carrera con el arranque del hijo.
    CHECK(termiosIs(master, false, 5000));

    // Ctrl+Z como byte (0x1A): regime raw, sin señal del driver.
    CHECK(writeByteRetry(master, 26));
    CHECK(waitStopped(pid, 5000));
    CHECK(termiosIs(master, true, 2000));

    // fg: retoma en raw y sigue atendiendo.
    CHECK(::kill(pid, SIGCONT) == 0);
    CHECK(waitContinued(pid, 5000));
    CHECK(termiosIs(master, false, 2000));

    // Salida limpia: Ctrl+K q (sin modificados, sale directo).
    CHECK(writeByteRetry(master, 11));
    CHECK(writeByteRetry(master, 'q'));
    int code = -1;
    CHECK(waitExited(pid, &code, 5000));
    CHECK_EQ(code, 0);
    reaper.dismiss();

    ::close(master);
}

// Al reanudar (SIGCONT) el loop debe repintar el frame COMPLETO aunque el
// tamaño no haya cambiado: la re-entrada a alt screen deja un buffer
// limpio y el diff sin invalidar emitiría (casi) vacío. Se mide por
// tamaño: el crecimiento tras el resume debe ser del orden de un frame
// completo (>1000B para 24x80), no el ruido de los handlers (~60B) ni un
// diff vacío. Sin Editor::invalidateScreen() en el camino de resume,
// el crecimiento es ~0 y falla.
TEST(terminal_resume_repaints_full_frame) {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) SKIP("sin pty disponible (posix_openpt)");
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        ::close(master);
        SKIP("sin pty disponible (grantpt/unlockpt)");
    }
    const char* slavePath = ::ptsname(master);
    if (!slavePath) {
        ::close(master);
        SKIP("sin pty disponible (ptsname)");
    }
    const std::string slave = slavePath;

    // Archivo de salida del hijo (unlink inmediato: sin rastro en /tmp).
    // Padre e hijo comparten la descripción abierta: el padre solo hace
    // fstat (no mueve el offset) y el hijo solo agrega al final.
    char tmpl[] = "/tmp/maestro_repaint_XXXXXX";
    const int outFd = ::mkstemp(tmpl);
    if (outFd < 0) {
        ::close(master);
        SKIP("sin archivo temporal");
    }
    ::unlink(tmpl);

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(master);
        ::close(outFd);
        SKIP("fork no disponible");
    }
    if (pid == 0) {
        ::close(master);
        loopChildFileMain(slave.c_str(), outFd);
    }
    ChildReaper reaper(pid);

    // Loop corriendo en raw; el primer frame ya quedó pintado.
    CHECK(termiosIs(master, false, 5000));
    const size_t s0 = fileSize(outFd);
    CHECK(s0 > 1000);

    // Suspender y reanudar.
    CHECK(writeByteRetry(master, 26));  // Ctrl+Z como byte
    CHECK(waitStopped(pid, 5000));
    CHECK(termiosIs(master, true, 2000));
    CHECK(::kill(pid, SIGCONT) == 0);
    CHECK(waitContinued(pid, 5000));
    CHECK(termiosIs(master, false, 2000));

    // El resume repintó el frame completo.
    size_t s1 = s0;
    CHECK(pollFileGrowth(outFd, s0, &s1, 3000));
    CHECK(s1 - s0 > 1000);

    // Salida limpia: Ctrl+K q.
    CHECK(writeByteRetry(master, 11));
    CHECK(writeByteRetry(master, 'q'));
    int code = -1;
    CHECK(waitExited(pid, &code, 5000));
    CHECK_EQ(code, 0);
    reaper.dismiss();

    ::close(master);
    ::close(outFd);
}

namespace {

// Foto del termios para comparar restauraciones byte a byte.
bool snapshotTermios(int fd, struct termios* out) {
    return ::tcgetattr(fd, out) == 0;
}

bool sameTermios(const struct termios& a, const struct termios& b) {
    return a.c_iflag == b.c_iflag && a.c_oflag == b.c_oflag &&
           a.c_cflag == b.c_cflag && a.c_lflag == b.c_lflag &&
           std::memcmp(a.c_cc, b.c_cc, sizeof(a.c_cc)) == 0;
}

}  // namespace

// El resume debe restaurar el termios EXACTO que aplicó enableRawMode,
// no una reconstrucción de la receta (fuente única: rawApplied).
// Reusa el hijo en raw (sin loop): suspende con kill externo, reanuda y
// compara el termios de antes/después campo por campo.
TEST(terminal_resume_restores_identical_raw) {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) SKIP("sin pty disponible (posix_openpt)");
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        ::close(master);
        SKIP("sin pty disponible (grantpt/unlockpt)");
    }
    const char* slavePath = ::ptsname(master);
    if (!slavePath) {
        ::close(master);
        SKIP("sin pty disponible (ptsname)");
    }
    const std::string slave = slavePath;

    int readyPipe[2] = {-1, -1}, exitPipe[2] = {-1, -1};
    if (::pipe(readyPipe) != 0 || ::pipe(exitPipe) != 0) {
        ::close(master);
        ::close(readyPipe[0]); ::close(readyPipe[1]);
        ::close(exitPipe[0]); ::close(exitPipe[1]);
        SKIP("sin pipes disponibles");
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(master);
        ::close(readyPipe[0]); ::close(readyPipe[1]);
        ::close(exitPipe[0]); ::close(exitPipe[1]);
        SKIP("fork no disponible");
    }
    if (pid == 0) {
        ::close(master);
        ::close(readyPipe[0]);
        ::close(exitPipe[1]);
        childMain(slave.c_str(), readyPipe[1], exitPipe[0]);
    }
    ::close(readyPipe[1]);
    ::close(exitPipe[0]);
    ChildReaper reaper(pid);

    CHECK(waitReadable(readyPipe[0], 5000));
    char tmp = 0;
    CHECK(::read(readyPipe[0], &tmp, 1) == 1);
    ::close(readyPipe[0]);

    // Raw aplicado por enableRawMode: foto de referencia.
    CHECK(termiosIs(master, false, 2000));
    struct termios before{};
    CHECK(snapshotTermios(master, &before));

    // Un ciclo completo de suspensión.
    CHECK(::kill(pid, SIGTSTP) == 0);
    CHECK(waitStopped(pid, 5000));
    CHECK(termiosIs(master, true, 2000));
    CHECK(::kill(pid, SIGCONT) == 0);
    CHECK(waitContinued(pid, 5000));
    CHECK(termiosIs(master, false, 2000));

    // El raw reanudado debe ser idéntico al aplicado (no reconstruido).
    struct termios after{};
    CHECK(snapshotTermios(master, &after));
    CHECK(sameTermios(before, after));

    CHECK(writeByteRetry(exitPipe[1], 'X'));
    int code = -1;
    CHECK(waitExited(pid, &code, 5000));
    CHECK_EQ(code, 0);
    reaper.dismiss();

    ::close(master);
    ::close(exitPipe[1]);
}
