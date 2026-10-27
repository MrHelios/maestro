#include "test_framework.h"
#include "syntax/SyntaxHighlighter.h"
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"
// #include "syntax/SyntaxCache.h" // Descomentar si se vuelven a agregar tests de cache
#include "document/Document.h"
#include <string>
#include <vector>
#include <cstring>
#include <cctype>
#include <algorithm>

// --- Helpers Reutilizables ---

static bool hasSpan(const std::vector<SyntaxSpan>& spans, size_t b, size_t e, SyntaxToken tok) {
    for (auto &s : spans) if (s.begin==b && s.end==e && s.token==tok) return true;
    return false;
}

// Verifica invariantes básicos de integridad de spans
static void checkSpanInvariants(const std::vector<SyntaxSpan>& spans,
                                std::string_view line) {
    size_t prevEnd = 0;
    for (const auto& s : spans) {
        CHECK(s.begin < s.end);
        CHECK(s.end <= line.size());
        // Asumimos contrato de no solapamiento y orden
        CHECK(s.begin >= prevEnd);
        prevEnd = s.end;
    }
}

static bool hasAnyToken(const std::vector<SyntaxSpan>& spans, SyntaxToken tok) {
    for (const auto& s : spans) {
        if (s.token == tok) return true;
    }
    return false;
}

static bool hasSpanText(const std::vector<SyntaxSpan>& spans,
                        std::string_view line,
                        std::string_view text,
                        SyntaxToken tok) {
    for (const auto& s : spans) {
        if (s.token == tok && line.substr(s.begin, s.end - s.begin) == text) {
            return true;
        }
    }
    return false;
}

// --- Tests Originales (con invariants agregados donde aplica) ---

TEST(Clike_EmptyLine) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("");
    CHECK(spans.empty());
}

TEST(Clike_SimpleKeywordsAndTypes) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "int x = 0; return x;";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(spans, 11, 17, SyntaxToken::Keyword));
}

TEST(Clike_StringWithEscapes) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "const char* s = \"hello \\\"world\\\"\";";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 16, 33, SyntaxToken::String));
}

TEST(Clike_UnclosedString) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "const char* s = \"unclosed";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 16, 25, SyntaxToken::String));
}

TEST(Clike_LineComment) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "int x; // comment";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 7, 17, SyntaxToken::Comment));
}

TEST(Clike_BlockCommentSingleLine) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "/* comment */ int x;";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 0, 13, SyntaxToken::Comment));
    CHECK(hasSpan(spans, 14, 17, SyntaxToken::Type));
}

TEST(Clike_BlockCommentMultiLine_State) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    SyntaxState st{}; st.inBlockComment=false;
    SyntaxState out;
    
    std::string l1 = "/* start";
    auto spans1 = hl.highlight(l1, st, out); st=out;
    checkSpanInvariants(spans1, l1);
    CHECK(hasSpan(spans1, 0, 8, SyntaxToken::Comment));
    CHECK(st.inBlockComment);
    
    std::string l2 = " middle ";
    auto spans2 = hl.highlight(l2, st, out); st=out;
    checkSpanInvariants(spans2, l2);
    CHECK(hasSpan(spans2, 0, 8, SyntaxToken::Comment));
    CHECK(st.inBlockComment);
    
    std::string l3 = " end */ int x;";
    auto spans3 = hl.highlight(l3, st, out); st=out;
    checkSpanInvariants(spans3, l3);
    CHECK(hasSpan(spans3, 0, 7, SyntaxToken::Comment));
    CHECK(!st.inBlockComment);
    CHECK(hasSpan(spans3, 8, 11, SyntaxToken::Type));
}

TEST(Clike_Preprocessor) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line1 = "#include <iostream>";
    auto spans = hl.highlight(line1);
    checkSpanInvariants(spans, line1);
    CHECK(hasSpan(spans, 0, 19, SyntaxToken::Preprocessor));
    
    std::string line2 = "  #define FOO 1";
    auto spans2 = hl.highlight(line2);
    checkSpanInvariants(spans2, line2);
    CHECK(hasSpan(spans2, 2, 15, SyntaxToken::Preprocessor));
}

