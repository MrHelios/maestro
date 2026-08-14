#include <string>
#include <vector>

#include "core/Cursor.h"
#include "core/Document.h"
#include "test_framework.h"

using Lines = std::vector<std::string>;

static Document docOf(const std::vector<const char*>& lines) {
    Document d;
    std::vector<std::string> v(lines.begin(), lines.end());
    d.restore(v);
    return d;
}

// Comprueba los invariantes del cursor siempre (grupo 14).
static void assertCursorConsistent(const Cursor& c, const Document& d) {
    CHECK(c.line >= 0);
    CHECK(c.col >= 0);
    CHECK(c.line < d.lineCount());
    CHECK(c.col <= d.lineLength(c.line));
}

// Invariante UTF-8: el cursor trabaja en BYTES; j/k (y lost movimientos)
// deben dejar el cursor siempre sobre el LEAD byte de un caracter o en el
// final de linea, NUNCA en un byte de continuacion (dentro de un caracter
// multibyte).
static void assertCursorValidUtf8(const Cursor& c, const Document& d) {
    assertCursorConsistent(c, d);
    const int len = d.lineLength(c.line);
    if (c.col == len) return; // final de linea: posicion valida
    const std::string& ln = d.lineAt(c.line);
    const unsigned char b = static_cast<unsigned char>(ln[c.col]);
    CHECK((b & 0xC0) != 0x80); // no debe caer dentro de un caracter multibyte
}

// ---------------------------------------------------------------------------
// 4. MoveLeft
// ---------------------------------------------------------------------------
TEST(cursor_left_mid_line) {
    Document d = docOf({"abc"});
    Cursor c;
    c.col = 1;
    c.moveLeft(d);
    CHECK_EQ(c.col, 0);
    assertCursorConsistent(c, d);
}

TEST(cursor_left_start_wraps_to_prev) {
    Document d = docOf({"abcdef", "ghi"});
    Cursor c;
    c.line = 1;
    c.col = 0;
    c.moveLeft(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 6);
    assertCursorConsistent(c, d);
}

TEST(cursor_left_empty_doc) {
    Document d;
    Cursor c;
    c.moveLeft(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
    assertCursorConsistent(c, d);
}

TEST(cursor_left_first_position_noop) {
    Document d = docOf({"abc"});
    Cursor c;
    c.moveLeft(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
}

TEST(cursor_left_wraps_from_empty_line) {
    Document d = docOf({"abc", ""});
    Cursor c;

    c.line = 1;
    c.col = 0;

    c.moveLeft(d);

    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 3);
    assertCursorConsistent(c, d);
}

TEST(cursor_left_wraps_to_empty_line) {
    Document d = docOf({"", "abc"});
    Cursor c;

    c.line = 1;
    c.col = 0;

    c.moveLeft(d);

    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
    assertCursorConsistent(c, d);
}

// ---------------------------------------------------------------------------
// 4. MoveRight
// ---------------------------------------------------------------------------
TEST(cursor_right_mid_line) {
    Document d = docOf({"abc"});
    Cursor c;
    c.col = 1;
    c.moveRight(d);
    CHECK_EQ(c.col, 2);
}

TEST(cursor_right_end_wraps_to_next) {
    Document d = docOf({"abc", "xyz"});
    Cursor c;
    c.line = 0;
    c.col = 3;
    c.moveRight(d);
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 0);
}

TEST(cursor_right_wraps_to_empty_line) {
    Document d = docOf({"abc", ""});
    Cursor c;

    c.line = 0;
    c.col = 3;

    c.moveRight(d);

    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 0);
    assertCursorConsistent(c, d);
}

TEST(cursor_right_wraps_from_empty_line) {
    Document d = docOf({"", "abc"});
    Cursor c;

    c.line = 0;
    c.col = 0;

    c.moveRight(d);

    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 0);
    assertCursorConsistent(c, d);
}

