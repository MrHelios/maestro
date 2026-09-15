#include <cstdio>
#include <string>

#include "test_framework.h"
#include "core/Cursor.h"
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
    CHECK_EQ(colAt(s, 3), 2);
    CHECK_EQ(colAt(s, 6), 4);
}

TEST(utf8column_four_byte_char) {
    const std::string s = U_EMOJI;
    CHECK_EQ(s.size(), 4u);
    CHECK_EQ(colAt(s, 0), 0);
    CHECK_EQ(colAt(s, 1), 2);
    CHECK_EQ(colAt(s, 4), 2);
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
    CHECK_EQ(utf8::columnOf(s, 6), 4);
}

TEST(column_orphan_at_start_with_valid) {
    const std::string s = "\x80" U_E "\xe4\xb8\xad";
    CHECK_EQ(utf8::isCellStart(s, 0), true);
    CHECK_EQ(utf8::isCellStart(s, 1), true);
    CHECK_EQ(utf8::isCellStart(s, 2), false);
    CHECK_EQ(utf8::isCellStart(s, 3), true);
    CHECK_EQ(utf8::columnOf(s, static_cast<int>(s.size())), 4);
}

TEST(column_mixed_orphan_interleaved_counts_correctly) {
    const std::string s = std::string(U_E "\x80\xe4\xb8\xad") + std::string("\x81") + "A";
    CHECK_EQ(s.size(), 8u);
    CHECK_EQ(utf8::isCellStart(s, 2), true);
    CHECK_EQ(utf8::isCellStart(s, 6), true);
    CHECK_EQ(utf8::columnOf(s, static_cast<int>(s.size())), 6);
    CHECK_EQ(utf8::columnOf(s, 3), 2);
    CHECK_EQ(utf8::columnOf(s, 6), 4);
}

TEST(column_byteCol_zero_and_size) {
    const std::string empty = "";
    CHECK_EQ(utf8::columnOf(empty, 0), 0);
    CHECK_EQ(utf8::columnOf(empty, 1), 0);
    CHECK_EQ(utf8::columnOf("abc", 0), 0);
    CHECK_EQ(utf8::columnOf("abc", 3), 3);
    const std::string s = U_E U_DASH U_EMOJI;
    CHECK_EQ(utf8::columnOf(s, 0), 0);
    CHECK_EQ(utf8::columnOf(s, static_cast<int>(s.size())), 4);
    const std::string inv = "A\x81\xff";
    CHECK_EQ(utf8::columnOf(inv, 0), 0);
    CHECK_EQ(utf8::columnOf(inv, static_cast<int>(inv.size())), 3);
}

TEST(column_lead_incompleto_al_final) {
    const std::string s2 = "a\xC3";
    CHECK_EQ(s2.size(), 2u);
    CHECK_EQ(utf8::isCellStart(s2, 1), true);
    CHECK_EQ(utf8::columnOf(s2, 0), 0);
    CHECK_EQ(utf8::columnOf(s2, 1), 1);
    CHECK_EQ(utf8::columnOf(s2, 2), 2);
    const std::string s3 = "a\xE2\x80";
    CHECK_EQ(s3.size(), 3u);
    CHECK_EQ(utf8::columnOf(s3, 3), 2);
    CHECK_EQ(utf8::columnOf(s3, 2), 2);
    const std::string s4 = "a\xF0\x9F\x98";
    CHECK_EQ(s4.size(), 4u);
    CHECK_EQ(utf8::columnOf(s4, 4), 2);
    CHECK_EQ(utf8::isCellStart(s4, 1), true);
    CHECK_EQ(utf8::isCellStart(s4, 2), false);
    CHECK_EQ(utf8::isCellStart(s4, 3), false);
}

