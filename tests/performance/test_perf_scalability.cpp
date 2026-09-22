#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "test_framework.h"
#include "helpers/perf_helpers.h"
#include "helpers/perf_time_utils.h"
#include "helpers/alloc_stats.h"
#include "helpers/test_render_utils.h"

#define private public
#include "ui/Editor.h"
#undef private

#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Viewport.h"
#include "ui/Renderer.h"
#include "syntax/SyntaxCache.h"
#include "syntax/SyntaxLanguage.h"
#include "core/BracketMatcher.h"

namespace {
using perf_helpers::makeLines;
using perf_helpers::moveEvent;

// 80 cols C++ válido repetido
inline std::vector<std::string> makeCppLines(int n) {
    std::vector<std::string> out;
    out.reserve(n > 0 ? n : 1);
    if (n <= 0) {
        out.emplace_back("");
        return out;
    }
    for (int i = 0; i < n; ++i) {
        std::string line = "int var" + std::to_string(i % 1000) + " = " + std::to_string(i) + "; // ";
        while ((int)line.size() < 80) line += "x";
        line.resize(80);
        out.push_back(line);
    }
    return out;
}

} // anonymous

// ---------------------------------------------------------------------------
// 2. Test de carga del documento
// ---------------------------------------------------------------------------
TEST(bench_perf_document_load_checked) {
    std::printf("\n== perf_document_load (0 / 1k / 10k / 25k) 80 cols Cpp ==\n");
    const int sizes[] = {0, 1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        int iters = n==0 ? 200 : n==1000 ? 100 : n==10000 ? 20 : 10;
        char label[64]; std::snprintf(label,sizeof(label),"load %5d lines",n);
        // medir restore (construcción)
        alloc_stats::resetAll();
        auto t0 = std::chrono::steady_clock::now();
        {
            alloc_stats::Scoped s(alloc_stats::kOther);
            for (int i=0;i<iters;++i){ Document d; d.restore(lines); perf_time::g_sink += d.lineCount(); }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/ (unsigned long long)iters, st.bytesAllocated/(unsigned long long)iters);
    }
    CHECK(perf_time::g_sink>0);
}

