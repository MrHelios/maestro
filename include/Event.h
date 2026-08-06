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
};

struct Event {
    EventType type = EventType::None;
    char ch = 0; // solo relevante para InsertChar
};