TEST(column_mezcla_ascii_utf8_bordes) {
    const std::string s = "a" U_E "b" U_DASH "c" U_EMOJI "d";
    CHECK_EQ(s.size(), 13u);
    CHECK_EQ(utf8::columnOf(s, 0), 0);
    CHECK_EQ(utf8::columnOf(s, 1), 1);
    CHECK_EQ(utf8::columnOf(s, 2), 2);
    CHECK_EQ(utf8::columnOf(s, 3), 2);
    CHECK_EQ(utf8::columnOf(s, 4), 3);
    CHECK_EQ(utf8::columnOf(s, 5), 4);
    CHECK_EQ(utf8::columnOf(s, 6), 4);
    CHECK_EQ(utf8::columnOf(s, 7), 4);
    CHECK_EQ(utf8::columnOf(s, 8), 5);
    CHECK_EQ(utf8::columnOf(s, 9), 7);
    CHECK_EQ(utf8::columnOf(s, 10), 7);
    CHECK_EQ(utf8::columnOf(s, 11), 7);
    CHECK_EQ(utf8::columnOf(s, 12), 7);
    CHECK_EQ(utf8::columnOf(s, 13), 8);
    CHECK_EQ(utf8::columnOf(s, static_cast<int>(s.size())), 8);
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
    CHECK_EQ(cols(t), 3);
    CHECK_EQ(utf8::truncate(t, 1), "");
    CHECK_EQ(utf8::truncate(t, 2), "\xe4\xbd\xa0");
    CHECK_EQ(utf8::truncate(t, 2).size(), 3u);
    CHECK_EQ(utf8::truncate(t, 3), t);
}

TEST(truncate_four_byte_char) {
    const std::string s = U_EMOJI;
    CHECK_EQ(cols(s), 2);
    CHECK_EQ(utf8::truncate(s, 1), "");
    CHECK_EQ(utf8::truncate(s, 2), s);
    CHECK_EQ(utf8::truncate(s, 2).size(), 4u);
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
    CHECK_EQ(cols(s), 8);
    CHECK_EQ(utf8::truncate(s, 3), "abc");
    CHECK_EQ(utf8::truncate(s, 4), "abc");
    CHECK_EQ(utf8::truncate(s, 5), "abc" U_EMOJI);
    CHECK_EQ(utf8::truncate(s, 8), s);
}