// ---------------------------------------------------------------------------
// 3. SyntaxCache
// ---------------------------------------------------------------------------
TEST(bench_perf_syntax_cold_checked) {
    std::printf("\n== perf_syntax_cold (cold ensureValid hasta final) ==\n");
    const int sizes[] = {0, 1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Document doc; doc.restore(lines);
        int iters = n==0 ? 200 : n==1000 ? 50 : n==10000 ? 10 : 5;
        char label[64]; std::snprintf(label,sizeof(label),"syntax cold %5d",n);
        // bench: crear cache vacío cada iter y ensureValid hasta n
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kOther);
            for (int i=0;i<iters;++i){
                SyntaxCache cache; cache.setLanguage(SyntaxLanguage::Cpp);
                cache.ensureValid(doc, n==0?1:n);
                perf_time::g_sink += cache.allBefore().size();
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)iters, st.bytesAllocated/(unsigned long long)iters);
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_syntax_warm_checked) {
    std::printf("\n== perf_syntax_warm (segunda ensureValid cache convergida) ==\n");
    const int sizes[] = {0, 1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Document doc; doc.restore(lines);
        SyntaxCache cache; cache.setLanguage(SyntaxLanguage::Cpp);
        cache.ensureValid(doc, n==0?1:n);
        int iters = n==0 ? 1000 : n==1000 ? 500 : n==10000 ? 200 : 200;
        char label[64]; std::snprintf(label,sizeof(label),"syntax warm %5d",n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kOther);
            for (int i=0;i<iters;++i){ cache.ensureValid(doc, n==0?1:n); perf_time::g_sink += cache.allBefore().size(); }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)iters, st.bytesAllocated/(unsigned long long)iters);
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_syntax_incremental_mid_checked) {
    std::printf("\n== perf_syntax_incremental_mid (edit medio) - solo ensureValid ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Document doc; doc.restore(lines);
        SyntaxCache cache; cache.setLanguage(SyntaxLanguage::Cpp);
        cache.ensureValid(doc, n);
        int mid = n/2;
        std::string origLine = doc.lineAt(mid);
        std::string modLine = "int MODIFIED = 999; // xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
        modLine.resize(80,'x');
        int iters = n==1000 ? 100 : 20;
        char label[64]; std::snprintf(label,sizeof(label),"syntax inc mid %5d",n);
        // preparación fuera de región medida ya hecha (doc convergido)
        long long total_ns = 0;
        long long total_allocs = 0;
        long long total_bytes = 0;
        for (int i=0;i<iters;++i){
            // --- edición real fuera de medición (O(line), no O(N)) ---
            {
                int len = doc.lineLength(mid);
                doc.deleteRange(mid,0,mid,len);
                doc.insertText(mid,0,modLine);
                cache.markDirty(mid);
            }
            // --- solo ensureValid medido ---
            alloc_stats::resetAll();
            auto s = std::chrono::steady_clock::now();
            {
                alloc_stats::Scoped sc(alloc_stats::kOther);
                cache.ensureValid(doc, n);
            }
            auto e = std::chrono::steady_clock::now();
            total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e-s).count();
            auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
            total_allocs += st.allocs;
            total_bytes += st.bytesAllocated;
            perf_time::g_sink += cache.allBefore().size();
            // --- revert fuera de medición para siguiente iter ---
            {
                int len2 = doc.lineLength(mid);
                doc.deleteRange(mid,0,mid,len2);
                doc.insertText(mid,0,origLine);
                cache.markDirty(mid);
                cache.ensureValid(doc, n);
            }
        }
        double us = (double)total_ns/iters/1000.0;
        std::printf("%-48s %6d iters  %8.1f us/op  %6lld allocs/op  %8lld bytes/op\n", label, iters, us, total_allocs/iters, total_bytes/iters);
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_syntax_incremental_near_end_checked) {
    std::printf("\n== perf_syntax_incremental_near_end (edit cerca final) - solo ensureValid ==\n");
    struct Case{int n; int line;};
    Case cases[] = {{1000,900},{10000,9000},{25000,24000}};
    for (auto c: cases) {
        auto lines = makeCppLines(c.n);
        Document doc; doc.restore(lines);
        SyntaxCache cache; cache.setLanguage(SyntaxLanguage::Cpp);
        cache.ensureValid(doc, c.n);
        int target = c.line;
        std::string origLine = doc.lineAt(target);
        std::string modLine = "int MODIFIED_END = 1; // yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy";
        modLine.resize(80,'y');
        int iters = c.n==1000 ? 100 : 20;
        char label[64]; std::snprintf(label,sizeof(label),"syntax inc end %5d @%d",c.n,c.line);
        long long total_ns=0, total_allocs=0, total_bytes=0;
        for (int i=0;i<iters;++i){
            {
                int len = doc.lineLength(target);
                doc.deleteRange(target,0,target,len);
                doc.insertText(target,0,modLine);
                cache.markDirty(target);
            }
            alloc_stats::resetAll();
            auto s = std::chrono::steady_clock::now();
            {
                alloc_stats::Scoped sc(alloc_stats::kOther);
                cache.ensureValid(doc, c.n);
            }
            auto e = std::chrono::steady_clock::now();
            total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e-s).count();
            auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
            total_allocs += st.allocs; total_bytes += st.bytesAllocated;
            perf_time::g_sink += cache.allBefore().size();
            {
                int len2 = doc.lineLength(target);
                doc.deleteRange(target,0,target,len2);
                doc.insertText(target,0,origLine);
                cache.markDirty(target);
                cache.ensureValid(doc, c.n);
            }
        }
        double us = (double)total_ns/iters/1000.0;
        std::printf("%-48s %6d iters  %8.1f us/op  %6lld allocs/op  %8lld bytes/op\n", label, iters, us, total_allocs/iters, total_bytes/iters);
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_syntax_incremental_repeat_checked) {
    std::printf("\n== perf_syntax_incremental_repeat (100 edits locales) - solo ensureValid ==\n");
    const int sizes[] = {10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Document doc; doc.restore(lines);
        SyntaxCache cache; cache.setLanguage(SyntaxLanguage::Cpp);
        cache.ensureValid(doc, n);
        int iters = 100;
        char label[64]; std::snprintf(label,sizeof(label),"syntax repeat %5d x100",n);
        long long total_ns=0, total_allocs=0, total_bytes=0;
        for (int i=0;i<iters;++i){
            int line = (n/2 + i*7) % n;
            std::string origLine = doc.lineAt(line);
            std::string modLine = "int rep" + std::to_string(i) + " = " + std::to_string(i) + "; // zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz";
            modLine.resize(80,'z');
            {
                int len = doc.lineLength(line);
                doc.deleteRange(line,0,line,len);
                doc.insertText(line,0,modLine);
                cache.markDirty(line);
            }
            alloc_stats::resetAll();
            auto s = std::chrono::steady_clock::now();
            {
                alloc_stats::Scoped sc(alloc_stats::kOther);
                cache.ensureValid(doc, n);
            }
            auto e = std::chrono::steady_clock::now();
            total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e-s).count();
            auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
            total_allocs += st.allocs; total_bytes += st.bytesAllocated;
            perf_time::g_sink += cache.allBefore().size();
            // revert fuera de medición y reconverger
            {
                int len2 = doc.lineLength(line);
                doc.deleteRange(line,0,line,len2);
                doc.insertText(line,0,origLine);
                cache.markDirty(line);
                cache.ensureValid(doc, n);
            }
        }
        double us = (double)total_ns/iters/1000.0;
        std::printf("%-48s %6d iters  %8.1f us/op  %6lld allocs/op  %8lld bytes/op\n", label, iters, us, total_allocs/iters, total_bytes/iters);
    }
    CHECK(perf_time::g_sink>0);
}