TEST(Clike_PreprocessorAfterBlockComment) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line1 = "/* comment */ #define FOO";
    auto spans = hl.highlight(line1);
    checkSpanInvariants(spans, line1);
    CHECK(hasSpan(spans, 0, 13, SyntaxToken::Comment));
    CHECK(hasSpan(spans, 14, 25, SyntaxToken::Preprocessor));
    
    SyntaxState st{}; st.inBlockComment=true;
    SyntaxState out;
    std::string line2 = " still */ #define BAR";
    auto spans2 = hl.highlight(line2, st, out);
    checkSpanInvariants(spans2, line2);
    CHECK(hasSpan(spans2, 0, 9, SyntaxToken::Comment));
    CHECK(hasSpan(spans2, 10, 21, SyntaxToken::Preprocessor));
}

TEST(Clike_PreprocessorWithCodeBefore) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "/* comment */ foo #define BAR";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 0, 13, SyntaxToken::Comment));
    CHECK(!hasSpan(spans, 14, 25, SyntaxToken::Preprocessor));
    bool hasPP=false; for(auto &s:spans) if(s.token==SyntaxToken::Preprocessor) hasPP=true;
    CHECK(!hasPP);
}

TEST(Clike_PreprocessorMultipleLeadingComments) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "/**/ /* */ #define X";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 0, 4, SyntaxToken::Comment));
    CHECK(hasSpan(spans, 5, 10, SyntaxToken::Comment));
    CHECK(hasSpan(spans, 11, 20, SyntaxToken::Preprocessor));
}

TEST(Clike_Numbers) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "42 0xFF 0b1010 3.14 1e10";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 0, 2, SyntaxToken::Number));
    CHECK(hasSpan(spans, 3, 7, SyntaxToken::Number));
    CHECK(hasSpan(spans, 8, 14, SyntaxToken::Number));
    CHECK(hasSpan(spans, 15, 19, SyntaxToken::Number));
    CHECK(hasSpan(spans, 20, 24, SyntaxToken::Number));
}

TEST(Clike_InvalidHexBin) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line1 = "0xG";
    auto spans = hl.highlight(line1);
    checkSpanInvariants(spans, line1);
    CHECK(hasSpan(spans, 0, 1, SyntaxToken::Number));
    CHECK(!hasSpan(spans, 0, 3, SyntaxToken::Number));
    
    std::string line2 = "0b2";
    auto spans2 = hl.highlight(line2);
    checkSpanInvariants(spans2, line2);
    CHECK(hasSpan(spans2, 0, 1, SyntaxToken::Number));
    
    std::string line3 = "0xFFu";
    auto spans3 = hl.highlight(line3);
    checkSpanInvariants(spans3, line3);
    CHECK(hasSpan(spans3, 0, 5, SyntaxToken::Number));
}

TEST(Clike_RawString) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string l1 = "auto s = R\"(hello \"world\")\";";
    auto spans = hl.highlight(l1);
    checkSpanInvariants(spans, l1);
    CHECK(spans.size() >= 2);
    bool hasRaw=false;
    for(auto &s:spans) if(s.token==SyntaxToken::String) hasRaw=true;
    CHECK(hasRaw);
    
    std::string l2 = "auto s = R\"delim(Hello \"world\")delim\";";
    auto spans2 = hl.highlight(l2);
    checkSpanInvariants(spans2, l2);
    CHECK(hasSpan(spans2, 9, l2.size()-1, SyntaxToken::String));
}

TEST(Clike_RawStringMultiline) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    SyntaxState st{}; SyntaxState out;
    
    std::string l1 = "auto s = R\"foo(";
    auto s1 = hl.highlight(l1, st, out); st=out;
    checkSpanInvariants(s1, l1);
    CHECK(st.inRawString);
    CHECK(std::string_view(st.rawDelim, st.rawDelimLen)=="foo");
    
    std::string l2 = "still raw";
    auto s2 = hl.highlight(l2, st, out); st=out;
    checkSpanInvariants(s2, l2);
    CHECK(hasSpan(s2, 0, l2.size(), SyntaxToken::String));
    CHECK(st.inRawString);
    
    std::string l3 = ")foo\"; int x=1;";
    auto s3 = hl.highlight(l3, st, out); st=out;
    checkSpanInvariants(s3, l3);
    CHECK(!st.inRawString);
    CHECK(st.rawDelimLen==0);
    CHECK(std::string_view(st.rawDelim, st.rawDelimLen).empty());
    CHECK(hasSpan(s3, 0, 5, SyntaxToken::String));
    CHECK(hasSpan(s3, 7, 10, SyntaxToken::Type));
}

TEST(Clike_StringVsComment_Interaction) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "\"/* not a comment */\"";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 0, 21, SyntaxToken::String));
    CHECK(spans.size()==1);
}

