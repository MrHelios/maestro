#include <algorithm>
#include <string>

#include "test_framework.h"

// Probamos buildScreen(), que es la parte pura del renderer: construye
// el frame ANSI completo sin tocar la terminal. Para montar un frame
// usamos construimos un Document/Cursor/Viewport de nivel bajo.
#include "ui/Renderer.h"
#include "ui/Editor.h"

// Secuencias ANSI usadas por el renderer.
#define ANSI_INV "\x1b[48;5;60m"   // seleccion azul grisáceo (kSelectionStyle)
#define ANSI_RESET "\x1b[0m" // reset de estilo

namespace {

// MEJORA 1: Unificar funciones duplicadas. Una sola versión robusta.
std::string stripAnsi(const std::string& s) {
    std::string out;
    bool inEsc = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b') {
            inEsc = true;
            if (i + 1 < s.size() && s[i + 1] == '[') i++;
        } else if (inEsc) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c >= 0x40 && c <= 0x7E) inEsc = false; // Fin de secuencia CSI
        } else {
            out += s[i];
        }
    }
    return out;
}

// Columnas visuales simplificadas: 1 por code point UTF-8.
// Para los tests del renderer, cada carácter UTF-8 cuenta como una columna.
int colWidth(const std::string& s) {
    int col = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++col;
    return col;
}

} // namespace

namespace {

// Monta un frame con el contenido dado y devuelve la secuencia ANSI.
// `sel` opcional; si es std::nullopt, sin seleccion.
std::string frame(const std::vector<std::string>& lines,
                  const std::optional<Selection>& sel) {
    Document doc;
    doc.restore(lines);

    Viewport viewport;
    viewport.top = 0;
    viewport.height = static_cast<int>(lines.size()) + 1;
    viewport.width = 200;

    Cursor cursor;
    cursor.line = 0;
    cursor.col = 0;

    Renderer r;
    return r.buildScreen(doc, cursor, viewport, "test.txt", false, "",
                         State::Navegacion, sel);
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

Selection selAt(Position a, Position p) {
    Selection s;
    s.anchor = a;
    s.position = p;
    return s;
}

// Monta un frame de una unica linea con el cursor en `byteCol` (offset
// de BYTES, como lo modela Document/Cursor) y devuelve la secuencia ANSI.
std::string curFrame(const std::string& line, int byteCol) {
    Document doc;
    doc.restore({line});

    Viewport viewport;
    viewport.top = 0;
    viewport.height = 1;
    viewport.width = 200;

    Cursor cursor;
    cursor.line = 0;
    cursor.col = byteCol;

    Renderer r;
    return r.buildScreen(doc, cursor, viewport, "test.txt", false, "",
                         State::Navegacion, std::nullopt);
}

// Ancho del gutter de numeros de linea (mismo calculo que el Renderer).
int gutterWFor(int totalLines) {
    int digits = 1;
    for (int n = totalLines; n >= 10; n /= 10) ++digits;
    return std::max(3, digits + 1);
}

// Extrae la columna VISUAL del texto a la que el renderer mueve el cursor
// al final del frame. La secuencia es "\x1b[<fila>;<col>H" (fila 1 en un
// frame de una sola linea); de esa columna de terminal se resta el gutter
// de numeros de linea (3 para un frame de 1 linea), porque el texto ya no
// arranca en la columna 1 de la terminal. Devuelve -1 si no la encuentra.
int cursorVisibleCol(const std::string& frame) {
    size_t pos = frame.rfind("\x1b[1;");
    if (pos == std::string::npos) return -1;
    size_t end = frame.find('H', pos);
    if (end == std::string::npos) return -1;

    int terminalCol = std::stoi(frame.substr(pos + 4, end - pos - 4));
    return terminalCol - gutterWFor(1);
}

} // namespace

// ---------------------------------------------------------------------------
// Paso 9: renderizado visual de la seleccion
// ---------------------------------------------------------------------------
// El renderer marca el texto seleccionado con video inverso
// (\x1b[7m ... \x1b[0m). La direccion de la seleccion (hacia adelante o
// hacia atras) NO debe alterar el resultado visual.
TEST(renderer_no_selection) {
    std::string out = frame({"hello world"}, std::nullopt);

    // Sin seleccion no debe haber video inverso envolviendo al texto
    // del documento. (La barra de estado siempre usa video inverso,
    // por eso comprobamos que el texto en si no este "invertido".)
    CHECK(!contains(out, ANSI_INV "hello"));
    CHECK(contains(out, "hello world"));
}

TEST(renderer_single_line_selection) {
    // Seleccion [0,5) de "hello world" -> "hello" en video inverso.
    std::string out = frame({"hello world"}, selAt({0, 0}, {0, 5}));

    CHECK(contains(out, ANSI_INV "hello" ANSI_RESET));
    CHECK(contains(out, " world"));
}

TEST(renderer_multiline_selection) {
    // Seleccion desde (0,0) hasta (2,2) sobre:
    //   hello
    //   world
    //   foo
    // Debe invertir "hello", "world" y "fo".
    std::string out = frame({"hello", "world", "foo"}, selAt({0, 0}, {2, 2}));

    CHECK(contains(out, ANSI_INV "hello" ANSI_RESET));
    CHECK(contains(out, ANSI_INV "world" ANSI_RESET));
    CHECK(contains(out, ANSI_INV "fo" ANSI_RESET));
    CHECK(contains(out, "o\r\n")); // el resto de "foo" sin invertir
}

TEST(renderer_selection_reverse_direction) {
    // Mismo rango pero seleccionado "hacia atras" (cursor < anchor).
    std::string out = frame({"hello world"}, selAt({0, 5}, {0, 0}));

    // El resultado visual debe ser identico al de una seleccion hacia
    // delante: el renderer normaliza antes de dibujar.
    CHECK(contains(out, ANSI_INV "hello" ANSI_RESET));
    CHECK(contains(out, " world"));
}

TEST(renderer_empty_selection) {
    // anchor == position: no hay texto seleccionado.
    std::string out = frame({"hello world"}, selAt({0, 2}, {0, 2}));

    CHECK(!contains(out, ANSI_INV "hello"));
    CHECK(contains(out, "hello world"));
}

TEST(renderer_selection_partial_line_edges) {
    // Seleccion dentro de una linea: solo la parte media queda invertida.
    std::string out = frame({"hello world"}, selAt({0, 6}, {0, 9}));

    CHECK(!contains(out, ANSI_INV "hello"));
    CHECK(contains(out, ANSI_INV "wor" ANSI_RESET));
    CHECK(contains(out, "hello "));
    CHECK(contains(out, "ld"));
}

TEST(renderer_selection_clamps_to_line_end) {
    // Seleccion que termina mas alla del largo de la linea: se achica
    // al final real de la linea sin corromper el render.
    std::string out = frame({"abc"}, selAt({0, 1}, {0, 99}));

    CHECK(contains(out, ANSI_INV "bc" ANSI_RESET));
    CHECK(contains(out, "a"));
}

// ---------------------------------------------------------------------------
// Cursor con UTF-8: el cursor se posiciona usando la COLUMNA VISUAL, no
// el offset de bytes. Para "café" (5 bytes pero 4 columnas visuales,
// c a f é -> 0 1 2 3) mover el cursor despues de un caracter multibyte
// (é) debe ir a la columna 4 (posicion tras é), NUNCA a la 5.
// ---------------------------------------------------------------------------
TEST(cursor_utf8_start) {
    // Al comienzo (byte 0): columna visual 0 -> secuencia 1;1H.
    CHECK_EQ(cursorVisibleCol(curFrame("caf\xc3\xa9", 0)), 1);
}

TEST(cursor_utf8_after_c) {
    // Despues de 'c' (byte 1): visual 1 -> 1;2H.
    CHECK_EQ(cursorVisibleCol(curFrame("caf\xc3\xa9", 1)), 2);
}

TEST(cursor_utf8_after_a) {
    // Despues de 'a' (byte 2): visual 2 -> 1;3H.
    CHECK_EQ(cursorVisibleCol(curFrame("caf\xc3\xa9", 2)), 3);
}

TEST(cursor_utf8_after_f) {
    // Despues de 'f' (byte 3, al inicio del acento): visual 3 -> 1;4H.
    CHECK_EQ(cursorVisibleCol(curFrame("caf\xc3\xa9", 3)), 4);
}

TEST(cursor_utf8_after_e) {
    // Despues de 'é' (byte 5, fin de la linea): visual 4 -> 1;5H.
    // Es lo que NO se debe ver si se usara el offset de bytes (que daria 6).
    CHECK_EQ(cursorVisibleCol(curFrame("caf\xc3\xa9", 5)), 5);
}

TEST(cursor_utf8_end) {
    // Al final (byte 5): misma posicion que tras 'é', columna visual 4.
    CHECK_EQ(cursorVisibleCol(curFrame("caf\xc3\xa9", 5)), 5);
}

// ---------------------------------------------------------------------------
// Cursor con caracter UTF-8 de 3 bytes ("—", em dash). Bytes:
//   a b c — — — d e f      (9 bytes, 7 columnas visuales)
//   0 1 2 3 4 5 6 7 8
// La columna visual ignora los 2 bytes de continuacion del dash.
// ---------------------------------------------------------------------------
TEST(cursor_threebyte_before_dash) {
    // Antes de '—' (despues de 'c', byte 3): visual 3 -> 1;4H.
    CHECK_EQ(cursorVisibleCol(curFrame("abc\xe2\x80\x94" "def", 3)), 4);
}

TEST(cursor_threebyte_after_dash) {
    // Despues de '—' (byte 6): visual 4 -> 1;5H.
    // Con offset de bytes daria 7; el renderer debe usar la columna visual.
    CHECK_EQ(cursorVisibleCol(curFrame("abc\xe2\x80\x94" "def", 6)), 5);
}

TEST(cursor_threebyte_after_several_chars) {
    // Despues de "abc—d" (byte 7): visual 5 -> 1;6H.
    CHECK_EQ(cursorVisibleCol(curFrame("abc\xe2\x80\x94" "def", 7)), 6);
}

TEST(cursor_threebyte_end) {
    // Al final (byte 9): visual 7 -> 1;8H.
    CHECK_EQ(cursorVisibleCol(curFrame("abc\xe2\x80\x94" "def", 9)), 8);
}

// ---------------------------------------------------------------------------
// Cursor con caracter UTF-8 de 4 bytes ("😀", emoji). Bytes:
//   a b c 😀😀😀😀 d e f     (10 bytes, 7 columnas visuales)
//   0 1 2 3 4 5 6 7 8 9
// Es el caso que mejor delata a una implementacion que confunde bytes
// con columnas: tras el emoji hay un salto de 4 bytes pero de 1 columna.
// ---------------------------------------------------------------------------
TEST(cursor_fourbyte_before_emoji) {
    // Antes de '😀' (despues de 'c', byte 3): visual 3 -> 1;4H.
    CHECK_EQ(cursorVisibleCol(curFrame("abc\xf0\x9f\x98\x80" "def", 3)), 4);
}

TEST(cursor_fourbyte_after_emoji) {
    // Despues de '😀' (byte 7): visual 4 -> 1;5H.
    // Si se usara el offset de bytes se iria a la 8, señal de confundir
    // bytes con columnas.
    CHECK_EQ(cursorVisibleCol(curFrame("abc\xf0\x9f\x98\x80" "def", 7)), 5);
}

TEST(cursor_fourbyte_end) {
    // Al final (byte 10): visual 7 -> 1;8H.
    CHECK_EQ(cursorVisibleCol(curFrame("abc\xf0\x9f\x98\x80" "def", 10)), 8);
}

// ---------------------------------------------------------------------------
// Truncado horizontal: cuando la terminal es mas angosta que la linea, el
// renderer recorta por COLUMNAS VISUALES. Condicion fundamental: nunca debe
// cortar un caracter UTF-8 por la mitad (siempre produce UTF-8 valido).
// ---------------------------------------------------------------------------
namespace {

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

// Fila del documento (la primera, antes del primer \r\n) ya sin ANSI y con
// el gutter de numeros de linea restado. `lineCount` es el numero de lineas
// del documento con que se (re)calcula el ancho del gutter.
std::string rowText(const std::string& frame, int lineCount) {
    std::string plain = stripAnsi(frame);
    size_t nl = plain.find("\r\n");
    if (nl == std::string::npos) nl = plain.size();
    std::string row = plain.substr(0, nl);
    int gw = gutterWFor(lineCount);
    row.erase(0, std::min(gw, static_cast<int>(row.size())));
    return row;
}

// Renderiza la fila de un documento de 2 lineas (el cursor va en la linea
// 2, asi la fila probada no lleva resaltado de "fila actual") con `cols`
// columnas de ANCHO DE TEXTO (el gutter se resta del viewport) y devuelve
// solo el TEXTO visible, sin ANSI y sin el gutter.
std::string textRow(const std::string& line, int cols) {
    Document doc; doc.restore({line, ""});
    int gw = gutterWFor(2);
    Viewport vp; vp.top = 0; vp.height = 1; vp.width = gw + cols;
    Cursor cur; cur.line = 1; cur.col = 0;
    Renderer r;
    std::string f = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt);
    return rowText(f, 2);
}

} // namespace

