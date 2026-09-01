#include <cstdio>
#include <string>

#include "test_framework.h"
#include "core/utf8.h"

#define U_E "\xc3\xa9"
#define U_DASH "\xe2\x80\x94"
#define U_EMOJI "\xf0\x9f\x98\x80"

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

namespace {
int cols(const std::string& s) {
    return utf8::columnOf(s, static_cast<int>(s.size()));
}
int colAt(const std::string& s, int byte) {
    return utf8::columnOf(s, byte);
}
}

// ---------------------------------------------------------------------------
// columnOf: columna visual por caracteres, no por bytes
// ---------------------------------------------------------------------------

TEST(utf8column_ascii_pure) {
    const std::string s = "hello";
    CHECK_EQ(colAt(s, 0), 0);
    CHECK_EQ(colAt(s, 1), 1);
    CHECK_EQ(colAt(s, 3), 3);
    CHECK_EQ(colAt(s, 5), 5);
}

TEST(utf8column_two_byte_char) {
    const std::string s = "caf" U_E;
    CHECK_EQ(s.size(), 5u);
    CHECK_EQ(colAt(s, 0), 0);
    CHECK_EQ(colAt(s, 3), 3);
    CHECK_EQ(colAt(s, 4), 4);
    CHECK_EQ(colAt(s, 5), 4);
}

TEST(utf8column_three_byte_char) {
    const std::string s = "\xe4\xbd\xa0\xe5\xa5\xbd";
    CHECK_EQ(s.size(), 6u);
    CHECK_EQ(colAt(s, 0), 0);
    CHECK_EQ(colAt(s, 3), 1);
    CHECK_EQ(colAt(s, 6), 2);
}

TEST(utf8column_four_byte_char) {
    const std::string s = U_EMOJI;
    CHECK_EQ(s.size(), 4u);
    CHECK_EQ(colAt(s, 0), 0);
    CHECK_EQ(colAt(s, 1), 1);
    CHECK_EQ(colAt(s, 4), 1);
}

TEST(utf8column_ascii_then_utf8) {
    const std::string s = "ab" U_E "cd";
    CHECK_EQ(s.size(), 6u);
    CHECK_EQ(colAt(s, 0), 0);
    CHECK_EQ(colAt(s, 2), 2);
    CHECK_EQ(colAt(s, 4), 3);
    CHECK_EQ(colAt(s, 6), 5);
}

TEST(utf8column_utf8_then_ascii) {
    const std::string s = U_E "abcd";
    CHECK_EQ(s.size(), 6u);
    CHECK_EQ(colAt(s, 0), 0);
    CHECK_EQ(colAt(s, 2), 1);
    CHECK_EQ(colAt(s, 3), 2);
    CHECK_EQ(colAt(s, 6), 5);
}

TEST(utf8column_multiple_utf8_mixed) {
    const std::string s = "caf" U_E U_DASH "test";
    CHECK_EQ(s.size(), 12u);
    CHECK_EQ(colAt(s, 0), 0);
    CHECK_EQ(colAt(s, 3), 3);
    CHECK_EQ(colAt(s, 4), 4);
    CHECK_EQ(colAt(s, 5), 4);
    CHECK_EQ(colAt(s, 8), 5);
    CHECK_EQ(colAt(s, 12), 9);
}

TEST(utf8column_byte_beyond_end_clamps) {
    const std::string s = "caf" U_E;
    CHECK_EQ(colAt(s, 99), 4);
    CHECK_EQ(colAt(s, -1), 0);
}

// ---------------------------------------------------------------------------
// isCellStart / columnOf con bytes invalidos (modelo byte-safe)
// ---------------------------------------------------------------------------

TEST(cell_orphan_continuation_after_ascii_is_own_cell) {
    const std::string s = "A\x81";
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), true);
}

TEST(cell_valid_continuation_belongs_to_lead) {
    const std::string s = U_E;
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), false);
}

TEST(cell_extra_continuation_after_valid_seq_is_orphan) {
    const std::string s = U_E "\x80";
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), false);
    CHECK_EQ(utf8::isCellStart(s, 2), true);
}

TEST(cell_orphan_continuation_at_line_start) {
    const std::string s = "\x80\x80";
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), true);
}

TEST(cell_invalid_lead_is_own_cell) {
    const std::string s = "\xff\xff";
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), true);
}

TEST(cell_3byte_valid) {
    const std::string s = "\xe4\xb8\xad";
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), false);
    CHECK_EQ(utf8::isCellStart(s, 2), false);
}

TEST(column_orphan_continuation_counts_as_column) {
    const std::string s = "A\x81";
    CHECK_EQ(utf8::columnOf(s, 2), 2);
}

