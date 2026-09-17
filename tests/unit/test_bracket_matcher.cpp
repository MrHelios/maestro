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