TEST(truncate_line_to_ascii_width) {
    // "abcdef" recortado a 3 columnas.
    CHECK_EQ(textRow("abcdef", 3), "abc");
    CHECK(validUtf8(textRow("abcdef", 3)));
}

TEST(truncate_cafe_just_before_e) {
    // "café" con ancho 3: termina justo antes del acento. No debe dejar
    // un byte suelto del acento.
    CHECK_EQ(textRow("caf\xc3\xa9", 3), "caf");
    CHECK(validUtf8(textRow("caf\xc3\xa9", 3)));
}

TEST(truncate_cafe_includes_e) {
    // "café" con ancho 4: el acento entra completo, 2 bytes intactos.
    CHECK_EQ(textRow("caf\xc3\xa9", 4), "caf\xc3\xa9");
    CHECK(validUtf8(textRow("caf\xc3\xa9", 4)));
}

TEST(truncate_dash_around_em_dash) {
    // "abc—def" con anchos alrededor del "—": cols caben contiguos o el
    // corte queda en limite de caracter, nunca partido.
    struct { int w; const char* expect; } cases[] = {
        {3, "abc"},
        {4, "abc\xe2\x80\x94" ""}, // justo despues del "—"
        {5, "abc\xe2\x80\x94" "d"},
        {6, "abc\xe2\x80\x94" "de"},
    };
    for (const auto& cs : cases) {
        CHECK(validUtf8(textRow("abc\xe2\x80\x94" "def", cs.w)));
        CHECK_EQ(textRow("abc\xe2\x80\x94" "def", cs.w), cs.expect);
    }
}

TEST(truncate_emoji_around) {
    // "abc😀def" con anchos alrededor del "😀".
    struct { int w; const char* expect; } cases[] = {
        {3, "abc"},
        {4, "abc\xf0\x9f\x98\x80" ""}, // justo despues del emoji
        {5, "abc\xf0\x9f\x98\x80" "d"},
        {6, "abc\xf0\x9f\x98\x80" "de"},
    };
    for (const auto& cs : cases) {
        CHECK(validUtf8(textRow("abc\xf0\x9f\x98\x80" "def", cs.w)));
        CHECK_EQ(textRow("abc\xf0\x9f\x98\x80" "def", cs.w), cs.expect);
    }
}

// Invariante global: para cualquier ancho, recortar la linea nunca
// produce UTF-8 invalido y el resultado respeta el limite de columnas.
TEST(truncate_never_splits_multibyte_any_width) {
    const std::string lines[] = {
        "abcdef",
        "caf\xc3\xa9",
        "abc\xe2\x80\x94" "def",
        "abc\xf0\x9f\x98\x80" "def",
    };
    for (const std::string& line : lines) {
        const int total = colWidth(line);
        for (int w = 1; w <= total + 3; ++w) {
            std::string row = textRow(line, w);
            CHECK(validUtf8(row));
            CHECK(colWidth(row) <= w);
        }
    }
}

// ---------------------------------------------------------------------------
// Seleccion de texto UTF-8: los offsets de bytes del `Selection` (anchor/
// position.col) se traducen a COLUMNAS VISUALES antes de pintar, de forma
// que el bloque en video inverso sea exactamente el caracter (o grupo de
// caracteres) pedido, sin partir ningun multibyte.
// ---------------------------------------------------------------------------
namespace {
const char* cafe  = "caf\xc3\xa9";
const char* dash  = "abc\xe2\x80\x94" "def";
const char* emoji = "abc\xf0\x9f\x98\x80" "def";
} // namespace

