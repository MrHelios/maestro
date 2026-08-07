#pragma once

#include "Event.h"

// Encapsula todo lo especifico de la terminal (POSIX/Linux/macOS):
// activar/desactivar el modo "raw", leer teclas crudas, y consultar
// el tamano de la ventana. Nada de esto sabe de Document/Cursor/etc.
class Terminal {
public:
    Terminal();
    ~Terminal();

    // Pone la terminal en modo raw: sin buffer de linea, sin eco,
    // teclas especiales (Ctrl+C, Ctrl+Z, etc) entregadas tal cual.
    void enableRawMode();

    // Restaura la configuracion original de la terminal.
    void disableRawMode();

    // Bloquea hasta leer una tecla y la traduce a un Event de alto
    // nivel (esta es la unica funcion que "sabe" de teclas).
    Event readEvent();

    // Tamano actual de la terminal.
    void getWindowSize(int& rows, int& cols);

private:
    bool rawModeEnabled_ = false;
    // true si EDIT_DEBUG_KEYS esta definida: vuelca a stderr los bytes
    // crudos de las teclas no reconocidas (util para diagnosticar como
    // emite la terminal las secuencias, p.ej. Shift+Flecha).
    bool debugKeys_ = false;
    void* origTermios_; // puntero opaco a struct termios (evita incluir <termios.h> aqui)

    // Terminal es un recurso unico ligado a la terminal fisica del
    // proceso (modo raw, tamano de la ventana, el mismo STDIN_FILENO).
    // No tiene sentido copiar/mover una instancia: gestiona memoria
    // (new/delete de struct termios), asi que copiarla llevaria a doble
    // delete y comportamiento indefinido. Prohibir copia y movimiento
    // convierte ese bug potencial en un error de compilacion.
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&&) = delete;
    Terminal& operator=(Terminal&&) = delete;
};
