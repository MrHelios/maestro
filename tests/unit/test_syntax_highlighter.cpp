#include "test_framework.h"
#include "syntax/SyntaxHighlighter.h"
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"
#include "syntax/SyntaxCache.h"
#include "document/Document.h"
#include <string>
#include <vector>
#include <cstring>

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
    CHECK(languageFromFilename("test.md")==SyntaxLanguage::Markdown);
    CHECK(languageFromFilename("test.markdown")==SyntaxLanguage::Markdown);
    CHECK(languageFromFilename("TEST.MD")==SyntaxLanguage::Markdown);
    CHECK(languageFromFilename("README")==SyntaxLanguage::None);
}

TEST(SyntaxHighlighter_Markdown_LanguageDetection) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Markdown);
    auto spans = hl.highlight("# Heading");
    CHECK(spans.size() == 1);
    CHECK(spans[0].token == SyntaxToken::Heading);
}

TEST(SyntaxHighlighter_Markdown_Headings) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    auto h1 = hl.highlight("# H1");
    CHECK(hasSpan(h1, 0, 4, SyntaxToken::Heading));
    auto h2 = hl.highlight("## H2");
    CHECK(hasSpan(h2, 0, 5, SyntaxToken::Heading));
    auto h6 = hl.highlight("###### H6");
    CHECK(hasSpan(h6, 0, 9, SyntaxToken::Heading));
    auto h7 = hl.highlight("####### H7");
    CHECK(!hasSpan(h7, 0, 10, SyntaxToken::Heading));
    CHECK(h7.empty());
    auto noSpace = hl.highlight("#NoSpace");
    CHECK(noSpace.empty());
}

TEST(SyntaxHighlighter_Markdown_BoldItalic) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    auto bold = hl.highlight("**bold**");
    CHECK(hasSpan(bold, 0, 8, SyntaxToken::Bold));
    auto italic = hl.highlight("*italic*");
    CHECK(hasSpan(italic, 0, 8, SyntaxToken::Italic));
    auto underscoreBold = hl.highlight("__bold__");
    CHECK(hasSpan(underscoreBold, 0, 8, SyntaxToken::Bold));
    auto underscoreItalic = hl.highlight("_italic_");
    CHECK(hasSpan(underscoreItalic, 0, 8, SyntaxToken::Italic));
    auto mixed = hl.highlight("**bold** and *italic*");
    CHECK(hasSpan(mixed, 0, 8, SyntaxToken::Bold));
    CHECK(hasSpan(mixed, 13, 21, SyntaxToken::Italic));
    auto noClose = hl.highlight("**unclosed");
    CHECK(noClose.empty());
}

TEST(SyntaxHighlighter_Markdown_Emphasis_NestedAndUnclosed) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);

    auto nested = hl.highlight("**text with `code` inside**");
    CHECK(hasSpan(nested, 0, 27, SyntaxToken::Bold));
    CHECK(hasSpan(nested, 12, 18, SyntaxToken::Code));

    auto unclosed = hl.highlight("*unclosed and [link](target)");
    CHECK(hasSpan(unclosed, 14, 28, SyntaxToken::Link));
    CHECK(hasSpan(unclosed, 21, 27, SyntaxToken::LinkUrl));
    bool hasItalic = false;
    for (auto& s : unclosed) {
        if (s.token == SyntaxToken::Italic) hasItalic = true;
    }
    CHECK(!hasItalic);
}

TEST(SyntaxHighlighter_Markdown_InlineCode) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    auto code = hl.highlight("`code`");
    CHECK(hasSpan(code, 0, 6, SyntaxToken::Code));
    auto codeWithSpecial = hl.highlight("`code *not italic* **not bold**`");
    CHECK(hasSpan(codeWithSpecial, 0, 32, SyntaxToken::Code));
    auto multiCode = hl.highlight("`a` `b` `c`");
    CHECK(hasSpan(multiCode, 0, 3, SyntaxToken::Code));
    CHECK(hasSpan(multiCode, 4, 7, SyntaxToken::Code));
    CHECK(hasSpan(multiCode, 8, 11, SyntaxToken::Code));

    auto doubleBacktick = hl.highlight("``code with ` backtick``");
    CHECK(hasSpan(doubleBacktick, 0, 24, SyntaxToken::Code));
    auto tripleBacktick = hl.highlight("text ```code``` more");
    CHECK(hasSpan(tripleBacktick, 5, 15, SyntaxToken::Code));

    auto unclosed = hl.highlight("`unclosed");
    CHECK(unclosed.empty());
    auto unclosedDouble = hl.highlight("``unclosed");
    CHECK(unclosedDouble.empty());
}