TEST(truncate_all_multibyte) {
    const std::string s = U_E U_DASH U_EMOJI;
    CHECK_EQ(cols(s), 4);
    CHECK_EQ(utf8::truncate(s, 1), U_E);
    CHECK_EQ(utf8::truncate(s, 2), U_E U_DASH);
    CHECK_EQ(utf8::truncate(s, 3), U_E U_DASH);
    CHECK_EQ(utf8::truncate(s, 4), s);
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
    CHECK_EQ(utf8::range(s, 0, 10), "abc" U_E U_DASH U_EMOJI "xyz");
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

TEST(range_wide_before_inside_after) {
    const std::string s = "abc" U_EMOJI "def";
    CHECK_EQ(utf8::range(s, 0, 3), "abc");
    CHECK_EQ(utf8::range(s, 0, 4), "abc" U_EMOJI);
    CHECK_EQ(utf8::range(s, 0, 5), "abc" U_EMOJI);
    CHECK_EQ(utf8::range(s, 0, 6), "abc" U_EMOJI "d");
    CHECK_EQ(utf8::range(s, 3, 4), U_EMOJI);
    CHECK_EQ(utf8::range(s, 3, 5), U_EMOJI);
    CHECK_EQ(utf8::range(s, 4, 5), "");
    CHECK_EQ(utf8::range(s, 4, 6), "d");
    CHECK_EQ(utf8::range(s, 5, 6), "d");
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

TEST(cellStartBefore_orphan_overflow_at_zero_consistent) {
    std::string s0 = std::string("\xC3\x80\x80", 3);
    std::string s1 = std::string("a\xC3\x80\x80", 4);
    CHECK_EQ(utf8::cellStartBefore(s0, 3), 2);
    CHECK_EQ(utf8::cellStartBefore(s1, 4), 3);
    CHECK_EQ(utf8::alignStart(s0, 2), 2);
    CHECK_EQ(utf8::alignStart(s1, 3), 3);
    CHECK_EQ(utf8::isCellStart(s0, 2), true);
    CHECK_EQ(utf8::isCellStart(s1, 3), true);
}

TEST(isValid_empty_and_ascii) {
    CHECK(utf8::isValid(""));
    CHECK(utf8::isValid("hello"));
    CHECK(utf8::isValid("café"));
    CHECK(utf8::isValid("😀"));
}

TEST(isValid_rejects_overlong_and_surrogates) {
    CHECK(!utf8::isValid("\xC0\x80"));
    CHECK(!utf8::isValid("\xE0\x80\x80"));
    CHECK(!utf8::isValid("\xED\xA0\x80"));
    CHECK(!utf8::isValid("\xF4\x90\x80\x80"));
    CHECK(!utf8::isValid("\xC3"));
    CHECK(!utf8::isValid("\x80"));
}

TEST(columnCache_invalidation_explicit_same_storage) {
    std::string line = "aaaaaaaaaa";
    line.reserve(32);
    Cursor cur;
    cur.col = 10;
    CHECK_EQ(cur.visualColumn(line), 10);
    const char* oldData = line.data();
    int oldSize = (int)line.size();
    line[5] = char(0xC3);
    line[6] = char(0xA9);
    CHECK_EQ(line.data(), oldData);
    CHECK_EQ((int)line.size(), oldSize);
    CHECK_EQ(utf8::columnOf(line, 10), 9);
    CHECK_EQ(cur.visualColumn(line), 10);
    cur.invalidateColumnCache();
    CHECK_EQ(cur.visualColumn(line), 9);
    CHECK_EQ(cur.visualColumn(line), utf8::columnOf(line, 10));
}

TEST(columnCache_contract_caller_must_invalidate) {
    std::string line = "abcdefgh";
    Cursor cur;
    cur.col = 8;
    CHECK_EQ(cur.visualColumn(line), 8);
    std::string mutated = "abcd";
    mutated += "\xC3\xA9";
    mutated += "fg";
    line.assign(mutated);
    CHECK_EQ((int)line.size(), 8);
    CHECK_EQ(cur.visualColumn(line), 8);
    cur.invalidateColumnCache();
    CHECK_EQ(cur.visualColumn(line), 7);
}

TEST(columnCache_reallocation_no_false_hit) {
    std::string line = "abcdefgh";
    line.reserve(16);
    Cursor cur;
    cur.col = 8;
    CHECK_EQ(cur.visualColumn(line), 8);
    const char* oldData = line.data();
    int oldSize = (int)line.size();
    line.append(100, 'x');
    CHECK(line.data() != oldData);
    CHECK((int)line.size() != oldSize);
    CHECK_EQ(cur.visualColumn(line), utf8::columnOf(line, 8));
    cur.col = (int)line.size();
    CHECK_EQ(cur.visualColumn(line), utf8::columnOf(line, (int)line.size()));
}

TEST(columnCache_correctness_exhaustive) {
    auto makeMixed = [](int cols) {
        std::string s;
        s.reserve(cols * 3);
        for (int i = 0; i < cols; ++i) {
            if (i % 4 == 0) s += "a";
            else if (i % 4 == 1) s += "\xC3\xA9";
            else if (i % 4 == 2) s += "\xE2\x80\x94";
            else s += "\xF0\x9F\x98\x80";
        }
        return s;
    };
    auto buildExpected = [](const std::string& line) {
        std::vector<int> expected(line.size() + 1);
        int col = 0;
        int i = 0;
        int n = static_cast<int>(line.size());
        while (i < n) {
            expected[i] = col;
            if (line[i] == '\t')
                col = ((col / utf8::TAB_WIDTH) + 1) * utf8::TAB_WIDTH;
            else
                col += utf8::cellWidth(line, i, n);
            i += utf8::cellLen(line, i, n);
        }
        expected[n] = col;
        return expected;
    };
    auto verify = [&](const std::string& line, int startByte) {
        const int n = static_cast<int>(line.size());
        const auto expected = buildExpected(line);
        startByte = std::min(startByte, n);
        startByte = utf8::alignStart(line, startByte);
        std::vector<int> steps;
        int b = startByte;
        for (int k = 0; k < 100 && b > 0; ++k) {
            int prev = utf8::cellStartBefore(line, b);
            if (prev == b) break;
            steps.push_back(prev);
            b = prev;
        }
        if (steps.empty()) return;
        Cursor cur;
        cur.col = startByte;
        CHECK_EQ(cur.visualColumn(line), expected[startByte]);
        for (int v : steps) {
            cur.col = v;
            CHECK_EQ(cur.visualColumn(line), expected[v]);
        }
        std::vector<int> jumps;
        jumps.reserve(32 + steps.size() * 2);
        jumps.push_back(startByte);
        if (!steps.empty()) {
            jumps.push_back(steps.front());
            jumps.push_back(steps.back());
        }
        jumps.push_back(n / 2);
        jumps.push_back(0);
        jumps.push_back(80);
        jumps.push_back(1000);
        jumps.push_back(n);
        jumps.push_back(n - 1);
        jumps.push_back(1);
        jumps.push_back(50000);
        if (n > 10) {
            jumps.push_back(10);
            jumps.push_back(n - 10);
        }
        for (int iter = 0; iter < 2; ++iter) {
            jumps.push_back(startByte);
            for (int v : steps) jumps.push_back(v);
            jumps.push_back(n / 2);
            for (int k = 0; k < 5 && k < static_cast<int>(steps.size()); ++k) jumps.push_back(steps[k]);
            jumps.push_back(0);
            jumps.push_back(80);
            jumps.push_back(n);
        }
        for (int raw : jumps) {
            int v = utf8::alignStart(line, std::min(std::max(raw, 0), n));
            cur.col = v;
            CHECK_EQ(cur.visualColumn(line), expected[v]);
        }
    };
    std::string ascii100k(100000, 'a');
    std::string utf8_100k = makeMixed(40000);
    verify(ascii100k, 50000);
    verify(utf8_100k, 50000);
    verify(utf8_100k, static_cast<int>(utf8_100k.size()));
}

TEST(columnCache_consistency_wide) {
    std::string line = "a\xE2\x9D\x8C" "bc\xF0\x9F\x98\x80" "\xE4\xB8\xAD";
    int n = static_cast<int>(line.size());
    std::vector<int> cells;
    for (int i = 0; i < n; ) {
        if (!utf8::isCellStart(line, i)) { ++i; continue; }
        cells.push_back(i);
        i += utf8::cellLen(line, i, n);
    }
    cells.push_back(n);
    Cursor cur;
    for (int pos : cells) {
        cur.col = pos;
        cur.invalidateColumnCache();
        int cached = cur.visualColumn(line);
        int direct = utf8::columnOf(line, pos);
        CHECK_EQ(cached, direct);
    }
    cur.col = 0;
    cur.invalidateColumnCache();
    for (size_t k = 0; k < cells.size(); ++k) {
        int pos = cells[k];
        cur.col = pos;
        int cached = cur.visualColumn(line);
        int direct = utf8::columnOf(line, pos);
        CHECK_EQ(cached, direct);
    }
    cur.col = n;
    cur.invalidateColumnCache();
    (void)cur.visualColumn(line);
    for (int k = static_cast<int>(cells.size()) - 1; k >= 0; --k) {
        int pos = cells[k];
        cur.col = pos;
        int cached = cur.visualColumn(line);
        int direct = utf8::columnOf(line, pos);
        CHECK_EQ(cached, direct);
    }
}

TEST(codepointWidth_boundaries_fixed) {
    CHECK_EQ(utf8::codepointWidth(0x1F320), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F321), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F32C), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F32D), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F93A), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F93B), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F93C), 2);
    CHECK_EQ(utf8::codepointWidth(0x1FA70), 2);
    CHECK_EQ(utf8::codepointWidth(0x1FA7C), 2);
    CHECK_EQ(utf8::codepointWidth(0x1FA7D), 1);
    CHECK_EQ(utf8::codepointWidth(0x1FA80), 2);
    CHECK_EQ(utf8::codepointWidth(0x1FA8A), 2);
    CHECK_EQ(utf8::codepointWidth(0x1FA8B), 1);
    CHECK_EQ(utf8::codepointWidth(0x1FA8E), 2);
    CHECK_EQ(utf8::codepointWidth(0x1FAC6), 2);
    CHECK_EQ(utf8::codepointWidth(0x1FAC7), 1);
    CHECK_EQ(utf8::codepointWidth(0x1FAEF), 2);
    CHECK_EQ(utf8::codepointWidth(0x1FAF8), 2);
}