TEST(selection_utf8_cafe_c) {
    std::string out = frame({cafe}, selAt({0, 0}, {0, 1}));
    CHECK(contains(out, ANSI_INV "c" ANSI_RESET));
    // El resto ("afé") queda sin invertir.
    CHECK(!contains(out, ANSI_INV "f"));
}

TEST(selection_utf8_cafe_e) {
    std::string out = frame({cafe}, selAt({0, 3}, {0, 5}));
    CHECK(contains(out, ANSI_INV "\xc3\xa9" ANSI_RESET));
    CHECK(!contains(out, ANSI_INV "a"));
}

TEST(selection_utf8_cafe_fe) {
    std::string out = frame({cafe}, selAt({0, 2}, {0, 5}));
    CHECK(contains(out, ANSI_INV "f\xc3\xa9" ANSI_RESET));
    CHECK(!contains(out, ANSI_INV "a\xc3\xa9"));
}

TEST(selection_utf8_cafe_all) {
    std::string out = frame({cafe}, selAt({0, 0}, {0, 5}));
    CHECK(contains(out, ANSI_INV "caf\xc3\xa9" ANSI_RESET));
}

TEST(selection_utf8_dash_only) {
    // Solo el "—" (bytes 3..5, una columna visual 3).
    std::string out = frame({dash}, selAt({0, 3}, {0, 6}));
    CHECK(contains(out, ANSI_INV "\xe2\x80\x94" ANSI_RESET));
    CHECK(!contains(out, ANSI_INV "c\xe2\x80\x94"));
}

TEST(selection_utf8_dash_cdashd) {
    // "c—d": empieza en 'c' (byte 2, col 2) y termina tras 'd' (byte 7, col 5).
    std::string out = frame({dash}, selAt({0, 2}, {0, 7}));
    CHECK(contains(out, ANSI_INV "c\xe2\x80\x94" "d" ANSI_RESET));
}

TEST(selection_utf8_dash_across) {
    // Desde antes del "—" hasta despues: empieza al inicio (byte 0) y
    // termina tras el "—" (byte 6, col 4).
    std::string out = frame({dash}, selAt({0, 0}, {0, 6}));
    CHECK(contains(out, ANSI_INV "abc\xe2\x80\x94" ANSI_RESET));
}

TEST(selection_utf8_emoji_only) {
    // Solamente el "😀" (bytes 3..6, una columna visual 3).
    std::string out = frame({emoji}, selAt({0, 3}, {0, 7}));
    CHECK(contains(out, ANSI_INV "\xf0\x9f\x98\x80" ANSI_RESET));
    CHECK(!contains(out, ANSI_INV "c\xf0\x9f\x98\x80"));
}

// ---------------------------------------------------------------------------
// Seleccion UTF-8 en direccion inversa: seleccionar hacia atras (cursor <
// anchor) debe producir EXACTAMENTE el mismo rango visual que hacerlo hacia
// adelante. El renderer normaliza la seleccion (start <= end) antes de
// traducir los offsets de bytes a columnas visuales.
// ---------------------------------------------------------------------------
namespace {
// "abcédef": a b c é d e f  (é = \xc3\xa9). 7 cols, 8 bytes.
// "éde" ocupa los bytes [3,7) y las columnas visuales 3..6.
const char* abcedef = "abc\xc3\xa9" "def";
} // namespace

TEST(selection_utf8_reverse_same_as_forward) {
    // Hacia adelante: anchor=byte 3 (tras 'c'), position=byte 7 (fin).
    std::string fwd = frame({abcedef}, selAt({0, 3}, {0, 7}));
    // Hacia atras: misma seleccion, cursor a la izquierda del anchor.
    std::string rev = frame({abcedef}, selAt({0, 7}, {0, 3}));

    // Ambos deben invertir exactamente "éde".
    CHECK(contains(fwd, ANSI_INV "\xc3\xa9" "de" ANSI_RESET));
    CHECK(contains(rev, ANSI_INV "\xc3\xa9" "de" ANSI_RESET));
    // Y el resultado visual completo debe ser identico.
    CHECK_EQ(fwd, rev);
}

TEST(selection_utf8_reverse_inverts_whole_multibyte) {
    // Seleccionar hacia atras "éde" completo no debe partir el acento: el
    // bloque invertido empieza en el lead byte y lleva el "é" entero.
    std::string rev = frame({abcedef}, selAt({0, 7}, {0, 3}));
    // El bloque invertido es exactamente "éde" (los 2 bytes del "é" y "de").
    CHECK(contains(rev, ANSI_INV "\xc3\xa9" "de" ANSI_RESET));
    // No debe invertir un byte suelto del "é" (p.ej. solo el continuacion).
    CHECK(!contains(rev, ANSI_INV "\xa9" "de" ANSI_RESET));
}

// ---------------------------------------------------------------------------
// Seleccion MULTILINEA con UTF-8. Documento de 3 lineas:
//   l0: "hola café"
//   l1: "mañana — test"     (ñ y — son UTF-8)
//   l2: "😀 mundo"          (😀 es UTF-8 de 4 bytes)
// ---------------------------------------------------------------------------
TEST(selection_multiline_utf8_within_one_line) {
    // Seleccion dentro de una sola linea: solo "café" en l0 queda invertido.
    std::vector<std::string> lines = {"hola café",
                                      "mañana — test",
                                      "😀 mundo"};
    std::string out = frame(lines, selAt({0, 5}, {0, 10}));
    CHECK(contains(out, ANSI_INV "café" ANSI_RESET));
    // "hola " queda sin invertir.
    CHECK(!contains(out, ANSI_INV "hola "));
}

TEST(selection_multiline_utf8_from_ascii_to_utf8) {
    // Desde el inicio de l0 (ASCII) hasta "mañana —" en l1 (termina en el
    // guion UTF-8). l0 entera queda invertida; l1 hasta el "—".
    std::vector<std::string> lines = {"hola café",
                                      "mañana — test",
                                      "unused"};
    std::string out = frame(lines, selAt({0, 0}, {1, 10}));
    CHECK(contains(out, ANSI_INV "hola café" ANSI_RESET));
    CHECK(contains(out, ANSI_INV "mañana —" ANSI_RESET));
}

TEST(selection_multiline_utf8_utf8_line_to_another) {
    // Desde "café" (l0) pasando por toda l1 hasta "😀" (l2): atraviesa
    // café, ñ, — y 😀 en varias lineas.
    std::vector<std::string> lines = {"hola café",
                                      "mañana — test",
                                      "😀 mundo"};
    std::string out = frame(lines, selAt({0, 5}, {2, 4}));
    // l0: desde "café" al final.
    CHECK(contains(out, ANSI_INV "café" ANSI_RESET));
    // l1: linea intermedia, entera.
    CHECK(contains(out, ANSI_INV "mañana — test" ANSI_RESET));
    // l2: solo "😀", dejando " mundo" sin invertir.
    CHECK(contains(out, ANSI_INV "😀" ANSI_RESET));
    CHECK(!contains(out, ANSI_INV "😀 mundo"));
}

// ---------------------------------------------------------------------------
// Seleccion MULTILINEA que empieza en MITAD de la primera linea y termina en
// MITAD de la tercera. Es el caso mas propenso a errores de offset
// byte/columna: los cortes de cada linea estan pegados a caracteres UTF-8
// (é, ñ, —). Documento:
//   l0: "café"      (é = \xc3\xa9, 5 bytes / 4 cols)
//   l1: "mañana"    (ñ = \xc3\xb1, 6 bytes / 5 cols)
//   l2: "— mundo"   (— = \xe2\x80\x94, 9 bytes / 7 cols)
// Seleccion de (l0,byte 1) a (l2,byte 5): empieza tras la 'c' de "café" y
// termina tras "m" de "mundo" (a medio camino de la tercera linea).
//   bytes:  c a f é         -> [1,5) = "afé"
//   mañana                  -> linea intermedia, completa
//   — m u n d o             -> [0,5) = "— m"
// ---------------------------------------------------------------------------
TEST(selection_multiline_utf8_mid_first_to_mid_third) {
    const std::vector<std::string> lines = {"café",
                                            "mañana",
                                            "— mundo"};
    std::string out = frame(lines, selAt({0, 1}, {2, 5}));

    // l0: la 'c' queda sin invertir (pero con el estilo de "fila actual",
    // que se cierra con reset antes del bloque invertido), "afé" (bytes
    // 1..5, incl. é) invertido.
    CHECK(contains(out, "c" ANSI_RESET ANSI_INV "afé" ANSI_RESET));
    CHECK(!contains(out, ANSI_INV "café"));
    // l1: linea intermedia invertida entera.
    CHECK(contains(out, ANSI_INV "mañana" ANSI_RESET));
    // l2: "— m" (bytes 0..4, incl. — de 3 bytes) invertido, "undo" no.
    CHECK(contains(out, ANSI_INV "— m" ANSI_RESET));
    CHECK(contains(out, "undo"));
    CHECK(!contains(out, ANSI_INV "— mundo"));
}