// ---------------------------------------------------------------------------
// 4. Renderer / buildScreen
// ---------------------------------------------------------------------------
TEST(bench_perf_render_static_checked) {
    std::printf("\n== perf_render_static (viewport 24x80, cache convergida) ==\n");
    const int sizes[] = {0, 1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Document doc; doc.restore(lines);
        Viewport vp; vp.top=0; vp.height=24; vp.width=80; vp.left=0;
        Cursor cur; cur.line=0; cur.col=0;
        Renderer r; // need cpp language
        // warm syntax cache via editor-like path: use renderer internal cache
        // For benchmark we force SyntaxCache via Renderer activeCache
        // Instead use Editor to prime cache
        Editor ed;
        ed.active().document.restore(lines);
        ed.active().viewport = vp;
        ed.active().cursor = cur;
        // ensure cache convergido
        ed.renderer_.activeCache().setLanguage(SyntaxLanguage::Cpp);
        ed.renderer_.activeCache().ensureValid(ed.active().document, n==0?1:n);
        int iters = n==0?2000 : n==1000?1000 : n==10000?200:100;
        char label[64]; std::snprintf(label,sizeof(label),"render static %5d",n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kRenderFrame);
            for (int i=0;i<iters;++i){
                std::string out = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, State::Navegacion, std::nullopt);
                perf_time::g_sink += out.size();
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)iters, st.bytesAllocated/(unsigned long long)iters);
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_render_cursor_positions_checked) {
    std::printf("\n== perf_render_cursor_top/middle/end (10k/25k) ==\n");
    const int sizes[] = {10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Editor ed;
        ed.active().document.restore(lines);
        ed.active().viewport.height=24; ed.active().viewport.width=80;
        ed.renderer_.activeCache().setLanguage(SyntaxLanguage::Cpp);
        ed.renderer_.activeCache().ensureValid(ed.active().document, n);
        struct Pos{const char* name; int line;};
        Pos poses[] = {{"top",0},{"middle",n/2},{"end",n-1}};
        for (auto p: poses) {
            ed.active().cursor.line = p.line;
            ed.active().cursor.col = 0;
            ed.active().viewport.top = std::max(0, p.line-12);
            int iters = 200;
            char label[64]; std::snprintf(label,sizeof(label),"render %s %5d",p.name,n);
            auto t0 = std::chrono::steady_clock::now();
            alloc_stats::resetAll();
            {
                alloc_stats::Scoped s(alloc_stats::kRenderFrame);
                for (int i=0;i<iters;++i){
                    std::string out = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, State::Navegacion, std::nullopt);
                    perf_time::g_sink += out.size();
                }
            }
            auto t1 = std::chrono::steady_clock::now();
            double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0;
            auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
            std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)iters, st.bytesAllocated/(unsigned long long)iters);
        }
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_render_syntax_cpp_checked) {
    std::printf("\n== perf_render_syntax_cpp vs None (0/1k/10k/25k) ==\n");
    const int sizes[] = {0, 1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        // With syntax
        {
            Editor ed;
            ed.active().document.restore(lines);
            ed.active().viewport.height=24; ed.active().viewport.width=80;
            ed.renderer_.activeCache().setLanguage(SyntaxLanguage::Cpp);
            ed.renderer_.activeCache().ensureValid(ed.active().document, n==0?1:n);
            int iters = n==0?1000 : n==1000?500 : 100;
            char label[64]; std::snprintf(label,sizeof(label),"render Cpp %5d",n);
            auto t0 = std::chrono::steady_clock::now();
            alloc_stats::resetAll();
            {
                alloc_stats::Scoped s(alloc_stats::kRenderFrame);
                for (int i=0;i<iters;++i){
                    std::string out = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, State::Navegacion, std::nullopt);
                    perf_time::g_sink += out.size();
                }
            }
            auto t1 = std::chrono::steady_clock::now();
            double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0;
            auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
            std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)iters, st.bytesAllocated/(unsigned long long)iters);
        }
        // Without syntax
        {
            Editor ed;
            ed.active().document.restore(lines);
            ed.active().viewport.height=24; ed.active().viewport.width=80;
            ed.renderer_.activeCache().setLanguage(SyntaxLanguage::None);
            int iters = n==0?1000 : n==1000?500 : 100;
            char label[64]; std::snprintf(label,sizeof(label),"render None %5d",n);
            auto t0 = std::chrono::steady_clock::now();
            alloc_stats::resetAll();
            {
                alloc_stats::Scoped s(alloc_stats::kRenderFrame);
                for (int i=0;i<iters;++i){
                    std::string out = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.txt", false, Message{}, State::Navegacion, std::nullopt);
                    perf_time::g_sink += out.size();
                }
            }
            auto t1 = std::chrono::steady_clock::now();
            double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0;
            auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
            std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)iters, st.bytesAllocated/(unsigned long long)iters);
        }
    }
    CHECK(perf_time::g_sink>0);
}

