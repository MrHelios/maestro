#include <cstdio>
#include <string>

#include "test_framework.h"
#include "core/utf8.h"

// utf8::range(line, fromCol, toCol) devuelve los bytes de `line` cuyas
// COLUMNAS VISUALES caen en [fromCol, toCol), sin cortar un caracter
// multibyte por la mitad. Como columnas y bytes no coinciden cuando hay
// caracteres multibyte, los limites de columna se traducen a los limites
// de caracter completos.

// Valores UTF-8 usados en los tests.
#define U_E   "\xc3\xa9"          // e (2 bytes)
#define U_DASH "\xe2\x80\x94"     // — em dash (3 bytes)
#define U_EMOJI "\xf0\x9f\x98\x80" // 😀 (4 bytes)

// Devuelve true si la cadena es UTF-8 bien formado.
static bool validUtf8(const std::string& s) {
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

// ---------------------------------------------------------------------------
// ASCII puro
// ---------------------------------------------------------------------------
TEST(range_ascii_empty) {
    CHECK_EQ(utf8::range("hello", 0, 0), "");
}

TEST(range_ascii_first_char) {
    CHECK_EQ(utf8::range("hello", 0, 1), "h");
}

TEST(range_ascii_last_char) {
    CHECK_EQ(utf8::range("hello", 4, 5), "o");
}

TEST(range_ascii_full) {
    CHECK_EQ(utf8::range("hello", 0, 5), "hello");
}

TEST(range_ascii_middle) {
    CHECK_EQ(utf8::range("hello", 1, 4), "ell");
}

// ---------------------------------------------------------------------------
// UTF-8: "café" — c,a,f son 1 byte; é son 2 bytes (4 cols, 5 bytes).
// ---------------------------------------------------------------------------
TEST(range_utf8_select_ascii_c) {
    const std::string s = "caf" U_E;
    CHECK_EQ(utf8::range(s, 0, 1), "c");
}

TEST(range_utf8_select_e) {
    const std::string s = "caf" U_E;
    CHECK_EQ(utf8::range(s, 3, 4), U_E);
}

TEST(range_utf8_select_fe) {
    const std::string s = "caf" U_E;
    CHECK_EQ(utf8::range(s, 2, 4), "f" U_E);
}

TEST(range_utf8_select_all) {
    const std::string s = "caf" U_E;
    CHECK_EQ(utf8::range(s, 0, 4), "caf" U_E);
    // El byte total devuelto es 5 (3 ASCII + 2 del acento).
    CHECK_EQ(utf8::range(s, 0, 4).size(), 5u);
}

// Un rango SALE de la linea: el final llega hasta el ultimo caracter
// completo, sin cortar el multibyte por la mitad.
TEST(range_utf8_range_past_end) {
    const std::string s = "caf" U_E;
    CHECK_EQ(utf8::range(s, 0, 99), "caf" U_E);
    CHECK_EQ(utf8::range(s, 3, 99), U_E);
    // De la mitad del multibyte hasta el fin: no debe partir el char,
    // debe incluir el char completo (denota que from apunta al mismo ajedrez).
    CHECK_EQ(utf8::range(s, 3, 3), "");
}

// ---------------------------------------------------------------------------
// Mezcla: "abcé—😀xyz" (3 ASCII + 3 multibyte + 3 ASCII)
// ---------------------------------------------------------------------------
TEST(range_mixed_starts_before_utf8) {
    const std::string s = "abc" U_E U_DASH U_EMOJI "xyz";
    // Empieza antes del primer multibyte y termina tras el primer acento.
    CHECK_EQ(utf8::range(s, 0, 4), "abc" U_E);
}

TEST(range_mixed_ends_after_utf8) {
    const std::string s = "abc" U_E U_DASH U_EMOJI "xyz";
    // Empieza antes del primer multibyte y termina despues del ultimo.
    CHECK_EQ(utf8::range(s, 0, 9), "abc" U_E U_DASH U_EMOJI "xyz");
    // Mitad: de "c" (col 2) hasta tras el emoji (col 6).
    CHECK_EQ(utf8::range(s, 2, 6), "c" U_E U_DASH U_EMOJI);
}

TEST(range_mixed_only_utf8) {
    const std::string s = "abc" U_E U_DASH U_EMOJI "xyz";
    // Contiene solo caracteres multibyte (cols 3..6).
    CHECK_EQ(utf8::range(s, 3, 6), U_E U_DASH U_EMOJI);
}

TEST(range_mixed_spans_multibyte) {
    const std::string s = "abc" U_E U_DASH U_EMOJI "xyz";
    // Atraviesa varios caracteres multibyte: de "b" (col 1) hasta el
    // inicio de "x" (col 6). b,c son ASCII y luego viene é,—,😀.
    CHECK_EQ(utf8::range(s, 1, 6), "bc" U_E U_DASH U_EMOJI);
}

// Cualquier rango [a,b) con a<=b sobre un texto con multibyte nunca
// produce UTF-8 invalido.
TEST(range_never_produces_invalid_utf8) {
    const std::string cases[] = {
        "caf" U_E,
        "abc" U_E U_DASH U_EMOJI "xyz",
        U_E U_DASH U_EMOJI,
    };
    for (const std::string& s : cases) {
        const int total = utf8::columnOf(s, static_cast<int>(s.size()));
        for (int from = 0; from <= total + 3; ++from)
            for (int to = from; to <= total + 3; ++to) {
                std::string out = utf8::range(s, from, to);
                if (!validUtf8(out)) {
                    CHECK(false);
                    std::printf("  UTF-8 invalido | from=%d to=%d len=%d\n",
                                from, to, static_cast<int>(s.size()));
                }
            }
    }
}