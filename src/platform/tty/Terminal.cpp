#include "platform/tty/Terminal.h"

#include "platform/tty/TtyMouse.h"
#include "platform/tty/TtySignalState.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>

// ---------------------------------------------------------------------------
// Restauracion de la terminal ante senales (SIGSEGV, SIGTERM, abort, ...).
//
// Un destructor (RAII) cubre las salidas normales y las excepciones, pero NO
// se ejecuta ante senales fatales. Si el proceso muere por un SIGSEGV o lo
// matan con SIGTERM mientras esta en raw mode, la terminal quedaria sin echo
// y sin modo canonico: rota para el usuario. Para evitarlo se instala, solo
// mientras el raw mode esta activo, un handler minimo que restaura el termios
// original y relanza la senal con su accion por defecto (para conservar el
// codigo de salida y el core dump).
//
// También maneja SIGTSTP/SIGCONT (suspender/reanudar con Ctrl+Z / fg):
// al suspender, restaura terminal y envía SIGSTOP; al reanudar, reactiva
// raw mode, mouse tracking, alternate screen y notifica resize.
//
// Nota: tcsetattr() no es async-signal-safe segun POSIX, pero es la practica
// habitual en editores de terminal (el propio proceso es el unico que usa
// stdin y el riesgo real es despreciable frente a dejar la terminal inutil).
//
// El handler es una funcion libre y no puede capturar `this`. El estado
// mínimo signal-safe vive en Terminal::signalState_ (miembro, ver
// TtySignalState.h) y este único puntero lo publica mientras el raw mode
// está activo. Se asume una UNICA Terminal viva a la vez (el Editor
// tiene una sola). No agregar más globales: todo lo que el handler
// necesite va al TtySignalState miembro.
// ---------------------------------------------------------------------------
namespace {

const int kFatalSignals[] = { SIGINT, SIGTERM, SIGQUIT, SIGHUP,
                              SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };
constexpr int kFatalSignalCount = static_cast<int>(sizeof(kFatalSignals) / sizeof(kFatalSignals[0]));
static_assert(kFatalSignalCount == TtySignalState::kFatalCount,
              "TtySignalState::kFatalCount debe cubrir kFatalSignals");

const int kSuspendSignals[] = { SIGTSTP, SIGCONT };
constexpr int kSuspendSignalCount = static_cast<int>(sizeof(kSuspendSignals) / sizeof(kSuspendSignals[0]));
static_assert(kSuspendSignalCount == TtySignalState::kSuspendCount,
              "TtySignalState::kSuspendCount debe cubrir kSuspendSignals");

// Único hook global: apunta al signalState_ miembro del Terminal vivo.
// Se publica cuando ALGÚN modo está activo (raw/mouse/alt) y se retira
// cuando los tres están apagados. Los flags son independientes (el handler
// debe limpiar mouse/alt aunque el crash ocurra fuera de raw).
TtySignalState* g_activeSignalState = nullptr;

// Sincroniza la publicación con los flags del miembro. Solo el Terminal
// vivo publica; al apagarse el último modo se retira el puntero.
void publishIfActive(TtySignalState* st) {
    if (st->rawActive || st->mouseActive || st->altActive) {
        g_activeSignalState = st;
    } else if (g_activeSignalState == st) {
        g_activeSignalState = nullptr;
    }
}

void sigwinchHandler(int) {
    if (g_activeSignalState) g_activeSignalState->resized = 1;
}

// Restaura la terminal al estado original (mouse off / alt off / cursor
// default / termios orig), espejando los modos activos: mouse tracking y
// alt-screen son independientes de raw y deben limpiarse aunque el evento
// ocurra fuera de raw (p.ej. crash tras enableMouseTracking()).
// FUENTE ÚNICA para los tres caminos que la necesitan (fatal, suspend,
// atexit). Solo usa write() + tcsetattr(), así que es apta para signal
// handlers (la salvedad de tcsetattr ya está asumida en este archivo).
// Acepta nullptr (sin terminal publicada: no-op). Los (void) silencian
// warn_unused_result en toolchains que lo emiten para write().
void restoreTerminalNow(TtySignalState* st) {
    if (!st) return;
    if (st->mouseActive) {
        (void)write(STDOUT_FILENO, "\x1b[?1006l\x1b[?1002l\x1b[?1000l", sizeof("\x1b[?1006l\x1b[?1002l\x1b[?1000l") - 1);
    }
    if (st->altActive) {
        (void)write(STDOUT_FILENO, "\x1b[?1049l", sizeof("\x1b[?1049l") - 1);
    }
    if (st->rawActive) {
        (void)write(STDOUT_FILENO, kCursorDefault, sizeof(kCursorDefault) - 1);
    }
    if (st->rawActive && st->orig) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, st->orig);
    }
}