TEST(selection_multiline_utf8_mid_first_to_mid_third_reverse) {
    // Misma seleccion "hacia atras" (cursor < anchor): el render normaliza
    // y el resultado visual es identico al de la direccion adelante.
    const std::vector<std::string> lines = {"café",
                                            "mañana",
                                            "— mundo"};
    std::string fwd = frame(lines, selAt({0, 1}, {2, 5}));
    std::string rev = frame(lines, selAt({2, 5}, {0, 1}));
    CHECK_EQ(fwd, rev);
}

// ---------------------------------------------------------------------------
// v0.4: barra de estado de dos filas
// ---------------------------------------------------------------------------
namespace {

// Monta un frame solo con la barra de estado, controlando la ruta, el
// estado de guardado, el estado de la maquina de estados, el mensaje y
// el ancho de la terminal. El documento es una unica linea vacia.
std::string barFrame(const std::string& file, bool modified,
                     const std::string& msg, State state, int width) {
    Document doc;
    doc.restore({""});

    Viewport v;
    v.top = 0;
    v.height = 1;
    v.width = width;

    Cursor c;
    c.line = 0;
    c.col = 0;

    Renderer r;
    return r.buildScreen(doc, c, v, file, modified, msg, state, std::nullopt);
}

} // namespace

TEST(statusbar_two_rows_present) {
    // Debe haber exactamente una fila fija (fondo gris 60%) y una fila de
    // mensajes (sin estilo). El texto del mensaje no debe estar estilizado.
    std::string out = barFrame("/a/b.txt", false, "hola", State::Navegacion, 200);
    CHECK(contains(out, kStatusBarStyle));
    CHECK(contains(out, "hola"));
    CHECK(!contains(out, std::string(kStatusBarStyle) + "hola"));
}

TEST(statusbar_left_format_name_path_estado) {
    // <nombre> - <ruta> - <ESTADO>, en ese orden. Se compara sobre el texto
    // plano (stripAnsi): los fragmentos llevan colores distintos
    // (nombre blanco, estado dorado) que rompen la contiguedad cruda.
    std::string out = barFrame("/home/alice/proyecto/odo.txt", false, "",
                               State::Navegacion, 200);
    CHECK(contains(stripAnsi(out), "odo.txt - /home/alice/proyecto - NAVEGACION"));
}

TEST(statusbar_modified_indicator) {
    // Un cambio sin guardar agrega [modificado] junto al nombre.
    std::string out = barFrame("/home/a/x.cc", true, "", State::Navegacion, 200);
    CHECK(contains(stripAnsi(out), "x.cc [modificado] - /home/a - NAVEGACION"));
}

TEST(statusbar_modified_indicator_survives_long_name) {
    // Un nombre de archivo largo + modified=true: el sufijo [modificado]
    // se RESERVA entero y NO debe perderse ni cortarse. La parte del
    // nombre es la que cede (se trunca), nunca el indicador, para que el
    // usuario siempre sepa que hay cambios sin guardar.
    const std::string nombre = "un_archivo_muy_muy_largo_para_verificar_"
                               "el_indicador_de_modificado_en_la_barra.txt";
    std::string out = barFrame("/dir/" + nombre, true, "", State::Navegacion, 200);

    // El indicador aparece completo (nunca cortado a "modificad").
    CHECK(contains(out, "[modificado]"));
    // Se sacrifica el final del nombre, no el marcador.
    CHECK(!contains(out, "modificad" ANSI_RESET));

    // Con el mismo nombre pero sin cambios, el indicador no debe estar.
    std::string limpio = barFrame("/dir/" + nombre, false, "", State::Navegacion, 200);
    CHECK(!contains(limpio, "[modificado]"));
}

namespace {

// Anchura VISIBLE de la fila de la barra de estado (la 2da fila, tras
// la unica fila de documento del frame armado con barFrame).
int barVisibleCols(const std::string& frame) {
    std::string plain = stripAnsi(frame);
    // Las filas estan separadas por \r\n; la barra es la segunda.
    size_t start = plain.find("\r\n");
    if (start == std::string::npos) return colWidth(plain);
    size_t end = plain.find('\r', start + 2);
    return colWidth(plain.substr(start + 2, end - start - 2));
}

} // namespace

TEST(statusbar_label_fills_whole_width_edge) {
    // Caso extremo de la revision: ancho de terminal apenas mayor que la
    // etiqueta de estado (Seleccion -> "SELECCION", 9 columnas). Ahi
    // partsBudget = budget - estadoW - sep podia volverse negativo y el
    // bloque final (" - SELECCION") exceder viewport.width. El bloque
    // Ln/Col fijo a la derecha tambien exige ancho: por eso recursemos la
    // ventana de anchos donde el presupuesto izquierdo queda en esa banda.
    for (int width = 15; width <= 40; ++width) {
        std::string out = barFrame("/a/archivo.txt", false, "",
                                   State::Seleccion, width);
        int barW = barVisibleCols(out);
        CHECK(barW <= width); // nunca debe desbordar el ancho de la terminal
    }
}

TEST(statusbar_label_fills_whole_width_edge_normal) {
    // Mismo chequeo con NAVEGACION (10 cols), cuya banda desbordada cae en
    // anchos un poco menores.
    for (int width = 15; width <= 34; ++width) {
        std::string out = barFrame("/a/archivo.txt", false, "",
                                   State::Navegacion, width);
        int barW = barVisibleCols(out);
        CHECK(barW <= width);
    }
}

TEST(statusbar_state_labels) {
    // SELECCION y COMANDO se mapean 1 a 1 con los estados Seleccion y Prefix.
    std::string sel = barFrame("/a.txt", false, "", State::Seleccion, 200);
    CHECK(contains(stripAnsi(sel), "a.txt - / - SELECCION"));

    std::string pre = barFrame("/a.txt", false, "", State::Prefix, 200);
    CHECK(contains(stripAnsi(pre), "a.txt - / - COMANDO"));
}

TEST(statusbar_right_block_always_visible) {
    // (fila,columna) y el porcentaje de altura siempre se muestran, anclados
    // a la derecha, incluso en una terminal estrecha: no es un derrota del
    // sacrificio. Con un documento de una sola linea, la posicion del cursor
    // (linea 0) es 0% del archivo.
    std::string out = barFrame(
        "/data/muy/largo/dir/de/archivos/nombre.txt", false, "",
        State::Navegacion, 20);
    CHECK(contains(out, "(1,1)"));
    CHECK(contains(stripAnsi(out), "0%"));
}

TEST(statusbar_cursor_percentage_position) {
    // El bloque derecho conserva (fila,columna) y agrega la posicion vertical
    // del cursor como porcentaje del archivo: 0% en la primera linea, 100% en
    // la ultima. En un doc de 6 lineas, la linea 2 esta al 40% (2 / (6-1)).
    const int lines = 6;
    for (int cursorLine : {0, 2, 5}) {
        Document doc;
        doc.restore(std::vector<std::string>(static_cast<size_t>(lines), "x"));
        Viewport v;
        v.top = 0;
        v.height = 1;
        v.width = 200;
        Cursor c;
        c.line = cursorLine;
        c.col = 0;
        Renderer r;
        int expected = (cursorLine * 100) / (lines - 1);
        std::string out = r.buildScreen(doc, c, v, "/a.txt", false, "",
                                        State::Navegacion, std::nullopt);
        CHECK(contains(stripAnsi(out), std::to_string(expected) + "%"));
    }
}

