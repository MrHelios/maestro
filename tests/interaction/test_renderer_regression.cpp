// Tests de regresion del renderer (paso 11). Cubren las combinaciones de
// tamano de documento y de terminal que la suite existente no recorre de
// forma sistematica, y verifican el invariante central del cambio visual
// v1.1: NINGUNA fila visible del frame (contenido o barra) escribe fuera del
// ancho de la terminal, y la fila fija de la barra ocupa EXACTAMENTE el
// ancho. Se prueban primariamente sobre el Editor (buildScreen) y sobre las
// pantallas de lista (BufferSelector/FileBrowser) "donde aplique".
//
// Casos cubiertos:
//   - tamano del documento: vazio, 1, 9, 10, 99, 100, 999 y 1000 lineas
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
#include <string>
#include <vector>

#include "test_framework.h"

#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Layout.h"
#include "core/Viewport.h"
#include "ui/Message.h"
#include "ui/Renderer.h"

namespace {

std::string stripAnsiLocal(const std::string& s) {
    std::string out;
    bool inEsc = false;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\x1b') {
            inEsc = true;
            if (i + 1 < s.size() && s[i + 1] == '[') i++;
        } else if (inEsc) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c >= 0x40 && c <= 0x7E) inEsc = false;
        } else {
            out += s[i];
        }
        i++;
    }
    return out;
}

int colWidthLocal(const std::string& s) {
    int col = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) col++;
    return col;
}

bool validUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int need;
        if ((c & 0x80) == 0) need = 0;
        else if ((c & 0xE0) == 0xC0) need = 1;
        else if ((c & 0xF0) == 0xE0) need = 2;
        else if ((c & 0xF8) == 0xF0) need = 3;
        else return false;
        if (i + static_cast<size_t>(need) >= s.size()) return false;
        for (int k = 1; k <= need; ++k)
            if ((static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0xC0) != 0x80)
                return false;
        i += static_cast<size_t>(need) + 1;
    }
    return true;
}

// Divide el frame en filas visibles, ya sin ANSI ni \r.
std::vector<std::string> visibleRows(const std::string& frame) {
    std::string plain = stripAnsiLocal(frame);
    std::vector<std::string> out;
    std::string cur;
    for (char c : plain) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

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
        CHECK(colWidthLocal(r) <= width);
        CHECK(validUtf8(r));
    }

    // 3/4. La fila fija llena exactamente el ancho; la de mensajes no lo pasa.
    const int barRow = static_cast<int>(rows.size()) - kStatusBarRows;
    CHECK_EQ(colWidthLocal(rows[barRow]), width);
    CHECK(colWidthLocal(rows[barRow]) <= width);
    CHECK(colWidthLocal(rows[static_cast<int>(rows.size()) - 1]) <= width);
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
    // visual (y por tanto la cota de ancho) no debe cambiar.
    const std::vector<std::string> lines = {"hello world", "segunda"};
    Selection fwd; fwd.anchor = Position{0, 0}; fwd.position = Position{0, 5};
    Selection rev; rev.anchor = Position{0, 5}; rev.position = Position{0, 0};
    std::string a = frameEditor(lines, 6, 40, fwd);
    std::string b = frameEditor(lines, 6, 40, rev);
    checkFrameWithinBounds(a, 6, 40);
    checkFrameWithinBounds(b, 6, 40);
    CHECK_EQ(stripAnsiLocal(a), stripAnsiLocal(b));
}

// ---------------------------------------------------------------------------
// Contenido UTF-8: acentos, guiones largos y emoji mezclados. El gutter se
// resta del ancho, asi que el recorte de columnas no debe partir multibyte.
// ---------------------------------------------------------------------------
TEST(regression_utf8_content) {
    // NOTA: los literales con secuencias hex seguidas de [a-f0-9] se
    // dividen en trozos contiguos para que \x no "coma" los digitos hex
    // del texto (greedy): "mañana" = "ma\xc3\xb1" + "ana", etc.
    const std::vector<std::string> lines = {
        "caf\xc3\xa9 y \xe2\x80\x94 guion",
        "ma\xc3\xb1" "ana \xf0\x9f\x98\x80 emoji",
        "abc\xe2\x80\x94" "def",
        "\xc3\xa9\xc3\xa9\xc3\xa9",
    };
    for (int width : {10, 15, 25, 60}) {
        std::string f = frameEditor(lines, 6, width);
        checkFrameWithinBounds(f, 6, width);
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
// ---------------------------------------------------------------------------
TEST(regression_gutter_clamped_at_narrow_width) {
    std::vector<std::string> doc;
    for (int i = 0; i < 100; ++i) doc.push_back("linea " + std::to_string(i + 1));
    // Ancho 4: el gutter minimo (3) cabe entero y deja 1 columna de texto.
    for (int width = 4; width <= 8; ++width) {
        std::string f = frameEditor(doc, 6, width);
        const auto rows = visibleRows(f);
        // El contenido (filas 0..content-1) nunca excede el ancho.
        for (int row = 0; row < 6; ++row)
            CHECK(colWidthLocal(rows[row]) <= width);
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