void fatalSignalHandler(int sig) {
    // Restaurar la terminal antes de morir (ver restoreTerminalNow).
    restoreTerminalNow(g_activeSignalState);
    // Volver a la accion por defecto y relanzar la senal, para morir de
    // verdad con el codigo de salida adecuado. La senal actual esta bloqueda
    // durante este handler, asi que el relanzamiento se entrega al volver.
    signal(sig, SIG_DFL);
    raise(sig);
}

// Handler para SIGTSTP (Ctrl+Z / suspend): restaura terminal y envía SIGSTOP.
void suspendSignalHandler(int) {
    restoreTerminalNow(g_activeSignalState);
    // Reenviar SIGSTOP para suspender de verdad.
    signal(SIGTSTP, SIG_DFL);
    raise(SIGSTOP);
}

// Handler para SIGCONT (fg / reanudar): reactiva modos y notifica resize.
void continueSignalHandler(int) {
    // El handler de SIGTSTP se resetea a SIG_DFL en suspendSignalHandler
    // (necesario para suspender de verdad con raise). Reinstalarlo acá al
    // reanudar: sin esto, el segundo Ctrl+Z externo caería en la acción
    // por defecto y suspendería sin restaurar la terminal. sigaction() es
    // async-signal-safe, igual que en installSuspendSignalHandlers.
    // No toca st->suspendActions[0].old: ese guarda la disposición previa
    // del proceso, que restoreSuspendSignalHandlers restaura al salir.
    {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = suspendSignalHandler;
        sigaction(SIGTSTP, &sa, nullptr);
    }
    TtySignalState* st = g_activeSignalState;
    if (st) {
        if (st->rawActive) {
            if (st->hasRawApplied) {
                // Reaplicar el raw YA APLICADO (guardado en enableRawMode),
                // no reconstruir la receta: fuente única.
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &st->rawApplied);
            }
            write(STDOUT_FILENO, kCursorBlock, sizeof(kCursorBlock) - 1);
        }
        if (st->mouseActive) {
            write(STDOUT_FILENO, "\x1b[?1000h\x1b[?1002h\x1b[?1006h", sizeof("\x1b[?1000h\x1b[?1002h\x1b[?1006h") - 1);
        }
        if (st->altActive) {
            write(STDOUT_FILENO, "\x1b[?1049h", sizeof("\x1b[?1049h") - 1);
        }
        // Notificar resize para que el loop principal lo detecte, y marcar
        // re-entrada a alt screen: la pantalla física se perdió aunque el
        // tamaño sea el mismo (ver altReentered en TtySignalState.h).
        st->resized = 1;
        st->altReentered = 1;
    }
}

// Captura las senales fatales. Guarda como estaban antes, para restaurarlas
// luego (no borrar un handler previo del proceso).
void installFatalSignalHandlers(TtySignalState* st) {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fatalSignalHandler;
    sigemptyset(&sa.sa_mask);
    for (int i = 0; i < kFatalSignalCount; ++i) {
        if (sigaction(kFatalSignals[i], &sa, &st->savedActions[i].old) == 0) {
            st->savedActions[i].sig = kFatalSignals[i];
        }
    }
}

// Restaura los handlers previos (llamada al apagar el raw mode).
void restoreFatalSignalHandlers(TtySignalState* st) {
    for (int i = 0; i < kFatalSignalCount; ++i) {
        if (st->savedActions[i].sig != 0) {
            sigaction(st->savedActions[i].sig, &st->savedActions[i].old, nullptr);
            st->savedActions[i].sig = 0;
        }
    }
}

// Instala handlers para suspend/reanudar (SIGTSTP/SIGCONT).
void installSuspendSignalHandlers(TtySignalState* st) {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sa.sa_handler = suspendSignalHandler;
    sigaction(SIGTSTP, &sa, &st->suspendActions[0].old);
    st->suspendActions[0].sig = SIGTSTP;

    sa.sa_handler = continueSignalHandler;
    sigaction(SIGCONT, &sa, &st->suspendActions[1].old);
    st->suspendActions[1].sig = SIGCONT;
}

