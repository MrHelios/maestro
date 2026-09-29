#include "platform/InputEvent.h"
#include "test_framework.h"

TEST(event_default_state) {
    InputEvent e;
    CHECK(e.type == InputEventType::None);
    CHECK(e.text.empty());
}
