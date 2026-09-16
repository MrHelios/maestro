#include "test_framework.h"
#include "helpers/alloc_stats.h"
#include "syntax/SyntaxHighlighter.h"
#include <cstdio>
#include <string>
#include <vector>

static void benchOneReturn(const char* label, SyntaxLanguage lang, std::string_view line, int iters) {
    SyntaxHighlighter hl;
    hl.setLanguage(lang);
    size_t sampleSpans = hl.highlight(line).size();
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped s(alloc_stats::kRenderFrame);
        volatile size_t sink = 0;
        for (int i = 0; i < iters; ++i) {
            SyntaxState st{}; SyntaxState out{};
            auto spans = hl.highlight(line, st, out);
            sink += spans.size();
        }
        (void)sink;
    }
    const auto& st = alloc_stats::statsFor(alloc_stats::kRenderFrame);
    std::printf("[return] %-28s lang=%-4s iters=%5d  allocs=%6llu  perIter %.2f  +%8llu  spans=%zu  line=\"%.40s\"\n",
        label, lang == SyntaxLanguage::Cpp ? "Cpp" : lang == SyntaxLanguage::C ? "C" : "None", iters, st.allocs, st.allocs/double(iters), st.bytesAllocated, sampleSpans, std::string(line).c_str());
}

static void benchOneReuse(const char* label, SyntaxLanguage lang, std::string_view line, int iters) {
    SyntaxHighlighter hl;
    hl.setLanguage(lang);
    std::vector<SyntaxSpan> buf;
    size_t sampleSpans = 0;
    { SyntaxState s{}; SyntaxState o{}; hl.highlight(line, s, o, buf); sampleSpans = buf.size(); }
    alloc_stats::resetAll();
    {
        alloc_stats::Scoped s(alloc_stats::kRenderFrame);
        volatile size_t sink = 0;
        std::vector<SyntaxSpan> spans;
        for (int i = 0; i < iters; ++i) {
            SyntaxState st{}; SyntaxState out{};
            hl.highlight(line, st, out, spans);
            sink += spans.size();
        }
        (void)sink;
    }
    const auto& st = alloc_stats::statsFor(alloc_stats::kRenderFrame);
    std::printf("[reuse ] %-28s lang=%-4s iters=%5d  allocs=%6llu  perIter %.4f  +%8llu  spans=%zu  line=\"%.40s\"\n",
        label, lang == SyntaxLanguage::Cpp ? "Cpp" : "None", iters, st.allocs, st.allocs/double(iters), st.bytesAllocated, sampleSpans, std::string(line).c_str());
}

static void benchFrameReturn(const char* label, SyntaxLanguage lang, const std::vector<std::string>& lines, int iters) {
    SyntaxHighlighter hl; hl.setLanguage(lang);
    alloc_stats::resetAll();
    { alloc_stats::Scoped s(alloc_stats::kRenderFrame);
        volatile size_t sink=0;
        for(int k=0;k<iters;++k){ SyntaxState st{}; for(auto& l:lines){ SyntaxState out{}; auto spans=hl.highlight(l,st,out); sink+=spans.size(); st=out; } } (void)sink; }
    const auto& st=alloc_stats::statsFor(alloc_stats::kRenderFrame);
    int total=iters*(int)lines.size();
    std::printf("[return] %-28s lang=%-4s frames=%4d lines=%2zu total=%6d  allocs=%7llu  perCall %.2f perFrame %.1f\n", label, lang==SyntaxLanguage::Cpp?"Cpp":"None", iters, lines.size(), total, st.allocs, st.allocs/double(total), st.allocs/double(iters));
}
static void benchFrameReuse(const char* label, SyntaxLanguage lang, const std::vector<std::string>& lines, int iters) {
    SyntaxHighlighter hl; hl.setLanguage(lang);
    alloc_stats::resetAll();
    { alloc_stats::Scoped s(alloc_stats::kRenderFrame);
        volatile size_t sink=0;
        std::vector<SyntaxSpan> buf;
        for(int k=0;k<iters;++k){ SyntaxState st{}; for(auto& l:lines){ SyntaxState out{}; hl.highlight(l,st,out,buf); sink+=buf.size(); st=out; } } (void)sink; }
    const auto& st=alloc_stats::statsFor(alloc_stats::kRenderFrame);
    int total=iters*(int)lines.size();
    std::printf("[reuse ] %-28s lang=%-4s frames=%4d lines=%2zu total=%6d  allocs=%7llu  perCall %.4f perFrame %.2f\n", label, lang==SyntaxLanguage::Cpp?"Cpp":"None", iters, lines.size(), total, st.allocs, st.allocs/double(total), st.allocs/double(iters));
}