TEST(statusbar_path_sacrificed_before_name) {
    // Terminal no muy ancha: la ruta cede con "..." al inicio, el nombre
    // y los bloques (linea,col) / % se mantienen enteros.
    std::string out = barFrame(
        "/a/very/long/directory/chain/for/the/path/iz/archivo.txt",
        false, "", State::Navegacion, 55);
    CHECK(contains(out, "archivo.txt"));   // nombre intacto
    CHECK(contains(out, "..."));           // la ruta se corto con "..." al inicio
    CHECK(contains(out, "NAVEGACION"));        // etiqueta de estado intacta
    CHECK(contains(out, "(1,1)"));
}

TEST(statusbar_path_uses_rest_after_name_reserved) {
    // El nombre se reserva primero; la ruta toma SOLO lo que sobra y se
    // agota (hasta "..." ) antes de tocar el nombre. En un presupuesto
    // apretado, el nombre queda completo y la ruta se reduce a lo minimo.
    // (El ancho considera el padding de la barra y el bloque %:
    // con mas columnas la ruta mostraria mas alla de "...".)
    std::string out = barFrame(
        "/some/verylongdirectorychainloading/ending.txt",
        false, "", State::Navegacion, 41);
    CHECK(contains(stripAnsi(out), "ending.txt - ... - NAVEGACION"));
    // La ruta casi no deja componente visible: su nombre de archivo no
    // debe colarse en el recorte.
    CHECK(!contains(out, "verylongdirectory"));
}

TEST(statusbar_second_row_message_independent) {
    // La fila de mensajes es propia: se muestra tal cual y se trunca al
    // ancho de la terminal (descontando el padding de la barra superior)
    // sin competir con la barra fija.
    std::string out = barFrame("/a.txt", false, "mensaje de estado", State::Navegacion, 15);
    CHECK(contains(out, "mensaje de ")); // truncado a 11 columnas (15 - 1 izq - 3 der)
    CHECK(!contains(out, "mensaje de estado")); // el texto sigue alineado con la barra
    // La barra fija tiene su propio ancho: en 15 columnas el bloque
    // (fila,col) cabe (no depende del mensaje).
    CHECK(contains(out, "(1,1)"));
}

// ---------------------------------------------------------------------------
// Caracteres UTF-8 CONSECUTIVOS (sin ASCII que separe): "éééé", "😀😀😀" y
// "———". Cada caracter es un bloque multibyte adyacente al otro, lo que
// delata errores que un texto con contexto ASCII ("café") oculta.
// ---------------------------------------------------------------------------
namespace {

struct Consecutive {
    const char* utf8; // bytes del caracter repetido
    int nchars;       // cuantos se repiten
    int nbytes;       // bytes de un caracter
};

const Consecutive kConsecutive[] = {
    {"\xc3\xa9",             4, 2}, // é  x4
    {"\xf0\x9f\x98\x80",     3, 4}, // 😀 x3
    {"\xe2\x80\x94",         3, 3}, // —  x3
};

std::string repeatChar(const Consecutive& c, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) out += c.utf8;
    return out;
}

} // namespace

TEST(consecutive_selection_char_by_char) {
    // Seleccionar CADA caracter individual produce exactamente ese caracter
    // invertido (y nunca mezcla el vecino). col (byte offset) del caracter i
    // es [i*nbytes, (i+1)*nbytes).
    for (const Consecutive& c : kConsecutive) {
        const std::string line = repeatChar(c, c.nchars);
        for (int i = 0; i < c.nchars; ++i) {
            const int from = i * c.nbytes;
            const int to = (i + 1) * c.nbytes;
            std::string out = frame({line}, selAt({0, from}, {0, to}));
            // El bloque invertido es exactamente el caracter i.
            CHECK(contains(out, std::string(ANSI_INV) + c.utf8 + ANSI_RESET));
        }
    }
}

TEST(consecutive_truncate_each_position) {
    // Recortar a `width` columnas deja EXACTAMENTE los `width` caracteres
    // iniciales, sin partir ninguno, y respeta el limite de columnas.
    for (const Consecutive& c : kConsecutive) {
        const std::string line = repeatChar(c, c.nchars);
        for (int w = 1; w <= c.nchars; ++w) {
            std::string expect;
            for (int i = 0; i < w; ++i) expect += c.utf8;

            std::string row = textRow(line, w);
            CHECK_EQ(row, expect);
            CHECK(validUtf8(row));
            CHECK(colWidth(row) <= w);
        }
    }
}

TEST(consecutive_cursor_at_end) {
    // El cursor al final de una linea multibyte pura se dibuja en la fila
    // 1, columna (columnas visuales + 1), no en (bytes + 1).
    for (const Consecutive& c : kConsecutive) {
        const std::string line = repeatChar(c, c.nchars);
        // "Al final": byte = largo de la linea -> columna visual c.nchars.
        CHECK_EQ(cursorVisibleCol(curFrame(line, static_cast<int>(line.size()))),
                 c.nchars + 1);
    }
}

// ---------------------------------------------------------------------------
// Mezcla EXTREMA de tamaños UTF-8: "aé—😀bé—😀c"
//   a     é    —     😀    b     é    —     😀    c
//   ASCII 2B    3B    4B    ASCII 2B    3B    4B    ASCII
// bytes: [0,1) [1,3) [3,6) [6,10)[10,11)[11,13)[13,16)[16,20)[20,21)
// cols:  0     1     2     3     4      5      6      7      8
// 21 bytes, 9 columnas visuales. Es el mejor delator de confusion
// byte/columna: cada caracter tiene un ancho distinto.
// ---------------------------------------------------------------------------
namespace {
const char* kMix = "a\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80"
                   "b\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80" "c";
} // namespace

TEST(mixed_extreme_cursor_each_position) {
    // Cada posicion valida del cursor (limite de caracter) -> byte offset y
    // la columna VISUAL+1 que el Renderer debe emitir. No la cuenta de bytes.
    struct Pos { int byteCol; int screenCol; } pos[] = {
        {0, 1},   // antes de 'a'
        {1, 2},   // tras 'a', antes de 'é'
        {3, 3},   // tras 'é', antes de '—'
        {6, 4},   // tras '—', antes de '😀'
        {10, 5},  // tras '😀', antes de 'b'
        {11, 6},  // tras 'b', antes de 'é'
        {13, 7},  // tras 'é', antes de '—'
        {16, 8},  // tras '—', antes de '😀'
        {20, 9},  // tras '😀', antes de 'c'
        {21, 10}, // al final
    };
    for (const Pos& p : pos) {
        CHECK_EQ(cursorVisibleCol(curFrame(kMix, p.byteCol)), p.screenCol);
    }
}

TEST(mixed_extreme_truncate_each_width) {
    // Recortar a cada ancho deja EXACTAMENTE los primeros `w` caracteres,
    // nunca un multibyte partido, y respeta el limite de columnas.
    struct Tr { int w; const char* expect; } tr[] = {
        {1, "a"},
        {2, "a\xc3\xa9"},
        {3, "a\xc3\xa9\xe2\x80\x94"},
        {4, "a\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80"},
        {5, "a\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80" "b"},
        {6, "a\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80" "b\xc3\xa9"},
        {7, "a\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80" "b\xc3\xa9\xe2\x80\x94"},
        {8, "a\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80" "b\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80"},
        {9, kMix},
    };
    for (const Tr& t : tr) {
        std::string row = textRow(kMix, t.w);
        CHECK_EQ(row, t.expect);
        CHECK(validUtf8(row));
        CHECK(colWidth(row) <= t.w);
    }
}

// ---------------------------------------------------------------------------
// UTF-8 vacio y casos limite: string vacio, un unico caracter (ASCII, 2B,
// 3B, 4B), solo UTF-8, y truncado con limites extremos (0, mayor que el
// string, y justo al final de un caracter).
// ---------------------------------------------------------------------------
namespace {
std::string renderRow(const std::string& line, int width) {
    // textRow aïslA el recorte a columnas del resaltado de "fila actual"
    // (el cursor va en la linea 2) y del gutter: estos tests solo verifican
    // el truncado de columnas, no el resaltado ni la numeracion.
    return textRow(line, width);
}
} // namespace