TEST(SyntaxHighlighter_Markdown_Links) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    auto link = hl.highlight("[link](url)");
    CHECK(hasSpan(link, 0, 11, SyntaxToken::Link));
    CHECK(hasSpan(link, 7, 10, SyntaxToken::LinkUrl));

    auto linkWithPath = hl.highlight("[text](https://example.com/path)");
    CHECK(hasSpan(linkWithPath, 0, 32, SyntaxToken::Link));
    CHECK(hasSpan(linkWithPath, 7, 31, SyntaxToken::LinkUrl));

    auto noLink = hl.highlight("[not a link");
    CHECK(noLink.empty());

    auto textBetween = hl.highlight("[text] not a link (url)");
    bool hasLink2 = false;
    for (auto &s : textBetween) if (s.token == SyntaxToken::Link) hasLink2 = true;
    CHECK(!hasLink2);

    auto parensInUrl = hl.highlight("[link](url(with)parens)");
    CHECK(hasSpan(parensInUrl, 0, 23, SyntaxToken::Link));
    CHECK(hasSpan(parensInUrl, 7, 22, SyntaxToken::LinkUrl));

    auto escapedParens = hl.highlight("[link](url\\)with\\(parens)");
    CHECK(hasSpan(escapedParens, 0, 25, SyntaxToken::Link));
    CHECK(hasSpan(escapedParens, 7, 24, SyntaxToken::LinkUrl));

    auto unclosedNested = hl.highlight("[link](url(with)");
    bool hasLink3 = false;
    for (auto &s : unclosedNested) if (s.token == SyntaxToken::Link) hasLink3 = true;
    CHECK(!hasLink3);
}

TEST(SyntaxHighlighter_Markdown_Images) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    auto img = hl.highlight("![alt](img.png)");
    bool hasImage = false;
    for (auto &s : img) if (s.token == SyntaxToken::Image) hasImage = true;
    CHECK(hasImage);
    auto imgWithUrl = hl.highlight("![alt](https://example.com/img.png)");
    hasImage = false;
    for (auto &s : imgWithUrl) if (s.token == SyntaxToken::Image) hasImage = true;
    CHECK(hasImage);
    auto notImg = hl.highlight("[not an image](url)");
    bool hasLink = false;
    for (auto &s : notImg) if (s.token == SyntaxToken::Link) hasLink = true;
    CHECK(hasLink);
}

TEST(SyntaxHighlighter_Markdown_BlockQuote) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    auto bq = hl.highlight("> quote");
    CHECK(hasSpan(bq, 0, 7, SyntaxToken::BlockQuote));
    auto bqNested = hl.highlight(">> nested");
    CHECK(hasSpan(bqNested, 0, 9, SyntaxToken::BlockQuote));
}

TEST(SyntaxHighlighter_Markdown_HorizontalRule) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    auto hr1 = hl.highlight("---");
    CHECK(hasSpan(hr1, 0, 3, SyntaxToken::HorizontalRule));
    auto hr2 = hl.highlight("***");
    CHECK(hasSpan(hr2, 0, 3, SyntaxToken::HorizontalRule));
    auto hr3 = hl.highlight("___");
    CHECK(hasSpan(hr3, 0, 3, SyntaxToken::HorizontalRule));
    auto hr4 = hl.highlight("----");
    CHECK(hasSpan(hr4, 0, 4, SyntaxToken::HorizontalRule));
    auto hr5 = hl.highlight("*****");
    CHECK(hasSpan(hr5, 0, 5, SyntaxToken::HorizontalRule));
    auto hr6 = hl.highlight("______");
    CHECK(hasSpan(hr6, 0, 6, SyntaxToken::HorizontalRule));
    auto hrWithSpaces = hl.highlight("- - -");
    CHECK(hasSpan(hrWithSpaces, 0, 5, SyntaxToken::HorizontalRule));
    auto hrWithSpaces2 = hl.highlight("* * *");
    CHECK(hasSpan(hrWithSpaces2, 0, 5, SyntaxToken::HorizontalRule));
    auto hrWithSpaces3 = hl.highlight("_ _ _");
    CHECK(hasSpan(hrWithSpaces3, 0, 5, SyntaxToken::HorizontalRule));
    auto notHr = hl.highlight("--");
    CHECK(notHr.empty());
    auto notHr2 = hl.highlight("- -");
    CHECK(hasSpan(notHr2, 0, 1, SyntaxToken::ListItem));
}

