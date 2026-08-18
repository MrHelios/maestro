#include "terminal/Terminal.h"

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
// Nota: tcsetattr() no es async-signal-safe segun POSIX, pero es la practica
// habitual en editores de terminal (el propio proceso es el unico que usa
// stdin y el riesgo real es despreciable frente a dejar la terminal inutil).
//
// El handler es una funcion libre y no puede tocar el objeto Terminal, asi
// que el estado minimo (el termios original y si el raw mode esta activo) se
// guarda en globales. Se asume una UNICA Terminal viva a la vez (el Editor
// tiene una sola).
// ---------------------------------------------------------------------------
namespace {

const int kFatalSignals[] = { SIGINT, SIGTERM, SIGQUIT, SIGHUP,
                              SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };
constexpr int kFatalSignalCount = static_cast<int>(sizeof(kFatalSignals) / sizeof(kFatalSignals[0]));

termios* g_origTermios = nullptr;
volatile sig_atomic_t g_rawActive = 0;

struct SavedAction {
    int sig = 0;
    struct sigaction old;
};
SavedAction g_savedActions[kFatalSignalCount];

void fatalSignalHandler(int sig) {
    // Restaurar la terminal antes de morir. write() a un fd conocido es
    // razonablemente seguro aqui (mismo criterio que tcsetattr en este
    // handler): devolver el cursor a su forma por defecto, ya que el raw
    // mode deja la terminal en cursor de bloque fijo.
    if (g_rawActive) {
        write(STDOUT_FILENO, "\x1b[0 q", 5);
    }
    if (g_rawActive && g_origTermios) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, g_origTermios);
    }
    // Volver a la accion por defecto y relanzar la senal, para morir de
    // verdad con el codigo de salida adecuado. La senal actual esta bloqueda
    // durante este handler, asi que el relanzamiento se entrega al volver.
    signal(sig, SIG_DFL);
    raise(sig);
}

// Captura las senales fatales. Guarda como estaban antes, para restaurarlas
// luego (no borrar un handler previo del proceso).
void installFatalSignalHandlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fatalSignalHandler;
    sigemptyset(&sa.sa_mask);
    for (int i = 0; i < kFatalSignalCount; ++i) {
        if (sigaction(kFatalSignals[i], &sa, &g_savedActions[i].old) == 0) {
            g_savedActions[i].sig = kFatalSignals[i];
        }
    }
}

// Restaura los handlers previos (llamada al apagar el raw mode).
void restoreFatalSignalHandlers() {
    for (int i = 0; i < kFatalSignalCount; ++i) {
        if (g_savedActions[i].sig != 0) {
            sigaction(g_savedActions[i].sig, &g_savedActions[i].old, nullptr);
            g_savedActions[i].sig = 0;
        }
    }
}

} // namespace

Terminal::Terminal() {
    origTermios_ = new termios();
    debugKeys_ = std::getenv("EDIT_DEBUG_KEYS") != nullptr;
}

Terminal::~Terminal() {
    if (rawModeEnabled_) {
        disableRawMode();
    }
    delete static_cast<termios*>(origTermios_);
}

void Terminal::enableRawMode() {
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
    write(STDOUT_FILENO, "\x1b[2 q", 5);

    // Raw mode activo: instalar el handler de restauracion de senales.
    g_origTermios = orig;
    g_rawActive = 1;
    installFatalSignalHandlers();
}

void Terminal::disableRawMode() {
    // Apagar los handlers ANTES de restaurar: una senal que caiga sobre una
    // terminal que ya no esta en raw mode no debe intentar restaurarla.
    restoreFatalSignalHandlers();

    // Restaurar la forma por defecto del cursor antes de devolver la
    // terminal al shell (el raw mode la deja en bloque fijo).
    write(STDOUT_FILENO, "\x1b[0 q", 5);

    termios* orig = static_cast<termios*>(origTermios_);
    // Restaurar el estado original. Aunque falle (poco probable), dejamos de
    // considerarnos en raw mode: no hay nada mas que hacer aqui.
    tcsetattr(STDIN_FILENO, TCSAFLUSH, orig);
    rawModeEnabled_ = false;
    g_rawActive = 0;
    g_origTermios = nullptr;
}

void Terminal::getWindowSize(int& rows, int& cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        rows = 24;
        cols = 80;
        return;
    }
    rows = ws.ws_row;
    cols = ws.ws_col;
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

Event Terminal::readEvent() {
    Event e;
    readEvent(e, -1); // -1: bloquea indefinidamente
    return e;
}

bool Terminal::readEvent(Event& e, int timeoutMs) {
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
    // Ctrl+Y, Backspace, Enter). El significado vive en el Keymap
    // (remapeable); aqui solo se hace la busqueda.
    if (auto type = keymap_.control(static_cast<unsigned char>(c)); type) {
        e.type = *type;
        return true;
    }

    // Secuencias de escape: flechas, Home, End, Delete, RePag, AvPag.
    //
    // Leemos los parametros (numeros y ';') hasta el caracter final,
    // esperando cada byte con un timeout corto. Si no llega nada tras el
    // ESC, era un ESC suelto (EventType::Escape); si una secuencia queda
    // a medias, se descarta sin colgar el editor.
    if (c == 27) { // ESC
        std::string contents;
        while (true) {
            char b = 0;
            if (!readByteWithTimeout(&b, kEscapeSequenceTimeoutMs)) {
                if (contents.empty()) {
                    e.type = EventType::Escape; // ESC sin nada mas
                    return true;
                }
                e.type = EventType::None; dumpUnrecognized(); return true;
            }
            contents.push_back(b);
            raw.push_back(b);
            // El caracter final es cualquier cosa distinta de digitos, ';' y ESC.
            if (b != '[' && !(b >= '0' && b <= '9') && b != ';') break;
        }

        // Solo el prefijo [ y O preceden a los parametros. Las demas
        // secuencias (p.ej. ESC ~) no nos interesan.
        const char prefix = contents.empty() ? 0 : contents[0];
        if (prefix != '[' && prefix != 'O') {
            e.type = EventType::None; dumpUnrecognized(); return true;
        }

        // Busqueda en el Keymap: primero tal cual la emitio la terminal
        // ("[1;2A"), y si no esta, en su forma simple sin modificadores
        // ("[A"). El significado de cada secuencia es remapeable.
        auto type = keymap_.sequence(contents);
        if (!type) type = keymap_.sequence(simpleEscapeForm(contents));
        if (type) {
            e.type = *type;
            return true;
        }
        e.type = EventType::None; dumpUnrecognized(); return true;
    }

    // Caracter imprimible normal.
    if (c >= 32 && c < 127) {
        e.type = EventType::InsertChar;
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
            e.type = EventType::None;
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
                e.type = EventType::None; dumpUnrecognized(); return true;
            }
            bytes.push_back(b);
        }

        e.type = EventType::InsertChar;
        e.text = bytes;
        return true;
    }
}