TEST(highlight_alloc_benchmark) {
    std::printf("\n== highlight: return (1 alloc/línea) vs reuse (clear+reuse buffer) ==\n");
    const int N=10000;
    benchOneReturn("vacia", SyntaxLanguage::Cpp, "", N);
    benchOneReuse("vacia", SyntaxLanguage::Cpp, "", N);
    benchOneReturn("normal", SyntaxLanguage::Cpp, "int x = 42;", N);
    benchOneReuse("normal", SyntaxLanguage::Cpp, "int x = 42;", N);
    benchOneReturn("muchos_ident", SyntaxLanguage::Cpp, "foo bar baz qux hello world a b c d e f g h i j k l m n", N);
    benchOneReuse("muchos_ident", SyntaxLanguage::Cpp, "foo bar baz qux hello world a b c d e f g h i j k l m n", N);
    benchOneReturn("muchos_keywords", SyntaxLanguage::Cpp, "if else for while return class template typename virtual override final break continue switch case default", N);
    benchOneReuse("muchos_keywords", SyntaxLanguage::Cpp, "if else for while return class template typename virtual override final break continue switch case default", N);
    benchOneReturn("linea_larga_80x", SyntaxLanguage::Cpp, std::string(80,'x'), N);
    benchOneReuse("linea_larga_80x", SyntaxLanguage::Cpp, std::string(80,'x'), N);
    benchOneReturn("linea_larga_kw", SyntaxLanguage::Cpp, "int a=1; int b=2; int c=3; int d=4; int e=5; int f=6; int g=7; int h=8; int i=9;", N);
    benchOneReuse("linea_larga_kw", SyntaxLanguage::Cpp, "int a=1; int b=2; int c=3; int d=4; int e=5; int f=6; int g=7; int h=8; int i=9;", N);
    benchOneReturn("strings", SyntaxLanguage::Cpp, "auto s = \"hello \\\"world\\\"\"; char c = 'x';", N);
    benchOneReuse("strings", SyntaxLanguage::Cpp, "auto s = \"hello \\\"world\\\"\"; char c = 'x';", N);
    benchOneReturn("coment_linea", SyntaxLanguage::Cpp, "// comment with int for while", N);
    benchOneReuse("coment_linea", SyntaxLanguage::Cpp, "// comment with int for while", N);
    benchOneReturn("numeros", SyntaxLanguage::Cpp, "42 0xFF 0b1010 3.14 1e10 123_u 0xDEADBEEF", N);
    benchOneReuse("numeros", SyntaxLanguage::Cpp, "42 0xFF 0b1010 3.14 1e10 123_u 0xDEADBEEF", N);

    std::printf("\n== frame 11 líneas / 24 líneas / 1524 líneas ==\n");
    std::vector<std::string> cppFile = {
        "#include \"syntax/CLikeHighlighter.h\"",
        "#include <unordered_set>",
        "std::vector<SyntaxSpan> CLikeHighlighter::highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, SyntaxLanguage lang) const {",
        "    std::vector<SyntaxSpan> spans;",
        "    if (outNext.inBlockComment) {",
        "        size_t end = line.find(\"*/\");",
        "        if (end == std::string_view::npos) {",
        "            spans.push_back({0, n, SyntaxToken::Comment});",
        "            return spans;",
        "        }",
    };
    std::vector<std::string> viewport24; for(int i=0;i<24;++i) viewport24.push_back(cppFile[i%cppFile.size()]);
    std::vector<std::string> big1524(1524, "int x = 42; // comment");

    benchFrameReturn("cpp 11 líneas", SyntaxLanguage::Cpp, cppFile, 2000);
    benchFrameReuse("cpp 11 líneas", SyntaxLanguage::Cpp, cppFile, 2000);
    benchFrameReturn("viewport 24", SyntaxLanguage::Cpp, viewport24, 1000);
    benchFrameReuse("viewport 24", SyntaxLanguage::Cpp, viewport24, 1000);
    benchFrameReturn("1524 líneas", SyntaxLanguage::Cpp, big1524, 200);
    benchFrameReuse("1524 líneas", SyntaxLanguage::Cpp, big1524, 200);
    benchFrameReturn("1524 líneas", SyntaxLanguage::None, big1524, 200);
    benchFrameReuse("1524 líneas", SyntaxLanguage::None, big1524, 200);

    std::printf("\n== None vs Cpp (reuse) sanity ==\n");
    benchOneReuse("vacia None", SyntaxLanguage::None, "", N);
    benchOneReuse("normal None", SyntaxLanguage::None, "int x = 42;", N);
}
