#include "test_framework.h"
#include "syntax/SyntaxHighlighter.h"
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"
#include <string>
#include <vector>

static bool hasSpan(const std::vector<SyntaxSpan>& spans, size_t b, size_t e, SyntaxToken tok) {
    for (auto &s : spans) if (s.begin==b && s.end==e && s.token==tok) return true;
    return false;
}

TEST(SyntaxHighlighter_EmptyLine) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("");
    CHECK(spans.empty());
}

TEST(SyntaxHighlighter_SimpleKeywordsAndTypes) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("int x = 0; return x;");
    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(spans, 11, 17, SyntaxToken::Keyword));
}

TEST(SyntaxHighlighter_StringWithEscapes) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("const char* s = \"hello \\\"world\\\"\";");
    CHECK(hasSpan(spans, 16, 33, SyntaxToken::String));
}

TEST(SyntaxHighlighter_UnclosedString) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("const char* s = \"unclosed");
    CHECK(hasSpan(spans, 16, 25, SyntaxToken::String));
}

TEST(SyntaxHighlighter_LineComment) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("int x; // comment");
    CHECK(hasSpan(spans, 7, 17, SyntaxToken::Comment));
}

TEST(SyntaxHighlighter_BlockCommentSingleLine) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("/* comment */ int x;");
    CHECK(hasSpan(spans, 0, 13, SyntaxToken::Comment));
    CHECK(hasSpan(spans, 14, 17, SyntaxToken::Type));
}

TEST(SyntaxHighlighter_BlockCommentMultiLine_State) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    SyntaxState st{}; st.inBlockComment=false;
    SyntaxState out;
    auto spans1 = hl.highlight("/* start", st, out); st=out;
    CHECK(hasSpan(spans1, 0, 8, SyntaxToken::Comment));
    CHECK(st.inBlockComment);
    auto spans2 = hl.highlight(" middle ", st, out); st=out;
    CHECK(hasSpan(spans2, 0, 8, SyntaxToken::Comment));
    CHECK(st.inBlockComment);
    auto spans3 = hl.highlight(" end */ int x;", st, out); st=out;
    CHECK(hasSpan(spans3, 0, 7, SyntaxToken::Comment));
    CHECK(!st.inBlockComment);
    CHECK(hasSpan(spans3, 8, 11, SyntaxToken::Type));
}

TEST(SyntaxHighlighter_Preprocessor) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("#include <iostream>");
    CHECK(hasSpan(spans, 0, 19, SyntaxToken::Preprocessor));
    auto spans2 = hl.highlight("  #define FOO 1");
    CHECK(hasSpan(spans2, 2, 15, SyntaxToken::Preprocessor));
}

TEST(SyntaxHighlighter_PreprocessorAfterBlockComment) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("/* comment */ #define FOO");
    CHECK(hasSpan(spans, 0, 13, SyntaxToken::Comment));
    CHECK(hasSpan(spans, 14, 25, SyntaxToken::Preprocessor));
    SyntaxState st{}; st.inBlockComment=true;
    SyntaxState out;
    auto spans2 = hl.highlight(" still */ #define BAR", st, out);
    CHECK(hasSpan(spans2, 0, 9, SyntaxToken::Comment));
    CHECK(hasSpan(spans2, 10, 21, SyntaxToken::Preprocessor));
}

TEST(SyntaxHighlighter_PreprocessorWithCodeBefore) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("/* comment */ foo #define BAR");
    CHECK(hasSpan(spans, 0, 13, SyntaxToken::Comment));
    CHECK(!hasSpan(spans, 14, 25, SyntaxToken::Preprocessor));
    bool hasPP=false; for(auto &s:spans) if(s.token==SyntaxToken::Preprocessor) hasPP=true;
    CHECK(!hasPP);
}

TEST(SyntaxHighlighter_PreprocessorMultipleLeadingComments) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("/**/ /* */ #define X");
    CHECK(hasSpan(spans, 0, 4, SyntaxToken::Comment));
    CHECK(hasSpan(spans, 5, 10, SyntaxToken::Comment));
    CHECK(hasSpan(spans, 11, 20, SyntaxToken::Preprocessor));
}

TEST(SyntaxHighlighter_Numbers) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("42 0xFF 0b1010 3.14 1e10");
    CHECK(hasSpan(spans, 0, 2, SyntaxToken::Number));
    CHECK(hasSpan(spans, 3, 7, SyntaxToken::Number));
    CHECK(hasSpan(spans, 8, 14, SyntaxToken::Number));
    CHECK(hasSpan(spans, 15, 19, SyntaxToken::Number));
    CHECK(hasSpan(spans, 20, 24, SyntaxToken::Number));
}

TEST(SyntaxHighlighter_InvalidHexBin) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("0xG");
    CHECK(hasSpan(spans, 0, 1, SyntaxToken::Number));
    CHECK(!hasSpan(spans, 0, 3, SyntaxToken::Number));
    auto spans2 = hl.highlight("0b2");
    CHECK(hasSpan(spans2, 0, 1, SyntaxToken::Number));
    auto spans3 = hl.highlight("0xFFu");
    CHECK(hasSpan(spans3, 0, 5, SyntaxToken::Number));
}

