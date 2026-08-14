#include "terminal/Keymap.h"

// Enlaces por defecto del editor. Son los mismos que hasta v0.7 estaban
// hardcodeados en Terminal::readEvent; ahora viven como datos y pueden
// reconfigurarse en tiempo de ejecucion.
//
// Vendria muy bien conservar los comentarios de por que cada tecla hace lo
// que hace (Ctrl+K es el prefijo de comando, Ctrl+U/Y deshacer/rehacer,
// v0.5 no da significado a los modificadores, etc.); quedan en el .h de
// Keymap y en esta tabla se mantienen los nombres distinguibles.
void Keymap::resetDefaults() {
    controlBytes_.clear();
    sequences_.clear();

    // --- Teclas de control de un byte ---
    // Todas son bytes UNICOS (no secuencias), asi que funcionan igual en
    // cualquier emulador.
    bindControl(17, EventType::Quit);                // Ctrl+Q -> salir
    bindControl(19, EventType::Save);                // Ctrl+S -> guardar (solo tras Ctrl+K)
    bindControl(11, EventType::Prefix);              // Ctrl+K -> prefijo de comando
    bindControl(21, EventType::Undo);                // Ctrl+U -> deshacer
    bindControl(25, EventType::Redo);                // Ctrl+Y -> rehacer
    bindControl(127, EventType::Backspace);          // DEL
    bindControl(8, EventType::Backspace);            // BS (otra forma de Backspace)
    bindControl(13, EventType::InsertNewline);       // Enter (\r)
    bindControl(10, EventType::InsertNewline);       // Enter (\n)

    // --- Secuencias de escape (el contenido que sigue al ESC) ---
    // Flechas y Home/End sin parametros: "ESC [ A" se guarda como "[A"...,
    // y "ESC O H" (teclas de cursor en modo aplicacion) como "OH".
    bindSequence("A",  EventType::MoveUp);
    bindSequence("B",  EventType::MoveDown);
    bindSequence("C",  EventType::MoveRight);
    bindSequence("D",  EventType::MoveLeft);
    bindSequence("[A", EventType::MoveUp);
    bindSequence("[B", EventType::MoveDown);
    bindSequence("[C", EventType::MoveRight);
    bindSequence("[D", EventType::MoveLeft);
    bindSequence("[H", EventType::MoveHome);
    bindSequence("[F", EventType::MoveEnd);
    bindSequence("OH", EventType::MoveEnd);   // modo aplicacion: End
    bindSequence("OF", EventType::MoveHome);  // modo aplicacion: Home
    // Modo aplicacion (SS3): las flechas tambien llegan como "ESC O A"...
    // (p.ej. tras activar smkx en algunos emuladores).
    bindSequence("OA", EventType::MoveUp);
    bindSequence("OB", EventType::MoveDown);
    bindSequence("OC", EventType::MoveRight);
    bindSequence("OD", EventType::MoveLeft);

    // Secuencias con parametros e "~": Home/End/Delete/RePag/AvPag.
    // Aqui el parametro SI importa: es la propia tecla, no un modificador.
    bindSequence("[1~", EventType::MoveHome);
    bindSequence("[7~", EventType::MoveHome);
    bindSequence("[4~", EventType::MoveEnd);
    bindSequence("[8~", EventType::MoveEnd);
    bindSequence("[3~", EventType::Delete);
    bindSequence("[5~", EventType::PageUp);
    bindSequence("[6~", EventType::PageDown);
}

Keymap::Keymap() {
    resetDefaults();
}

void Keymap::bindControl(unsigned char byte, EventType type) {
    controlBytes_[byte] = type;
}

std::optional<EventType> Keymap::control(unsigned char byte) const {
    auto it = controlBytes_.find(byte);
    return it == controlBytes_.end() ? std::nullopt
                                     : std::optional<EventType>(it->second);
}

void Keymap::bindSequence(const std::string& contents, EventType type) {
    sequences_[contents] = type;
}

std::optional<EventType> Keymap::sequence(const std::string& contents) const {
    auto it = sequences_.find(contents);
    return it == sequences_.end() ? std::nullopt
                                  : std::optional<EventType>(it->second);
}