TEST(cursor_right_empty_doc) {
    Document d;
    Cursor c;
    c.moveRight(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
    assertCursorConsistent(c, d);
}

TEST(cursor_right_last_char_noop) {
    Document d = docOf({"abc"});
    Cursor c;
    c.line = 0;
    c.col = 3;
    c.moveRight(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 3);
}

// ---------------------------------------------------------------------------
// 5. MoveUp / MoveDown
// ---------------------------------------------------------------------------
TEST(cursor_up_middle) {
    Document d = docOf({"aaaa", "bb", "cccc"});
    Cursor c;
    c.line = 2;
    c.col = 3;
    c.moveUp(d);
    CHECK_EQ(c.line, 1);
    assertCursorConsistent(c, d);
}

TEST(cursor_up_first_line_noop) {
    Document d = docOf({"abc"});
    Cursor c;
    c.moveUp(d);
    CHECK_EQ(c.line, 0);
}

TEST(cursor_up_empty_doc) {
    Document d;
    Cursor c;
    c.moveUp(d);
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
    assertCursorConsistent(c, d);
}

TEST(cursor_up_shorter_line_clamps_and_remembers) {
    Document d = docOf({"aaaaa", "bb", "aaaaa"});
    Cursor c;
    c.line = 2;
    for (int i = 0; i < 4; ++i)
        c.moveRight(d); // col y preferredCol pasan a 4
    CHECK_EQ(c.col, 4);
    c.moveUp(d); // -> linea 1, corta: se recorta a 2
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 2);
    c.moveUp(d); // -> linea 0, larga: recupera la columna preferida (4)
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 4);
    assertCursorConsistent(c, d);
}

TEST(cursor_up_longer_line) {
    Document d = docOf({"aaaa", "bb", "cccccc"});
    Cursor c;
    c.line = 2;
    c.moveRight(d); // preferredCol = 1
    c.moveUp(d);
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 1);
}

TEST(cursor_down_middle) {
    Document d = docOf({"aaaa", "bb", "cccc"});
    Cursor c;
    c.col = 1;
    c.moveDown(d);
    CHECK_EQ(c.line, 1);
    assertCursorConsistent(c, d);
}

TEST(cursor_down_last_line_noop) {
    Document d = docOf({"abc"});
    Cursor c;
    c.line = 0;
    c.moveDown(d);
    CHECK_EQ(c.line, 0);
}

TEST(cursor_down_empty_doc) {
    Document d;
    Cursor c;
    c.moveDown(d);
    CHECK_EQ(c.line, 0);
    assertCursorConsistent(c, d);
}

TEST(cursor_down_shorter_line_clamps_and_remembers) {
    Document d = docOf({"aaaaa", "bb"});
    Cursor c;
    for (int i = 0; i < 4; ++i)
        c.moveRight(d); // preferredCol = 4
    c.moveDown(d); // -> linea 1 corta -> col 2
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 2);
    c.moveDown(d); // ya no hay mas
    CHECK_EQ(c.line, 1);
    assertCursorConsistent(c, d);
}

// Moverse horizontalmente debe actualizar la columna preferida, de modo
// que la siguiente navegacion vertical use la nueva posicion, no la anterior.
TEST(cursor_horizontal_move_updates_preferred_column) {
    Document d = docOf({"aaaaa", "bb", "ccccc"});
    Cursor c;

    for (int i = 0; i < 4; ++i)
        c.moveRight(d); // preferredCol = col = 4
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 4);

    c.moveDown(d); // -> linea 1 corta, se clampa a col 2
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 2);

    c.moveLeft(d); // ahora la posicion deseada pasa a 1
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 1);

    c.moveDown(d); // linea 2 larga, usa la nueva preferredCol (1)
    CHECK_EQ(c.line, 2);
    CHECK_EQ(c.col, 1);

    assertCursorConsistent(c, d);
}

// Home actualiza la columna preferida a 0: el movimiento vertical
// siguiente apunta a principios de linea.
TEST(cursor_home_updates_preferred_column) {
    Document d = docOf({"abcdef", "123456"});
    Cursor c;

    c.col = 5;
    c.moveHome();
    CHECK_EQ(c.col, 0);

    c.moveDown(d);
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 0);

    assertCursorConsistent(c, d);
}

