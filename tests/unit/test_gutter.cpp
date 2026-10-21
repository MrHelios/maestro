#include "layout/Gutter.h"
#include "test_framework.h"

// Fuente unica: layout/Gutter.h. Cubre el minimo de 5 columnas para el
// campo numerico (hasta 10000), el separador de 2 columnas, el centrado
// (competencias del backend) y el clamp a viewports angostos.

TEST(gutter_small_doc_minimum) {
    CHECK_EQ(gutterWidth(1, 80), 7);
    CHECK_EQ(gutterWidth(9, 80), 7);
}

TEST(gutter_minimum_for_10000) {
    // Hasta 99999 el campo es 5 (minimo) + 2 de separador.
    CHECK_EQ(gutterWidth(100, 80), 7);
    CHECK_EQ(gutterWidth(9999, 80), 7);
    CHECK_EQ(gutterWidth(10000, 80), 7);
    CHECK_EQ(gutterWidth(99999, 80), 7);
    // 100000 exige un campo de 6 digitos.
    CHECK_EQ(gutterWidth(100000, 80), 8);
}

TEST(gutter_metrics_field_and_gap) {
    GutterMetrics m = gutterMetrics(1, 80);
    CHECK_EQ(m.numberField, 5);
    CHECK_EQ(m.gap, 2);
    CHECK_EQ(m.total, 7);

    m = gutterMetrics(100000, 80);
    CHECK_EQ(m.numberField, 6);
    CHECK_EQ(m.gap, 2);
    CHECK_EQ(m.total, 8);
}

TEST(gutter_clamped_to_viewport) {
    CHECK_EQ(gutterWidth(1000, 7), 7);
    CHECK_EQ(gutterWidth(1000, 6), 6);
    CHECK_EQ(gutterWidth(1000, 5), 5);
    CHECK_EQ(gutterWidth(9999, 2), 2);
}

TEST(gutter_narrow_prioritizes_number_field) {
    // Se prioriza el campo numerico; el separador usa lo que sobra.
    GutterMetrics m = gutterMetrics(1, 6);
    CHECK_EQ(m.numberField, 5);
    CHECK_EQ(m.gap, 1);
    CHECK_EQ(m.total, 6);

    m = gutterMetrics(1, 5);
    CHECK_EQ(m.numberField, 5);
    CHECK_EQ(m.gap, 0);
    CHECK_EQ(m.total, 5);

    m = gutterMetrics(1, 4);
    CHECK_EQ(m.numberField, 4);
    CHECK_EQ(m.gap, 0);
    CHECK_EQ(m.total, 4);
}

TEST(gutter_tiny_viewport) {
    CHECK_EQ(gutterWidth(1, 7), 7);
    CHECK_EQ(gutterWidth(1, 6), 6);
    CHECK_EQ(gutterWidth(1, 5), 5);
    CHECK_EQ(gutterWidth(1, 4), 4);
    CHECK_EQ(gutterWidth(1, 3), 3);
    CHECK_EQ(gutterWidth(1, 2), 2);
    CHECK_EQ(gutterWidth(1, 1), 1);
    CHECK_EQ(gutterWidth(1, 0), 0);
    CHECK_EQ(gutterWidth(9999, 0), 0);
}

TEST(gutter_invariant_range) {
    for (int total : {0, 1, 9, 10, 99, 100, 999, 1000, 9999, 10000, 99999,
                      100000}) {
        for (int w : {0, 1, 2, 3, 4, 5, 6, 7, 10, 80}) {
            GutterMetrics m = gutterMetrics(total, w);
            CHECK(m.numberField >= 0);
            CHECK(m.gap >= 0);
            CHECK(m.total >= 0);
            CHECK_EQ(m.total, m.numberField + m.gap);
            CHECK(m.total <= w);
            CHECK_EQ(gutterWidth(total, w), m.total);
        }
    }
}
