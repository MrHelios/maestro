#include <cstdio>
#include <string>

#include "test_framework.h"
#include "utf8.h"

// utf8::truncate(line, maxCols) trunca a maxCols COLUMNAS VISUALES sin
// cortar un caracter multibyte por la mitad. La propiedad mas importante
// de toda la suite: para CUALQUIER limite valido, el resultado NUNCA es
// UTF-8 invalido.

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
        else return false; // byte de continuacion suelto o 0xF8+
        if (i + static_cast<size_t>(need) >= s.size()) return false;
        for (int k = 1; k <= need; ++k) {
            if ((static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) &
                 0xC0) != 0x80)
                return false;
        }
        i += static_cast<size_t>(need) + 1;
    }
    return true;
}

namespace {
int cols(const std::string& s) {
    return utf8::columnOf(s, static_cast<int>(s.size()));
}
} // namespace

// ---------------------------------------------------------------------------
// Propiedad global: nunca producir UTF-8 invalido.
// ---------------------------------------------------------------------------
TEST(truncate_never_produces_invalid_utf8) {
    const std::string cases[] = {
        "caf\xc3\xa9",                                     // cafe
        "\xe4\xbd\xa0\xe5\xa5\xbd",                        // you hao
        "\xe2\x80\x94",                                    // em dash
        "\xf0\x9f\x98\x80\xf0\x9f\x98\x80",                // emoji emoji
        "abc\xc3\xa9" "def",                               // abc e def
        "ab\xe2\x80\x94" "cd",                                // ab dash cd
        "abc\xf0\x9f\x98\x80" "def",                          // abc emoji def
        "\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80",            // e dash emoji
    };
    for (const std::string& s : cases) {
        const int max = cols(s);
        // De 0..max (corte en cada posicion valida de caracter) y hasta
        // max+algunos bytes extra (corte intentado en medio de un char).
        for (int limit = 0; limit <= max + 5; ++limit) {
            std::string out = utf8::truncate(s, limit);
            if (!validUtf8(out)) {
                CHECK(false);
                std::printf("  UTF-8 invalido | limit=%d len=%d\n", limit,
                            static_cast<int>(s.size()));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// ASCII puro
// ---------------------------------------------------------------------------
TEST(truncate_ascii) {
    const std::string s = "hello";
    CHECK_EQ(utf8::truncate(s, 10), "hello"); // mas corto que el limite
    CHECK_EQ(utf8::truncate(s, 5), "hello");  // exactamente el limite
    CHECK_EQ(utf8::truncate(s, 3), "hel");    // mas largo
    CHECK_EQ(utf8::truncate(s, 1), "h");
    CHECK_EQ(utf8::truncate(s, 0), "");
}

// ---------------------------------------------------------------------------
// UTF-8 de 2 bytes
// ---------------------------------------------------------------------------
TEST(truncate_two_byte_char) {
    const std::string s = "caf\xc3\xa9"; // cafe (4 cols, 5 bytes)
    CHECK_EQ(cols(s), 4);

    CHECK_EQ(utf8::truncate(s, 3), "caf");         // antes de e
    CHECK_EQ(utf8::truncate(s, 4), "caf\xc3\xa9"); // exactamente despues de e
    CHECK_EQ(utf8::truncate(s, 1), "c");           // limite en el ASCII
    CHECK_EQ(utf8::truncate(s, 5), "caf\xc3\xa9"); // limite mayor al largo
}

// ---------------------------------------------------------------------------
// UTF-8 de 3 bytes
// ---------------------------------------------------------------------------
TEST(truncate_three_byte_char) {
    const std::string s = "\xe2\x80\x94"; // em dash (1 col, 3 bytes)
    CHECK_EQ(cols(s), 1);

    CHECK_EQ(utf8::truncate(s, 0), "");
    CHECK_EQ(utf8::truncate(s, 1), s); // cabe entero, 3 bytes intactos
    CHECK_EQ(utf8::truncate(s, 2), s); // no hay mas que recortar
}

// Dos caracteres de 3 bytes seguidos: cortar en el 1ro no parte el 2do.
TEST(truncate_three_byte_two_chars) {
    const std::string t = "\xe4\xbd\xa0\xe2\x80\x94"; // you (3B) dash (3B)
    CHECK_EQ(cols(t), 2);

    CHECK_EQ(utf8::truncate(t, 1), "\xe4\xbd\xa0"); // solo 0x93you, 3 bytes
    CHECK_EQ(utf8::truncate(t, 1).size(), 3u);
    CHECK_EQ(utf8::truncate(t, 2), t); // ambos caben
}

// ---------------------------------------------------------------------------
// UTF-8 de 4 bytes
// ---------------------------------------------------------------------------
TEST(truncate_four_byte_char) {
    const std::string s = "\xf0\x9f\x98\x80"; // emoji (1 col, 4 bytes)
    CHECK_EQ(cols(s), 1);

    CHECK_EQ(utf8::truncate(s, 1), s);   // cabe entero, 4 bytes
    CHECK_EQ(utf8::truncate(s, 1).size(), 4u);
    CHECK_EQ(utf8::truncate(s, 2), s);   // ya no hay mas que recortar
    CHECK_EQ(utf8::truncate(s, 0), "");
}

// ---------------------------------------------------------------------------
// Casos mixtos
// ---------------------------------------------------------------------------
TEST(truncate_mixed_ascii_utf8_len2) {
    const std::string s = "abc\xc3\xa9" "def"; // abc e def = 7 cols
    CHECK_EQ(cols(s), 7);

    CHECK_EQ(utf8::truncate(s, 3), "abc");              // antes del char
    CHECK_EQ(utf8::truncate(s, 4), "abc\xc3\xa9");      // despues del char
    CHECK_EQ(utf8::truncate(s, 6), "abc\xc3\xa9" "de"); // + 2 ascii
    CHECK_EQ(utf8::truncate(s, 9), s);                  // mayor al largo
}

TEST(truncate_mixed_utf8_len3) {
    const std::string s = "ab\xe2\x80\x94" "cd"; // ab dash cd = 5 cols
    CHECK_EQ(cols(s), 5);

    CHECK_EQ(utf8::truncate(s, 2), "ab");             // antes del char
    CHECK_EQ(utf8::truncate(s, 3), "ab\xe2\x80\x94"); // despues del char
    CHECK_EQ(utf8::truncate(s, 6), s);
}

TEST(truncate_mixed_utf8_len4) {
    const std::string s = "abc\xf0\x9f\x98\x80" "def"; // abc emoji def = 7 cols
    CHECK_EQ(cols(s), 7);

    CHECK_EQ(utf8::truncate(s, 3), "abc");                    // antes
    CHECK_EQ(utf8::truncate(s, 4), "abc\xf0\x9f\x98\x80");    // despues
    CHECK_EQ(utf8::truncate(s, 8), s);
}

TEST(truncate_all_multibyte) {
    const std::string s = "\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80"; // e dash emoji
    CHECK_EQ(cols(s), 3);

    CHECK_EQ(utf8::truncate(s, 1), "\xc3\xa9");                   // e
    CHECK_EQ(utf8::truncate(s, 2), "\xc3\xa9\xe2\x80\x94");       // e dash
    CHECK_EQ(utf8::truncate(s, 3), s);                             // todo
}

// Limites no positivos son seguros y no rompen el UTF-8.
TEST(truncate_non_positive_limits) {
    const std::string s = "ab\xe2\x80\x94" "cd";
    CHECK_EQ(utf8::truncate(s, 0), "");
    CHECK_EQ(utf8::truncate(s, -1), "");
    CHECK(validUtf8(utf8::truncate(s, -5)));
}