TEST(Clike_CMode) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::C);
    std::string line = "class template restrict _Bool int";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(!hasSpan(spans, 0, 5, SyntaxToken::Keyword));
    CHECK(!hasSpan(spans, 6, 14, SyntaxToken::Keyword));
    CHECK(hasSpan(spans, 15, 23, SyntaxToken::Keyword));
    CHECK(hasSpan(spans, 24, 29, SyntaxToken::Keyword));
    CHECK(hasSpan(spans, 30, 33, SyntaxToken::Type));
    
    SyntaxHighlighter hlCpp; hlCpp.setLanguage(SyntaxLanguage::Cpp);
    auto spansCpp = hlCpp.highlight(line);
    checkSpanInvariants(spansCpp, line);
    CHECK(hasSpan(spansCpp, 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(spansCpp, 6, 14, SyntaxToken::Keyword));
}

TEST(Clike_CharacterLiteral) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string l1 = "'a'";
    auto s1 = hl.highlight(l1);
    checkSpanInvariants(s1, l1);
    CHECK(hasSpan(s1, 0, 3, SyntaxToken::Character));
    
    std::string l2 = "'\\n'"; 
    auto s2 = hl.highlight(l2);
    checkSpanInvariants(s2, l2);
    CHECK(hasSpan(s2, 0, 4, SyntaxToken::Character));
    
    std::string l3 = "'\\''";
    auto s3 = hl.highlight(l3);
    checkSpanInvariants(s3, l3);
    CHECK(hasSpan(s3, 0, 4, SyntaxToken::Character));
    
    std::string l4 = "'\\\\'";
    auto s4 = hl.highlight(l4);
    checkSpanInvariants(s4, l4);
    CHECK(hasSpan(s4, 0, 4, SyntaxToken::Character));
}

TEST(Clike_LineCommentWithKeyword) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "int x; // int y; class foo";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(spans, 7, line.size(), SyntaxToken::Comment));
    for (auto &s : spans) {
        if (s.token==SyntaxToken::Type || s.token==SyntaxToken::Keyword) {
            CHECK(s.end <= 7);
        }
    }
}

// --- Nuevos Tests P1: Identificadores y Palabras Reservadas ---

TEST(Clike_IdentifierBoundaries_NoPartialKeywords) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);

    auto checkNoPartial = [&](const char* text, size_t badBegin, size_t badEnd, SyntaxToken tok) {
        std::string line = text;
        auto spans = hl.highlight(line);
        checkSpanInvariants(spans, line);
        CHECK(!hasSpan(spans, badBegin, badEnd, tok));
    };

    checkNoPartial("integer", 0, 3, SyntaxToken::Type);       // "int" dentro de "integer"
    checkNoPartial("returnValue", 0, 6, SyntaxToken::Keyword); // "return" dentro de "returnValue"
    checkNoPartial("className", 0, 5, SyntaxToken::Keyword);   // "class" dentro de "className"
    checkNoPartial("namespace2", 0, 9, SyntaxToken::Keyword);  // "namespace" dentro de "namespace2"
    checkNoPartial("sizeofx", 0, 6, SyntaxToken::Keyword);     // "sizeof" dentro de "sizeofx"
    checkNoPartial("INT", 0, 3, SyntaxToken::Type);            // Case sensitive
    checkNoPartial("Return", 0, 6, SyntaxToken::Keyword);      // Case sensitive
}

TEST(Clike_IdentifierBoundaries_StandaloneKeywordsStillWork) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);

    auto spansInt = hl.highlight("int");
    CHECK(hasSpan(spansInt, 0, 3, SyntaxToken::Type));

    auto spansReturn = hl.highlight("return");
    CHECK(hasSpan(spansReturn, 0, 6, SyntaxToken::Keyword));

    auto spansClass = hl.highlight("class");
    CHECK(hasSpan(spansClass, 0, 5, SyntaxToken::Keyword));
}

// --- Nuevos Tests P1: Variantes de Números ---

TEST(Clike_NumberVariants_BasicFloatingAndHexFloat) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto expectWholeNumber = [&](const char* text) {
        std::string line = text;
        auto spans = hl.highlight(line);
        checkSpanInvariants(spans, line);
        CHECK(hasSpan(spans, 0, line.size(), SyntaxToken::Number));
    };

    expectWholeNumber(".5");
    expectWholeNumber("1.");
    expectWholeNumber("1e+10");
    expectWholeNumber("1e-10");
    expectWholeNumber("0x1.fp+2");
    expectWholeNumber("42ULL");
    expectWholeNumber("3.14f");
    expectWholeNumber("3.14F");
    expectWholeNumber("42ull");
    expectWholeNumber("42LL");
    expectWholeNumber("42ll");
}