// End lleva la columna preferida al final de linea: la navegacion
// vertical posterior busca ese final (clampandose si es mas corto).
TEST(cursor_end_updates_preferred_column) {
    Document d = docOf({"abcdef", "12"});
    Cursor c;

    c.moveEnd(d);
    CHECK_EQ(c.col, 6);

    c.moveDown(d); // linea mas corta: clampa a 2
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 2);

    assertCursorConsistent(c, d);
}

// ---------------------------------------------------------------------------
// 6. MoveHome
// ---------------------------------------------------------------------------
TEST(cursor_home_empty_line) {
    Document d = docOf({"", "abc"});
    Cursor c;
    c.line = 1;
    c.col = 2;
    c.moveHome();
    CHECK_EQ(c.col, 0);
}

TEST(cursor_home_normal) {
    Document d = docOf({"hola mundo"});
    Cursor c;
    c.col = 5;
    c.moveHome();
    CHECK_EQ(c.col, 0);
}

TEST(cursor_home_already_start) {
    Document d = docOf({"abc"});
    Cursor c;
    c.moveHome();
    CHECK_EQ(c.col, 0);
}

TEST(cursor_home_at_end_line) {
    Document d = docOf({"abc"});
    Cursor c;
    c.col = 3;
    c.moveHome();
    CHECK_EQ(c.col, 0);
}

// ---------------------------------------------------------------------------
// 7. MoveEnd
// ---------------------------------------------------------------------------
TEST(cursor_end_empty_line) {
    Document d = docOf({"", "abc"});
    Cursor c;
    c.line = 1;
    c.col = 0;
    c.moveEnd(d);
    CHECK_EQ(c.col, 3);
}

TEST(cursor_end_normal) {
    Document d = docOf({"hola"});
    Cursor c;
    c.col = 2;
    c.moveEnd(d);
    CHECK_EQ(c.col, 4);
}

TEST(cursor_end_from_start) {
    Document d = docOf({"abcd"});
    Cursor c;
    c.moveEnd(d);
    CHECK_EQ(c.col, 4);
}

TEST(cursor_end_already_end) {
    Document d = docOf({"abcd"});
    Cursor c;
    c.col = 4;
    c.moveEnd(d);
    CHECK_EQ(c.col, 4);
}

// ---------------------------------------------------------------------------
// 14. Consistencia tras muchas operaciones
// ---------------------------------------------------------------------------
TEST(cursor_consistent_after_navigation) {
    Document d = docOf({"hola", "mundo", "cruel"});
    Cursor c;
    for (int i = 0; i < 200; ++i) {
        switch (i % 4) {
            case 0: c.moveLeft(d); break;
            case 1: c.moveRight(d); break;
            case 2: c.moveUp(d); break;
            case 3: c.moveDown(d); break;
        }
        assertCursorConsistent(c, d);
    }
}

// Movimientos inversos deben devolver el cursor a la posicion original.
TEST(cursor_navigation_roundtrip) {
    Document d = docOf({"abc", "def", "ghi"});
    Cursor c;

    c.col = 1;

    c.moveRight(d);
    c.moveRight(d);
    c.moveLeft(d);
    c.moveLeft(d);

    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 1);

    c.moveDown(d);
    c.moveDown(d);
    c.moveUp(d);
    c.moveUp(d);

    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 1);

    assertCursorConsistent(c, d);
}

TEST(cursor_clamp_after_backspace) {
    Document d = docOf({"abcdef"});
    Cursor c;
    c.col = 6;
    c.clampToLine(d);
    CHECK_EQ(c.col, 6);
    d.deleteCharAt(0, 5);
    c.clampToLine(d);
    CHECK(c.col <= d.lineLength(0));
    assertCursorConsistent(c, d);
}

// clampToLine solo corrige col dentro de la linea actual; no toca line.
TEST(cursor_clamp_after_line_becomes_empty) {
    Document d = docOf({"abc", "abcdef"});
    Cursor c;

    c.line = 1;
    c.col = 6;

    d.restore({"abc", ""});
    c.clampToLine(d);

    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 0);
    assertCursorConsistent(c, d);
}