TEST(edge_empty_line) {
    const std::string line = "";
    // Una linea vacia: 0 columnas, cursor en la columna visual 0 -> 1;1H.
    CHECK_EQ(cursorVisibleCol(curFrame(line, 0)), 1);
    // Truncar una linea vacia a cualquier ancho deja vacio.
    CHECK_EQ(renderRow(line, 0), "");
    CHECK_EQ(renderRow(line, 5), "");
    CHECK(validUtf8(renderRow(line, 5)));
}

TEST(edge_single_ascii) {
    const std::string line = "a";
    CHECK_EQ(cursorVisibleCol(curFrame(line, 1)), 2);       // fin -> col visual 1
    CHECK_EQ(renderRow(line, 0), "");
    CHECK_EQ(renderRow(line, 1), "a");                      // exactamente al final
    CHECK_EQ(renderRow(line, 5), "a");                      // mayor que el string
    CHECK(validUtf8(renderRow(line, 1)));
}

TEST(edge_single_two_byte) {
    const std::string line = "\xc3\xa9";                    // é
    CHECK_EQ(cursorVisibleCol(curFrame(line, 2)), 2);       // fin -> col visual 1
    CHECK_EQ(cursorVisibleCol(curFrame(line, 0)), 1);
    CHECK_EQ(renderRow(line, 0), "");
    CHECK_EQ(renderRow(line, 1), "\xc3\xa9");               // el char entero en 1 col
    CHECK_EQ(renderRow(line, 2), "\xc3\xa9");               // exactamente al final
    CHECK_EQ(renderRow(line, 9), "\xc3\xa9");               // mayor que el string
    CHECK(validUtf8(renderRow(line, 1)));
}

TEST(edge_single_three_byte) {
    const std::string line = "\xe2\x80\x94";                // —
    CHECK_EQ(cursorVisibleCol(curFrame(line, 3)), 2);
    CHECK_EQ(renderRow(line, 0), "");
    CHECK_EQ(renderRow(line, 1), "\xe2\x80\x94");           // 1 col, 3 bytes intactos
    CHECK_EQ(renderRow(line, 3), "\xe2\x80\x94");
    CHECK_EQ(renderRow(line, 7), "\xe2\x80\x94");
    CHECK(validUtf8(renderRow(line, 1)));
}

TEST(edge_single_four_byte) {
    const std::string line = "\xf0\x9f\x98\x80";            // 😀
    CHECK_EQ(cursorVisibleCol(curFrame(line, 4)), 2);
    CHECK_EQ(renderRow(line, 0), "");
    CHECK_EQ(renderRow(line, 1), "\xf0\x9f\x98\x80");
    CHECK_EQ(renderRow(line, 4), "\xf0\x9f\x98\x80");
    CHECK_EQ(renderRow(line, 6), "\xf0\x9f\x98\x80");
    CHECK(validUtf8(renderRow(line, 1)));
}

TEST(edge_all_utf8_only) {
    const std::string line = "\xe2\x80\x94\xc3\xa9\xf0\x9f\x98\x80"; // — é 😀, 3 cols
    CHECK_EQ(colWidth(line), 3);
    // Cursor al final (byte 9) -> columna visual 3 -> 1;4H.
    CHECK_EQ(cursorVisibleCol(curFrame(line, 9)), 4);
    // Truncado en el limite justo tras el 2do caracter ("—é", 2 cols).
    CHECK_EQ(renderRow(line, 2), "\xe2\x80\x94\xc3\xa9");
    // Truncado en el limite justo tras el 1ro ("—").
    CHECK_EQ(renderRow(line, 1), "\xe2\x80\x94");
    CHECK_EQ(renderRow(line, 3), line);       // todo, exactamente al final
    CHECK_EQ(renderRow(line, 99), line);      // mayor que el string
    CHECK(validUtf8(renderRow(line, 2)));
}

TEST(statusbar_state_label_all_states) {
    // Los 4 estados producen su etiqueta, mapeando State -> text.
    struct Case { State s; const char* label; };
    Case cases[] = {
        {State::Navegacion, "NAVEGACION"},
        {State::Interaccion, "INTERACCION"},
        {State::Seleccion, "SELECCION"},
        {State::Prefix, "COMANDO"},
    };
    for (const auto& c : cases) {
        std::string out = barFrame("/a.txt", false, "", c.s, 200);
        CHECK(contains(out, " - " + std::string(c.label)));
        std::string expected = " - " + std::string(c.label);
        CHECK(contains(out, expected));
    }
}

TEST(statusbar_state_label_persists_across_modified) {
    // La etiqueta de estado no se pierde ni se convierte en otra cosa
    // cuando hay [modificado]: ambos coexisten.
    std::string out = barFrame("/a/b.txt", true, "", State::Interaccion, 200);
    CHECK(contains(out, "[modificado]"));
    CHECK(contains(out, "INTERACCION"));
}

TEST(statusbar_state_label_not_overwritten_by_message) {
    // El mensaje de estado (fila de mensajes, sin inverso) no pisa la
    // etiqueta de la barra fija: ambas filas coexisten.
    std::string out = barFrame("/a/b.txt", false, "mensaje de estado",
                               State::Seleccion, 200);
    CHECK(contains(out, "SELECCION"));
    CHECK(contains(out, "mensaje de estado"));
}

TEST(statusbar_state_label_survives_narrow_terminal) {
    // Terminal angosta: la etiqueta de estado es de bajo sacrificio y,
    // por encima de la cota minima del bloque Ln/Col (ancho 15), la barra
    // nunca debe desbordar la terminal.
    for (int width = 15; width <= 30; ++width) {
        std::string out = barFrame("/a/archivo.txt", false, "",
                                   State::Prefix, width);
        int barW = barVisibleCols(out);
        CHECK(barW <= width);
    }
}

TEST(statusbar_state_label_with_long_filename) {
    // Nombre de archivo largo: se trunca el nombre/ruta, no el estado.
    const std::string nombre = "un_archivo_absurdamente_largo_para_la_barra_"
                               "de_estado_del_editor_de_texto_en_cpp.txt";
    std::string out = barFrame("/dir/" + nombre, false, "",
                               State::Seleccion, 200);
    CHECK(contains(out, "SELECCION"));
}

TEST(statusbar_state_and_modified_each_state) {
    // Combinacion estado + modified: con cualquiera de los 4 estados la
    // barra muestra [modificado] y la etiqueta correcta a la vez.
    struct Case { State s; const char* label; };
    Case cases[] = {
        {State::Navegacion, "NAVEGACION"},
        {State::Interaccion, "INTERACCION"},
        {State::Seleccion, "SELECCION"},
        {State::Prefix, "COMANDO"},
    };
    for (const auto& c : cases) {
        std::string out = barFrame("/a/b.txt", true, "", c.s, 200);
        CHECK(contains(out, "[modificado]"));
        CHECK(contains(out, " - " + std::string(c.label)));
    }
}

// ---------------------------------------------------------------------------
// Renderer + seleccion + UTF-8 (integracion final)
// ---------------------------------------------------------------------------
// El renderer marca la seleccion con video inverso y el cursor con una
// secuencia de posicionamiento. Con caracteres multibyte en el rango debe:
//   - iniciar la inversion en la columna visual correcta (byte lead);
//   - terminarla sin partir bytes de continuacion;
//   - dejar el cursor en la columna visual correcta del byte final.
// La seleccion usa OFFSET DE BYTES (modelo Document); el render aplica
// columnas visuales.
// ---------------------------------------------------------------------------

// Monta un frame de una unica linea con cursor en `byteCol` y una
// seleccion dada (ambos en ofset de bytes).
std::string selCurFrame(const std::string& line, int byteCol,
                        const std::optional<Selection>& sel) {
    Document doc;
    doc.restore({line});

    Viewport viewport;
    viewport.top = 0;
    viewport.height = 1;
    viewport.width = 200;

    Cursor cursor;
    cursor.line = 0;
    cursor.col = byteCol;

    Renderer r;
    return r.buildScreen(doc, cursor, viewport, "test.txt", false, "",
                         State::Navegacion, sel);
}