// Restaura handlers de suspend/reanudar.
void restoreSuspendSignalHandlers(TtySignalState* st) {
    for (int i = 0; i < kSuspendSignalCount; ++i) {
        if (st->suspendActions[i].sig != 0) {
            sigaction(st->suspendActions[i].sig, &st->suspendActions[i].old, nullptr);
            st->suspendActions[i].sig = 0;
        }
    }
}

// atexit: red de seguridad para salida por exit() SIN pasar por el
// teardown normal (ni epílogo de TtyRunLoop ni ~Terminal).
//
// Estado verificado (no quitar por "parecer muerta"):
// - Salida normal y por excepción: es un no-op probado. El epílogo del
//   loop + ~Terminal ya apagaron los modos y retiraron
//   g_activeSignalState, así que restoreTerminalNow(nullptr) no hace nada.
// - _exit(): la salta por completo.
// - Ningún código del repo llama a exit()/abort().
// - ÚNICO camino vivo: exit() desde FUERA del repo, en particular el
//   handler de error de I/O de Xlib (XCloseDisplay/conexión perdida),
//   que por defecto termina con exit(1). Sin este atexit, ese caso deja
//   la terminal en raw. Por eso se conserva aunque hoy casi nunca actúe.
void atexitRestoreTerminal() {
    restoreTerminalNow(g_activeSignalState);
}

} // namespace

Terminal::Terminal() : signalState_(std::make_unique<TtySignalState>()) {
    origTermios_ = new termios();
    debugKeys_ = std::getenv("EDIT_DEBUG_KEYS") != nullptr;
}

Terminal::~Terminal() {
    if (mouseTrackingEnabled_) disableMouseTracking();
    if (altScreenActive_) leaveAlternateScreen();
    if (rawModeEnabled_) {
        disableRawMode();
    }
    delete static_cast<termios*>(origTermios_);
}

void Terminal::enableRawMode() {
    if (rawModeEnabled_) return;
    termios* orig = static_cast<termios*>(origTermios_);

    // Leer el estado actual. Falla con ENOTTY si stdin no es un TTY, o si
    // ocurre cualquier otro error: en ese caso NO debe dejarse el modo raw
    // "activo" sobre un estado que nunca se leyo.
    if (tcgetattr(STDIN_FILENO, orig) == -1) {
        rawModeEnabled_ = false;
        return;
    }

    termios raw = *orig;
    // Sin eco, sin modo canonico (linea por linea), sin señales
    // generadas por Ctrl+C/Ctrl+Z, sin procesamiento especial de \r.
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 1;  // read() devuelve apenas haya 1 byte
    raw.c_cc[VTIME] = 0;

    // Aplicar el raw mode. Si falla, no marcarlo como activo.
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        rawModeEnabled_ = false;
        return;
    }
    rawModeEnabled_ = true;

    // Cursor en bloque fijo (DECSCUSR). Se emite solo tras aplicar el raw
    // mode con exito: si tcsetattr fallo no estamos en raw mode y no tiene
    // sentido cambiar la forma del cursor.
    write(STDOUT_FILENO, kCursorBlock, sizeof(kCursorBlock) - 1);

    // Raw mode activo: publicar el signalState_ miembro y luego instalar
    // los handlers. enable/disable deben llamarse en pares estrictos; guard
    // contra reentrancia: si ya esta en raw, no pisar oldWinchAction
    // (perderia el original del sistema y disable restauraria el propio
    // handler). Mismo patron que kFatalSignals — ver winchInstalled.
    signalState_->orig = orig;
    signalState_->rawApplied = raw;
    signalState_->hasRawApplied = 1;
    signalState_->rawActive = 1;
    publishIfActive(signalState_.get());
    if (!signalState_->winchInstalled) {
        installFatalSignalHandlers(signalState_.get());
        installSuspendSignalHandlers(signalState_.get());
        // Registrar atexit una sola vez (idempotente por flag). Ver el
        // comentario en atexitRestoreTerminal: en el camino normal es un
        // no-op (el teardown ya limpió); cubre exit() externo (Xlib).
        // Nota de threads: el check-set no es atómico, pero todo este
        // componente es single-thread por contrato (una sola Terminal
        // viva, pares enable/disable estrictos). No poner call_once acá:
        // el día que haya multithreading hay que rediseñar la capa de
        // señales entera, no este flag.
        static bool atexitRegistered = false;
        if (!atexitRegistered) {
            std::atexit(atexitRestoreTerminal);
            atexitRegistered = true;
        }
    }

    if (!signalState_->winchInstalled) {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = sigwinchHandler;
        sigemptyset(&sa.sa_mask);
        // INTENCIONAL: sa_flags = 0 sin SA_RESTART. ppoll()/poll() debe
        // interrumpirse con EINTR ante SIGWINCH para que el bucle principal
        // detecte el resize via hasResized() sin latencia. No agregar
        // SA_RESTART aqui: haria que poll se reinicie automaticamente y el
        // resize quedaria bloqueado hasta la proxima tecla (ventana de carrera
        // con waitMs=-1). Ver TtyRunLoop::run() ppoll(..., &origMask).
        sa.sa_flags = 0;
        sigaction(SIGWINCH, &sa, &signalState_->oldWinchAction);
        signalState_->winchInstalled = true;
    }
}