// clampToLine no modifica line, aunque la linea actual desaparezca:
// solo garantiza que col quede dentro de los limites de esa linea.
TEST(cursor_clamp_shorter_than_current_col) {
    Document d = docOf({"abcdefgh"});
    Cursor c;

    c.line = 0;
    c.col = 7;

    d.restore({"ab"});
    c.clampToLine(d);

    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 2);
    assertCursorConsistent(c, d);
}

// ---------------------------------------------------------------------------
// j/k: movimiento por bloques (palabras)
// ---------------------------------------------------------------------------
// Regla: una corrida maxima de caracteres no-separadores (' '/'\\t') es una
// palabra. Todos los bytes multibyte son no-separadores, asi j/k nunca parten
// un caracter utf-8: los cortes caen siempre sobre limites de caracter valido.
TEST(cursor_j_to_next_word_end) {
    Document d = docOf({"uno dos tres"});
    Cursor c;
    c.line = 0;
    c.col = 0;              // inicio de "uno"
    c.moveToNextWord(d);    // fin de "uno" -> col 3
    CHECK_EQ(c.col, 3);
    c.moveToNextWord(d);    // fin de "dos"
    CHECK_EQ(c.col, 7);
    c.moveToNextWord(d);    // fin de "tres"
    CHECK_EQ(c.col, 12);
    c.moveToNextWord(d);    // ya no hay mas: se queda en EOF
    CHECK_EQ(c.col, 12);
    assertCursorConsistent(c, d);
}

TEST(cursor_j_from_inside_word_to_its_end) {
    Document d = docOf({"uno dos tres"});
    Cursor c;
    c.line = 0;
    c.col = 2;              // dentro de "uno"
    c.moveToNextWord(d);    // fin del bloque actual ("uno") -> 3
    CHECK_EQ(c.col, 3);
}

TEST(cursor_j_from_separator_to_next_word_end) {
    Document d = docOf({"uno dos tres"});
    Cursor c;
    c.line = 0;
    c.col = 3;              // el espacio tras "uno"
    c.moveToNextWord(d);    // fin de "dos" -> 7
    CHECK_EQ(c.col, 7);
}

TEST(cursor_j_crosses_line_break) {
    Document d = docOf({"uno dos", "tres cuatro"});
    Cursor c;
    c.line = 0;
    c.col = 3;              // el espacio tras "uno"
    c.moveToNextWord(d);    // fin de "dos" -> (0,7)
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 7);
    c.moveToNextWord(d);    // cruza el salto de linea -> fin de "tres" (1,4)
    CHECK_EQ(c.line, 1);
    CHECK_EQ(c.col, 4);
    assertCursorConsistent(c, d);
}

TEST(cursor_j_empty_and_no_next_block) {
    Document d = docOf({"abc"});
    Cursor c;
    c.line = 0;
    c.col = 3;              // EOF
    c.moveToNextWord(d);
    CHECK_EQ(c.col, 3);
    c.col = 2;              // dentro del unico bloque
    c.moveToNextWord(d);    // fin de "abc" -> 3
    CHECK_EQ(c.col, 3);
}

TEST(cursor_k_to_previous_word_start) {
    Document d = docOf({"uno dos tres"});
    Cursor c;
    c.line = 0;
    c.col = 11;             // EOF, tras "tres"
    c.moveToPreviousWord(d); // inicio de "tres" -> col 8
    CHECK_EQ(c.col, 8);
    c.moveToPreviousWord(d); // inicio de "dos" -> col 4
    CHECK_EQ(c.col, 4);
    c.moveToPreviousWord(d); // inicio de "uno" -> col 0
    CHECK_EQ(c.col, 0);
    c.moveToPreviousWord(d); // no hay mas: se queda en 0
    CHECK_EQ(c.col, 0);
    assertCursorConsistent(c, d);
}

