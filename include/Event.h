#pragma once

// En vez de razonar en terminos de "teclas", el Engine razona en
// terminos de EVENTOS. Esto hace que Editor sea completamente
// reutilizable: podria alimentarse desde un teclado real, desde un
// test automatizado, desde una macro grabada, etc.
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
    Backspace,
    Delete,
    Undo,
    Redo,
    Save,
    Quit,
    // Ctrl+K: entra en "modo prefijo"; el siguiente evento decide
    // (Ctrl+S guarda, Ctrl+Q sale, cualquier otra cosa lo cancela).
    Prefix,
    // Ctrl+S: entra al modo seleccion (reemplaza a Shift+Flecha, que
    // no funciona de forma fiable en todos los emuladores).
    Select,
    // ESC suelto (no seguido de una secuencia de flecha/Home/...).
    // Tipico uso: cancelar la seleccion activa.
    Escape,
};

struct Event {
    EventType type = EventType::None;
    char ch = 0; // solo relevante para InsertChar
    // Nota: desde v0.3 no hay campo "shift". La seleccion se activa con
    // Ctrl+S (evento Select) y NO depende del modificador Shift, que
    // cada terminal emite de forma distinta.
};