TEST(SyntaxHighlighter_RawString) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string l1 = "auto s = R\"(hello \"world\")\";";
    auto spans = hl.highlight(l1);
    CHECK(spans.size() >= 2);
    bool hasRaw=false;
    for(auto &s:spans) if(s.token==SyntaxToken::String) hasRaw=true;
    CHECK(hasRaw);
    std::string l2 = "auto s = R\"delim(Hello \"world\")delim\";";
    auto spans2 = hl.highlight(l2);
    CHECK(hasSpan(spans2, 9, l2.size()-1, SyntaxToken::String));
}

TEST(SyntaxHighlighter_RawStringMultiline) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    SyntaxState st{}; SyntaxState out;
    std::string l1 = "auto s = R\"foo(";
    auto s1 = hl.highlight(l1, st, out); st=out;
    CHECK(st.inRawString);
    CHECK(std::string_view(st.rawDelim, st.rawDelimLen)=="foo");
    std::string l2 = "still raw";
    auto s2 = hl.highlight(l2, st, out); st=out;
    CHECK(hasSpan(s2, 0, l2.size(), SyntaxToken::String));
    CHECK(st.inRawString);
    std::string l3 = ")foo\"; int x=1;";
    auto s3 = hl.highlight(l3, st, out); st=out;
    CHECK(!st.inRawString);
    CHECK(st.rawDelimLen==0);
    CHECK(std::string_view(st.rawDelim, st.rawDelimLen).empty());
    CHECK(hasSpan(s3, 0, 5, SyntaxToken::String));
    CHECK(hasSpan(s3, 7, 10, SyntaxToken::Type));
}

TEST(SyntaxHighlighter_StringVsComment_Interaction) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto spans = hl.highlight("\"/* not a comment */\"");
    CHECK(hasSpan(spans, 0, 21, SyntaxToken::String));
    CHECK(spans.size()==1);
}

TEST(SyntaxHighlighter_CMode) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::C);
    std::string line = "class template restrict _Bool int";
    auto spans = hl.highlight(line);
    CHECK(!hasSpan(spans, 0, 5, SyntaxToken::Keyword));
    CHECK(!hasSpan(spans, 6, 14, SyntaxToken::Keyword));
    CHECK(hasSpan(spans, 15, 23, SyntaxToken::Keyword));
    CHECK(hasSpan(spans, 24, 29, SyntaxToken::Keyword));
    CHECK(hasSpan(spans, 30, 33, SyntaxToken::Type));
    SyntaxHighlighter hlCpp; hlCpp.setLanguage(SyntaxLanguage::Cpp);
    auto spansCpp = hlCpp.highlight(line);
    CHECK(hasSpan(spansCpp, 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(spansCpp, 6, 14, SyntaxToken::Keyword));
}

TEST(SyntaxHighlighter_CharacterLiteral) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto s1 = hl.highlight("'a'");
    CHECK(hasSpan(s1, 0, 3, SyntaxToken::Character));
    auto s2 = hl.highlight("'\\n'");
    CHECK(hasSpan(s2, 0, 4, SyntaxToken::Character));
    auto s3 = hl.highlight("'\\''");
    CHECK(hasSpan(s3, 0, 4, SyntaxToken::Character));
    auto s4 = hl.highlight("'\\\\'");
    CHECK(hasSpan(s4, 0, 4, SyntaxToken::Character));
}

TEST(SyntaxHighlighter_LineCommentWithKeyword) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    std::string line = "int x; // int y; class foo";
    auto spans = hl.highlight(line);
    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(spans, 7, line.size(), SyntaxToken::Comment));
    for (auto &s : spans) {
        if (s.token==SyntaxToken::Type || s.token==SyntaxToken::Keyword) {
            CHECK(s.end <= 7);
        }
    }
}

TEST(SyntaxHighlighter_RawStringOnlyCpp) {
    SyntaxHighlighter hlC; hlC.setLanguage(SyntaxLanguage::C);
    std::string line = R"TEST(R"(hello)")TEST";
    SyntaxState stC{}; SyntaxState outC;
    auto spansC = hlC.highlight(line, stC, outC);
    CHECK(!hasSpan(spansC, 0, 10, SyntaxToken::String));
    CHECK(!outC.inRawString);
    CHECK(outC.rawDelimLen==0);
    SyntaxHighlighter hlCpp; hlCpp.setLanguage(SyntaxLanguage::Cpp);
    SyntaxState stCpp{}; SyntaxState outCpp;
    auto spansCpp = hlCpp.highlight(line, stCpp, outCpp);
    CHECK(hasSpan(spansCpp, 0, 10, SyntaxToken::String));
    CHECK(!outCpp.inRawString);
    CHECK(outCpp.rawDelimLen==0);
    std::string l2 = R"TEST(u8R"(x)")TEST";
    auto spansC2 = hlC.highlight(l2, stC, outC);
    CHECK(!hasSpan(spansC2, 0, 8, SyntaxToken::String));
    CHECK(!outC.inRawString);
    CHECK(outC.rawDelimLen==0);
    auto spansCpp2 = hlCpp.highlight(l2, stCpp, outCpp);
    CHECK(hasSpan(spansCpp2, 0, 8, SyntaxToken::String));
    CHECK(!outCpp.inRawString);
    CHECK(outCpp.rawDelimLen==0);
}

