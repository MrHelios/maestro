// Tests de la geometria del frame (paso 10). computeLayout() (core/Layout.h)
// es la UNICA fuente que decide cuantas filas tiene el contenido y donde
// arranca el chrome. Estos tests fijan esa geometria para terminales
// tipicas y verifican que nunca produzca cotas invalidas (filas negativas,
// contenido que invada el chrome, etc.) en terminales muy chicas.
#include "layout/Layout.h"
#include "test_framework.h"

namespace {

// Fila (1-indexada) de la terminal en la que empieza el chrome.
int chromeFirstRow(const Layout& l) { return l.chrome.row + 1; }

} // namespace

// Terminal 80x24: el editor reserva kChromeRows filas para el chrome, asi
// que el contenido queda en 80x22 y el chrome ocupa las filas 23 y 24.
TEST(layout_terminal_80x24) {
    const Layout l = computeLayout(24, 80);
    CHECK_EQ(l.content.row, 0);
    CHECK_EQ(l.content.col, 0);
    CHECK_EQ(l.content.width, 80);
    CHECK_EQ(l.content.height, 22);

    CHECK_EQ(l.chrome.row, 22);
    CHECK_EQ(l.chrome.col, 0);
    CHECK_EQ(l.chrome.width, 80);
    CHECK_EQ(l.chrome.height, kChromeRows);
    CHECK_EQ(chromeFirstRow(l), 23);
}

// Terminal 120x40: contenido 120x38; barra en las filas 39 y 40.
TEST(layout_terminal_120x40) {
    const Layout l = computeLayout(40, 120);
    CHECK_EQ(l.content.width, 120);
    CHECK_EQ(l.content.height, 38);
    CHECK_EQ(l.chrome.row, 38);
    CHECK_EQ(l.chrome.height, kChromeRows);
    CHECK_EQ(chromeFirstRow(l), 39);
}

// El contenido jamas baja de 1 fila y nunca produce coordenadas invalidas,
// por muy chica que sea la terminal (row >= 0, col == 0, width fiel).
// En terminales degeneradas (rows <= kChromeRows) la barra se recorta
// para no exceder el rango visible.
TEST(layout_very_small_no_invalid_geometry) {
    for (int rows : {1, 2, 3}) {
        for (int cols : {1, 5, 80}) {
            const Layout l = computeLayout(rows, cols);

            CHECK(l.content.height >= 1);
            CHECK(l.content.row == 0);
            CHECK(l.content.col == 0);
            CHECK(l.content.width == cols);

            CHECK(l.chrome.row == l.content.height);
            CHECK(l.chrome.col == 0);
            CHECK(l.chrome.width == cols);

            CHECK(l.chrome.row + l.chrome.height <= rows);
            CHECK(l.chrome.height >= 0);
            CHECK(l.chrome.height <= kChromeRows);
        }
    }
}

TEST(layout_zero_rows_keeps_virtual_content_row) {
    for (int cols : {1, 5, 80}) {
        const Layout l = computeLayout(0, cols);

        CHECK(l.content.height == 1);
        CHECK(l.content.row == 0);
        CHECK(l.content.col == 0);
        CHECK(l.content.width == cols);

        CHECK(l.chrome.height == 0);
    }
}

// Invariante global: para terminales normales (rows > kChromeRows) el
// contenido + el chrome llenan EXACTAMENTE la terminal, sin solaparse ni
// exceder las cotas. El contenido nunca invade el chrome.
TEST(layout_content_never_invades_chrome) {
    for (int rows = kChromeRows+1; rows <= 40; ++rows) {
        for (int cols : {10, 80, 120}) {
            const Layout l = computeLayout(rows, cols);
            CHECK(l.content.height == rows - kChromeRows);
            CHECK(l.chrome.row == l.content.height);
            CHECK(l.chrome.row + l.chrome.height == rows);
            CHECK(l.content.row + l.content.height <= l.chrome.row);
            CHECK(l.content.col == 0 && l.content.row == 0);
        }
    }
}