void Terminal::disableRawMode() {
    if (!rawModeEnabled_) return;
    if (signalState_->winchInstalled) {
        sigaction(SIGWINCH, &signalState_->oldWinchAction, nullptr);
        signalState_->winchInstalled = false;
    }

    // Apagar los handlers ANTES de restaurar: una senal que caiga sobre una
    // terminal que ya no esta en raw mode no debe intentar restaurarla.
    restoreFatalSignalHandlers(signalState_.get());
    restoreSuspendSignalHandlers(signalState_.get());

    // Restaurar la forma por defecto del cursor antes de devolver la
    // terminal al shell (el raw mode la deja en bloque fijo).
    write(STDOUT_FILENO, kCursorDefault, sizeof(kCursorDefault) - 1);

    termios* orig = static_cast<termios*>(origTermios_);
    // Restaurar el estado original. Aunque falle (poco probable), dejamos de
    // considerarnos en raw mode: no hay nada mas que hacer aqui.
    tcsetattr(STDIN_FILENO, TCSAFLUSH, orig);
    rawModeEnabled_ = false;
    signalState_->rawActive = 0;
    signalState_->hasRawApplied = 0;
    signalState_->orig = nullptr;
    publishIfActive(signalState_.get());
}

void Terminal::enableMouseTracking() {
    // El flag se publica despues del write(); existe una ventana minima
    // en la que el signal handler aun no conoce este estado.
    if (mouseTrackingEnabled_) return;
    write(STDOUT_FILENO, "\x1b[?1000h\x1b[?1002h\x1b[?1006h", sizeof("\x1b[?1000h\x1b[?1002h\x1b[?1006h") - 1);
    mouseTrackingEnabled_ = true;
    signalState_->mouseActive = 1;
    publishIfActive(signalState_.get());
}

void Terminal::disableMouseTracking() {
    if (!mouseTrackingEnabled_) return;
    write(STDOUT_FILENO, "\x1b[?1006l\x1b[?1002l\x1b[?1000l", sizeof("\x1b[?1006l\x1b[?1002l\x1b[?1000l") - 1);
    mouseTrackingEnabled_ = false;
    signalState_->mouseActive = 0;
    publishIfActive(signalState_.get());
}

void Terminal::enterAlternateScreen() {
    // El flag se publica despues del write(); existe una ventana minima
    // en la que el signal handler aun no conoce este estado.
    if (altScreenActive_) return;
    write(STDOUT_FILENO, "\x1b[?1049h", sizeof("\x1b[?1049h") - 1);
    altScreenActive_ = true;
    signalState_->altActive = 1;
    publishIfActive(signalState_.get());
}

void Terminal::leaveAlternateScreen() {
    if (!altScreenActive_) return;
    write(STDOUT_FILENO, "\x1b[?1049l", sizeof("\x1b[?1049l") - 1);
    altScreenActive_ = false;
    signalState_->altActive = 0;
    publishIfActive(signalState_.get());
}

bool Terminal::isMouseActiveForTest() const { return signalState_->mouseActive != 0; }
// NOTA: leen el miembro propio publicado al handler, no g_activeSignalState.
// El test interroga al objeto que mutó (ver Terminal.h).
bool Terminal::isAltActiveForTest() const { return signalState_->altActive != 0; }
bool Terminal::isRawActiveForTest() const { return signalState_->rawActive != 0; }

bool Terminal::hasResized() {
    if (signalState_->resized) {
        signalState_->resized = 0;
        return true;
    }
    return false;
}

bool Terminal::hasResumed() {
    if (signalState_->altReentered) {
        signalState_->altReentered = 0;
        return true;
    }
    return false;
}

