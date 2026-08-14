#include "terminal/Event.h"
#include "test_framework.h"

// ---------------------------------------------------------------------------
// Paso 2: el archivo Event transporta un solo EventType, sin campo "shift".
// Desde v0.5 la seleccion se activa con la letra 's' dentro del modo
// Navegacion (un InsertChar que el Editor interpreta), no con un evento
// propio ni con un modificador Shift sobre el movimiento. No existe un
// EventType::Select.
// ---------------------------------------------------------------------------

TEST(event_move_left) {
    Event e;
    e.type = EventType::MoveLeft;
    CHECK(e.type == EventType::MoveLeft);
    CHECK(e.text.empty());
}

TEST(event_move_right) {
    Event e;
    e.type = EventType::MoveRight;
    CHECK(e.type == EventType::MoveRight);
}

TEST(event_move_up) {
    Event e;
    e.type = EventType::MoveUp;
    CHECK(e.type == EventType::MoveUp);
}

TEST(event_move_down) {
    Event e;
    e.type = EventType::MoveDown;
    CHECK(e.type == EventType::MoveDown);
}

TEST(event_move_home) {
    Event e;
    e.type = EventType::MoveHome;
    CHECK(e.type == EventType::MoveHome);
}

TEST(event_move_end) {
    Event e;
    e.type = EventType::MoveEnd;
    CHECK(e.type == EventType::MoveEnd);
}

// Desde v0.5 la entrada a seleccion NO es un evento propio: se hace con la
// letra 's' (InsertChar) dentro del modo Navegacion, y el Editor decide
// segun su estado. Por eso no existe EventType::Select; si existe Save,
// que es el Ctrl+S (guardar) efectivo solo tras el prefijo.

// El prefijo Ctrl+K tambien es un evento propio.
TEST(event_prefix_is_present) {
    Event e;
    e.type = EventType::Prefix;
    CHECK(e.type == EventType::Prefix);
}

// Save es el evento Ctro+S (guardar). Solo tiene efecto tras el prefijo.
TEST(event_save_is_present) {
    Event e;
    e.type = EventType::Save;
    CHECK(e.type == EventType::Save);
}