#include "syntax/SyntaxCache.h"
#include <functional>
#include "test_framework.h"
#include "core/Document.h"
#include "core/BracketMatcher.h"
#include "syntax/SyntaxLanguage.h"

TEST(bracket_matcher_single) {
    Document doc;
    doc.restore({"{}"});
    auto p = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.col, 0); CHECK_EQ(p->close.col, 1);
    auto p2 = findMatchingBracket(doc, {0,1}, SyntaxLanguage::Cpp);
    CHECK(p2.has_value()); CHECK_EQ(p2->open.col, 0); CHECK_EQ(p2->close.col, 1);
    doc.restore({"[]"});
    auto p3 = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p3.has_value()); CHECK_EQ(p3->open.col, 0); CHECK_EQ(p3->close.col, 1);
    doc.restore({"()"});
    auto p4 = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p4.has_value()); CHECK_EQ(p4->open.col, 0); CHECK_EQ(p4->close.col, 1);
}
TEST(bracket_matcher_nested) {
    Document doc;
    doc.restore({"{[]}"});
    auto p = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p.has_value());
    CHECK_EQ(p->open.col, 0); CHECK_EQ(p->close.col, 3);
    doc.restore({"([{}])"});
    auto p2 = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p2.has_value());
    CHECK_EQ(p2->open.col, 0); CHECK_EQ(p2->close.col, 5);
}
TEST(bracket_matcher_cursor_on_opening) {
    Document doc; doc.restore({"(a)"});
    auto p = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.col, 0);
}
TEST(bracket_matcher_cursor_on_closing) {
    Document doc; doc.restore({"(a)"});
    auto p = findMatchingBracket(doc, {0,2}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->close.col, 2);
}
TEST(bracket_matcher_cursor_inside) {
    Document doc; doc.restore({"(a)"});
    auto p = findMatchingBracket(doc, {0,1}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.col, 0);
    doc.restore({"[x]"}); CHECK(findMatchingBracket(doc, {0,1}, SyntaxLanguage::Cpp).has_value());
    doc.restore({"{x}"}); CHECK(findMatchingBracket(doc, {0,1}, SyntaxLanguage::Cpp).has_value());
}
TEST(bracket_matcher_nested_inside) {
    Document doc; doc.restore({"({[]})"});
    auto p = findMatchingBracket(doc, {0,2}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.col, 2);
    auto p2 = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK_EQ(p2->open.col, 0);
}
TEST(bracket_matcher_multiline) {
    Document doc; doc.restore({"{" ,"  foo();" ,"}"});
    auto p = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.line, 0); CHECK_EQ(p->close.line, 2);
    auto p2 = findMatchingBracket(doc, {1,3}, SyntaxLanguage::Cpp);
    CHECK(p2.has_value()); CHECK_EQ(p2->open.line, 0);
}
TEST(bracket_matcher_multiline_nested) {
    Document doc; doc.restore({"{" ,"  [" ,"    (" ,"    )" ,"  ]" ,"}"});
    auto p = findMatchingBracket(doc, {2,4}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.line, 2);
    auto p2 = findMatchingBracket(doc, {1,2}, SyntaxLanguage::Cpp);
    CHECK(p2.has_value()); CHECK_EQ(p2->open.line, 1);
}
TEST(bracket_matcher_mismatch) {
    Document doc; doc.restore({"{[}]"});
    CHECK(!findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp).has_value());
    doc.restore({"([)]"});
    CHECK(!findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp).has_value());
    doc.restore({"{[}]"});
    CHECK(!findMatchingBracket(doc, {0,1}, SyntaxLanguage::Cpp).has_value());
}
TEST(bracket_matcher_ignore_string) {
    Document doc; doc.restore({"std::string s = \"{[()]}\";"});
    // Bracket dentro de string literal debe ser ignorado: calcular posición del '{' dentro de las comillas, no hardcodear col 19
    const std::string& line = doc.lineAt(0);
    size_t quote = line.find('"');
    CHECK(quote != std::string::npos);
    size_t brace = line.find('{', quote);
    CHECK(brace != std::string::npos);
    Position p{0, (int)brace}; // brace dentro de string, byte column (Maestro usa byte column)
    CHECK(!findMatchingBracket(doc, p, SyntaxLanguage::Cpp).has_value());
    // También verificar '[' y '(' dentro del mismo string
    size_t bracket = line.find('[', quote);
    CHECK(!findMatchingBracket(doc, {0, (int)bracket}, SyntaxLanguage::Cpp).has_value());
}
TEST(bracket_matcher_ignore_char) {
    Document doc; doc.restore({"char c = '}';"});
    // '}' dentro de char literal debe ser ignorado: buscar la comilla simple y el bracket dentro
    const std::string& line = doc.lineAt(0);
    size_t firstQuote = line.find('\'');
    CHECK(firstQuote != std::string::npos);
    size_t brace = line.find('}', firstQuote);
    CHECK(brace != std::string::npos);
    Position p{0, (int)brace}; // byte column del '}' dentro de '}' (char literal)
    CHECK(!findMatchingBracket(doc, p, SyntaxLanguage::Cpp).has_value());
}
TEST(bracket_matcher_ignore_line_comment) {
    Document doc; doc.restore({"// ( [ ] )"});
    CHECK(!findMatchingBracket(doc, {0,3}, SyntaxLanguage::Cpp).has_value());
}
TEST(bracket_matcher_ignore_block_comment) {
    Document doc; doc.restore({"/* { */", "int x = (1);"});
    CHECK(!findMatchingBracket(doc, {0,3}, SyntaxLanguage::Cpp).has_value());
    CHECK(findMatchingBracket(doc, {1,8}, SyntaxLanguage::Cpp).has_value());
}
TEST(bracket_matcher_unicode_before) {
    Document doc; doc.restore({"é[ ]"});
    // 'é' = 2 bytes (0xC3 0xA9), '[' en byte 2, ']' en byte 4: col es byte column (Maestro usa byte column, no visual)
    const std::string& line = doc.lineAt(0);
    CHECK_EQ((int)line.find('['), 2);
    CHECK_EQ((int)line.find(']'), 4);
    auto p = findMatchingBracket(doc, {0, (int)line.find(']')}, SyntaxLanguage::Cpp); // cursor sobre ']' (byte 4)
    CHECK(p.has_value());
    CHECK_EQ(p->open.col, 2); // byte column
}
TEST(bracket_matcher_tab_before) {
    Document doc; doc.restore({"\t{", "\tfoo();", "\t}"});
    // '\t' = 1 byte, visual 4 columnas, pero col es byte column: '{' en byte 1, cursor en byte 1 dentro de "\tfoo();"
    auto p = findMatchingBracket(doc, {1,1}, SyntaxLanguage::Cpp); // byte column 1, dentro del rango {\n...\n}
    CHECK(p.has_value());
    CHECK_EQ(p->open.line, 0);
    CHECK_EQ(p->open.col, 1); // byte column, no visual
}