TEST(cursor_k_from_inside_word_to_its_start) {
    Document d = docOf({"uno dos tres"});
    Cursor c;
    c.line = 0;
    c.col = 6;              // dentro de "dos"
    c.moveToPreviousWord(d); // inicio de "dos" -> col 4
    CHECK_EQ(c.col, 4);
}

TEST(cursor_k_from_separator_to_previous_word_start) {
    Document d = docOf({"uno dos tres"});
    Cursor c;
    c.line = 0;
    c.col = 4;              // el espacio tras "uno"
    c.moveToPreviousWord(d); // inicio de "uno" -> col 0
    CHECK_EQ(c.col, 0);
}

TEST(cursor_k_crosses_line_break) {
    Document d = docOf({"uno dos", "tres cuatro"});
    Cursor c;
    c.line = 1;
    c.col = 0;              // inicio de linea 1, delante de "tres"
    c.moveToPreviousWord(d); // inicio de "dos" en la linea 0 -> (0,4)
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 4);
    assertCursorConsistent(c, d);
}

TEST(cursor_k_empty_and_no_previous_block) {
    Document d = docOf({"abc"});
    Cursor c;
    c.line = 0;
    c.col = 0;
    c.moveToPreviousWord(d);
    CHECK_EQ(c.col, 0);     // sin bloque anterior: se queda
}

// Cruce de VARIAS lineas vacias (vacio = linea sin caracteres): j para
// delante y k para atras deben atravesar los huecos sin quedarse clavados
// y sin aterrizar en una linea vacia.
TEST(cursor_j_crosses_empty_lines_forward) {
    Document d = docOf({"uno", "", "", "dos"});
    Cursor c;
    c.line = 0;
    c.col = 3;              // fin de "uno"
    c.moveToNextWord(d);    // cruza 2 lineas vacias -> fin de "dos" (3,3)
    CHECK_EQ(c.line, 3);
    CHECK_EQ(c.col, 3);
}

TEST(cursor_k_crosses_empty_lines_backward) {
    Document d = docOf({"uno", "", "", "dos"});
    Cursor c;
    c.line = 3;
    c.col = 0;              // inicio de "dos"
    c.moveToPreviousWord(d); // cruza 2 lineas vacias -> inicio de "uno" (0,0)
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
}

// Partir desde DENTRO de una linea vacia (bof === eof de esa linea): no
// debe crashear y debe saltar al bloque adecuado de la linea vecina.
TEST(cursor_j_from_inside_empty_line_forward) {
    Document d = docOf({"uno", "", "dos"});
    Cursor c;
    c.line = 1;             // la linea vacia intermedia
    c.col = 0;
    c.moveToNextWord(d);    // -> fin de "dos" (2,3)
    CHECK_EQ(c.line, 2);
    CHECK_EQ(c.col, 3);
}

TEST(cursor_k_from_inside_empty_line_backward) {
    Document d = docOf({"uno", "", "dos"});
    Cursor c;
    c.line = 1;             // la linea vacia intermedia
    c.col = 0;
    c.moveToPreviousWord(d); // -> inicio de "uno" (0,0)
    CHECK_EQ(c.line, 0);
    CHECK_EQ(c.col, 0);
}

TEST(cursor_jk_utf8_no_split) {
    // "hola café mundo": la 'é' es multibyte; j/k deben saltar bloques sin
    // aterrizar en medio de un caracter.
    Document d = docOf({"hola café mundo"});
    Cursor c;
    c.line = 0;
    c.col = 0;
    c.moveToNextWord(d);    // fin de "hola" -> col 4
    CHECK_EQ(c.col, 4);
    c.moveToNextWord(d);    // fin de "café" -> cae justo tras la é
    CHECK_EQ(c.col, static_cast<int>(static_cast<std::string>("hola café").size()));
    c.moveToNextWord(d);    // fin de "mundo" -> fin de linea
    CHECK_EQ(c.col, static_cast<int>(static_cast<std::string>("hola café mundo").size()));

    // k hacia atras desde el fin: cae en limites de caracter.
    c.moveToPreviousWord(d); // inicio de "mundo" -> 11
    CHECK_EQ(c.col, 11);
    c.moveToPreviousWord(d); // inicio de "café" -> 5
    CHECK_EQ(c.col, 5);
    assertCursorConsistent(c, d);
}