// ---------------------------------------------------------------------------
// 6. Bracket matching
// ---------------------------------------------------------------------------
TEST(bench_perf_bracket_match_checked) {
    std::printf("\n== perf_bracket_match (cold/cached/after_edit) ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        // crear doc con un único par balanceado: { en 0 y } en n-1, resto sin brackets para medir matching puro
        auto lines = makeCppLines(n);
        if (n>0) {
            lines[0] = "{ // open bracket";
            lines[0].resize(80,' ');
            lines[n-1] = "} // close bracket";
            lines[n-1].resize(80,' ');
            for (int i=1;i<n-1;++i) {
                // asegurar que no haya brackets accidentales en makeCppLines
                std::string &s = lines[i];
                for (char &c : s) if (c=='{'||c=='}'||c=='('||c==')'||c=='['||c==']') c='x';
            }
        }
        Document doc; doc.restore(lines);
        Position pos{0,0};
        // cold - jump lazy (findMatchingBracketFrom sobre SyntaxCache frío)
        {
            int iters = n==1000 ? 20 : n==10000 ? 5 : 3;
            char label[64];
            std::snprintf(label, sizeof(label), "bracket jump lazy cold %5d", n);

            auto t0 = std::chrono::steady_clock::now();
            alloc_stats::resetAll();
            {
                alloc_stats::Scoped s(alloc_stats::kOther);
                for (int i = 0; i < iters; ++i) {
                    SyntaxCache cache;
                    cache.setLanguage(SyntaxLanguage::Cpp);

                    BracketSpanSource src;
                    src.ensure = [&cache, &doc](int line) {
                        if (line >= 0) cache.ensureValid(doc, line + 1);
                    };
                    src.spans = [&cache](int line) -> const std::vector<SyntaxSpan>& {
                        return cache.spansFor(line);
                    };

                    auto p = findMatchingBracketFrom(doc, pos, SyntaxLanguage::Cpp, src);
                    perf_time::g_sink += p ? 1 : 0;
                }
            }
            auto t1 = std::chrono::steady_clock::now();

            double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters / 1000.0;
            auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
            std::printf(
                "%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n",
                label,
                iters,
                us,
                st.allocs / (unsigned long long)iters,
                st.bytesAllocated / (unsigned long long)iters
            );
        }
        // cached via SyntaxCache - usa allSpans() por referencia (camino real Editor)
        {
            SyntaxCache cache; cache.setLanguage(SyntaxLanguage::Cpp);
            cache.ensureValid(doc, n);
            const auto& spans = cache.allSpans();
            int iters = n==1000?100 : n==10000?20 : 10;
            char label[64]; std::snprintf(label,sizeof(label),"bracket cached %5d",n);
            auto t0 = std::chrono::steady_clock::now();
            alloc_stats::resetAll();
            {
                alloc_stats::Scoped s(alloc_stats::kOther);
                for (int i=0;i<iters;++i){
                    auto p = findMatchingBracket(doc, pos, SyntaxLanguage::Cpp, spans);
                    perf_time::g_sink += p ? 1 : 0;
                }
            }
            auto t1 = std::chrono::steady_clock::now();
            double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0;
            auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
            std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)iters, st.bytesAllocated/(unsigned long long)iters);
        }
        // after edit: B. bracket después de convergencia (edit->markDirty->ensureValid->spans actualizados->bracket)
        {
            auto mod = lines;
            int mid = n/2;
            if (mid < (int)mod.size()) mod[mid] = "/* block comment start";
            if (mid+1 < (int)mod.size()) mod[mid+1] = "still comment";
            Document doc2; doc2.restore(mod);
            SyntaxCache cache; cache.setLanguage(SyntaxLanguage::Cpp);
            cache.ensureValid(doc2, n);
            std::string origLine = doc2.lineAt(mid);
            std::string editLine = "int x = 1; // edit";
            editLine.resize(80,' ');
            int iters = n==1000 ? 100 : 20;
            char label[64]; std::snprintf(label,sizeof(label),"bracket afterEdit %5d",n);
            long long total_ns=0, total_allocs=0, total_bytes=0;
            for (int i=0;i<iters;++i){
                // --- edición + convergencia fuera de medición de bracket ---
                // toggle: si está origLine, pasar a editLine y viceversa
                std::string cur = doc2.lineAt(mid);
                std::string next = (cur == origLine ? editLine : origLine);
                {
                    int len = doc2.lineLength(mid);
                    doc2.deleteRange(mid,0,mid,len);
                    doc2.insertText(mid,0,next);
                    cache.markDirty(mid);
                    cache.ensureValid(doc2, n);
                }
                    // spans actualizados vía allSpans() (referencia, camino real Editor)
                const auto& spans = cache.allSpans();
                // --- solo bracket medido ---
                alloc_stats::resetAll();
                auto s = std::chrono::steady_clock::now();
                {
                    alloc_stats::Scoped sc(alloc_stats::kOther);
                    auto p = findMatchingBracket(doc2, pos, SyntaxLanguage::Cpp, spans);
                    perf_time::g_sink += p ? 1 : 0;
                }
                auto e = std::chrono::steady_clock::now();
                total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e-s).count();
                auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
                total_allocs += st.allocs; total_bytes += st.bytesAllocated;
            }
            double us = (double)total_ns/iters/1000.0;
            std::printf("%-48s %6d iters  %8.1f us/op  %6lld allocs/op  %8lld bytes/op\n", label, iters, us, total_allocs/iters, total_bytes/iters);
        }
    }
    CHECK(perf_time::g_sink>0);
}