TEST(bracket_matcher_cache_document_reuse) {
    std::optional<Document> slot;

    slot.emplace();
    slot->restore({
        "line0",
        "line1",
        "line2",
        "line3",
        "line4",
        "{}"
    });

    auto p1 = findMatchingBracket(*slot, {5, 0}, SyntaxLanguage::Cpp);
    CHECK(p1.has_value());

    slot.reset();
    slot.emplace();
    slot->restore({
        "line0",
        "line1",
        "line2",
        "line3",
        "line4",
        "\"{}\""
    });

    auto p2 = findMatchingBracket(*slot, {5, 1}, SyntaxLanguage::Cpp);
    CHECK(!p2.has_value());
}
namespace {

BracketSpanSource makeWarmCacheSource(SyntaxCache& cache) {
    BracketSpanSource src;
    src.ensure = [](int) {};
    src.spans = [&cache](int line) -> const std::vector<SyntaxSpan>& {
        static const std::vector<SyntaxSpan> empty;
        if (line < 0 || line >= (int)cache.size()) return empty;
        return cache.spansFor(line);
    };
    return src;
}

} // namespace

// ---------------------------------------------------------------------------
// findMatchingBracketBounded
// ---------------------------------------------------------------------------

TEST(bracket_matcher_bounded_open_inside_close_outside) {
    Document doc;
    doc.restore({"{", "x", "x", "x", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketBounded(doc, {0, 0}, SyntaxLanguage::Cpp, src, 0, 3);
    CHECK(!p.has_value());

    auto full = findMatchingBracketBounded(doc, {0, 0}, SyntaxLanguage::Cpp, src, 0, doc.lineCount());
    CHECK(full.has_value());
    CHECK_EQ(full->open.line, 0);
    CHECK_EQ(full->close.line, 4);
}

TEST(bracket_matcher_bounded_close_inside_open_outside) {
    Document doc;
    doc.restore({"{", "x", "x", "x", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketBounded(doc, {4, 0}, SyntaxLanguage::Cpp, src, 2, 5);
    CHECK(!p.has_value());

    auto full = findMatchingBracketBounded(doc, {4, 0}, SyntaxLanguage::Cpp, src, 0, 5);
    CHECK(full.has_value());
    CHECK_EQ(full->open.line, 0);
    CHECK_EQ(full->close.line, 4);
}

TEST(bracket_matcher_bounded_enclosing_visible) {
    Document doc;
    doc.restore({"{", "  x", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketBounded(doc, {1, 2}, SyntaxLanguage::Cpp, src, 0, 3);
    CHECK(p.has_value());
    CHECK_EQ(p->open.line, 0);
    CHECK_EQ(p->close.line, 2);

    // Si el open queda fuera del rango, no debe reportar envolvente.
    auto p2 = findMatchingBracketBounded(doc, {1, 2}, SyntaxLanguage::Cpp, src, 1, 3);
    CHECK(!p2.has_value());
}

// ---------------------------------------------------------------------------
// findMatchingBracketFrom
// ---------------------------------------------------------------------------

TEST(bracket_matcher_from_open_forward) {
    Document doc;
    doc.restore({"{", "x", "x", "x", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketFrom(doc, {0, 0}, SyntaxLanguage::Cpp, src);
    CHECK(p.has_value());
    CHECK_EQ(p->open.line, 0);
    CHECK_EQ(p->close.line, 4);
}

TEST(bracket_matcher_from_close_backward) {
    Document doc;
    doc.restore({"{", "x", "x", "x", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketFrom(doc, {4, 0}, SyntaxLanguage::Cpp, src);
    CHECK(p.has_value());
    CHECK_EQ(p->open.line, 0);
    CHECK_EQ(p->close.line, 4);
}

TEST(bracket_matcher_from_enclosing_backward) {
    Document doc;
    doc.restore({"{", "  x", "  y", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketFrom(doc, {2, 1}, SyntaxLanguage::Cpp, src);
    CHECK(p.has_value());
    CHECK_EQ(p->open.line, 0);
    CHECK_EQ(p->close.line, 3);
}

TEST(bracket_matcher_from_no_bracket) {
    Document doc;
    doc.restore({"abc", "def"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketFrom(doc, {0, 1}, SyntaxLanguage::Cpp, src);
    CHECK(!p.has_value());
}

// ---------------------------------------------------------------------------
// Equivalencia backward/forward en contornos malformados acordados
// ---------------------------------------------------------------------------

TEST(bracket_matcher_from_backward_forward_equivalence_contours) {
    auto checkCase = [](std::vector<std::string> lines, Position pos) {
        Document doc;
        doc.restore(lines);

        SyntaxCache cache;
        cache.setLanguage(SyntaxLanguage::Cpp);
        cache.ensureValid(doc, doc.lineCount());

        auto src = makeWarmCacheSource(cache);

        auto legacy = findMatchingBracket(doc, pos, SyntaxLanguage::Cpp);
        auto incremental = findMatchingBracketFrom(doc, pos, SyntaxLanguage::Cpp, src);

        CHECK(legacy.has_value() == incremental.has_value());
        if (legacy.has_value() && incremental.has_value()) {
            CHECK(*legacy == *incremental);
        }
    };

    // Envolvente bien formado con close posterior.
    checkCase({"} { x }"}, {0, 4});

    // Envolvente anidado bien formado.
    checkCase({"({ { } x })"}, {0, 7});

    // Mismatch directo en bracket abierto/cerrado.
    checkCase({"{[}]"}, {0, 0});
    checkCase({"{[}]"}, {0, 2});
    checkCase({"([)]"}, {0, 0});
}