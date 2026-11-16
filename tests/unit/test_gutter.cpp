#include "layout/Gutter.h"
#include "test_framework.h"

// Fuente unica: layout/Gutter.h. 
// Cubre el minimo de 5 columnas para el campo numerico (hasta 10000), 
// el separador de 2 columnas

// casos de 0 lineas hasta 100k
// mas los casos de un viewport con 0 hasta 80
TEST(gutter_invariant_range) {
    for (int total : {0, 1, 9, 10, 99, 100, 999, 1000, 9999, 10000, 99999, 100000}) {
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

// caso extremo donde el viewport tiene las misma dimesion que el gutter.
TEST(gutter_clamped_to_viewport) {
    CHECK_EQ(gutterWidth(1000, 7), 7);
    CHECK_EQ(gutterWidth(1000, 6), 6);
    CHECK_EQ(gutterWidth(1000, 5), 5);
    CHECK_EQ(gutterWidth(9999, 2), 2);
}

// Se prioriza el campo numerico
// el gap usa lo que sobra.
TEST(gutter_narrow_prioritizes_number_field) {
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

