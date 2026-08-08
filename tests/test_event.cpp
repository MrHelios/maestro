#include "Event.h"
#include "test_framework.h"

// ---------------------------------------------------------------------------
// Paso 2: el archivo Event transporta un solo EventType, sin campo "shift".
// Desde v0.3 la seleccion se activa con el evento Select (Ctrl+S), no con un
// modificador Shift sobre el movimiento. Un evento de movimiento "plain" ya
// no lleva flag: es un movimiento simple.
// ---------------------------------------------------------------------------

TEST(event_move_left) {
    Event e;
    e.type = EventType::MoveLeft;
    CHECK(e.type == EventType::MoveLeft);
    CHECK_EQ(e.ch, 0);
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

// La seleccion vive en un evento propio: Select (Ctrl+S).
TEST(event_select_is_present) {
    Event e;
    e.type = EventType::Select;
    CHECK(e.type == EventType::Select);
    CHECK_EQ(e.ch, 0);
}

// El prefijo Ctrl+K tambien es un evento propio.
TEST(event_prefix_is_present) {
    Event e;
    e.type = EventType::Prefix;
    CHECK(e.type == EventType::Prefix);
}