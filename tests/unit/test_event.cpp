#include "terminal/Event.h"
#include "test_framework.h"

TEST(event_default_state) {
    Event e;
    CHECK(e.type == EventType::None);
    CHECK(e.text.empty());
}