TEST(cursor_jk_tab_is_separator) {
    Document d = docOf({"uno\tdos"});
    Cursor c;
    c.line = 0;
    c.col = 0;
    c.moveToNextWord(d);    // fin de "uno" -> col 3 (antes del tab)
    CHECK_EQ(c.col, 3);
    c.moveToNextWord(d);    // fin de "dos" -> col 7
    CHECK_EQ(c.col, 7);
    c.moveToPreviousWord(d);// inicio de "dos" -> col 4
    CHECK_EQ(c.col, 4);
}

// ---------------------------------------------------------------------------
// j/k con GRUPOS de separadores (varios espacios entre palabras)
// ---------------------------------------------------------------------------
// Cuando hay mas de un separador seguido, j/k deben atravesar el grupo
// ENTERO antes de buscar el bloque, tanto desde el fin de la palabra
// anterior ("abc|   def") como desde el inicio de la siguiente
// ("abc   |def").
//   "abc   def   ghi": a0b1c2 ' '3 ' '4 ' '5 d6e7f8 ' '9 ' '10 ' '11 g12h13i14
TEST(cursor_j_skips_groups_of_separators) {
    Document d = docOf({"abc   def   ghi"});
    Cursor c;
    c.line = 0;
    // Desde justo despues de "abc" (primer separador del grupo).
    c.col = 3;
    c.moveToNextWord(d);
    CHECK_EQ(c.col, 9);     // fin de "def" -> cruzo el grupo 3-5
    c.moveToNextWord(d);
    CHECK_EQ(c.col, 15);    // fin de "ghi" -> cruzo el grupo 9-11
    c.moveToNextWord(d);    // EOF: se queda
    CHECK_EQ(c.col, 15);
    // Desde el inicio de "def" (delante del muro de espacios).
    c.col = 6;
    c.moveToNextWord(d);
    CHECK_EQ(c.col, 9);     // fin de "def" (no hay que saltar nada delante)
    assertCursorConsistent(c, d);
}

TEST(cursor_k_skips_groups_of_separators) {
    Document d = docOf({"abc   def   ghi"});
    Cursor c;
    c.line = 0;
    // Desde el final del documento.
    c.col = 15;
    c.moveToPreviousWord(d);
    CHECK_EQ(c.col, 12);    // inicio de "ghi" -> cruzo el grupo 9-11
    c.moveToPreviousWord(d);
    CHECK_EQ(c.col, 6);     // inicio de "def" -> cruzo el grupo 3-5
    c.moveToPreviousWord(d);
    CHECK_EQ(c.col, 0);     // inicio de "abc"
    c.moveToPreviousWord(d); // no hay previo: se queda
    CHECK_EQ(c.col, 0);
    assertCursorConsistent(c, d);
}

TEST(cursor_jk_groups_of_separators_jk_roundtrip) {
    // j hacia delante y k hacia atras atraviesan los mismos grupos de
    // separadores de forma simetrica. Desde el FIN de un bloque, k va al
    // COMIENZO de ese mismo bloque; desde el COMIENZO, k al bloque anterior.
    Document d = docOf({"abc   def   ghi"});
    Cursor c;
    c.line = 0;
    c.col = 3;              // primer separador tras "abc"
    c.moveToNextWord(d);    // fin de "def" (9)
    CHECK_EQ(c.col, 9);
    c.moveToPreviousWord(d); // inicio de "def" (6)
    CHECK_EQ(c.col, 6);
    c.moveToPreviousWord(d); // inicio de "abc" (0)
    CHECK_EQ(c.col, 0);
    c.moveToNextWord(d);    // fin de "abc" (3)
    CHECK_EQ(c.col, 3);
    c.moveToNextWord(d);    // fin de "def" (9)
    CHECK_EQ(c.col, 9);
}