TEST(Clike_NumberVariants_DigitSeparators) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);

    auto spans1 = hl.highlight("1'000");
    checkSpanInvariants(spans1, "1'000");
    CHECK(hasSpan(spans1, 0, 5, SyntaxToken::Number));
    CHECK(!hasAnyToken(spans1, SyntaxToken::Character));

    auto spans2 = hl.highlight("1'000'000");
    checkSpanInvariants(spans2, "1'000'000");
    CHECK(hasSpan(spans2, 0, 9, SyntaxToken::Number));
    CHECK(!hasAnyToken(spans2, SyntaxToken::Character));

    auto spans3 = hl.highlight("0x1F'FF");
    checkSpanInvariants(spans3, "0x1F'FF");
    CHECK(hasSpan(spans3, 0, 7, SyntaxToken::Number));
    CHECK(!hasAnyToken(spans3, SyntaxToken::Character));
}

// --- Nuevos Tests P1: Prefijos de Strings/Chars Ordinarios ---

TEST(Clike_OrdinaryStringPrefixes) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);

    std::string l1 = "u8\"x\"";
    auto spansU8 = hl.highlight(l1);
    checkSpanInvariants(spansU8, l1);
    CHECK(hasSpan(spansU8, 0, 5, SyntaxToken::String));

    std::string l2 = "u\"x\"";
    auto spansu = hl.highlight(l2);
    checkSpanInvariants(spansu, l2);
    CHECK(hasSpan(spansu, 0, 4, SyntaxToken::String));

    std::string l3 = "U\"x\"";
    auto spansU = hl.highlight(l3);
    checkSpanInvariants(spansU, l3);
    CHECK(hasSpan(spansU, 0, 4, SyntaxToken::String));

    std::string l4 = "L\"x\"";
    auto spansL = hl.highlight(l4);
    checkSpanInvariants(spansL, l4);
    CHECK(hasSpan(spansL, 0, 4, SyntaxToken::String));
}

TEST(Clike_CharacterLiteralPrefixes) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);

    std::string l1 = "u'x'";
    auto spansu = hl.highlight(l1);
    checkSpanInvariants(spansu, l1);
    CHECK(hasSpan(spansu, 0, 4, SyntaxToken::Character));

    std::string l2 = "U'x'";
    auto spansU = hl.highlight(l2);
    checkSpanInvariants(spansU, l2);
    CHECK(hasSpan(spansU, 0, 4, SyntaxToken::Character));

    std::string l3 = "L'x'";
    auto spansL = hl.highlight(l3);
    checkSpanInvariants(spansL, l3);
    CHECK(hasSpan(spansL, 0, 4, SyntaxToken::Character));
}

TEST(Clike_PrefixedStringDoesNotSwallowFollowingCode) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "L\"x\" int y;";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 0, 4, SyntaxToken::String));
    CHECK(hasSpan(spans, 5, 8, SyntaxToken::Type));
}

// --- Nuevos Tests P1: Transiciones de Estado Límite ---

TEST(Clike_BlockComment_EmptyLineKeepsState) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    SyntaxState st{};
    st.inBlockComment = true;
    SyntaxState out;
    auto spans = hl.highlight("", st, out);
    CHECK(spans.empty());
    CHECK(out.inBlockComment);
}

TEST(Clike_BlockComment_CloseReopenSameLine) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    SyntaxState st{};
    st.inBlockComment = true;
    SyntaxState out;
    std::string line = "*/ int x; /* y */";
    auto spans = hl.highlight(line, st, out);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 0, 2, SyntaxToken::Comment)); // Cierre previo
    CHECK(hasSpan(spans, 3, 6, SyntaxToken::Type));    // Código normal
    CHECK(hasSpan(spans, 10, 17, SyntaxToken::Comment)); // Nuevo comentario
    CHECK(!out.inBlockComment);
}