// ---------------------------------------------------------------------------
// 7. Interacción real
// ---------------------------------------------------------------------------
TEST(bench_perf_typing_render_cycle_checked) {
    std::printf("\n== perf_typing_render_cycle (insert+render+backspace+render)/2 1k/10k/25k ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Editor ed;
        ed.active().document.restore(lines);
        ed.active().viewport.height=24; ed.active().viewport.width=80;
        ed.active().cursor.line = n/2; ed.active().cursor.col = 5;
        ed.state_ = State::Interaccion;
        ed.renderer_.activeCache().setLanguage(SyntaxLanguage::Cpp);
        ed.renderer_.activeCache().ensureValid(ed.active().document, n);
        Event e; e.type=EventType::InsertChar; e.text="a";
        int iters = n==1000?500 : n==10000?200 : 100;
        char label[64]; std::snprintf(label,sizeof(label),"typing cycle %5d",n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kTyping);
            for (int i=0;i<iters;++i){
                ed.handleEvent(e);
                std::string out = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                perf_time::g_sink += out.size();
                // undo char to keep doc size stable
                Event b; b.type=EventType::Backspace;
                ed.handleEvent(b);
                std::string out2 = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                perf_time::g_sink += out2.size();
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0/2;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)(iters*2), st.bytesAllocated/(unsigned long long)(iters*2));
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_backspace_render_cycle_checked) {
    std::printf("\n== perf_backspace_render_cycle (backspace+render+insert+render)/2 1k/10k/25k ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Editor ed;
        ed.active().document.restore(lines);
        ed.active().viewport.height=24; ed.active().viewport.width=80;
        ed.active().cursor.line = n/2; ed.active().cursor.col = 10;
        ed.state_ = State::Interaccion;
        ed.renderer_.activeCache().setLanguage(SyntaxLanguage::Cpp);
        ed.renderer_.activeCache().ensureValid(ed.active().document, n);
        int iters = n==1000?500:200;
        char label[64]; std::snprintf(label,sizeof(label),"backspace cycle %5d",n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kTyping);
            for (int i=0;i<iters;++i){
                Event b; b.type=EventType::Backspace;
                ed.handleEvent(b);
                std::string out = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                perf_time::g_sink += out.size();
                Event e; e.type=EventType::InsertChar; e.text="x";
                ed.handleEvent(e);
                std::string out2 = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                perf_time::g_sink += out2.size();
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0/2;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)(iters*2), st.bytesAllocated/(unsigned long long)(iters*2));
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_handleEvent_insertChar_checked) {
    std::printf("\n== perf_handleEvent_insertChar (solo handleEvent, sin render) 1k/10k/25k ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Editor ed;
        ed.active().document.restore(lines);
        ed.active().viewport.height=24; ed.active().viewport.width=80;
        ed.active().cursor.line = n/2; ed.active().cursor.col = 5;
        ed.state_ = State::Interaccion;
        ed.renderer_.activeCache().setLanguage(SyntaxLanguage::Cpp);
        ed.renderer_.activeCache().ensureValid(ed.active().document, n);
        Event e; e.type=EventType::InsertChar; e.text="a";
        int iters = n==1000?1000 : n==10000?500 : 200;
        char label[64]; std::snprintf(label,sizeof(label),"handleEvent insert %5d",n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kTyping);
            for (int i=0;i<iters;++i){
                ed.handleEvent(e);
                perf_time::g_sink += ed.active().cursor.col;
                Event b; b.type=EventType::Backspace;
                ed.handleEvent(b);
                perf_time::g_sink += ed.active().cursor.col;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0/2;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)(iters*2), st.bytesAllocated/(unsigned long long)(iters*2));
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_handleEvent_backspace_checked) {
    std::printf("\n== perf_handleEvent_backspace (solo handleEvent) 1k/10k/25k ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Editor ed;
        ed.active().document.restore(lines);
        ed.active().viewport.height=24; ed.active().viewport.width=80;
        ed.active().cursor.line = n/2; ed.active().cursor.col = 10;
        ed.state_ = State::Interaccion;
        ed.renderer_.activeCache().setLanguage(SyntaxLanguage::Cpp);
        ed.renderer_.activeCache().ensureValid(ed.active().document, n);
        // preparar un char extra para poder borrar
        Event ins; ins.type=EventType::InsertChar; ins.text="x";
        ed.handleEvent(ins);
        int iters = n==1000?1000:500;
        char label[64]; std::snprintf(label,sizeof(label),"handleEvent bspace %5d",n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kTyping);
            for (int i=0;i<iters;++i){
                Event b; b.type=EventType::Backspace;
                ed.handleEvent(b);
                perf_time::g_sink += ed.active().cursor.col;
                Event e; e.type=EventType::InsertChar; e.text="x";
                ed.handleEvent(e);
                perf_time::g_sink += ed.active().cursor.col;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0/2;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)(iters*2), st.bytesAllocated/(unsigned long long)(iters*2));
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_move_left_right_checked) {
    std::printf("\n== perf_move_left/right (1k/10k/25k) línea larga ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        // hacer una línea larga 4000 cols en el medio
        if (n>0) lines[n/2] = std::string(4000,'x');
        Editor ed;
        ed.active().document.restore(lines);
        ed.active().viewport.height=24; ed.active().viewport.width=80;
        ed.active().cursor.line = n/2; ed.active().cursor.col = 2000;
        ed.state_ = State::Navegacion;
        int iters = 1000;
        char label[64]; std::snprintf(label,sizeof(label),"move L/R %5d",n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kOther);
            for (int i=0;i<iters;++i){
                ed.handleEvent(moveEvent(EventType::MoveLeft));
                std::string out = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                perf_time::g_sink += out.size();
                ed.handleEvent(moveEvent(EventType::MoveRight));
                std::string out2 = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                perf_time::g_sink += out2.size();
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0/2;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)(iters*2), st.bytesAllocated/(unsigned long long)(iters*2));
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_page_up_down_render_cycle_checked) {
    std::printf("\n== perf_page_up_down_render_cycle (PageUp+render+PageDown+render)/2 (10k/25k) ==\n");
    const int sizes[] = {10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Editor ed;
        ed.active().document.restore(lines);
        ed.active().viewport.height=24; ed.active().viewport.width=80;
        ed.active().viewport.top = n/2;
        ed.active().cursor.line = n/2;
        ed.state_ = State::Navegacion;
        ed.renderer_.activeCache().setLanguage(SyntaxLanguage::Cpp);
        ed.renderer_.activeCache().ensureValid(ed.active().document, n);
        int iters = 200;
        char label[64]; std::snprintf(label,sizeof(label),"pageUp/Down cycle %5d",n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kOther);
            for (int i=0;i<iters;++i){
                ed.handleEvent(moveEvent(EventType::PageUp));
                std::string out = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                perf_time::g_sink += out.size();
                ed.handleEvent(moveEvent(EventType::PageDown));
                std::string out2 = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                perf_time::g_sink += out2.size();
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0/2;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)(iters*2), st.bytesAllocated/(unsigned long long)(iters*2));
    }
    CHECK(perf_time::g_sink>0);
}