TEST(codepointWidth_direct) {
    CHECK_EQ(utf8::codepointWidth('A'), 1);
    CHECK_EQ(utf8::codepointWidth(0xE9), 1);
    CHECK_EQ(utf8::codepointWidth(0x2014), 1);
    CHECK_EQ(utf8::codepointWidth(0x4E2D), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F600), 2);
    CHECK_EQ(utf8::codepointWidth(0x274C), 2);
    CHECK_EQ(utf8::codepointWidth(0xFF21), 2);
    CHECK_EQ(utf8::codepointWidth(0xD55C), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F321), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F32D), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F3CB), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F3CF), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F93B), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F93C), 2);
    CHECK_EQ(utf8::codepointWidth(0x1FA70), 2);
}

TEST(byteForColumn_wide) {
    const std::string s = "abc" U_EMOJI;
    CHECK_EQ(utf8::byteForColumn(s, 0), 0);
    CHECK_EQ(utf8::byteForColumn(s, 1), 1);
    CHECK_EQ(utf8::byteForColumn(s, 2), 2);
    CHECK_EQ(utf8::byteForColumn(s, 3), 3);
    CHECK_EQ(utf8::byteForColumn(s, 4), 3);
    CHECK_EQ(utf8::byteForColumn(s, 5), 7);
}