void Terminal::getWindowSize(int& rows, int& cols) {
    int pixelW = 0, pixelH = 0;
    getWindowSize(rows, cols, pixelW, pixelH);
}

void Terminal::getWindowSize(int& rows, int& cols, int& pixelW, int& pixelH) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        rows = 24;
        cols = 80;
        pixelW = 0;
        pixelH = 0;
        return;
    }
    rows = ws.ws_row;
    cols = ws.ws_col;
    pixelW = static_cast<int>(ws.ws_xpixel);
    pixelH = static_cast<int>(ws.ws_ypixel);
}

static char readRawByte() {
    char c = 0;
    // read() bloqueante de un byte. Con VMIN=1/VTIME=0 esto espera
    // hasta que llegue exactamente un byte.
    while (read(STDIN_FILENO, &c, 1) != 1) {
        // reintentar en caso de EINTR u otras interrupciones
    }
    return c;
}

// Ventana para distinguir un ESC suelto de una secuencia de escape.
// Las secuencias de control llegan con todos sus bytes juntos; si tras
// el ESC no llega el siguiente byte en esta ventana, era un ESC solo
// (p.ej. cancelar seleccion).
//
// - TUNING SSH (trabajo futuro): 50ms es instantaneo en loopback local,
//   pero en conexiones lentas con jitter (SSH) el segundo byte de una
//   flecha puede tardar mas y el editor lo leeria como ESC suelto y el
//   resto como None. Si algun dia se reporta "a veces se cancela la
//   seleccion sola por SSH", subir este valor (Vim usa 100ms por defecto).
// - DOBLE ESC RAPIDO (caso raro, consciente): un segundo 27 presionado
//   dentro de esta ventana se lee como "siguiente byte" de la primera
//   secuencia y se descarta como invalida, sin emitir su propio Escape.
//   Si en el futuro se quiere "doble ESC = comando especial", habra que
//   manejar explicitamente ese patron aqui.
static const int kEscapeSequenceTimeoutMs = 50;

// Lee el siguiente byte de stdin con un timeout corto. Devuelve false
// si no llega nada en `timeoutMs` (o si el read() falla), true si se
// leyo. Sin esto, un read() bloqueante (VMIN=1/VTIME=0) esperaria el
// siguiente byte indefinidamente y colgaria el editor.
static bool readByteWithTimeout(char* out, int timeoutMs) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    int pr;
    do {
        pr = poll(&pfd, 1, timeoutMs);
    } while (pr < 0 && errno == EINTR);
    if (pr <= 0) return false;

    ssize_t r;
    do {
        r = read(STDIN_FILENO, out, 1);
    } while (r < 0 && errno == EINTR);
    return r == 1;
}

// Forma "simple" de una secuencia de escape: sin los parametros de
// modificador. Desde v0.5 los modificadores (Shift/Ctrl/Alt) NO tienen
// significado: la seleccion se activa con la letra 's', no con Shift.
// Asi "[1;2A" (flecha arriba con modificador) se reduce a "[A" y se
// resuelve con el mismo enlace. Las secuencias de tecla con '~'
// ("[3~" Delete, "[5~" RePag, ...) NO se tocan: ahi el parametro es la
// propia tecla, no un modificador.
static std::string simpleEscapeForm(const std::string& contents) {
    if (contents.size() < 3) return contents;
    const char prefix = contents[0];
    const char final = contents[contents.size() - 1];
    if (final == '~') return contents; // el parametro es la tecla
    return std::string(1, prefix) + final; // "[1;2A" -> "[A"
}

bool Terminal::parseMouseSgr(std::string_view seq, InputEvent& e) {
    // Frontier (11): el parseo vive en TtyMouse (produce CellPos);
    // acá solo se delega para no duplicar la tabla Cb.
    CellPos cell;
    return decodeMouseSgr(seq, e, cell);
}

InputEvent Terminal::readEvent() {
    InputEvent e;
    readEvent(e, -1); // -1: bloquea indefinidamente
    return e;
}

