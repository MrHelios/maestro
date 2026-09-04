// Tests de regresion del renderer (paso 11). Cubren las combinaciones de
// tamano de documento y de terminal que la suite existente no recorre de
// forma sistematica, y verifican el invariante central del cambio visual
// v1.1: NINGUNA fila visible del frame (contenido o barra) escribe fuera del
// ancho de la terminal, y la fila fija de la barra ocupa EXACTAMENTE el
// ancho. Se prueban primariamente sobre el Editor (buildScreen) y sobre las
// pantallas de lista (BufferSelector/FileBrowser) "donde aplique".
// NOTA: suite principalmente geometrica: verifica cotas de ancho y UTF-8
// valido. Excepciones puntuales verifican correccion funcional del
// recorte: regression_utf8_content comprueba truncamiento exacto via
// utf8::truncate, y regression_gutter_clamped comprueba formato exacto
// del gutter. Ver Nota 3.
//
// Casos cubiertos:
//   - tamano del documento: vacio, 1, 9, 10, 99, 100, 999 y 1000 lineas
//     (incl. el cambio de digito del gutter en 9->10 y 99->100);
//   - contenido UTF-8 (acentos, guiones largos, emoji);
//   - seleccion: simple, multilinea, y linea vacia seleccionada;
//   - terminal pequena (muy angosta) y terminal grande.
//
// El invariante que se chequea para cada frame es:
//   1. el numero de filas visibles es exactamente `content + kStatusBarRows`;
//   2. ninguna fila visible supera `width` columnas;
//   3. la fila fija de la barra llena EXACTAMENTE `width` columnas;
//   4. la fila de mensajes jamas supera `width`;
//   5. el frame produce UTF-8 valido (ningun multibyte partido).
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "test_framework.h"
#include "helpers/test_render_utils.h"

#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Layout.h"
#include "core/utf8.h"
#include "core/Viewport.h"
#include "ui/Message.h"
#include "ui/Renderer.h"

namespace {

using testutil::stripAnsi;
using testutil::colWidth;
using testutil::gutterWidth;
using testutil::validUtf8;
using testutil::visibleRows;

// ----- Frames de cada pantalla ----------------------------------------------

// Editor: documento de `lines`, `content` filas de contenido, `width` cols.
std::string frameEditor(const std::vector<std::string>& lines,
                        int content, int width,
                        const std::optional<Selection>& sel = std::nullopt,
                        const std::string& msg = "") {
    Document doc;
    doc.restore(lines);
    Viewport vp;
    vp.top = 0;
    vp.height = content;
    vp.width = width;
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    Renderer r;
    return r.buildScreen(doc, cur, vp, "/ruta/proyecto/archivo.txt",
                         false, Message(msg), State::Navegacion, sel);
}

// BufferSelector: lista de `n` nombres.
std::string frameBuffer(int n, int content, int width, int selected = 0) {
    std::vector<std::string> names;
    for (int i = 0; i < n; ++i)
        names.push_back("buffer_" + std::to_string(i) + ".txt");
    Renderer r;
    return r.buildBufferListScreen(names, selected, width, content);
}

// FileBrowser: lista de `n` archivos con una ruta y un mensaje de ayuda.
std::string frameFile(int n, int content, int width) {
    std::vector<std::string> names;
    for (int i = 0; i < n; ++i)
        names.push_back("archivo_" + std::to_string(i) + ".cpp");
    Renderer r;
    return r.buildFileListScreen(names, 0, 0, "/datos/proyecto",
                                 Message("ayuda: direcc de naveg"), width, content);
}

// ----- Invariante global de regresion ---------------------------------------
//
// Verifica los 5 puntos del encabezado del archivo sobre un frame armado con
// `content` filas de contenido y `width` columnas. Es el corazon de la suite:
// cada caso lo llama y asi se garantiza que NINGUN tamano de documento o de
// terminal rompe la cota de ancho ni la validez UTF-8.
void checkFrameWithinBounds(const std::string& frame, int content, int width) {
    const auto rows = visibleRows(frame);

    // 1. El frame tiene exactamente content + kStatusBarRows filas visibles.
    CHECK_EQ((int)rows.size(), content + kStatusBarRows);

    // 2. Ninguna fila (contenido, barra fija ni mensajes) supera `width`.
    for (const std::string& r : rows) {
        CHECK(colWidth(r) <= width);
        CHECK(validUtf8(r));
    }

    // 3/4. La fila fija llena exactamente el ancho; la de mensajes no lo pasa.
    CHECK((int)rows.size() >= kStatusBarRows);
    const int barRow = static_cast<int>(rows.size()) - kStatusBarRows;
    CHECK_EQ(colWidth(rows[barRow]), width);
    CHECK(colWidth(rows[static_cast<int>(rows.size()) - 1]) <= width);
}

} // namespace