TEST(Clike_RawString_WrongDelimiterDoesNotCloseEarly) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    SyntaxState st{};
    SyntaxState out;
    // Delimitador es "foo", cierre debe ser ")foo"". 
    // El texto contiene ")bar"", que NO cierra el raw string.
    std::string line = "R\"foo(x)bar\")foo\"";
    auto spans = hl.highlight(line, st, out);
    checkSpanInvariants(spans, line);
    CHECK(hasSpan(spans, 0, line.size(), SyntaxToken::String));
    CHECK(!out.inRawString);
    CHECK(out.rawDelimLen == 0);
}

TEST(Clike_RawString_UnclosedAtEofPreservesState) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    SyntaxState st{};
    SyntaxState out;

    std::string line1 = "R\"foo(";
    auto spans1 = hl.highlight(line1, st, out);
    st = out;
    CHECK(st.inRawString);
    CHECK(std::string_view(st.rawDelim, st.rawDelimLen) == "foo");

    std::string line2 = "still raw";
    auto spans2 = hl.highlight(line2, st, out);
    st = out;
    checkSpanInvariants(spans2, line2);
    CHECK(hasSpan(spans2, 0, line2.size(), SyntaxToken::String));
    CHECK(st.inRawString);
    CHECK(st.rawDelimLen == 3);
}

// --- Nuevos Tests P2: Interacciones Cruzadas ---

TEST(Clike_MarkersInsideStringAreNotTokens) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "const char* url = \"http://example.com/*x*/\";";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpanText(spans, line, "\"http://example.com/*x*/\"", SyntaxToken::String));
    for (const auto& s : spans) {
        CHECK(s.token != SyntaxToken::Comment);
        CHECK(s.token != SyntaxToken::Preprocessor);
    }
}

TEST(Clike_StringMarkersInsideLineCommentAreNotStrings) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "// \"esto no abre un string\"";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpanText(spans, line, line, SyntaxToken::Comment));
    for (const auto& s : spans) {
        CHECK(s.token != SyntaxToken::String);
        CHECK(s.token != SyntaxToken::Character);
    }
}

TEST(Clike_PreprocessorMarkersInsideStringAreNotPreprocessor) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "const char* directive = \"#include <x>\";";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpanText(spans, line, "\"#include <x>\"", SyntaxToken::String));
    for (const auto& s : spans) {
        CHECK(s.token != SyntaxToken::Preprocessor);
    }
}

TEST(Clike_MarkersInsideRawStringAreNotTokens) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "auto raw = R\"(// /* #define FOO)\";";
    auto spans = hl.highlight(line);
    checkSpanInvariants(spans, line);
    CHECK(hasSpanText(spans, line, "R\"(// /* #define FOO)\"", SyntaxToken::String));
    for (const auto& s : spans) {
        CHECK(s.token != SyntaxToken::Comment);
        CHECK(s.token != SyntaxToken::Preprocessor);
    }
}

// --- Fortalecimiento de Assertions Existentes ---

TEST(Clike_RawStringOnlyCpp_Strong) {
    SyntaxHighlighter hlC; hlC.setLanguage(SyntaxLanguage::C);
    SyntaxHighlighter hlCpp; hlCpp.setLanguage(SyntaxLanguage::Cpp);
    
    // Caso 1: R"(hello)"
    std::string line1 = R"TEST(R"(hello)")TEST";
    
    // En C++: Raw string válido
    SyntaxState stCpp{}; SyntaxState outCpp;
    auto spansCpp1 = hlCpp.highlight(line1, stCpp, outCpp);
    checkSpanInvariants(spansCpp1, line1);
    CHECK(hasSpan(spansCpp1, 0, line1.size(), SyntaxToken::String));
    CHECK(!outCpp.inRawString);
    
    // En C: No es raw string. Debe degradar.
    // Asumimos comportamiento: 'R' es identificador/plano, "(...)" es string ordinario.
    SyntaxState stC{}; SyntaxState outC;
    auto spansC1 = hlC.highlight(line1, stC, outC);
    checkSpanInvariants(spansC1, line1);
    CHECK(!outC.inRawString);
    CHECK(outC.rawDelimLen==0);
    // No debe haber un span String que cubra todo (incluyendo la R inicial como parte del string raw)
    CHECK(!hasSpan(spansC1, 0, line1.size(), SyntaxToken::String));
    // Debería haber un string que empiece en la comilla (pos 1)
    CHECK(hasSpan(spansC1, 1, line1.size(), SyntaxToken::String));

    // Caso 2: u8R"(x)"
    std::string line2 = R"TEST(u8R"(x)")TEST";
    
    // En C++: Raw string válido con prefijo
    SyntaxState stCpp2{}; SyntaxState outCpp2;
    auto spansCpp2 = hlCpp.highlight(line2, stCpp2, outCpp2);
    checkSpanInvariants(spansCpp2, line2);
    CHECK(hasSpan(spansCpp2, 0, line2.size(), SyntaxToken::String));
    
    // En C: No raw. "u8R" es plano/ident, "(x)" es string.
    SyntaxState stC2{}; SyntaxState outC2;
    auto spansC2 = hlC.highlight(line2, stC2, outC2);
    checkSpanInvariants(spansC2, line2);
    CHECK(!outC2.inRawString);
    CHECK(!hasSpan(spansC2, 0, line2.size(), SyntaxToken::String));
    CHECK(hasSpan(spansC2, 3, line2.size(), SyntaxToken::String)); // Empieza en la comilla tras u8R
}