TEST(isCellStart_out_of_range) {
    const std::string s = "abc";
    CHECK_EQ(utf8::isCellStart(s, -1), false);
    CHECK_EQ(utf8::isCellStart(s, -99), false);
    CHECK_EQ(utf8::isCellStart(s, 3), false);
    CHECK_EQ(utf8::isCellStart(s, 4), false);
    CHECK_EQ(utf8::isCellStart(s, 99), false);
    CHECK_EQ(utf8::isCellStart("", 0), false);
    CHECK_EQ(utf8::isCellStart("", -1), false);
}

TEST(cell_mixed_valid_orphan_valid_longer) {
    const std::string s = U_E "\x80\xe4\xb8\xad";
    CHECK_EQ(s.size(), 6u);
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), false);
    CHECK_EQ(utf8::isCellStart(s, 2), true);
    CHECK_EQ(utf8::isCellStart(s, 3), true);
    CHECK_EQ(utf8::isCellStart(s, 4), false);
    CHECK_EQ(utf8::isCellStart(s, 5), false);
    CHECK_EQ(utf8::columnOf(s, 6), 3);
}

TEST(column_orphan_at_start_with_valid) {
    const std::string s = "\x80" U_E "\xe4\xb8\xad";
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), true);
    CHECK_EQ(utf8::isCellStart(s, 2), false);
    CHECK_EQ(utf8::isCellStart(s, 3), true);
    CHECK_EQ(utf8::columnOf(s, static_cast<int>(s.size())), 3);
}

TEST(column_mixed_orphan_interleaved_counts_correctly) {
    const std::string s = std::string(U_E "\x80\xe4\xb8\xad") + std::string("\x81") + "A";
    CHECK_EQ(s.size(), 8u);
    CHECK_EQ(utf8::isCellStart(s, 2), true);
    CHECK_EQ(utf8::isCellStart(s, 6), true);
    CHECK_EQ(utf8::columnOf(s, static_cast<int>(s.size())), 5);
    CHECK_EQ(utf8::columnOf(s, 3), 2);
    CHECK_EQ(utf8::columnOf(s, 6), 3);
}

// ---------------------------------------------------------------------------
// truncate: columnas visuales sin partir caracter
// ---------------------------------------------------------------------------

TEST(truncate_never_produces_invalid_utf8) {
    const std::string cases[] = {
        "caf" U_E,
        "\xe4\xbd\xa0\xe5\xa5\xbd",
        U_DASH,
        U_EMOJI U_EMOJI,
        "abc" U_E "def",
        "ab" U_DASH "cd",
        "abc" U_EMOJI "def",
        U_E U_DASH U_EMOJI,
    };
    for (const std::string& s : cases) {
        const int max = cols(s);
        for (int limit = -5; limit <= max + 5; ++limit) {
            std::string out = utf8::truncate(s, limit);
            if (!validUtf8(out)) {
                CHECK(false);
                std::printf("  UTF-8 invalido | limit=%d len=%d\n", limit,
                            static_cast<int>(s.size()));
            }
        }
    }
}

TEST(truncate_ascii) {
    const std::string s = "hello";
    CHECK_EQ(utf8::truncate(s, 10), "hello");
    CHECK_EQ(utf8::truncate(s, 5), "hello");
    CHECK_EQ(utf8::truncate(s, 3), "hel");
    CHECK_EQ(utf8::truncate(s, 1), "h");
    CHECK_EQ(utf8::truncate(s, 0), "");
}

TEST(truncate_two_byte_char) {
    const std::string s = "caf" U_E;
    CHECK_EQ(cols(s), 4);
    CHECK_EQ(utf8::truncate(s, 3), "caf");
    CHECK_EQ(utf8::truncate(s, 4), "caf" U_E);
    CHECK_EQ(utf8::truncate(s, 1), "c");
    CHECK_EQ(utf8::truncate(s, 5), "caf" U_E);
}

TEST(truncate_three_byte_char) {
    const std::string s = U_DASH;
    CHECK_EQ(cols(s), 1);
    CHECK_EQ(utf8::truncate(s, 0), "");
    CHECK_EQ(utf8::truncate(s, 1), s);
    CHECK_EQ(utf8::truncate(s, 2), s);
}

TEST(truncate_three_byte_two_chars) {
    const std::string t = "\xe4\xbd\xa0" U_DASH;
    CHECK_EQ(cols(t), 2);
    CHECK_EQ(utf8::truncate(t, 1), "\xe4\xbd\xa0");
    CHECK_EQ(utf8::truncate(t, 1).size(), 3u);
    CHECK_EQ(utf8::truncate(t, 2), t);
}

