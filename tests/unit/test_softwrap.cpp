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

    CHECK(!idx.ensure(d, 5, false));
    CHECK(!idx.enabled());
    CHECK_EQ(idx.totalRows(), 2);
    CHECK_EQ(idx.visualRowOf({0, 8}), 0);
    CHECK_EQ(idx.visualRowOf({1, 1}), 1);
}

TEST(softwrap_rows_and_boundary_affinity) {
    Document d = docOf({"abcdefghij"});
    softwrap::Index idx;

    CHECK(idx.ensure(d, 5, true));
    CHECK(idx.enabled());
    CHECK_EQ(idx.totalRows(), 2);
    CHECK_EQ(idx.rowsInLine(0), 2);

    const softwrap::VisualRow& r0 = idx.rowAt(0);
    const softwrap::VisualRow& r1 = idx.rowAt(1);
    CHECK_EQ(r0.byteBegin, 0);
    CHECK_EQ(r0.byteEnd, 5);
    CHECK_EQ(r1.byteBegin, 5);
    CHECK_EQ(r1.byteEnd, 10);

    // La posición 5 es simultáneamente fin de la fila 0 e inicio de la 1.
    // La afinidad explícita decide a cuál pertenece.
    CHECK_EQ(idx.visualRowOf({0, 5}, softwrap::VisualAffinity::Start), 1);
    CHECK_EQ(idx.visualRowOf({0, 5}, softwrap::VisualAffinity::End), 0);
    CHECK_EQ(idx.visualColumnOf(d, {0, 5}, softwrap::VisualAffinity::Start), 0);
    CHECK_EQ(idx.visualColumnOf(d, {0, 5}, softwrap::VisualAffinity::End), 5);

    // Home/End en una fila intermedia no deben cruzar el límite:
    // positionAt(row1, 0) es el inicio de la fila 1 (columna 5 lógica),
    // y positionAt(row0, width) es la última columna VÁLIDA de la fila 0.
    CHECK(idx.positionAt(d, 1, 0) == (Position{0, 5}));
    CHECK(idx.positionAt(d, 0, 5) == (Position{0, 4}));
}

TEST(softwrap_incremental_update_shifts_later_lines) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;
    CHECK(idx.ensure(d, 5, true));
    CHECK_EQ(idx.rowsInLine(0), 2);
    CHECK_EQ(idx.rowsInLine(1), 1);
    CHECK_EQ(idx.firstRowOfLine(1), 2);
    CHECK_EQ(idx.totalRows(), 3);

    // Misma cantidad de líneas, pero la primera crece. La marca dirty
    // permite actualizar solo esa línea y desplazar las posteriores.
    d.restore({"abcdefghijKLMNO", "xy"});
    idx.markLineDirty(0);
    CHECK(idx.ensure(d, 5, true));
    CHECK_EQ(idx.rowsInLine(0), 3);
    CHECK_EQ(idx.rowsInLine(1), 1);
    CHECK_EQ(idx.firstRowOfLine(1), 3);
    CHECK_EQ(idx.totalRows(), 4);
    CHECK(idx.positionAt(d, 3, 0) == (Position{1, 0}));

    // Sin cambios de versión ni marcas, no hay trabajo.
    CHECK(!idx.ensure(d, 5, true));
}

TEST(softwrap_same_row_count_updates_in_place) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;
    CHECK(idx.ensure(d, 5, true));
    CHECK_EQ(idx.firstRowOfLine(1), 2);

    // La línea cambia de longitud pero sigue ocupando dos filas: los
    // metadatos posteriores no deben moverse.
    d.restore({"abcdefghi", "xy"});
    idx.markLineDirty(0);
    CHECK(idx.ensure(d, 5, true));
    CHECK_EQ(idx.rowsInLine(0), 2);
    CHECK_EQ(idx.rowAt(1).byteEnd, 9);
    CHECK_EQ(idx.firstRowOfLine(1), 2);
    CHECK_EQ(idx.totalRows(), 3);
}

TEST(softwrap_duplicate_dirty_marks_are_idempotent) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;
    CHECK(idx.ensure(d, 5, true));

    d.restore({"abc", "xy"});
    idx.markLineDirty(0);
    idx.markLineDirty(0);
    CHECK(idx.ensure(d, 5, true));
    CHECK_EQ(idx.rowsInLine(0), 1);
    CHECK_EQ(idx.firstRowOfLine(1), 1);
    CHECK_EQ(idx.totalRows(), 2);
}

TEST(softwrap_unmarked_change_forces_rebuild) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;
    CHECK(idx.ensure(d, 5, true));
    CHECK_EQ(idx.rowsInLine(1), 1);

    // Cambio de versión sin markLineDirty (p.ej. undo/redo o una mutación
    // externa): debe reconstruirse, nunca devolver filas obsoletas.
    d.restore({"abcdefghij", "xyzw123456789"});
    CHECK(idx.ensure(d, 5, true));
    CHECK_EQ(idx.rowsInLine(1), 3);
    CHECK_EQ(idx.totalRows(), 5);
}

TEST(softwrap_zero_width_does_not_rebuild_forever) {
    Document d = docOf({"abcdefghij", "xy"});
    softwrap::Index idx;

    CHECK(!idx.ensure(d, 0, true));
    CHECK(!idx.enabled());
    CHECK(!idx.ensure(d, 0, true));
    CHECK(!idx.ensure(d, 0, true));
}
