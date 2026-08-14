#include <string>

#include "test_framework.h"
#include "utf8.h"

// utf8::columnOf(line, byteCol) devuelve la COLUMNA VISUAL que ocupan
// los primeros `byteCol` bytes. El objetivo: la columna se calcula por
// CARACTERES, no por bytes. Un caracter UTF-8 ocupa varios bytes en el
// std::string pero una sola columna en la terminal.

namespace {

// columna en el byte i (final de caracter valido)
int colAt(const std::string& s, int byte) {
    return utf8::columnOf(s, byte);
}

} // namespace

// ---------------------------------------------------------------------------
// ASCII puro: byte offset == columna
// ---------------------------------------------------------------------------
TEST(utf8column_ascii_pure) {
    const std::string s = "hello";
    CHECK_EQ(colAt(s, 0), 0);
    CHECK_EQ(colAt(s, 1), 1);
    CHECK_EQ(colAt(s, 3), 3);
    CHECK_EQ(colAt(s, 5), 5);
}

// ---------------------------------------------------------------------------
// Un caracter de 2 bytes
// ---------------------------------------------------------------------------
TEST(utf8column_two_byte_char) {
    // "café": 5 bytes, 4 columnas. "é" empieza en el byte 3 y ocupa 2.
    const std::string s = "caf\xc3\xa9";
    CHECK_EQ(s.size(), 5u);

    CHECK_EQ(colAt(s, 0), 0);           // inicio
    CHECK_EQ(colAt(s, 3), 3);           // antes de "é" (al llegar a su lead byte)
    CHECK_EQ(colAt(s, 4), 4);           // dentro de "é" (2do byte) ya es col 4
    CHECK_EQ(colAt(s, 5), 4);           // despues de "é" / final
}

// ---------------------------------------------------------------------------
// Caracter multibyte de 3 bytes (CJK): 1 columna por caracter
// ---------------------------------------------------------------------------
TEST(utf8column_three_byte_char) {
    // "你好": 2 caracteres, 3 bytes cada uno = 6 bytes, 2 columnas.
    const std::string s = "\xe4\xbd\xa0\xe5\xa5\xbd";
    CHECK_EQ(s.size(), 6u);

    CHECK_EQ(colAt(s, 0), 0);           // inicio
    CHECK_EQ(colAt(s, 3), 1);           // lead byte del 2do caracter ya es col 1
    CHECK_EQ(colAt(s, 6), 2);           // final: 2 caracteres => 2 columnas
}

// ---------------------------------------------------------------------------
// Caracter multibyte de 4 bytes (emoji): no son 4 columnas
// ---------------------------------------------------------------------------
TEST(utf8column_four_byte_char) {
    // "😀" (U+1F600): 4 bytes, 1 caracter, 1 columna.
    const std::string s = "\xf0\x9f\x98\x80";
    CHECK_EQ(s.size(), 4u);

    CHECK_EQ(colAt(s, 0), 0);           // inicio
    CHECK_EQ(colAt(s, 1), 1);           // 2do byte ya es col 1 (no 4)
    CHECK_EQ(colAt(s, 4), 1);           // final: 1 columna, no 4
}

// ---------------------------------------------------------------------------
// Posiciones mixtas: ASCII + UTF-8
// ---------------------------------------------------------------------------
TEST(utf8column_ascii_then_utf8) {
    // "abécd": a,b (2 cols) + "é" (2 bytes, col 3) + c,d
    const std::string s = "ab\xc3\xa9"
                          "cd";
    CHECK_EQ(s.size(), 6u);

    CHECK_EQ(colAt(s, 0), 0);           // inicio
    CHECK_EQ(colAt(s, 2), 2);           // antes de "é"
    CHECK_EQ(colAt(s, 4), 3);           // despues de "é" (2do byte = col 3)
    CHECK_EQ(colAt(s, 6), 5);           // final
}