TEST(SyntaxHighlighter_Markdown_ListItem) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    auto dash = hl.highlight("- item");
    CHECK(hasSpan(dash, 0, 1, SyntaxToken::ListItem));
    auto star = hl.highlight("* item");
    CHECK(hasSpan(star, 0, 1, SyntaxToken::ListItem));
    auto plus = hl.highlight("+ item");
    CHECK(hasSpan(plus, 0, 1, SyntaxToken::ListItem));
    auto noSpace = hl.highlight("-item");
    CHECK(noSpace.empty());

    auto ordered1 = hl.highlight("1. First");
    CHECK(hasSpan(ordered1, 0, 3, SyntaxToken::ListItem));
    auto ordered2 = hl.highlight("42. Second");
    CHECK(hasSpan(ordered2, 0, 4, SyntaxToken::ListItem));
    auto orderedNoSpace = hl.highlight("1.Item");
    CHECK(orderedNoSpace.empty());

    auto orderedParen = hl.highlight("1) Not a list");
    CHECK(hasSpan(orderedParen, 0, 3, SyntaxToken::ListItem));
    auto orderedTab = hl.highlight("2)\tItem");
    CHECK(hasSpan(orderedTab, 0, 3, SyntaxToken::ListItem));
    auto tooManyDigits = hl.highlight("1234567890. Item");
    CHECK(tooManyDigits.empty());
    auto notOrdered = hl.highlight("1)Not a list");
    CHECK(notOrdered.empty());
}

TEST(SyntaxHighlighter_Markdown_CodeBlock) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    SyntaxState st{}; SyntaxState out;

    auto l1 = hl.highlight("```", st, out); st = out;
    CHECK(hasSpan(l1, 0, 3, SyntaxToken::CodeBlock));
    CHECK(st.inCodeBlock);
    CHECK(st.codeBlockDelimChar == '`');
    CHECK(st.codeBlockDelimLen == 3);

    auto l2 = hl.highlight("code line", st, out); st = out;
    CHECK(hasSpan(l2, 0, 9, SyntaxToken::CodeBlock));
    CHECK(st.inCodeBlock);

    auto l3 = hl.highlight("```", st, out); st = out;
    CHECK(hasSpan(l3, 0, 3, SyntaxToken::CodeBlock));
    CHECK(!st.inCodeBlock);
    CHECK(st.codeBlockDelimLen == 0);
}

TEST(SyntaxHighlighter_Markdown_CodeBlock_Tilde) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    SyntaxState st{}; SyntaxState out;

    auto l1 = hl.highlight("~~~", st, out); st = out;
    CHECK(hasSpan(l1, 0, 3, SyntaxToken::CodeBlock));
    CHECK(st.inCodeBlock);
    CHECK(st.codeBlockDelimChar == '~');
    CHECK(st.codeBlockDelimLen == 3);

    auto l2 = hl.highlight("code", st, out); st = out;
    CHECK(hasSpan(l2, 0, 4, SyntaxToken::CodeBlock));

    auto l3 = hl.highlight("~~~", st, out); st = out;
    CHECK(hasSpan(l3, 0, 3, SyntaxToken::CodeBlock));
    CHECK(!st.inCodeBlock);
}

TEST(SyntaxHighlighter_Markdown_CodeBlock_LongerDelim) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    SyntaxState st{}; SyntaxState out;

    auto l1 = hl.highlight("````cpp", st, out); st = out;
    CHECK(hasSpan(l1, 0, 7, SyntaxToken::CodeBlock));
    CHECK(st.inCodeBlock);
    CHECK(st.codeBlockDelimChar == '`');
    CHECK(st.codeBlockDelimLen == 4);

    auto l2 = hl.highlight("code", st, out); st = out;
    CHECK(hasSpan(l2, 0, 4, SyntaxToken::CodeBlock));

    auto l3 = hl.highlight("````", st, out); st = out;
    CHECK(hasSpan(l3, 0, 4, SyntaxToken::CodeBlock));
    CHECK(!st.inCodeBlock);
}

