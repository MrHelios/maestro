#include "Terminal.h"

#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

Terminal::Terminal() {
    origTermios_ = new termios();
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

Event Terminal::readEvent() {
    char c = readRawByte();

    Event e;

    // Teclas de control basicas
    if (c == 17) { // Ctrl+Q -> salir
        e.type = EventType::Quit;
        return e;
    }
    if (c == 19) { // Ctrl+S -> guardar
        e.type = EventType::Save;
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
    if (c == 27) { // ESC
        char seq[3] = {0, 0, 0};
        if (read(STDIN_FILENO, &seq[0], 1) != 1) { e.type = EventType::None; return e; }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) { e.type = EventType::None; return e; }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                // secuencias tipo ESC [ 3 ~  (Delete)
                if (read(STDIN_FILENO, &seq[2], 1) != 1) { e.type = EventType::None; return e; }
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '3': e.type = EventType::Delete; return e;
                        case '1': case '7': e.type = EventType::MoveHome; return e;
                        case '4': case '8': e.type = EventType::MoveEnd; return e;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': e.type = EventType::MoveUp; return e;
                    case 'B': e.type = EventType::MoveDown; return e;
                    case 'C': e.type = EventType::MoveRight; return e;
                    case 'D': e.type = EventType::MoveLeft; return e;
                    case 'H': e.type = EventType::MoveHome; return e;
                    case 'F': e.type = EventType::MoveEnd; return e;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': e.type = EventType::MoveHome; return e;
                case 'F': e.type = EventType::MoveEnd; return e;
            }
        }

        e.type = EventType::None;
        return e;
    }

    // Caracter imprimible normal.
    if (c >= 32 && c < 127) {
        e.type = EventType::InsertChar;
        e.ch = c;
        return e;
    }

    e.type = EventType::None;
    return e;
}