// ---------------------------------------------------------------------------
// 8. Scroll
// ---------------------------------------------------------------------------
TEST(bench_perf_scroll_checked) {
    std::printf("\n== perf_scroll (10k/25k) line-by-line ==\n");
    const int sizes[] = {10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Editor ed;
        ed.active().document.restore(lines);
        ed.active().viewport.height=24; ed.active().viewport.width=80;
        ed.active().viewport.top = n/2;
        ed.active().cursor.line = n/2;
        ed.state_ = State::Navegacion;
        int iters = 500;
        char label[64]; std::snprintf(label,sizeof(label),"scroll %5d",n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kOther);
            for (int i=0;i<iters;++i){
                ed.handleEvent(moveEvent(EventType::MoveDown));
                std::string out = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                perf_time::g_sink += out.size();
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/iters/1000.0;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n", label, iters, us, st.allocs/(unsigned long long)iters, st.bytesAllocated/(unsigned long long)iters);
    }
    CHECK(perf_time::g_sink>0);
}

// ---------------------------------------------------------------------------
// 9. Edición estructural
// ---------------------------------------------------------------------------
TEST(bench_perf_insert_line_checked) {
    std::printf("\n== perf_insert_line (solo InsertNewline, 1k/10k/25k) principio/mitad/final ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        for (const char* posName : {"top","mid","end"}) {
            int line = 0;
            if (std::string(posName)=="mid") line = n/2;
            else if (std::string(posName)=="end") line = n-1;
            auto lines = makeCppLines(n);
            int iters = n==1000?200:50;
            char label[64]; std::snprintf(label,sizeof(label),"insertLine %s %5d",posName,n);
            long long total_ns=0, total_allocs=0, total_bytes=0;
            for (int i=0;i<iters;++i){
                Editor ed;
                ed.active().document.restore(lines);
                ed.active().viewport.height=24; ed.active().viewport.width=80;
                ed.state_ = State::Interaccion;
                ed.active().cursor.line = line;
                ed.active().cursor.col = 0;
                alloc_stats::resetAll();
                auto s = std::chrono::steady_clock::now();
                {
                    alloc_stats::Scoped sc(alloc_stats::kOther);
                    ed.handleEvent(moveEvent(EventType::InsertNewline));
                    perf_time::g_sink += ed.active().document.lineCount();
                }
                auto e = std::chrono::steady_clock::now();
                total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e-s).count();
                auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
                total_allocs += st.allocs; total_bytes += st.bytesAllocated;
            }
            double us = (double)total_ns/iters/1000.0;
            std::printf("%-48s %6d iters  %8.1f us/op  %6lld allocs/op  %8lld bytes/op\n", label, iters, us, total_allocs/iters, total_bytes/iters);
        }
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_insert_line_render_cycle_checked) {
    std::printf("\n== perf_insert_line_render_cycle (InsertNewline+render, 1k/10k/25k) ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        for (const char* posName : {"top","mid","end"}) {
            int line = 0;
            if (std::string(posName)=="mid") line = n/2;
            else if (std::string(posName)=="end") line = n-1;
            auto lines = makeCppLines(n);
            Editor ed;
            ed.active().document.restore(lines);
            ed.active().viewport.height=24; ed.active().viewport.width=80;
            ed.state_ = State::Interaccion;
            int iters = n==1000?200:50;
            char label[64]; std::snprintf(label,sizeof(label),"insertLine+render %s %5d",posName,n);
            long long total_ns=0, total_allocs=0, total_bytes=0;
            for (int i=0;i<iters;++i){
                ed.active().cursor.line = line;
                ed.active().cursor.col = 0;
                alloc_stats::resetAll();
                auto s = std::chrono::steady_clock::now();
                {
                    alloc_stats::Scoped sc(alloc_stats::kOther);
                    ed.handleEvent(moveEvent(EventType::InsertNewline));
                    std::string out = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                    perf_time::g_sink += out.size();
                }
                auto e = std::chrono::steady_clock::now();
                total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e-s).count();
                auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
                total_allocs += st.allocs; total_bytes += st.bytesAllocated;
                ed.handleEvent(moveEvent(EventType::Undo));
            }
            double us = (double)total_ns/iters/1000.0;
            std::printf("%-48s %6d iters  %8.1f us/op  %6lld allocs/op  %8lld bytes/op\n", label, iters, us, total_allocs/iters, total_bytes/iters);
        }
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_undo_checked) {
    std::printf("\n== perf_undo (solo undo, 1k/10k/25k) ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        for (const char* posName : {"top","mid","end"}) {
            int line = 0;
            if (std::string(posName)=="mid") line = n/2;
            else if (std::string(posName)=="end") line = n-1;
            auto lines = makeCppLines(n);
            int iters = n==1000?200:50;
            char label[64]; std::snprintf(label,sizeof(label),"undo %s %5d",posName,n);
            long long total_ns=0, total_allocs=0, total_bytes=0;
            for (int i=0;i<iters;++i){
                Editor ed;
                ed.active().document.restore(lines);
                ed.active().viewport.height=24; ed.active().viewport.width=80;
                ed.state_ = State::Interaccion;
                ed.active().cursor.line = line;
                ed.active().cursor.col = 0;
                ed.handleEvent(moveEvent(EventType::InsertNewline));
                // medir solo undo
                alloc_stats::resetAll();
                auto s = std::chrono::steady_clock::now();
                {
                    alloc_stats::Scoped sc(alloc_stats::kOther);
                    ed.handleEvent(moveEvent(EventType::Undo));
                }
                auto e = std::chrono::steady_clock::now();
                total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e-s).count();
                auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
                total_allocs += st.allocs; total_bytes += st.bytesAllocated;
                perf_time::g_sink += ed.active().document.lineCount();
            }
            double us = (double)total_ns/iters/1000.0;
            std::printf("%-48s %6d iters  %8.1f us/op  %6lld allocs/op  %8lld bytes/op\n", label, iters, us, total_allocs/iters, total_bytes/iters);
        }
    }
    CHECK(perf_time::g_sink>0);
}