TEST(SyntaxHighlighter_Markdown_CodeBlock_ClosingLongerDelim) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    SyntaxState st{}; SyntaxState out;

    auto l1 = hl.highlight("```", st, out); st = out;
    CHECK(st.inCodeBlock);
    CHECK(st.codeBlockDelimLen == 3);

    auto l2 = hl.highlight("code", st, out); st = out;
    CHECK(hasSpan(l2, 0, 4, SyntaxToken::CodeBlock));

    auto l3 = hl.highlight("`````   ", st, out); st = out;
    CHECK(hasSpan(l3, 0, 8, SyntaxToken::CodeBlock));
    CHECK(!st.inCodeBlock);
}

TEST(SyntaxHighlighter_Markdown_CodeBlock_Indentation) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    SyntaxState st{}; SyntaxState out;

    auto l1 = hl.highlight("    ```", st, out); st = out;
    CHECK(!st.inCodeBlock);
    CHECK(!hasSpan(l1, 4, 7, SyntaxToken::CodeBlock));

    auto l2 = hl.highlight("    code", st, out); st = out;
    CHECK(!st.inCodeBlock);
    CHECK(!hasSpan(l2, 0, 8, SyntaxToken::CodeBlock));

    hl.highlight("```", st, out); st = out;
    CHECK(st.inCodeBlock);

    auto l3 = hl.highlight("    ```", st, out); st = out;
    CHECK(st.inCodeBlock);
    CHECK(hasSpan(l3, 0, 7, SyntaxToken::CodeBlock));

    auto l4 = hl.highlight("    still code", st, out); st = out;
    CHECK(st.inCodeBlock);

    auto l5 = hl.highlight("   ```", st, out); st = out;
    CHECK(!st.inCodeBlock);
    CHECK(hasSpan(l5, 0, 6, SyntaxToken::CodeBlock));
}

TEST(SyntaxHighlighter_Markdown_CodeBlock_NoMarkdownInside) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    SyntaxState st{}; SyntaxState out;

    hl.highlight("```", st, out); st = out;
    auto l2 = hl.highlight("# Not a heading", st, out); st = out;
    CHECK(hasSpan(l2, 0, 15, SyntaxToken::CodeBlock));
    auto l3 = hl.highlight("**Not bold**", st, out); st = out;
    CHECK(hasSpan(l3, 0, 12, SyntaxToken::CodeBlock));
    auto l4 = hl.highlight("[Not a link](url)", st, out); st = out;
    bool hasCodeBlock = false;
    for (auto &s : l4) if (s.token == SyntaxToken::CodeBlock) hasCodeBlock = true;
    CHECK(hasCodeBlock);
    hl.highlight("```", st, out);
}

TEST(SyntaxHighlighter_Markdown_AutoLinks) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    auto url = hl.highlight("<https://example.com>");
    CHECK(hasSpan(url, 0, 21, SyntaxToken::Link));
    CHECK(hasSpan(url, 1, 20, SyntaxToken::LinkUrl));
    auto email = hl.highlight("<test@example.com>");
    CHECK(hasSpan(email, 0, 18, SyntaxToken::Link));
    CHECK(hasSpan(email, 1, 17, SyntaxToken::LinkUrl));

    auto ftp = hl.highlight("<ftp://files.example.org/path>");
    CHECK(hasSpan(ftp, 0, 30, SyntaxToken::Link));
    CHECK(hasSpan(ftp, 1, 29, SyntaxToken::LinkUrl));

    auto invalidAt = hl.highlight("<texto cualquiera @ sin ser una direccion>");
    CHECK(invalidAt.empty());

    auto invalidScheme = hl.highlight("<htp://example.com>");
    CHECK(hasSpan(invalidScheme, 0, 19, SyntaxToken::Link));
    CHECK(hasSpan(invalidScheme, 1, 18, SyntaxToken::LinkUrl));

    auto noScheme = hl.highlight("<example.com>");
    CHECK(noScheme.empty());
}

