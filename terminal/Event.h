#pragma once

#include <string>

// En vez de razonar en terminos de "teclas", el Engine razona en
// terminos de EVENTOS. Esto hace que Editor sea completamente
// reutilizable: podria alimentarse desde un teclado real, desde un
// test automatizado, desde una macro grabada, etc.
// Diseño: Event transporta un único EventType, sin campo "shift".
// Desde v0.5 la selección se activa con la letra 's' en modo Navegación
// (un InsertChar que el Editor interpreta), no con un evento propio ni
// con modificador Shift. No existe EventType::Select; Prefix (Ctrl+K) y
// Save (Ctrl+S) modelan el guardado con prefijo.
enum class EventType {
    None,
    InsertChar,
    InsertNewline,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    MoveHome,
    MoveEnd,
    // RePag / AvPag (Page Up / Page Down): el viewport se desplaza una
    // pagina y el cursor la misma cantidad, conservando su posicion
    // relativa dentro del viewport. Trabajan en Navegacion, Interaccion
    // (movimiento libre) y Seleccion (extienden la seleccion como una
    // flecha). Se ignoran durante el prefijo 'a'.
    PageUp,
    PageDown,
    Backspace,
    Delete,
    Undo,
    Redo,
    Quit,
    // Ctrl+K: entra en "modo prefijo"; el siguiente evento decide
    // (Ctrl+S guarda, Ctrl+Q sale, cualquier otra cosa lo cancela).
    Prefix,
    // Ctrl+S: guardar. Solo tiene efecto tras el prefijo (Ctrl+K); fuera
    // de el se ignora. La entrada a seleccion ya NO es por Ctrl+S: desde
    // v0.5 se hace con la letra 's' dentro del modo Navegacion.
    Save,
    // ESC suelto (no seguido de una secuencia de flecha/Home/...).
    // Tipico uso: cancelar la seleccion activa o salir de Interaccion.
    Escape,
};

struct Event {
    EventType type = EventType::None;
    // Texto a insertar (solo relevante para InsertChar). Guarda los
    // BYTES UTF-8 de un solo caracter: un ASCII (1 byte) o un caracter
    // multibyte (2-4 bytes, p.ej. "á", "ñ", "—", "😀"). Asi el Editor
    // recibee el caracter completo, no byte por byte.
    std::string text;
    // Nota: desde v0.3 no hay campo "shift". La seleccion no depende del
    // modificador Shift (que cada terminal emite de forma distinta); la
    // entrada a seleccion se hace con la letra 's' en modo Navegacion.
};