// ---------------------------------------------------------------------------
// Posiciones mixtas: UTF-8 + ASCII
// ---------------------------------------------------------------------------
TEST(utf8column_utf8_then_ascii) {
    // "éabcd": "é" (2 bytes, col 1) + a,b,c,d
    const std::string s = "\xc3\xa9"
                          "abcd";
    CHECK_EQ(s.size(), 6u);

    CHECK_EQ(colAt(s, 0), 0);           // inicio
    CHECK_EQ(colAt(s, 2), 1);           // antes de "a" (despues de "é") = col 1
    CHECK_EQ(colAt(s, 3), 2);           // "a" = col 2
    CHECK_EQ(colAt(s, 6), 5);           // final
}

// ---------------------------------------------------------------------------
// Varios caracteres UTF-8 intercalados con ASCII
// ---------------------------------------------------------------------------
TEST(utf8column_multiple_utf8_mixed) {
    // "café—test": c,a,f (3) + "é" (col 4) + "—" (emis/dash 3 bytes, col 5) +
    // t,e,s,t (4)
    const std::string s = "caf\xc3\xa9\xe2\x80\x94test";
    CHECK_EQ(s.size(), 12u);

    CHECK_EQ(colAt(s, 0), 0);           // inicio
    CHECK_EQ(colAt(s, 3), 3);           // antes de "é"
    CHECK_EQ(colAt(s, 4), 4);           // despues de "é" (col 4)
    CHECK_EQ(colAt(s, 5), 4);           // antes de "—" (aun col 4, "café")
    CHECK_EQ(colAt(s, 8), 5);           // despues de "—" = col 5
    CHECK_EQ(colAt(s, 12), 9);          // final: 9 caracteres => 9 columnas
}

TEST(utf8column_byte_beyond_end_clamps) {
    // byteCol mayor que el largo real se acota al final (no overflow).
    const std::string s = "caf\xc3\xa9"; // 5 bytes, 4 cols
    CHECK_EQ(colAt(s, 99), 4);
    CHECK_EQ(colAt(s, -1), 0);
}

// ---------------------------------------------------------------------------
// Modelo byte-safe: una secuencia UTF-8 valida es una celda; un byte
// invalido suelto (continuacion huerfana, lead invalido) es su propia
// celda de 1 byte. No se "pega" un byte invalido a un vecino.
// ---------------------------------------------------------------------------
TEST(cell_orpan_continuation_after_ascii_is_own_cell) {
    // 'A' + una continuacion huerfana (0x81): 2 celdas, 2 columnas.
    const std::string s = "A\x81";
    CHECK_EQ(utf8::isCellStart(s, 0), true);  // 'A'
    CHECK_EQ(utf8::isCellStart(s, 1), true);  // 0x81 huerfana -> propia celda
}

TEST(cell_valid_continuation_belongs_to_lead) {
    const std::string s = "\xc3\xa9"; // "é" valido: una sola celda
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), false); // 0xA9 la posee el lead
}

TEST(cell_extra_continuation_after_valid_seq_is_orphan) {
    // "é" completo (0xC3 0xA9) + continuacion extra 0x80: esa es huerfana.
    const std::string s = "\xc3\xa9\x80";
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), false); // parte de "é"
    CHECK_EQ(utf8::isCellStart(s, 2), true);  // 0x80 extra -> propia celda
}

TEST(cell_orphan_continuation_at_line_start) {
    const std::string s = "\x80\x80"; // dos huerfanas seguidas: 2 celdas
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), true);
}

TEST(cell_invalid_lead_is_own_cell) {
    const std::string s = "\xff\xff"; // lead invalido (0xFF) no cubre nada
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), true); // el segundo 0xFF es otra celda
}

TEST(cell_3byte_valid) {
    const std::string s = "\xe4\xb8\xad"; // "中" valido: una celda
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), false);
    CHECK_EQ(utf8::isCellStart(s, 2), false);
}

TEST(column_orphan_continuation_counts_as_column) {
    // 'A' + huerfana 0x81: columnOf cuenta 2 celdas.
    const std::string s = "A\x81";
    CHECK_EQ(utf8::columnOf(s, 2), 2);
}