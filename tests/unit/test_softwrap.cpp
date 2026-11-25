#include <string>
#include <vector>

#include "document/Document.h"
#include "document/Position.h"
#include "layout/SoftWrap.h"
#include "test_framework.h"

namespace {

Document docOf(const std::vector<std::string>& lines) {
    Document d;
    d.restore(lines);
    return d;
}

} // namespace

TEST(softwrap_identity_without_wrap) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;

    CHECK(!idx.ensure(d, 5, false, 0, 3));
    CHECK(!idx.enabled());

    CHECK_EQ(idx.visualRowOf({0, 4}), 0);
    CHECK_EQ(idx.visualRowOf({0, 9}), 0);
    CHECK_EQ(idx.visualRowOf({1, 1}), 1);
}

TEST(softwrap_wrap_rows_are_relative_to_window) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;

    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK(idx.enabled());
    // Ventana [0, 2): L0 ocupa 2 filas y L1, 1 => total 3 dentro de la ventana.
    CHECK_EQ(idx.rowCount(), 3);
    CHECK(idx.coversLine(0));
    CHECK(idx.coversLine(1));
    CHECK(!idx.coversLine(2));

    CHECK_EQ(idx.rowsInLine(0), 2);
    CHECK_EQ(idx.firstRowOfLine(0), 0);
    CHECK_EQ(idx.firstRowOfLine(1), 2);
}

TEST(softwrap_ensure_scrolls_window) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;

    CHECK(idx.ensure(d, 5, true, 1, 3));
    // Ventana: top=1 => start = max(0, 1-5) = 0, end = min(2, 1+3+5+1=10) = 2.
    CHECK(idx.coversLine(1));
    // L0 ocupa las filas 0-1 y L1, la fila 2: el top logico 1 arranca en su
    // primera fila visual, que es la fila 2 de la ventana.
    CHECK_EQ(idx.firstRowOfLine(1), 2);
}

TEST(softwrap_rows_and_boundary_affinity) {
    Document d = docOf({"abcdefghij"});
    softwrap::Index idx;

    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK(idx.enabled());
    CHECK_EQ(idx.rowCount(), 2);
    CHECK_EQ(idx.rowsInLine(0), 2);

    const softwrap::VisualRow& r0 = idx.rowAt(0);
    const softwrap::VisualRow& r1 = idx.rowAt(1);
    CHECK_EQ(r0.byteBegin, 0);
    CHECK_EQ(r0.byteEnd, 5);
    CHECK_EQ(r1.byteBegin, 5);
    CHECK_EQ(r1.byteEnd, 10);

    // La posición 5 es simultáneamente fin de la fila 0 e inicio de la 1.
    CHECK_EQ(idx.visualRowOf({0, 5}, softwrap::VisualAffinity::Start), 1);
    CHECK_EQ(idx.visualRowOf({0, 5}, softwrap::VisualAffinity::End), 0);
    CHECK_EQ(idx.visualColumnOf(d, {0, 5}, softwrap::VisualAffinity::Start), 0);
    CHECK_EQ(idx.visualColumnOf(d, {0, 5}, softwrap::VisualAffinity::End), 5);

    CHECK(idx.positionAt(d, 0, 5) == (Position{0, 4}));
    // ultima fila visual, pero primera fila logica
    CHECK(idx.positionAt(d, 1, 0) == (Position{0, 5}));
    // En la última fila de la línea sí se alcanza el fin exclusivo.
    CHECK(idx.positionAt(d, 1, 5) == (Position{0, 10}));
}

TEST(softwrap_incremental_update_shifts_later_lines) {
    Document d = docOf({"abcdefghij"});
    softwrap::Index idx;
    
    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK_EQ(idx.rowsInLine(0), 2);
    CHECK_EQ(idx.rowCount(), 2);

    d.restore({"abcdefghijKLMNO"});
    idx.markLineDirty(0);
    
    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK_EQ(idx.rowsInLine(0), 3);
    CHECK_EQ(idx.rowCount(), 3);

    // Sin cambios de versión ni marcas, no hay trabajo.
    CHECK(!idx.ensure(d, 5, true, 0, 3));
}