TEST(truncate_four_byte_char) {
    const std::string s = U_EMOJI;
    CHECK_EQ(cols(s), 1);
    CHECK_EQ(utf8::truncate(s, 1), s);
    CHECK_EQ(utf8::truncate(s, 1).size(), 4u);
    CHECK_EQ(utf8::truncate(s, 2), s);
    CHECK_EQ(utf8::truncate(s, 0), "");
}

TEST(truncate_mixed_ascii_utf8_len2) {
    const std::string s = "abc" U_E "def";
    CHECK_EQ(cols(s), 7);
    CHECK_EQ(utf8::truncate(s, 3), "abc");
    CHECK_EQ(utf8::truncate(s, 4), "abc" U_E);
    CHECK_EQ(utf8::truncate(s, 6), "abc" U_E "de");
    CHECK_EQ(utf8::truncate(s, 9), s);
}

TEST(truncate_mixed_utf8_len3) {
    const std::string s = "ab" U_DASH "cd";
    CHECK_EQ(cols(s), 5);
    CHECK_EQ(utf8::truncate(s, 2), "ab");
    CHECK_EQ(utf8::truncate(s, 3), "ab" U_DASH);
    CHECK_EQ(utf8::truncate(s, 6), s);
}

TEST(truncate_mixed_utf8_len4) {
    const std::string s = "abc" U_EMOJI "def";
    CHECK_EQ(cols(s), 7);
    CHECK_EQ(utf8::truncate(s, 3), "abc");
    CHECK_EQ(utf8::truncate(s, 4), "abc" U_EMOJI);
    CHECK_EQ(utf8::truncate(s, 8), s);
}

TEST(truncate_all_multibyte) {
    const std::string s = U_E U_DASH U_EMOJI;
    CHECK_EQ(cols(s), 3);
    CHECK_EQ(utf8::truncate(s, 1), U_E);
    CHECK_EQ(utf8::truncate(s, 2), U_E U_DASH);
    CHECK_EQ(utf8::truncate(s, 3), s);
}

TEST(truncate_non_positive_limits) {
    const std::string s = "ab" U_DASH "cd";
    CHECK_EQ(utf8::truncate(s, 0), "");
    CHECK_EQ(utf8::truncate(s, -1), "");
    CHECK(validUtf8(utf8::truncate(s, -5)));
}

// ---------------------------------------------------------------------------
// range: [fromCol, toCol) sin partir caracter
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
    CHECK_EQ(utf8::range(s, 0, 4).size(), 5u);
}

TEST(range_utf8_range_past_end) {
    const std::string s = "caf" U_E;
    CHECK_EQ(utf8::range(s, 0, 99), "caf" U_E);
    CHECK_EQ(utf8::range(s, 3, 99), U_E);
    CHECK_EQ(utf8::range(s, 3, 3), "");
}

TEST(range_mixed_starts_before_utf8) {
    const std::string s = "abc" U_E U_DASH U_EMOJI "xyz";
    CHECK_EQ(utf8::range(s, 0, 4), "abc" U_E);
}

TEST(range_mixed_ends_after_utf8) {
    const std::string s = "abc" U_E U_DASH U_EMOJI "xyz";
    CHECK_EQ(utf8::range(s, 0, 9), "abc" U_E U_DASH U_EMOJI "xyz");
    CHECK_EQ(utf8::range(s, 2, 6), "c" U_E U_DASH U_EMOJI);
}

TEST(range_mixed_only_utf8) {
    const std::string s = "abc" U_E U_DASH U_EMOJI "xyz";
    CHECK_EQ(utf8::range(s, 3, 6), U_E U_DASH U_EMOJI);
}

TEST(range_mixed_spans_multibyte) {
    const std::string s = "abc" U_E U_DASH U_EMOJI "xyz";
    CHECK_EQ(utf8::range(s, 1, 6), "bc" U_E U_DASH U_EMOJI);
}

TEST(range_reversed_bounds_returns_empty) {
    const std::string s = "caf" U_E;
    CHECK_EQ(utf8::range(s, 4, 1), "");
    CHECK_EQ(utf8::range(s, 4, 0), "");
    CHECK_EQ(utf8::range(s, 1, 0), "");
    CHECK_EQ(utf8::range(s, 5, 3), "");
}

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
                std::string out = std::string(utf8::range(s, from, to));
                if (!validUtf8(out)) {
                    CHECK(false);
                    std::printf("  UTF-8 invalido | from=%d to=%d len=%d\n",
                                from, to, static_cast<int>(s.size()));
                }
            }
    }
}
