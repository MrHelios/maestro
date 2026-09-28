#include "test_framework.h"

#include "event_shim.h"
#include "platform/CellPos.h"
#include "platform/Event.h"

// Contrato de compat del shim (Fase A): cuando CellPos migre a 0-based,
// la falla debe aparecer ACÁ —no en decenas de TESTs—. Fija que
// makeMouseEvent(type, CellPos) recibe dominio destino y que los ints
// legacy siguen espejando cellPos() mientras dure la transición.
TEST(shim_mouse_roundtrip_and_legacy_ints) {
    Event e = testshim::makeMouseEvent(EventType::MouseDrag, CellPos{7, 3});
    CHECK(e.type == EventType::MouseDrag);
    CHECK_EQ(e.cellPos().col, 7);
    CHECK_EQ(e.cellPos().row, 3);
    CHECK_EQ(e.mouseCol, 7);
    CHECK_EQ(e.mouseRow, 3);  // compat transitoria
}

// La entrada 1-based SGR es identidad hoy; tras la migración este TEST
// sigue verde sin cambios porque el offset vive en cellFromSgr().
TEST(shim_sgr_entry_point_is_identity_today) {
    CHECK((testshim::cellFromSgr(7, 3) == CellPos{7, 3}));
    Event e = testshim::makeMouseEventSgr(EventType::MousePress, 7, 3);
    CHECK(e.type == EventType::MousePress);
    CHECK_EQ(e.cellPos().col, 7);
    CHECK_EQ(e.cellPos().row, 3);
}

TEST(shim_fake_resize_payload) {
    Event e = testshim::fakeResize(30, 100);
    CHECK(e.type == EventType::Resize);
    CHECK_EQ(e.resizeRows, 30);
    CHECK_EQ(e.resizeCols, 100);
}