TEST(bench_perf_delete_line_checked) {
    std::printf("\n== perf_delete_line (deleteRange+render solo, 1k/10k/25k) ==\n");
    const int sizes[] = {1000, 10000, 25000};
    for (int n : sizes) {
        for (const char* posName : {"top","mid","end"}) {
            int line = 0;
            if (std::string(posName)=="mid") line = n/2;
            else if (std::string(posName)=="end") line = n-1;
            auto lines = makeCppLines(n);
            int iters = n==1000?200:50;
            char label[64]; std::snprintf(label,sizeof(label),"deleteLine %s %5d",posName,n);
            long long total_ns=0, total_allocs=0, total_bytes=0;
            for (int i=0;i<iters;++i){
                Editor ed;
                ed.active().document.restore(lines);
                ed.active().viewport.height=24; ed.active().viewport.width=80;
                ed.active().cursor.line = line;
                ed.active().cursor.col = 0;
                alloc_stats::resetAll();
                auto s = std::chrono::steady_clock::now();
                {
                    alloc_stats::Scoped sc(alloc_stats::kOther);
                    ed.active().document.deleteRange(line,0,line+1<ed.active().document.lineCount()?line+1:line,0);
                    std::string out = ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                    perf_time::g_sink += out.size();
                }
                auto e = std::chrono::steady_clock::now();
                total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e-s).count();
                auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
                total_allocs += st.allocs; total_bytes += st.bytesAllocated;
            }
            double us = (double)total_ns/iters/1000.0;
            std::printf("%-48s %6d iters  %8.1f us/op  %6lld allocs/op  %8lld bytes/op\n", label, iters, us, total_allocs/iters, total_bytes/iters);
        }
    }
    CHECK(perf_time::g_sink>0);
}