TEST(Clike_RawStringInvalidDelim_Strong) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    
    auto checkValidRaw = [&](std::string line){
        SyntaxState st{}; SyntaxState out;
        auto spans = hl.highlight(line, st, out);
        checkSpanInvariants(spans, line);
        CHECK(hasSpan(spans, 0, line.size(), SyntaxToken::String));
        CHECK(!out.inRawString);
        CHECK(out.rawDelimLen == 0);
    };

    auto checkInvalidRawFallbackToString = [&](std::string line){
        SyntaxState st{}; SyntaxState out;
        auto spans = hl.highlight(line, st, out);
        checkSpanInvariants(spans, line);
        
        // 1. No debe quedar estado raw
        CHECK(!out.inRawString);
        CHECK(out.rawDelimLen == 0);
        
        // 2. No debe ser un raw string que cubra toda la línea
        CHECK(!hasSpan(spans, 0, line.size(), SyntaxToken::String));
        
        // 3. El prefijo R no debe ser parte del string
        CHECK(!hasSpan(spans, 0, 1, SyntaxToken::String));
        
        // 4. Contrato propuesto: degrada a string ordinario desde la primera comilla
        size_t quotePos = line.find('"');
        if (quotePos != std::string::npos) {
             // Buscamos un span String que empiece en la comilla
             bool foundOrdinaryString = false;
             for(const auto& s : spans) {
                 if(s.token == SyntaxToken::String && s.begin == quotePos) {
                     foundOrdinaryString = true;
                     break;
                 }
             }
             CHECK(foundOrdinaryString);
        }
    };

    checkValidRaw(R"TEST(R"(x)")TEST");
    checkValidRaw(R"TEST(R"foo(x)foo")TEST");
    checkValidRaw("R\"123(x)123\"");
    checkValidRaw("R\"(x)\"");

    std::string delim16 = "1234567890123456";
    std::string l16 = "R\"" + delim16 + "(x)" + delim16 + "\"";
    checkValidRaw(l16);

    std::string delim17 = "12345678901234567";
    std::string l17 = "R\"" + delim17 + "(x)" + delim17 + "\"";
    checkInvalidRawFallbackToString(l17);
    checkInvalidRawFallbackToString("R\"a b(x)a b\"");
    checkInvalidRawFallbackToString("R\"a)b(x)a)b\"");
    checkInvalidRawFallbackToString("R\"a\\b(x)a\\b\"");
}

TEST(Clike_Language_FromFilename) {
    CHECK(languageFromFilename("test.c")==SyntaxLanguage::C);
    CHECK(languageFromFilename("test.cpp")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("test.h")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("test.txt")==SyntaxLanguage::None);
    CHECK(languageFromFilename("")==SyntaxLanguage::None);
    CHECK(languageFromFilename("test.cc")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("test.cxx")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("test.hpp")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("test.hh")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("test.ipp")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("TEST.CPP")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("TEST.H")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("TeSt.Cc")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename(".cpp")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("foo.")==SyntaxLanguage::None);
    CHECK(languageFromFilename("foo.cpp.bak")==SyntaxLanguage::None);
    CHECK(languageFromFilename("dir.with.dot/test.cpp")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("TEST.HPP")==SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("a.b/c")==SyntaxLanguage::None);
    CHECK(languageFromFilename("test.md")==SyntaxLanguage::Markdown);
    CHECK(languageFromFilename("test.markdown")==SyntaxLanguage::Markdown);
    CHECK(languageFromFilename("TEST.MD")==SyntaxLanguage::Markdown);
    CHECK(languageFromFilename("README")==SyntaxLanguage::None);
}