#include "platform/tty/Keymap.h"

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
    bindControl(17, InputEventType::Quit);                // Ctrl+Q -> salir
    bindControl(19, InputEventType::Save);                // Ctrl+S -> guardar (solo tras Ctrl+K)
    bindControl(11, InputEventType::Prefix);              // Ctrl+K -> prefijo de comando
    bindControl(21, InputEventType::Undo);                // Ctrl+U -> deshacer
    bindControl(25, InputEventType::Redo);                // Ctrl+Y -> rehacer
    bindControl(127, InputEventType::Backspace);          // DEL
    bindControl(8, InputEventType::Backspace);            // BS (otra forma de Backspace)
    bindControl(13, InputEventType::InsertNewline);       // Enter (\r)
    bindControl(10, InputEventType::InsertNewline);       // Enter (\n)

    // --- Secuencias de escape (el contenido que sigue al ESC) ---
    // Flechas y Home/End sin parametros: "ESC [ A" se guarda como "[A"...,
    // y "ESC O H" (teclas de cursor en modo aplicacion) como "OH".
    bindSequence("A",  InputEventType::MoveUp);
    bindSequence("B",  InputEventType::MoveDown);
    bindSequence("C",  InputEventType::MoveRight);
    bindSequence("D",  InputEventType::MoveLeft);
    bindSequence("[A", InputEventType::MoveUp);
    bindSequence("[B", InputEventType::MoveDown);
    bindSequence("[C", InputEventType::MoveRight);
    bindSequence("[D", InputEventType::MoveLeft);
    bindSequence("[H", InputEventType::MoveHome);
    bindSequence("[F", InputEventType::MoveEnd);
    bindSequence("OH", InputEventType::MoveEnd);   // modo aplicacion: End
    bindSequence("OF", InputEventType::MoveHome);  // modo aplicacion: Home
    // Modo aplicacion (SS3): las flechas tambien llegan como "ESC O A"...
    // (p.ej. tras activar smkx en algunos emuladores).
    bindSequence("OA", InputEventType::MoveUp);
    bindSequence("OB", InputEventType::MoveDown);
    bindSequence("OC", InputEventType::MoveRight);
    bindSequence("OD", InputEventType::MoveLeft);

    // Secuencias con parametros e "~": Home/End/Delete/RePag/AvPag.
    // Aqui el parametro SI importa: es la propia tecla, no un modificador.
    bindSequence("[1~", InputEventType::MoveHome);
    bindSequence("[7~", InputEventType::MoveHome);
    bindSequence("[4~", InputEventType::MoveEnd);
    bindSequence("[8~", InputEventType::MoveEnd);
    bindSequence("[3~", InputEventType::Delete);
    bindSequence("[5~", InputEventType::PageUp);
    bindSequence("[6~", InputEventType::PageDown);
}

Keymap::Keymap() {
    resetDefaults();
}

void Keymap::bindControl(unsigned char byte, InputEventType type) {
    controlBytes_[byte] = type;
}

std::optional<InputEventType> Keymap::control(unsigned char byte) const {
    auto it = controlBytes_.find(byte);
    return it == controlBytes_.end() ? std::nullopt
                                     : std::optional<InputEventType>(it->second);
}

void Keymap::bindSequence(const std::string& contents, InputEventType type) {
    sequences_[contents] = type;
}

std::optional<InputEventType> Keymap::sequence(const std::string& contents) const {
    auto it = sequences_.find(contents);
    if (it != sequences_.end()) return it->second;
    if (contents.size() >= 3) {
        const char final = contents.back();
        if (final != '~') {
            std::string simple;
            simple.push_back(contents[0]);
            simple.push_back(final);
            auto it2 = sequences_.find(simple);
            if (it2 != sequences_.end()) return it2->second;
        }
    }
    return std::nullopt;
}