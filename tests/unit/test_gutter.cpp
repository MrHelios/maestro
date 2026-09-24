#include "layout/Gutter.h"
#include "test_framework.h"

// Fuente unica: layout/Gutter.h. Cubre cruces de digitos, clamp a
// viewport y el borde ultra-chico (width < 3 no puede ser violado
// por el max(3, ...) gracias al min final).

TEST(gutter_small_doc_minimum) {
    CHECK_EQ(gutterWidth(1, 80), 3);
    CHECK_EQ(gutterWidth(9, 80), 3);
}

TEST(gutter_digit_crossings) {
    // 10-99: 2 digitos + separador = 3 (igual al minimo).
    CHECK_EQ(gutterWidth(10, 80), 3);
    CHECK_EQ(gutterWidth(99, 80), 3);
    // 100-999: 3 digitos + separador = 4.
    CHECK_EQ(gutterWidth(100, 80), 4);
    CHECK_EQ(gutterWidth(999, 80), 4);
    // 1000-9999: 4 digitos + separador = 5.
    CHECK_EQ(gutterWidth(1000, 80), 5);
    CHECK_EQ(gutterWidth(9999, 80), 5);
}

TEST(gutter_clamped_to_viewport) {
    CHECK_EQ(gutterWidth(1000, 80), 5);
    CHECK_EQ(gutterWidth(1000, 5), 5);
    CHECK_EQ(gutterWidth(1000, 4), 4);
    CHECK_EQ(gutterWidth(9999, 2), 2);
}

TEST(gutter_tiny_viewport) {
    // El max(3, ...) nunca viola el ancho disponible.
    CHECK_EQ(gutterWidth(1, 3), 3);
    CHECK_EQ(gutterWidth(1, 2), 2);
    CHECK_EQ(gutterWidth(1, 1), 1);
    CHECK_EQ(gutterWidth(1, 0), 0);
    CHECK_EQ(gutterWidth(9999, 0), 0);
}

TEST(gutter_invariant_range) {
    for (int total : {1, 9, 10, 99, 100, 999, 1000, 9999, 100000}) {
        for (int w : {0, 1, 2, 3, 4, 5, 10, 80}) {
            int g = gutterWidth(total, w);
            CHECK(g >= 0);
            CHECK(g <= w);
        }
    }
}