// ---------------------------------------------------------------------------
// UTF-8 en j/k: el cursor nunca cae dentro de un caracter multibyte
// ---------------------------------------------------------------------------
// El cursor trabaja en BYTES; las posiciones que dejan j/k (y MoveLeft/
// MoveRight) deben caer sobre un lead byte o el final de linea, nunca en un
// byte de continuacion. Se verifica con assertCursorValidUtf8() en cada paso.

// "uno — dos": la raya (—, 3 bytes) es un bloque de no-separadores por si
// sola. bytes: u0n1o2' '3 —(4,5,6) ' '7 d8o9s10
TEST(cursor_jk_utf8_em_dash) {
    Document d = docOf({"uno — dos"});
    Cursor c;
    c.line = 0;
    c.col = 0;
    c.moveToNextWord(d);    // fin de "uno" -> 3
    CHECK_EQ(c.col, 3);
    assertCursorValidUtf8(c, d);
    c.moveToNextWord(d);    // fin de "—" -> 7 (tras los 3 bytes)
    CHECK_EQ(c.col, 7);
    assertCursorValidUtf8(c, d);
    c.moveToNextWord(d);    // fin de "dos" -> 11
    CHECK_EQ(c.col, 11);
    assertCursorValidUtf8(c, d);

    c.moveToPreviousWord(d); // inicio de "dos" -> 8
    CHECK_EQ(c.col, 8);
    assertCursorValidUtf8(c, d);
    c.moveToPreviousWord(d); // inicio de "—" -> 4 (lead byte)
    CHECK_EQ(c.col, 4);
    assertCursorValidUtf8(c, d);
    c.moveToPreviousWord(d); // inicio de "uno" -> 0
    CHECK_EQ(c.col, 0);
    assertCursorValidUtf8(c, d);
}

// "áéíóú": TODO es multibyte y no hay separadores -> un unico bloque.
// El bloque entero va de 0 a 10 bytes; j/k saltan al inicio/fin sin nunca
// aterrizar en medio de una vocal.
TEST(cursor_jk_utf8_all_multibyte_single_block) {
    Document d = docOf({"áéíóú"});
    Cursor c;
    c.line = 0;
    c.col = 0;
    c.moveToNextWord(d);    // fin del unico bloque -> 10
    CHECK_EQ(c.col, 10);    // == lineLength (todos los bytes de las 5 vocales)
    assertCursorValidUtf8(c, d);
    c.moveToPreviousWord(d); // inicio -> 0
    CHECK_EQ(c.col, 0);
    assertCursorValidUtf8(c, d);
    c.moveToPreviousWord(d); // no hay previo: se queda en 0
    CHECK_EQ(c.col, 0);
    assertCursorValidUtf8(c, d);
}

// Combinacion MoveLeft/MoveRight + j/k: todas las paradas caen en limites
// de caracter validos.
TEST(cursor_jk_utf8_mixed_character_moves) {
    Document d = docOf({"hola café mundo"});
    Cursor c;
    c.line = 0;
    c.col = 0;
    c.moveToNextWord(d);    // fin de "hola" -> 4
    CHECK_EQ(c.col, 4);
    assertCursorValidUtf8(c, d);

    c.moveLeft(d);          // -> 3 (lead del ultimo char de "hola")
    CHECK_EQ(c.col, 3);
    assertCursorValidUtf8(c, d);
    c.moveRight(d);         // -> 4
    CHECK_EQ(c.col, 4);
    assertCursorValidUtf8(c, d);

    c.moveToNextWord(d);    // fin de "café" -> 10 (tras la é)
    CHECK_EQ(c.col, 10);
    assertCursorValidUtf8(c, d);

    c.moveRight(d);         // -> 11 (el espacio tras "café")
    CHECK_EQ(c.col, 11);
    assertCursorValidUtf8(c, d);

    c.moveToPreviousWord(d); // inicio de "café" -> 5
    CHECK_EQ(c.col, 5);
    assertCursorValidUtf8(c, d);
    c.moveToNextWord(d);    // fin de "café" -> 10
    CHECK_EQ(c.col, 10);
    assertCursorValidUtf8(c, d);
}