TEST(SyntaxHighlighter_Markdown_ComplexLine) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    auto spans = hl.highlight("# Heading with **bold** and `code` and [link](url)");
    size_t lineLen = strlen("# Heading with **bold** and `code` and [link](url)");
    CHECK(hasSpan(spans, 0, lineLen, SyntaxToken::Heading));
    bool hasBold = false, hasCode = false, hasLink = false, hasLinkUrl = false;
    for (auto &s : spans) {
        if (s.token == SyntaxToken::Bold) hasBold = true;
        if (s.token == SyntaxToken::Code) hasCode = true;
        if (s.token == SyntaxToken::Link) hasLink = true;
        if (s.token == SyntaxToken::LinkUrl) hasLinkUrl = true;
    }
    CHECK(hasBold);
    CHECK(hasCode);
    CHECK(hasLink);
    CHECK(hasLinkUrl);
}

TEST(SyntaxHighlighter_Markdown_CacheConvergence) {
    SyntaxHighlighter hl; hl.setLanguage(SyntaxLanguage::Markdown);
    SyntaxState st{}; SyntaxState out;
    std::vector<SyntaxSpan> spans;

    hl.highlight("```", st, out); st = out;
    CHECK(st.inCodeBlock);

    for (int i = 0; i < 5; ++i) {
        hl.highlight("code line", st, out, spans); st = out;
        CHECK(st.inCodeBlock);
        CHECK(hasSpan(spans, 0, 9, SyntaxToken::CodeBlock));
    }

    hl.highlight("```", st, out); st = out;
    CHECK(!st.inCodeBlock);
    CHECK(st.codeBlockDelimLen == 0);

    hl.highlight("normal", st, out, spans);
    CHECK(!hasSpan(spans, 0, 6, SyntaxToken::CodeBlock));
}

TEST(SyntaxHighlighter_Markdown_SyntaxCacheIntegration) {
    Document doc;
    doc.restore({"```cpp", "int x = 1;", "int y = 2;", "int z = 3;", "```", "normal line"});
    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Markdown);
    cache.ensureValid(doc, doc.lineCount());

    CHECK(hasSpan(cache.spansFor(0), 0, 6, SyntaxToken::CodeBlock));
    CHECK(hasSpan(cache.spansFor(1), 0, 10, SyntaxToken::CodeBlock));
    CHECK(hasSpan(cache.spansFor(2), 0, 10, SyntaxToken::CodeBlock));
    CHECK(hasSpan(cache.spansFor(3), 0, 10, SyntaxToken::CodeBlock));
    CHECK(hasSpan(cache.spansFor(4), 0, 3, SyntaxToken::CodeBlock));
    CHECK(!hasSpan(cache.spansFor(5), 0, 11, SyntaxToken::CodeBlock));
    CHECK(!cache.stateBefore(5).inCodeBlock);

    doc.restore({"normal line", "int x = 1;", "int y = 2;", "int z = 3;", "```", "normal line"});
    cache.markDirty(0);
    cache.ensureValid(doc, doc.lineCount());

    SyntaxCache fresh;
    fresh.setLanguage(SyntaxLanguage::Markdown);
    fresh.ensureValid(doc, doc.lineCount());

    for (int l = 0; l <= doc.lineCount(); ++l) {
        CHECK(cache.stateBefore(l) == fresh.stateBefore(l));
    }
    for (int l = 0; l < doc.lineCount(); ++l) {
        const auto& a = cache.spansFor(l);
        const auto& b = fresh.spansFor(l);
        bool equal = a.size() == b.size();
        for (size_t i = 0; equal && i < a.size(); ++i) {
            equal = a[i].begin == b[i].begin && a[i].end == b[i].end &&
                    a[i].token == b[i].token;
        }
        CHECK(equal);
    }

    CHECK(!hasSpan(cache.spansFor(0), 0, 11, SyntaxToken::CodeBlock));
    CHECK(!hasSpan(cache.spansFor(1), 0, 10, SyntaxToken::CodeBlock));
    CHECK(!hasSpan(cache.spansFor(2), 0, 10, SyntaxToken::CodeBlock));
    CHECK(!hasSpan(cache.spansFor(3), 0, 10, SyntaxToken::CodeBlock));
    CHECK(hasSpan(cache.spansFor(4), 0, 3, SyntaxToken::CodeBlock));
    CHECK(hasSpan(cache.spansFor(5), 0, 11, SyntaxToken::CodeBlock));
}