TEST(renderer_selection_utf8_cafe_accent) {
    // "café" = c,a,f,é (bytes 3..5). Seleccionar solo [é] (3..5).
    // Visualmente: "caf" sin invertir + "é" invertido; cursor tras "é"
    // (byte 5) en la columna visual 4 -> col terminal 5.
    std::string out = selCurFrame("caf\xc3\xa9", 5, selAt({0, 3}, {0, 5}));

    // No corta bytes: el bloque es el "é" completo, no un byte suelto.
    CHECK(contains(out, ANSI_INV "\xc3\xa9" ANSI_RESET));
    // Empieza DESPUES de "caf" (que no esta invertido; lleva el estilo de
    // "fila actual", cerrado con reset antes del bloque invertido).
    CHECK(contains(out, "caf" ANSI_RESET ANSI_INV));
    CHECK(!contains(out, ANSI_INV "caf"));
    // Columna final del cursor: tras é (visual 4) -> 1;5H.
    CHECK_EQ(cursorVisibleCol(out), 5);
}

TEST(renderer_selection_utf8_cafe_accent_cursor_break_pos) {
    // Cursor en el byte INTERMEDIO del é (byte 4) no debe "caer dentro"
    // de la inversion: el render clampa a la columna visual del lead.
    std::string out = selCurFrame("caf\xc3\xa9", 4, std::nullopt);
    CHECK_EQ(cursorVisibleCol(out), 5);   // byte 4 == byte 5 visualmente
}

TEST(renderer_selection_utf8_mixed_em_dash_emoji) {
    // "abc—😀def": a,b,c (0..3), — (3..6, 3 bytes), 😀 (6..10, 4 bytes),
    // d,e,f (10..13). Seleccionar [3..10) = "—😀" completo.
    // Columnas visuales: abcd=0,1,2,3 ; —=4 ; 😀=5 ; def=6,7,8.
    const std::string line = "abc\xe2\x80\x94\xf0\x9f\x98\x80" "def";
    // Cursor tras la seleccion (byte 10, inicio de 'd') -> visual 5 ->
    // col terminal 6 (a0 b1 c2 —3 😀4 d5).
    std::string out = selCurFrame(line, 10, selAt({0, 3}, {0, 10}));

    // El bloque invertido es "—😀" (9 bytes, sin partir).
    CHECK(contains(out, ANSI_INV "\xe2\x80\x94\xf0\x9f\x98\x80" ANSI_RESET));
    // "abc" sin invertir delante y "def" sin invertir detras. En la fila
    // actual ambos tramos llevan el estilo de linea (kCurrentLineStyle),
    // cerrado/abierto con reset alrededor del bloque seleccionado.
    CHECK(contains(out, "abc" ANSI_RESET ANSI_INV));
    CHECK(contains(out, std::string(ANSI_RESET) + kCurrentLineStyle + "def"));
    CHECK(!contains(out, ANSI_INV "abc"));
    CHECK(!contains(out, ANSI_INV "def"));
    // Cursor en la columna correcta (tras emoji, visual 5 -> 1;6H).
    CHECK_EQ(cursorVisibleCol(out), 6);
}

TEST(renderer_selection_utf8_mixed_start_only) {
    // Seleccionar solo el em dash [3..6): "—" invertido, emoji sin tocar.
    const std::string line = "abc\xe2\x80\x94\xf0\x9f\x98\x80" "def";
    std::string out = selCurFrame(line, 6, selAt({0, 3}, {0, 6}));

    CHECK(contains(out, ANSI_INV "\xe2\x80\x94" ANSI_RESET));
    CHECK(!contains(out, ANSI_INV "\xe2\x80\x94\xf0\x9f\x98\x80"));
    CHECK(!contains(out, ANSI_INV "\xf0\x9f\x98\x80"));  // emoji no invertido
    CHECK_EQ(cursorVisibleCol(out), 5);   // tras — (visual 4) -> 1;5H
}

TEST(renderer_selection_utf8_mixed_reverse_direction) {
    // Misma seleccion "—😀" pero "hacia atras": visual identica.
    const std::string line = "abc\xe2\x80\x94\xf0\x9f\x98\x80" "def";
    std::string out = selCurFrame(line, 3, selAt({0, 10}, {0, 3}));

    CHECK(contains(out, ANSI_INV "\xe2\x80\x94\xf0\x9f\x98\x80" ANSI_RESET));
    CHECK(contains(out, "abc" ANSI_RESET ANSI_INV));
    CHECK(contains(out, std::string(ANSI_RESET) + kCurrentLineStyle + "def"));
    CHECK_EQ(cursorVisibleCol(out), 4);   // cursor en el anchor (byte 3)
}

TEST(renderer_selection_utf8_cursor_after_each_char) {
    // Cursor recorriendo el inicio de cada caracter de "abc—😀def":
    // la columna terminal debe avanzar 1 por caracter visible.
    const std::string line = "abc\xe2\x80\x94\xf0\x9f\x98\x80" "def";
    struct Case { int byte; int termCol; };
    Case cases[] = {{0, 1}, {2, 3}, {3, 4}, {6, 5}, {10, 6}, {12, 8}};
    for (const auto& c : cases) {
        std::string out = selCurFrame(line, c.byte, std::nullopt);
        CHECK_EQ(cursorVisibleCol(out), c.termCol);
    }
}

// ---------------------------------------------------------------------------
// Resaltado de la fila del cursor (Paso 2). El resaltado usa
// kCurrentLineStyle (placeholder dim, a reemplazar por el color real en la
// etapa de colores). Reglas:
//   - la fila del cursor, sin seleccion, lleva el estilo en TODO el ancho
//     (incluido el relleno tras el texto);
//   - la seleccion SIEMPRE gana: en la fila del cursor, el tramo
//     seleccionado se pinta en video inverso y el estilo de fila cubre solo
//     los huecos (antes, despues y relleno);
//   - las filas sin cursor y las filas "~" nunca llevan resaltado.
// ---------------------------------------------------------------------------
namespace {

// Monta un frame de una unica linea con ancho y fila de cursor controlables.
std::string curRowFrame(const std::string& line, int width, int cursorLine,
                        const std::optional<Selection>& sel = std::nullopt) {
    Document doc; doc.restore({line});
    Viewport vp; vp.top = 0; vp.height = 1; vp.width = width;
    Cursor cur; cur.line = cursorLine; cur.col = 0;
    Renderer r;
    return r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, sel);
}

} // namespace

TEST(currentline_no_selection_full_width) {
    // Fila del cursor sin seleccion: todo el ancho lleva kCurrentLineStyle,
    // incluido el relleno tras el texto (hasta width).
    std::string out = curRowFrame("hello", 20, 0);
    CHECK(contains(out, std::string(kCurrentLineStyle) + "hello"));
    // El relleno llega al ancho completo del AREA DE TEXTO (20 - gutter de
    // 3 = 17) con el estilo aplicado.
    CHECK_EQ(rowText(out, 1), "hello" + std::string(12, ' '));
}

TEST(currentline_selection_wins_over_row_style) {
    // Seleccion completa de la fila del cursor: la seleccion gana, el bloque
    // seleccionado se ve en video inverso "limpio", sin kCurrentLineStyle
    // superpuesto al tramo seleccionado.
    std::string out = frame({"hello"}, selAt({0, 0}, {0, 5}));
    CHECK(contains(out, ANSI_INV "hello" ANSI_RESET));
    // El estilo de fila no debe quedar pegado al bloque invertido.
    CHECK(!contains(out, std::string(kCurrentLineStyle) + ANSI_INV));
}

TEST(currentline_partial_selection_unselected_keeps_style) {
    // Seleccion parcial ("world" de "hello world") sobre la fila del cursor:
    // el tramo NO seleccionado de esa misma fila ("hello " y el relleno)
    // lleva el estilo de fila actual.
    std::string out = curRowFrame("hello world", 20, 0, selAt({0, 6}, {0, 11}));
    CHECK(contains(out, std::string(kCurrentLineStyle) + "hello "));
    CHECK(contains(out, ANSI_INV "world" ANSI_RESET));
    // "world" son 5 cols; la fila se rellena hasta el area de texto (17).
    CHECK_EQ(rowText(out, 1), "hello world" + std::string(6, ' '));
}

TEST(currentline_line_without_cursor_unaffected) {
    // Fila sin cursor: sin estilo de fila ni relleno. El cursor en la linea
    // 1 de un documento de 1 linea mantiene la fila 0 fuera del resaltado.
    std::string out = curRowFrame("hello", 20, 1);
    CHECK(!contains(out, kCurrentLineStyle));
    CHECK_EQ(rowText(out, 1), "hello");
}

