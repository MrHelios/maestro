#include "Event.h"
#include "test_framework.h"

// ---------------------------------------------------------------------------
// Paso 2: el archivo Event debe poder transportar el estado de Shift sobre
// un EventType de movimiento. NO se crean variantes nuevas
// (ShiftLeft/ShiftRight/...): es el MISMO MoveLeft con shift=true.
// ---------------------------------------------------------------------------

TEST(event_move_left_with_shift) {
    Event e;
    e.type = EventType::MoveLeft;
    e.shift = true;
    CHECK(e.type == EventType::MoveLeft);
    CHECK(e.shift);
    CHECK_EQ(e.ch, 0);
}

TEST(event_move_right_with_shift) {
    Event e;
    e.type = EventType::MoveRight;
    e.shift = true;
    CHECK(e.type == EventType::MoveRight);
    CHECK(e.shift);
}

TEST(event_move_up_with_shift) {
    Event e;
    e.type = EventType::MoveUp;
    e.shift = true;
    CHECK(e.type == EventType::MoveUp);
    CHECK(e.shift);
}

TEST(event_move_down_with_shift) {
    Event e;
    e.type = EventType::MoveDown;
    e.shift = true;
    CHECK(e.type == EventType::MoveDown);
    CHECK(e.shift);
}

TEST(event_move_home_with_shift) {
    Event e;
    e.type = EventType::MoveHome;
    e.shift = true;
    CHECK(e.type == EventType::MoveHome);
    CHECK(e.shift);
}

TEST(event_move_end_with_shift) {
    Event e;
    e.type = EventType::MoveEnd;
    e.shift = true;
    CHECK(e.type == EventType::MoveEnd);
    CHECK(e.shift);
}

// El valor por defecto de shift es false: un evento de movimiento
// normal NO selecciona por accidente.
TEST(event_move_default_no_shift) {
    Event e;
    e.type = EventType::MoveRight;
    CHECK(!e.shift);
}

// El mismo tipo transporta tanto "sin shift" como "con shift":
// la combinacion es Move + flag, no tipos separados.
TEST(event_same_type_both_shift_states) {
    Event plain;
    plain.type = EventType::MoveLeft;
    plain.shift = false;

    Event shifted;
    shifted.type = EventType::MoveLeft;
    shifted.shift = true;

    CHECK(plain.type == shifted.type);
    CHECK(!plain.shift);
    CHECK(shifted.shift);
}