TEST(apis_concordance_ae_dash_emoji_zhong) {
    const std::string line = "a\xc3\xa9\xe2\x80\x94\xf0\x9f\x98\x80\xe4\xb8\xad" "b";
    struct Case { int bytePos; int col; };
    Case cases[] = {
        {0, 0},
        {1, 1},
        {3, 2},
        {6, 3},
        {10, 5},
        {13, 7},
        {14, 8},
    };
    for (auto c : cases) {
        CHECK_EQ(utf8::columnOf(line, c.bytePos), c.col);
        CHECK_EQ(utf8::byteForColumn(line, c.col), c.bytePos);
        Cursor cur; cur.col = c.bytePos; cur.invalidateColumnCache();
        CHECK_EQ(cur.visualColumn(line), c.col);
        CHECK_EQ(utf8::columnOf(line, utf8::byteForColumn(line, c.col)), c.col);
    }
    CHECK_EQ(utf8::columnOf(line, 4), 3);
    CHECK_EQ(utf8::byteForColumn(line, 4), 6);
}

TEST(codepointWidth_eaw_gap_boundaries) {
    CHECK_EQ(utf8::codepointWidth(0x1F202), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F203), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F20F), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F210), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F265), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F266), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F6D5), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F6D9), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F7E0), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F7F0), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F90B), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F90C), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F945), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F946), 1);
    CHECK_EQ(utf8::codepointWidth(0x1F947), 2);
    CHECK_EQ(utf8::codepointWidth(0x1F99F), 2);
    CHECK_EQ(utf8::codepointWidth(0x1FA00), 1);
}

