#include "Terminal.h"

#include <cstdlib>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>

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
    tcgetattr(STDIN_FILENO, orig);

    termios raw = *orig;
    // Sin eco, sin modo canonico (linea por linea), sin señales
    // generadas por Ctrl+C/Ctrl+Z, sin procesamiento especial de \r.
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 1;  // read() devuelve apenas haya 1 byte
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    rawModeEnabled_ = true;
}

void Terminal::disableRawMode() {
    termios* orig = static_cast<termios*>(origTermios_);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, orig);
    rawModeEnabled_ = false;
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

Event Terminal::readEvent() {
    char c = readRawByte();

    Event e;
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

    // Teclas de control basicas. Todas son bytes UNICOS (no secuencias
    // de escape), asi que funcionan igual en cualquier emulador.
    if (c == 17) { // Ctrl+Q -> salir
        e.type = EventType::Quit;
        return e;
    }
    if (c == 19) { // Ctrl+S -> entrar al modo seleccion
        e.type = EventType::Select;
        return e;
    }
    if (c == 11) { // Ctrl+K -> prefijo de comando (Ctrl+S guarda, Ctrl+Q sale)
        e.type = EventType::Prefix;
        return e;
    }
    if (c == 21) { // Ctrl+U -> deshacer
        e.type = EventType::Undo;
        return e;
    }
    if (c == 25) { // Ctrl+Y -> rehacer
        e.type = EventType::Redo;
        return e;
    }
    if (c == 127 || c == 8) { // Backspace (DEL o BS segun terminal)
        e.type = EventType::Backspace;
        return e;
    }
    if (c == 13 || c == 10) {
        // Enter: parte la linea actual en dos.
        e.type = EventType::InsertNewline;
        return e;
    }

    // Secuencias de escape: flechas, Home, End, Delete.
    //
    // DESDE v0.3, la seleccion ya NO depende de estas secuencias: el modo
    // seleccion se activa con Ctrl+S (un byte unico y fiable), no con
    // Shift+Flecha. Los modificadores (Shift/Ctrl/Alt) que alguna terminal
    // pueda anadir en el formato "ESC [ 1;2X" se ignoran: solo nos
    // interesa el caracter final para saber que flecha/Home/End es.
    if (c == 27) { // ESC
        // Leemos los parametros (numeros y ';') hasta el caracter final,
        // esperando cada byte con un timeout corto. Si no llega nada
        // tras el ESC, era un ESC suelto (EventType::Escape); si una
        // secuencia queda a medias, se descarta sin colgar el editor.
        std::string params;
        while (true) {
            char b = 0;
            if (!readByteWithTimeout(&b, kEscapeSequenceTimeoutMs)) {
                if (params.empty()) {
                    e.type = EventType::Escape; // ESC sin nada mas
                    return e;
                }
                e.type = EventType::None; dumpUnrecognized(); return e;
            }
            params.push_back(b);
            raw.push_back(b);
            // El caracter final es cualquier cosa distinta de digitos, ';' y ESC.
            if (b != '[' && !(b >= '0' && b <= '9') && b != ';') break;
        }
        const char prefix = params.empty() ? 0 : params[0];
        const char final = params[params.size() - 1];

        // Solo el prefijo [ y O preceden a los parametros. Las demas
        // secuencias (p.ej. ESC ~) no nos interesan.
        if (prefix != '[' && prefix != 'O') {
            e.type = EventType::None; dumpUnrecognized(); return e;
        }

        // Creamos "cuerpo" = params sin el prefijo ni el caracter final.
        std::string body = params.substr(1, params.size() - 2);

        // Sin parametros: flecha/Home/End simples (ESC [ A, ESC [ H, ...).
        if (body.empty()) {
            switch (final) {
                case 'A': e.type = EventType::MoveUp; return e;
                case 'B': e.type = EventType::MoveDown; return e;
                case 'C': e.type = EventType::MoveRight; return e;
                case 'D': e.type = EventType::MoveLeft; return e;
                case 'H': e.type = EventType::MoveHome; return e;
                case 'F': e.type = EventType::MoveEnd; return e;
                default: e.type = EventType::None; dumpUnrecognized(); return e;
            }
        }

        // Secuencias con parametros: p. ej. "3~" (Delete) o "1;2A"
        // (flecha con modificador). Los modificadores se ignoran: desde
        // v0.3 la seleccion se activa con Ctrl+S, no con Shift.
        if (prefix != '[') {
            e.type = EventType::None; dumpUnrecognized(); return e;
        }

        if (final == '~') {
            switch (body[0]) {
                case '3': e.type = EventType::Delete; return e; // Delete
                case '1': case '7': e.type = EventType::MoveHome; return e; // Home
                case '4': case '8': e.type = EventType::MoveEnd; return e; // End
                default: e.type = EventType::None; dumpUnrecognized(); return e;
            }
        }

        // Flecha/Home/End con parametros: nos da igual el modificador.
        switch (final) {
            case 'A': e.type = EventType::MoveUp; return e;
            case 'B': e.type = EventType::MoveDown; return e;
            case 'C': e.type = EventType::MoveRight; return e;
            case 'D': e.type = EventType::MoveLeft; return e;
            case 'H': e.type = EventType::MoveHome; return e;
            case 'F': e.type = EventType::MoveEnd; return e;
            default: e.type = EventType::None; dumpUnrecognized(); return e;
        }
    }

    // Caracter imprimible normal.
    if (c >= 32 && c < 127) {
        e.type = EventType::InsertChar;
        e.text = std::string(1, c);
        return e;
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
            return e;
        }

        std::string bytes;
        bytes.push_back(c);
        raw.push_back(c);
        for (int i = 1; i < len; ++i) {
            char b = readRawByte(); // VMIN=1: llega junto al lead en una tecla
            raw.push_back(b);
            // Todo byte salvo el lead debe ser de continuacion (10xxxxxx).
            if ((static_cast<unsigned char>(b) & 0xC0) != 0x80) {
                e.type = EventType::None; dumpUnrecognized(); return e;
            }
            bytes.push_back(b);
        }

        e.type = EventType::InsertChar;
        e.text = bytes;
        return e;
    }
}