TEST(currentline_tilde_row_never_highlighted) {
    // Una fila "~" (fuera del documento) nunca lleva el resaltado de fila.
    Document doc; doc.restore({"hello"});
    Viewport vp; vp.top = 0; vp.height = 3; vp.width = 20;
    Cursor cur; cur.line = 0; cur.col = 0;
    Renderer r;
    std::string out = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion,
                                    std::nullopt);
    CHECK(contains(out, std::string(kCurrentLineStyle) + "hello"));
    // Las filas 1 y 2 son "~" de relleno: jamas llevan el estilo de fila.
    CHECK(!contains(out, std::string(kCurrentLineStyle) + "~"));
}

// ---------------------------------------------------------------------------
// Gutter de numeros de linea (Paso 3). Reglas:
//   - numeros alineados a la derecha en un gutter de ancho `digitos+1`
//     (minimo 3), con un espacio de separacion antes del texto;
//   - las filas "~" (fuera del documento) llevan gutter en blanco del mismo
//     ancho, sin numero;
//   - el cursor real de terminal se posiciona tras el gutter (columna visual
//     0 = columna de terminal gutterW+1);
//   - al cruzar un umbral de digitos (9 -> 10 lineas) el gutter crece y
//     todas las filas se re-renderizan con el nuevo ancho.
// ---------------------------------------------------------------------------
namespace {

// Fila `index` (0-based) del frame ya sin ANSI (con su gutter y contenido).
std::string plainRow(const std::string& frame, int index) {
    std::string plain = stripAnsi(frame);
    size_t start = 0;
    for (int i = 0; i < index; ++i) {
        size_t pos = plain.find("\r\n", start);
        if (pos == std::string::npos) return "";
        start = pos + 2;
    }
    size_t end = plain.find("\r\n", start);
    if (end == std::string::npos) end = plain.size();
    return plain.substr(start, end - start);
}

} // namespace

TEST(gutter_shows_correct_numbers_when_scrolled) {
    // Documento de 100 líneas. El viewport muestra desde la línea 50 (índice 49).
    Document doc; 
    doc.restore(std::vector<std::string>(100, "linea"));
    
    Viewport vp; 
    vp.top = 49;       // Empezar a mostrar en la línea 50 del documento
    vp.height = 3;     // Mostrar 3 líneas
    vp.width = 20;     // Ancho total del viewport (incluye gutter)
    
    Cursor cur; 
    cur.line = 49; 
    cur.col = 0;
    
    Renderer r;
    std::string out = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion, std::nullopt);
    
    // Para 100 líneas, gutterWFor(100) devuelve 4.
    // El formato es: número alineado a la derecha en (ancho-1) + 1 espacio.
    // Para ancho 4 y línea 50: "%3d " -> " 50 "
    
    std::string row0 = plainRow(out, 0);
    CHECK(row0.substr(0, 4) == " 50 "); 
    
    // MEJORA: Comparamos solo los 5 caracteres de "linea", ignorando el relleno de espacios 
    // que el renderer agrega hasta llegar a vp.width (20 columnas).
    CHECK(row0.substr(4, 5) == "linea");   
    
    // La segunda fila visible debe mostrar la línea 51
    std::string row1 = plainRow(out, 1);
    CHECK(row1.substr(0, 4) == " 51 ");
}

TEST(gutter_tilde_rows_blank) {
    // Filas fuera del documento: gutter en blanco del mismo ancho y "~".
    Document doc; doc.restore({"aaa"});
    Viewport vp; vp.top = 0; vp.height = 3; vp.width = 20;
    Cursor cur; cur.line = 0; cur.col = 0;
    Renderer r;
    std::string out = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion,
                                    std::nullopt);
    CHECK(plainRow(out, 1) == std::string(3, ' ') + "~");
    CHECK(plainRow(out, 2) == std::string(3, ' ') + "~");
}

TEST(gutter_cursor_col_starts_after_gutter) {
    // Con gutter de ancho 3 y cursor en la columna visual 0, el cursor real
    // de terminal se posiciona en la columna 4 (no en la 1).
    Document doc; doc.restore({"hello"});
    Viewport vp; vp.top = 0; vp.height = 1; vp.width = 20;
    Cursor cur; cur.line = 0; cur.col = 0;
    Renderer r;
    std::string out = r.buildScreen(doc, cur, vp, "t", false, "", State::Navegacion,
                                    std::nullopt);
    size_t pos = out.rfind("\x1b[1;");
    CHECK(pos != std::string::npos);
    size_t end = out.find('H', pos);
    CHECK(out.substr(pos + 4, end - pos - 4) == "4");
}

TEST(gutter_width_grows_at_digit_threshold) {
    // De 99 a 100 lineas el gutter pasa de 3 a 4 (numeros de 3 digitos).
    // Hasta 99 (2 digitos) el gutter se mantiene en el minimo de 3.
    Viewport vp; vp.top = 0; vp.height = 100; vp.width = 40;
    Cursor cur; cur.line = 0; cur.col = 0;
    Renderer r;

    Document doc99; doc99.restore(std::vector<std::string>(99, "x"));
    std::string out99 = r.buildScreen(doc99, cur, vp, "t", false, "", State::Navegacion,
                                      std::nullopt);
    // 99 lineas -> gutter 3: la ultima fila de documento lleva "99 ".
    CHECK(plainRow(out99, 98).substr(0, 3) == "99 ");
    // La fila 99 ya es "~" (fuera del documento): gutter en blanco de 3.
    CHECK(plainRow(out99, 99) == std::string(3, ' ') + "~");

    Document doc100; doc100.restore(std::vector<std::string>(100, "x"));
    std::string out100 = r.buildScreen(doc100, cur, vp, "t", false, "", State::Navegacion,
                                       std::nullopt);
    // 100 lineas -> gutter 4: la fila 99 (linea 100 del doc) se
    // re-renderiza con el nuevo ancho: "100 " (numero + separador).
    CHECK(plainRow(out100, 99).substr(0, 4) == "100 ");
}

TEST(selection_empty_line_marks_crossed_line_break) {
    // Atravesar una fila VACIA con la seleccion la marca sin ningun
    // simbolo: la fila completa se pinta en video inverso (espacios).
    std::string out = frame({"a", "", "b"}, selAt({0, 0}, {2, 1}));
    CHECK(contains(out, std::string(ANSI_INV) + " "));
    CHECK(!contains(out, ANSI_INV + std::string("\xc2\xb6")));
    CHECK_EQ(plainRow(out, 1).substr(0, 3), " 2 ");
    // Las filas con texto se seleccionan como siempre.
    CHECK(contains(out, std::string(ANSI_INV) + "a" + ANSI_RESET));
    CHECK(contains(out, std::string(ANSI_INV) + "b" + ANSI_RESET));
}

TEST(selection_empty_line_not_in_selection_no_mark) {
    // La fila vacia no cae dentro de la seleccion: sin marca alguna.
    std::string out = frame({"a", "", "b"}, selAt({0, 0}, {0, 1}));
    CHECK(!contains(out, std::string(ANSI_INV) + " "));
    CHECK_EQ(plainRow(out, 1), " 2 ");
}

TEST(selection_empty_line_collapsed_no_mark) {
    // anchor == position en la fila vacia: nada seleccionado, sin marca y
    // la fila queda limpia.
    std::string out = frame({"a", "", "b"}, selAt({1, 0}, {1, 0}));
    CHECK(!contains(out, std::string(ANSI_INV) + " "));
    CHECK_EQ(plainRow(out, 1), " 2 ");
}

TEST(selection_empty_line_as_end_no_mark) {
    // La seleccion termina en el INICIO de la fila vacia: su salto no quedo
    // incluido, asi que no se marca.
    std::string out = frame({"a", "", "b"}, selAt({0, 1}, {1, 0}));
    CHECK(!contains(out, std::string(ANSI_INV) + " "));
    CHECK_EQ(plainRow(out, 1), " 2 ");
}

TEST(selection_empty_line_as_start_marks) {
    // La seleccion ARRANCA en la fila vacia y cruza hacia abajo: su salto
    // queda incluido y se marca en video inverso, sin simbolo.
    std::string out = frame({"a", "", "b"}, selAt({1, 0}, {2, 1}));
    CHECK(contains(out, std::string(ANSI_INV) + " "));
    CHECK(!contains(out, ANSI_INV + std::string("\xc2\xb6")));
    CHECK_EQ(plainRow(out, 1).substr(0, 3), " 2 ");
}