// ---------------------------------------------------------------------------
// 10. Caso combinado
// ---------------------------------------------------------------------------
TEST(bench_perf_editor_large_cpp_checked) {
    std::printf("\n== perf_editor_large_cpp (10k/25k escenario integrado) ==\n");
    const int sizes[] = {10000, 25000};
    for (int n : sizes) {
        auto lines = makeCppLines(n);
        Editor ed;
        ed.active().document.restore(lines);
        ed.active().viewport.height=24; ed.active().viewport.width=80;
        ed.active().viewport.top = n/2;
        ed.active().cursor.line = n/2; ed.active().cursor.col=5;
        ed.state_ = State::Interaccion;
        ed.renderer_.activeCache().setLanguage(SyntaxLanguage::Cpp);
        ed.renderer_.activeCache().ensureValid(ed.active().document, n);
        int cycles = 50;
        char label[64]; std::snprintf(label,sizeof(label),"full scenario %5d",n);
        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kOther);
            for (int i=0;i<cycles;++i){
                // load already done
                // move
                ed.handleEvent(moveEvent(EventType::MoveLeft));
                ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                // insert char
                Event e; e.type=EventType::InsertChar; e.text="a";
                ed.handleEvent(e);
                ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                // left/right
                ed.handleEvent(moveEvent(EventType::MoveRight));
                ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                // page down
                ed.handleEvent(moveEvent(EventType::PageDown));
                ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                // bracket match
                Position bpos{ed.active().cursor.line, ed.active().cursor.col};
                const auto& spans = ed.renderer_.activeCache().allSpans();
                auto p = findMatchingBracket(ed.active().document, bpos, SyntaxLanguage::Cpp, spans);
                perf_time::g_sink += p ? 1 : 0;
                // edit inside comment - edición línea real O(line), no snapshot/restore O(N)
                int cl = n/2 + 5;
                if (cl < ed.active().document.lineCount()){
                    std::string newLine = "/* comment edit " + std::to_string(i) + " */";
                    newLine.resize(80,' ');
                    int len = ed.active().document.lineLength(cl);
                    ed.active().document.deleteRange(cl,0,cl,len);
                    ed.active().document.insertText(cl,0,newLine);
                    ed.renderer_.activeCache().markDirty(cl);
                    ed.renderer_.activeCache().ensureValid(ed.active().document, n);
                }
                ed.renderer_.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "bench.cpp", false, Message{}, ed.state_, std::nullopt);
                perf_time::g_sink += 1;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()/cycles/1000.0;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d cycles %8.1f us/cycle  %6llu allocs/cycle %8llu bytes/cycle\n", label, cycles, us, st.allocs/(unsigned long long)cycles, st.bytesAllocated/(unsigned long long)cycles);
    }
    CHECK(perf_time::g_sink>0);
}

// A. Bounded matcher puro (cache caliente, solo scanning)
TEST(bench_perf_bracket_matcher_bounded_checked) {
    std::printf("\n== perf_bracket_matcher_bounded (scanning puro, cache caliente) ==\n");
    const int sizes[] = {1000, 10000, 25000};
    const int vh = 24;

    for (int n : sizes) {
        auto lines = makeCppLines(n);
        if (n > 0) {
            lines[0] = "{ // open";
            lines[n - 1] = "} // close";
        }
        Document doc;
        doc.restore(lines);

        SyntaxCache cache;
        cache.setLanguage(SyntaxLanguage::Cpp);
        cache.ensureValid(doc, n); // Cache completamente caliente

        BracketSpanSource src;
        src.ensure = [](int) {};
        src.spans = [&cache](int line) -> const std::vector<SyntaxSpan>& {
            static const std::vector<SyntaxSpan> empty;
            if (line < 0 || line >= (int)cache.size()) return empty;
            return cache.spansFor(line);
        };

        Position pos{0, 0};
        int iters = n==1000 ? 2000 : n==10000 ? 1000 : 500;
        char label[64];
        std::snprintf(label, sizeof(label), "matcher bounded %5d", n);

        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kOther);
            for (int i = 0; i < iters; ++i) {
                auto p = findMatchingBracketBounded(doc, pos, SyntaxLanguage::Cpp, src, 0, vh);
                perf_time::g_sink += p ? 1 : 0;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters / 1000.0;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n",
            label, iters, us, st.allocs / (unsigned long long)iters, st.bytesAllocated / (unsigned long long)iters);
    }
    CHECK(perf_time::g_sink > 0);
}

// B. Editor highlight path real (SyntaxCache + makeBracketSpanSource + top fijo)
TEST(bench_perf_editor_highlight_path_checked) {
    std::printf("\n== perf_editor_highlight_path (path real Editor, top fijo en N/2) ==\n");
    const int sizes[] = {1000, 10000, 25000};

    for (int n : sizes) {
        auto lines = makeCppLines(n);
        if (n > 0) {
            lines[0] = "{ // open";
            lines[n - 1] = "} // close";
        }
        
        Editor ed;
        ed.active().document.restore(lines);
        ed.active().viewport.height = 24;
        ed.active().viewport.width = 80;
        
        // Fijar top en el medio para demostrar que el costo no depende de N
        int fixedTop = n / 2;
        ed.active().viewport.top = fixedTop;
        ed.active().cursor.line = fixedTop + 10;
        ed.active().cursor.col = 0;

        // Calentar cache del buffer como lo haría el Renderer
        ed.active().syntaxCache.setLanguage(SyntaxLanguage::Cpp);
        ed.active().syntaxCache.ensureValid(ed.active().document, n);

        int iters = n==1000 ? 1000 : n==10000 ? 500 : 200;
        char label[64];
        std::snprintf(label, sizeof(label), "editor highlight %5d (top=%d)", n, fixedTop);

        auto t0 = std::chrono::steady_clock::now();
        alloc_stats::resetAll();
        {
            alloc_stats::Scoped s(alloc_stats::kOther);
            for (int i = 0; i < iters; ++i) {
                // Invalidar fast path para forzar el cálculo real
                ed.lastBracketCursor_ = {-1, -1}; 
                ed.updateBracketHighlight();
                perf_time::g_sink += ed.bracketPair_ ? 1 : 0;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / iters / 1000.0;
        auto st = alloc_stats::statsFor(alloc_stats::kGlobal);
        std::printf("%-48s %6d iters  %8.1f us/op  %6llu allocs/op  %8llu bytes/op\n",
            label, iters, us, st.allocs / (unsigned long long)iters, st.bytesAllocated / (unsigned long long)iters);
    }
    CHECK(perf_time::g_sink > 0);
}