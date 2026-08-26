#pragma once

#include "terminal/Event.h"
#include "terminal/Keymap.h"

// Encapsula todo lo especifico de la terminal (POSIX/Linux/macOS):
// activar/desactivar el modo "raw", leer teclas crudas, y consultar
// el tamano de la ventana. Nada de esto sabe de Document/Cursor/etc.
//
// Terminal es quien LEE y ENSAMBLA los bytes crudos (distingue un ESC
// suelto de una secuencia, acumula parametros con timeout, arma el
// caracter UTF-8 multibyte), pero NO decide el significado de cada tecla:
// eso vive en el Keymap (remapeable), que Terminal consulta para traducir
// lo leido a un Evento.
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
    // nivel (esta es la unica funcion que "sabe" de teclas). Bloquea
    // indefinidamente.
    Event readEvent();

    // Igual que readEvent(), pero espera a lo sumo `timeoutMs` milisegundos
    // (0 = no bloquea, negativo = indefinido). Devuelve true si se leyo y
    // tradujo una tecla; false si el timeout expiro sin entrada. Es el
    // mecanismo que permite al Editor despertar el ciclo para limpiar un
    // mensaje de accion expirado sin que el usuario aprete ninguna tecla.
    bool readEvent(Event& event, int timeoutMs);

    // Tamano actual de la terminal.
    void getWindowSize(int& rows, int& cols);

    bool hasResized();

    // Tabla tecla -> Evento que readEvent() consulta. El usuario puede
    // rebindear las teclas en tiempo de ejecucion (keymap().bindSequence(...)
    // / keymap().bindControl(...)) sin tocar la logica del Editor.
    Keymap& keymap() { return keymap_; }

private:
    bool rawModeEnabled_ = false;
    // true si EDIT_DEBUG_KEYS esta definida: vuelca a stderr los bytes
    // crudos de las teclas no reconocidas (util para diagnosticar como
    // emite la terminal las secuencias, p.ej. Shift+Flecha).
    bool debugKeys_ = false;
    void* origTermios_; // puntero opaco a struct termios (evita incluir <termios.h> aqui)

    // Significado de cada tecla/secuencia. Se consulta en readEvent();
    // remapeable en tiempo de ejecucion via keymap().
    Keymap keymap_;

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