// ---------------------------------------------------------------------------
// Tamano del documento: vacio, 1, 9, 10, 99, 100, 999 y 1000 lineas. Cada
// tamano se dibuja con una terminal estrecha y una ancha, y el frame debe
// respetar la cota de ancho y producir UTF-8 valido.
// ---------------------------------------------------------------------------
TEST(regression_document_sizes_varied) {
    std::vector<std::vector<std::string>> docs;
    docs.push_back({""}); // documento vacio (1 linea vacia)
    docs.push_back({"a"});
    {
        std::vector<std::string> d;
        for (int i = 0; i < 9; ++i) d.push_back("linea " + std::to_string(i + 1));
        docs.push_back(d);
    }
    {
        std::vector<std::string> d;
        for (int i = 0; i < 10; ++i) d.push_back("linea " + std::to_string(i + 1));
        docs.push_back(d);
    }
    {
        std::vector<std::string> d;
        for (int i = 0; i < 99; ++i) d.push_back("linea " + std::to_string(i + 1));
        docs.push_back(d);
    }
    {
        std::vector<std::string> d;
        for (int i = 0; i < 100; ++i) d.push_back("linea " + std::to_string(i + 1));
        docs.push_back(d);
    }
    {
        std::vector<std::string> d;
        for (int i = 0; i < 999; ++i) d.push_back("linea " + std::to_string(i + 1));
        docs.push_back(d);
    }
    {
        std::vector<std::string> d;
        for (int i = 0; i < 1000; ++i) d.push_back("linea " + std::to_string(i + 1));
        docs.push_back(d);
    }

    for (const auto& doc : docs) {
        for (int width : {15, 40, 200}) {
            // El viewport recorta el documento a `content` filas visibles.
            for (int content : {5, 22}) {
                std::string f = frameEditor(doc, content, width);
                checkFrameWithinBounds(f, content, width);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Seleccion: simple dentro de una linea, multilinea, y linea vacia atravesada
// por la seleccion. Con y sin terminal estrecha, midiendo columna a columna.
// ---------------------------------------------------------------------------
TEST(regression_selection_single_line) {
    const std::vector<std::string> lines = {"hello world", "segunda", "tercera"};
    // Seleccion (0,0)..(0,5) -> "hello" en la fila 0.
    Selection sel;
    sel.anchor = Position{0, 0};
    sel.position = Position{0, 5};
    for (int width : {15, 40}) {
        std::string f = frameEditor(lines, 6, width, sel);
        checkFrameWithinBounds(f, 6, width);
    }
}

TEST(regression_selection_multiline) {
    const std::vector<std::string> lines = {"hello", "world", "foo", "bar"};
    // (0,0)..(3,2): atraviesa todas las filas.
    Selection sel;
    sel.anchor = Position{0, 0};
    sel.position = Position{3, 2};
    for (int width : {15, 40}) {
        std::string f = frameEditor(lines, 6, width, sel);
        checkFrameWithinBounds(f, 6, width);
    }
}

TEST(regression_selection_empty_line) {
    const std::vector<std::string> lines = {"a", "", "b"};
    // Seleccion (0,1)..(2,1): la fila 1 es vacia y queda dentro del rango.
    Selection sel;
    sel.anchor = Position{0, 1};
    sel.position = Position{2, 1};
    for (int width : {15, 40}) {
        std::string f = frameEditor(lines, 6, width, sel);
        checkFrameWithinBounds(f, 6, width);
    }
}

TEST(regression_selection_reverse_direction) {
    // Seleccion "hacia atras" (cursor < anchor): normaliza y el resultado
    // visual (contenido y barra, cursor en 0,0) no debe cambiar.
    const std::vector<std::string> lines = {"hello world", "segunda"};
    Selection fwd; fwd.anchor = Position{0, 0}; fwd.position = Position{0, 5};
    Selection rev; rev.anchor = Position{0, 5}; rev.position = Position{0, 0};
    std::string a = frameEditor(lines, 6, 40, fwd);
    std::string b = frameEditor(lines, 6, 40, rev);
    checkFrameWithinBounds(a, 6, 40);
    checkFrameWithinBounds(b, 6, 40);
    auto ra = visibleRows(a);
    auto rb = visibleRows(b);
    CHECK_EQ((int)ra.size(), (int)rb.size());
    for (int i = 0; i < (int)ra.size(); ++i) CHECK_EQ(ra[i], rb[i]);
}

// ---------------------------------------------------------------------------
// Contenido UTF-8: acentos, guiones largos y emoji mezclados. El gutter se
// resta del ancho, asi que el recorte de columnas no debe partir multibyte.
// ---------------------------------------------------------------------------
TEST(regression_utf8_content) {
    // NOTA: los literales con secuencias hex seguidas de [a-f0-9] se
    // dividen en trozos contiguos para que \x no "coma" los digitos hex
    // del texto (greedy): "ma\xc3\xb1" + "ana", etc.
    const std::vector<std::string> lines = {
        "caf\xc3\xa9 y \xe2\x80\x94 guion",
        "ma\xc3\xb1" "ana \xf0\x9f\x98\x80 emoji",
        "abc\xe2\x80\x94" "def",
        "\xc3\xa9\xc3\xa9\xc3\xa9",
    };
    for (int width : {10, 15, 25, 60}) {
        std::string f = frameEditor(lines, 6, width);
        checkFrameWithinBounds(f, 6, width);
        const auto rows = visibleRows(f);
        int gw = std::min(gutterWidth((int)lines.size()), width);
        int tw = std::max(0, width - gw);
        for (size_t i = 0; i < lines.size() && (int)i < 6; ++i) {
            std::string row = rows[i];
            CHECK(validUtf8(row));
            std::string textPart = row.size() > (size_t)gw ? row.substr(gw) : "";
            std::string expected = utf8::truncate(lines[i], tw);
            if ((int)i == 0) {
                std::string padded = expected;
                int pad = tw - colWidth(expected);
                if (pad > 0) padded.append(pad, ' ');
                CHECK_EQ(textPart, padded);
            } else {
                CHECK_EQ(textPart, expected);
            }
            CHECK(validUtf8(textPart));
            CHECK(validUtf8(expected));
        }
    }
}

// ---------------------------------------------------------------------------
// Terminal MUY pequena: el gutter (minimo 3) no puede desbordarse en anchos
// menores que el. Es el caso que antes del fix podia escribir fuera de la
// terminal en la fila de documentos con numeros de linea.
// ---------------------------------------------------------------------------
TEST(regression_terminal_extremely_narrow) {
    std::vector<std::string> doc;
    for (int i = 0; i < 100; ++i) doc.push_back("linea " + std::to_string(i + 1));
    for (int width = 1; width <= 5; ++width) {
        std::string f = frameEditor(doc, 6, width);
        checkFrameWithinBounds(f, 6, width);
    }
}

// ---------------------------------------------------------------------------
// Terminal peque/moderada, barriendo un rango de anchos donde el gutter
// (3, luego 4 en 100 lineas) se recorta contra el ancho.
// ---------------------------------------------------------------------------
TEST(regression_terminal_narrow_sweep) {
    std::vector<std::string> doc;
    for (int i = 0; i < 100; ++i) doc.push_back("linea " + std::to_string(i + 1));
    for (int width = 3; width <= 25; ++width) {
        std::string f = frameEditor(doc, 10, width);
        checkFrameWithinBounds(f, 10, width);
    }
}

TEST(regression_terminal_large) {
    std::vector<std::string> doc;
    for (int i = 0; i < 1000; ++i) doc.push_back("linea " + std::to_string(i + 1));
    std::string f = frameEditor(doc, 30, 400);
    checkFrameWithinBounds(f, 30, 400);
}

// ---------------------------------------------------------------------------
// Gutter y recorte de columnas: con 100 lineas y una terminal estrecha, el
// numero de linea (de 3 digitos) no debe desbordar; con el fix, el gutter se
// recorta al ancho disponible y la fila sigue dentro de la cota.
// Verifica especificamente que el gutter esta recortado (cola del numero)
// y no desborda, no solo el invariante global.
// ---------------------------------------------------------------------------
TEST(regression_gutter_clamped_at_narrow_width) {
    std::vector<std::string> doc;
    for (int i = 0; i < 100; ++i) doc.push_back("linea " + std::to_string(i + 1));
    for (int width = 2; width <= 8; ++width) {
        std::string f = frameEditor(doc, 6, width);
        checkFrameWithinBounds(f, 6, width);
        const auto rows = visibleRows(f);
        int gw = std::min(gutterWidth((int)doc.size()), width);
        for (int row = 0; row < 6; ++row) {
            CHECK(colWidth(rows[row]) <= width);
            std::string numStr = std::to_string(row + 1);
            int maxNumCols = std::max(0, gw - 1);
            if ((int)numStr.size() > maxNumCols)
                numStr = numStr.substr(numStr.size() - (size_t)maxNumCols);
            int pad = std::max(0, gw - 1 - (int)numStr.size());
            std::string expectedGutter = std::string(pad, ' ') + numStr + ' ';
            std::string actualGutter = rows[row].size() >= (size_t)gw ? rows[row].substr(0, gw) : rows[row];
            CHECK_EQ(actualGutter, expectedGutter);
        }
    }
    std::vector<std::string> doc1000;
    for (int i = 0; i < 1000; ++i) doc1000.push_back("linea " + std::to_string(i + 1));
    for (int width = 3; width <= 6; ++width) {
        auto frameAt = [&](int top) {
            Document d; d.restore(doc1000);
            Viewport vp; vp.top = top; vp.height = 6; vp.width = width;
            Cursor cur; cur.line = top; cur.col = 0;
            Renderer r;
            return r.buildScreen(d, cur, vp, "/ruta/proyecto/archivo.txt", false, Message(""), State::Navegacion, std::nullopt);
        };
        {
            std::string f = frameAt(0);
            checkFrameWithinBounds(f, 6, width);
        }
        {
            int top = (int)doc1000.size() - 6;
            std::string f = frameAt(top);
            checkFrameWithinBounds(f, 6, width);
            const auto rows = visibleRows(f);
            int gw = std::min(gutterWidth((int)doc1000.size()), width);
            for (int row = 0; row < 6; ++row) {
                int docLine = top + row + 1;
                std::string numStr = std::to_string(docLine);
                int maxNumCols = std::max(0, gw - 1);
                if ((int)numStr.size() > maxNumCols)
                    numStr = numStr.substr(numStr.size() - (size_t)maxNumCols);
                int pad = std::max(0, gw - 1 - (int)numStr.size());
                std::string expectedGutter = std::string(pad, ' ') + numStr + ' ';
                std::string actualGutter = rows[row].size() >= (size_t)gw ? rows[row].substr(0, gw) : rows[row];
                CHECK_EQ(actualGutter, expectedGutter);
                CHECK(colWidth(rows[row]) <= width);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// BufferSelector y FileBrowser (pantallas de lista), para todos los tamanos
// de lista y anchos de terminal, incluidas las muy estrechas.
// ---------------------------------------------------------------------------
TEST(regression_list_selector_sizes) {
    for (int n : {0, 1, 5, 22, 100}) {
        for (int width : {5, 20, 80}) {
            for (int content : {5, 22}) {
                std::string f = frameBuffer(n, content, width);
                checkFrameWithinBounds(f, content, width);
            }
        }
    }
}

TEST(regression_list_filebrowser_sizes) {
    for (int n : {0, 1, 5, 22, 100}) {
        for (int width : {5, 20, 80}) {
            for (int content : {5, 22}) {
                std::string f = frameFile(n, content, width);
                checkFrameWithinBounds(f, content, width);
            }
        }
    }
}
