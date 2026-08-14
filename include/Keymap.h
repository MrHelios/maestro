#pragma once

#include <map>
#include <optional>
#include <string>

#include "Event.h"

// Tabla de datos que traduce las teclas crudas a Eventos de alto nivel,
// y que se puede reconfigurar en tiempo de ejecucion.
//
// Antes ese significado vivia hardcodeado en switchs/ifs dentro de
// Terminal::readEvent. Aqui lo sacamos a una tabla remapeable: Terminal
// sigue siendo el unico que sabe leer bytes crudos y ensamblar secuencias
// (distingue un ESC suelto de una secuencia de escape, acumula parametros
// con timeout, arma el caracter UTF-8 multibyte), pero el SIGNIFICADO -
// "que hace cada tecla" - queda como datos.
//
// Esto completa la separacion "InputEvent -> Event" que describe el README:
// el Editor ya trabajaba con Eventos (semanticos, no fisicos); ahora el
// mapeo input->Event deja de estar atornillado y se vuelve remapeable
// (micros, plugins, personalizacion de teclas...) sin tocar la logica del
// Editor: un mismo Evento puede venir de distintas teclas o secuencias sin
// que el Editor cambie una linea.
//
// Hay DOS tablas, una por "forma de llegar por la terminal":
//   - controlBytes_: teclas de UN byte de control (Ctrl+X, Enter, BS...).
//   - sequences_: secuencias de escape, identificadas por su CONTENIDO
//     (lo que sigue al ESC hasta el caracter final), p.ej. "A" (ESC[A
//     = flecha arriba), "[B", "[1;2C", "3~", "OH", ...
class Keymap {
public:
    Keymap();

    // --- Teclas de control de un byte (Ctrl+..., Enter, Backspace) ---
    // Asocia `byte` de control a un Evento. Reemplaza el anterior.
    void bindControl(unsigned char byte, EventType type);
    // Evento asociado al byte, o std::nullopt si no esta enlazado.
    std::optional<EventType> control(unsigned char byte) const;

    // --- Secuencias de escape ---
    // Asocia el CONTENIDO de una secuencia (sin el ESC inicial) a un
    // Evento. Reemplaza el anterior. Se guarda tal cual se acumula
    // despues del ESC: "[C" para flecha derecha, "[1;2C" con modificador,
    // "3~" para Delete, "OH" para fin...
    void bindSequence(const std::string& contents, EventType type);
    // Evento asociado al contenido, o std::nullopt si no esta enlazado.
    std::optional<EventType> sequence(const std::string& contents) const;

    // Restaura los enlaces por defecto del editor. Util para "volver a
    // cero" (p.ej. tras una sesion que los reconfiguro).
    void resetDefaults();

private:
    std::map<unsigned char, EventType> controlBytes_;
    std::map<std::string, EventType> sequences_;
};