bool Terminal::readEvent(InputEvent& e, int timeoutMs) {
    char c = 0;
    // Espera el primer byte con el timeout pedido. Si no llega nada en
    // `timeoutMs` (poll devuelve 0), no hay tecla que traducir.
    if (!readByteWithTimeout(&c, timeoutMs)) return false;

    std::string raw; // bytes leidos de esta tecla (para el debug)
    raw.push_back(c);

    auto dumpUnrecognized = [&]() {
        if (!debugKeys_) return;
        std::fprintf(stderr, "[keys] sin reconocer:");
        for (unsigned char b : raw) {
            std::fprintf(stderr, " %02X", b);
        }
        std::fprintf(stderr, " (%s)\n", raw.c_str());
    };

    // Teclas de control de UN byte (Ctrl+Q, Ctrl+S, Ctrl+K, Ctrl+U,
    // Ctrl+Y, Ctrl+Z, Backspace, Enter). El significado vive en el TtyKeymap
    // (remapeable); aqui solo se hace la busqueda.
    if (auto type = keymap_.control(static_cast<unsigned char>(c)); type) {
        e.type = *type;
        return true;
    }

    // Secuencias de escape: flechas, Home, End, Delete, RePag, AvPag.
    //
    // Leemos los parametros (numeros y ';') hasta el caracter final,
    // esperando cada byte con un timeout corto. Si no llega nada tras el
    // ESC, era un ESC suelto (InputEventType::Escape); si una secuencia queda
    // a medias, se descarta sin colgar el editor.
    if (c == 27) { // ESC
        std::string contents;
        while (true) {
            char b = 0;
            if (!readByteWithTimeout(&b, kEscapeSequenceTimeoutMs)) {
                if (contents.empty()) {
                    e.type = InputEventType::Escape; // ESC sin nada mas
                    return true;
                }
                e.type = InputEventType::None; dumpUnrecognized(); return true;
            }
            contents.push_back(b);
            raw.push_back(b);
            // El caracter final es cualquier cosa distinta de digitos, ';', '[' y '<' (SGR mouse: ESC[<Cb;Cx;CyM).
            if (b != '[' && b != '<' && !(b >= '0' && b <= '9') && b != ';') break;
        }

        // Secuencia de mouse SGR: "[<Cb;Cx;CyM" o "...m" (release).
        if (contents.size() >= 3 && contents[0] == '[' && contents[1] == '<') {
            return parseMouseSgr(contents, e);
        }

        // Solo el prefijo [ y O preceden a los parametros. Las demas
        // secuencias (p.ej. ESC ~) no nos interesan.
        const char prefix = contents.empty() ? 0 : contents[0];
        if (prefix != '[' && prefix != 'O') {
            e.type = InputEventType::None; dumpUnrecognized(); return true;
        }

        // Busqueda en el TtyKeymap: primero tal cual la emitio la terminal
        // ("[1;2A"), y si no esta, en su forma simple sin modificadores
        // ("[A"). El significado de cada secuencia es remapeable.
        auto type = keymap_.sequence(contents);
        if (!type) type = keymap_.sequence(simpleEscapeForm(contents));
        if (type) {
            e.type = *type;
            return true;
        }
        e.type = InputEventType::None; dumpUnrecognized(); return true;
    }

    // Caracter imprimible normal.
    if (c >= 32 && c < 127) {
        e.type = InputEventType::InsertChar;
        e.text = std::string(1, c);
        return true;
    }

    // Caracter UTF-8 multibyte. El primer byte es el byte de inicio y
    // define el largo total (2-4 bytes). Sin esto, "á" y "ñ" llegaban
    // byte por byte (cada byte >= 0x80) y se descartaban como None.
    {
        unsigned char uc = static_cast<unsigned char>(c);
        int len = 0;
        if ((uc & 0xE0) == 0xC0) len = 2;      // 110xxxxx -> 2 bytes
        else if ((uc & 0xF0) == 0xE0) len = 3; // 1110xxxx -> 3 bytes
        else if ((uc & 0xF8) == 0xF0) len = 4; // 11110xxx -> 4 bytes
        else {
            // Byte de continuacion suelto o lead invalido: no es imprimible.
            e.type = InputEventType::None;
            return true;
        }

        std::string bytes;
        bytes.push_back(c);
        raw.push_back(c);
        for (int i = 1; i < len; ++i) {
            char b = readRawByte(); // VMIN=1: llega junto al lead en una tecla
            raw.push_back(b);
            // Todo byte salvo el lead debe ser de continuacion (10xxxxxx).
            if ((static_cast<unsigned char>(b) & 0xC0) != 0x80) {
                e.type = InputEventType::None; dumpUnrecognized(); return true;
            }
            bytes.push_back(b);
        }

        e.type = InputEventType::InsertChar;
        e.text = bytes;
        return true;
    }
}