TEST(SyntaxHighlighter_RawStringInvalidDelim) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto checkNotSingleRaw = [&](std::string line){
        SyntaxState st{}; SyntaxState out;
        auto spans = hl.highlight(line, st, out);
        bool singleRaw = spans.size()==1 && spans[0].token==SyntaxToken::String && spans[0].begin==0 && spans[0].end==line.size();
        CHECK(!singleRaw);
    };
    auto checkIsRaw = [&](std::string line){
        SyntaxState st{}; SyntaxState out;
        auto spans = hl.highlight(line, st, out);
        CHECK(hasSpan(spans, 0, line.size(), SyntaxToken::String));
    };
    checkIsRaw(R"TEST(R"(x)")TEST");
    checkIsRaw(R"TEST(R"foo(x)foo")TEST");
    checkIsRaw("R\"123(x)123\"");
    checkIsRaw("R\"(x)\"");
    std::string delim16 = "1234567890123456";
    std::string l16 = "R\"" + delim16 + "(x)" + delim16 + "\"";
    checkIsRaw(l16);
    std::string delim17 = "12345678901234567";
    std::string l17 = "R\"" + delim17 + "(x)" + delim17 + "\"";
    checkNotSingleRaw(l17);
    checkNotSingleRaw("R\"a b(x)a b\"");
    checkNotSingleRaw("R\"a)b(x)a)b\"");
    checkNotSingleRaw("R\"a\\b(x)a\\b\"");
}

TEST(SyntaxHighlighter_RawStringPrefixes) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    auto checkRaw = [&](std::string line){
        SyntaxState st{}; SyntaxState out;
        auto spans = hl.highlight(line, st, out);
        CHECK(hasSpan(spans, 9, line.size()-1, SyntaxToken::String));
    };
    checkRaw("auto s = R\"(hello)\";");
    checkRaw("auto s = u8R\"(hello)\";");
    checkRaw("auto s = uR\"(hello)\";");
    checkRaw("auto s = UR\"(hello)\";");
    checkRaw("auto s = LR\"(hello)\";");
    SyntaxState st{}; SyntaxState out;
    std::string l1 = "auto s = u8R\"foo(";
    hl.highlight(l1, st, out); st=out;
    CHECK(st.inRawString);
    CHECK(std::string_view(st.rawDelim, st.rawDelimLen)=="foo");
    std::string l2 = "still )foo\";";
    auto s2 = hl.highlight(l2, st, out); st=out;
    CHECK(!st.inRawString);
    CHECK(st.rawDelimLen==0);
    CHECK(s2.size()>=1);
    CHECK(s2[0].token==SyntaxToken::String);
}

TEST(SyntaxHighlighter_RawStringCloseAndContinue) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Cpp);
    SyntaxState st{}; SyntaxState out;
    std::string l1 = "auto s = R\"foo(";
    hl.highlight(l1, st, out); st=out;
    CHECK(st.inRawString);
    std::string l2 = ")foo\" /* comment */ int x;";
    auto s2 = hl.highlight(l2, st, out); st=out;
    CHECK(!st.inRawString);
    CHECK(st.rawDelimLen==0);
    CHECK(hasSpan(s2, 0, 5, SyntaxToken::String));
    CHECK(hasSpan(s2, 6, 19, SyntaxToken::Comment));
    CHECK(hasSpan(s2, 20, 23, SyntaxToken::Type));
    std::string l3 = ")foo\" // comment int";
    SyntaxState st3{}; st3.inRawString=true; st3.setRawDelim("foo");
    auto s3 = hl.highlight(l3, st3, out);
    CHECK(hasSpan(s3, 0, 5, SyntaxToken::String));
    CHECK(hasSpan(s3, 6, 20, SyntaxToken::Comment));
    bool hasType=false; for(auto &s:s3) if(s.token==SyntaxToken::Type) hasType=true;
    CHECK(!hasType);
    CHECK(!out.inRawString);
    CHECK(out.rawDelimLen==0);
    std::string l4 = "R\"(x)\" /* c */ int y;";
    auto s4 = hl.highlight(l4);
    CHECK(hasSpan(s4, 0, 6, SyntaxToken::String));
    CHECK(hasSpan(s4, 7, 14, SyntaxToken::Comment));
    CHECK(hasSpan(s4, 15, 18, SyntaxToken::Type));
}

TEST(SyntaxLanguage_FromFilename) {
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
}