TEST(softwrap_incremental_multiplelines_update_shifts_later_lines) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;
    
    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK_EQ(idx.rowsInLine(0), 2);
    CHECK_EQ(idx.rowsInLine(1), 1);
    CHECK_EQ(idx.rowCount(), 3);

    d.restore({"abcdefghijKLMNO", "xy"});
    idx.markLineDirty(0);
    
    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK_EQ(idx.rowsInLine(0), 3);
    CHECK_EQ(idx.rowsInLine(1), 1);
    CHECK_EQ(idx.rowCount(), 4);
    CHECK(idx.positionAt(d, 3, 0) == (Position{1, 0}));

    // Sin cambios de versión ni marcas, no hay trabajo.
    CHECK(!idx.ensure(d, 5, true, 0, 3));
}

TEST(softwrap_same_row_count_updates_in_place) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;
    
    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK_EQ(idx.rowsInLine(0), 2);
    CHECK_EQ(idx.rowAt(1).byteEnd, 10);
    CHECK_EQ(idx.rowCount(), 3);

    // La línea cambia de longitud pero sigue ocupando dos filas: 
    // los metadatos posteriores no deben moverse.
    d.restore({"abcdefghi", "xy"});
    idx.markLineDirty(0);
    
    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK_EQ(idx.rowsInLine(0), 2);
    CHECK_EQ(idx.rowAt(1).byteEnd, 9);
    CHECK_EQ(idx.firstRowOfLine(1), 2);
    CHECK_EQ(idx.rowCount(), 3);
}

TEST(softwrap_duplicate_dirty_marks_are_idempotent) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;
    
    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK_EQ(idx.rowsInLine(0), 2);
    CHECK_EQ(idx.rowCount(), 3);
    
    d.restore({"abc", "xy"});
    idx.markLineDirty(0);
    
    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK_EQ(idx.rowsInLine(0), 1);
    CHECK_EQ(idx.rowCount(), 2);
}

// Cambio de versión sin markLineDirty
// debe reconstruirse, nunca devolver filas obsoletas.
TEST(softwrap_unmarked_change_forces_rebuild) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;
    
    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK_EQ(idx.rowsInLine(1), 1);
    CHECK_EQ(idx.rowCount(), 3);
    
    d.restore({"abcdefghij", "xyzw123456789"});
    
    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK_EQ(idx.rowsInLine(1), 3);
    CHECK_EQ(idx.rowCount(), 5);
}

TEST(softwrap_zero_width_does_not_rebuild_forever) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;

    CHECK(!idx.ensure(d, 0, true, 0, 3));
    CHECK(!idx.enabled());
}

TEST(softwrap_no_total_global_de_filas) {
    // El índice acotado no mantiene totales globales: el total de filas
    // visuales del documento no está disponible sin recorrerlo.
    Document d = docOf({"abcdefghij"});
    softwrap::Index idx;

    CHECK(idx.ensure(d, 5, true, 0, 3));
    CHECK_EQ(idx.rowCount(), 2);
}

// visibleLines() debe considerar topOffset: solo las filas restantes de la
// primera línea cuentan hacia el viewport.
TEST(softwrap_visibleLines_respects_topOffset) {
    Document d = docOf({
        std::string(30, 'a'),  // 3 filas visuales con width=12 (12+12+6)
        "b",                   // 1 fila
        "c",                   // 1 fila
        "d",                   // 1 fila
    });
    softwrap::Index idx;
    CHECK(idx.ensure(d, 12, true, 0, 4));
    CHECK_EQ(idx.rowsInLine(0), 3);
    CHECK_EQ(idx.rowsInLine(1), 1);
    CHECK_EQ(idx.rowsInLine(2), 1);
    CHECK_EQ(idx.rowsInLine(3), 1);

    // height=2, topOffset=2: L0 aporta 1 fila (la 2), L1 aporta 1 => 2 líneas.
    CHECK_EQ(idx.visibleLines(0, 2, 2), 2);

    // height=2, topOffset=0: L0 aporta 3 filas (>=2), solo 1 línea cabe.
    CHECK_EQ(idx.visibleLines(0, 2, 0), 1);

    // height=5, topOffset=0: L0=3, L1=1, L2=1 => 3 líneas (5 filas).
    CHECK_EQ(idx.visibleLines(0, 5, 0), 3);

    // height=5, topOffset=1: L0 restante=2, L1=1, L2=1, L3=1 => 4 líneas.
    CHECK_EQ(idx.visibleLines(0, 5, 1), 4);
}
