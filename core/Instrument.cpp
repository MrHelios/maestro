#include "core/Instrument.h"
#include <sstream>
#include <iomanip>

namespace instrument {

bool enabled = false;
thread_local FrameMetrics current{};
thread_local Utf8Tag currentTag = Utf8Tag::None;

void FrameMetrics::reset() {
    buildScreen = 0;
    buildEditorBody = 0;
    renderEditorContent = 0;
    renderEditorRow = 0;
    renderFilledRow = 0;
    buildScreen_nanos = 0;
    buildEditorBody_nanos = 0;
    renderEditorContent_nanos = 0;
    renderEditorRow_nanos = 0;
    renderFilledRow_nanos = 0;
    columnOf = {};
    range = {};
    truncate = {};
    expandTabs = {};
    range_visibleRaw = {};
    range_segments = {};
    range_filled = {};
    truncate_filled = {};
    columnOf_selection = {};
    columnOf_bracket = {};
    columnOf_syntax = {};
    columnOf_filled = {};
    expandTabs_visible = {};
    syntaxEnsureValid_calls = 0;
    syntaxLinesRequested = 0;
    syntaxLinesParsed = 0;
    syntaxHighlighterCalls = 0;
    syntaxEnsureValid_nanos = 0;
}

static std::string countLine(const char* label, const Count& c) {
    // Nota: "scanned_bytes" es historico; para columnOf es input_bytes/scan_limit
    // (limit, aprox. O(n)), para range/truncate es bytes escaneados hasta limite.
    std::ostringstream oss;
    oss << "    " << std::left << std::setw(22) << label
        << " calls: " << std::right << std::setw(8) << c.calls
        << "  scanned_bytes: " << std::setw(10) << c.scanned_bytes
        << "  (input/scan)";
    if (c.nanos) oss << "  time: " << std::fixed << std::setprecision(3) << c.nanos/1e6 << " ms";
    return oss.str();
}

static std::string timeLine(const char* label, uint64_t nanos, uint64_t total) {
    double ms = nanos / 1e6;
    double pct = total ? (100.0 * nanos / total) : 0.0;
    std::ostringstream oss;
    oss << "    " << std::left << std::setw(22) << label
        << std::right << std::setw(8) << std::fixed << std::setprecision(3) << ms << " ms"
        << "  (" << std::fixed << std::setprecision(1) << pct << "%)";
    return oss.str();
}

std::string FrameMetrics::report() const {
    std::ostringstream out;
    out << "frame:\n";
    out << "  buildScreen:              " << buildScreen << "\n";
    out << "  buildEditorBody:          " << buildEditorBody << "\n";
    out << "  renderEditorContent:      " << renderEditorContent << "\n";
    out << "  renderEditorRow:          " << renderEditorRow << "\n";
    out << "  renderFilledRow:          " << renderFilledRow << "\n";
    out << "\n  utf8 totals:\n";
    out << countLine("columnOf", columnOf) << "\n";
    out << countLine("range", range) << "\n";
    out << countLine("truncate", truncate) << "\n";
    out << countLine("expandTabs", expandTabs) << "\n";
    out << "\n  utf8 breakdown:\n";
    out << countLine("range[visibleRaw]", range_visibleRaw) << "\n";
    out << countLine("range[segments]", range_segments) << "\n";
    out << countLine("range[filled]", range_filled) << "\n";
    out << countLine("truncate[filled]", truncate_filled) << "\n";
    out << countLine("columnOf[selection]", columnOf_selection) << "\n";
    out << countLine("columnOf[bracket]", columnOf_bracket) << "\n";
    out << countLine("columnOf[syntax]", columnOf_syntax) << "\n";
    out << countLine("columnOf[filled]", columnOf_filled) << "\n";
    out << countLine("expandTabs[visible]", expandTabs_visible) << "\n";
    return out.str();
}

std::string FrameMetrics::reportHierarchical() const {
    std::ostringstream out;
    out << "buildScreen (" << buildScreen << ")\n";
    out << " └─ buildEditorBody (" << buildEditorBody << ")\n";
    out << "     └─ renderEditorContent (" << renderEditorContent << ")\n";
    out << "         └─ renderEditorRow (" << renderEditorRow << ")\n";
    out << "             ├─ utf8::range [visibleRaw]   " << countLine("", range_visibleRaw) << "\n";
    out << "             ├─ utf8::expandTabs           " << countLine("", expandTabs_visible) << "\n";
    out << "             ├─ utf8::columnOf [selection] " << countLine("", columnOf_selection) << "\n";
    out << "             ├─ utf8::columnOf [bracket]   " << countLine("", columnOf_bracket) << "\n";
    out << "             ├─ utf8::columnOf [syntax]    " << countLine("", columnOf_syntax) << "\n";
    out << "             ├─ utf8::range [segmentos]    " << countLine("", range_segments) << "\n";
    out << "             └─ renderFilledRow (" << renderFilledRow << ")\n";
    out << "                 ├─ range    " << countLine("", range_filled) << "\n";
    out << "                 ├─ truncate " << countLine("", truncate_filled) << "\n";
    out << "                 └─ columnOf " << countLine("", columnOf_filled) << "\n";
    out << "  -- totals --\n";
    out << countLine("columnOf", columnOf) << "\n";
    out << countLine("range", range) << "\n";
    out << countLine("truncate", truncate) << "\n";
    out << countLine("expandTabs", expandTabs) << "\n";
    return out.str();
}

std::string FrameMetrics::reportTimed() const {
    uint64_t total = buildScreen_nanos ? buildScreen_nanos : 1;
    std::ostringstream out;
    out << "frame time: " << std::fixed << std::setprecision(3) << total/1e6 << " ms\n";
    out << timeLine("buildScreen", buildScreen_nanos, total) << "  calls=" << buildScreen << "\n";
    out << timeLine(" buildEditorBody", buildEditorBody_nanos, total) << "  calls=" << buildEditorBody << "\n";
    out << timeLine("  renderEditorContent", renderEditorContent_nanos, total) << "  calls=" << renderEditorContent << "\n";
    out << timeLine("   renderEditorRow", renderEditorRow_nanos, total) << "  calls=" << renderEditorRow << "\n";
    out << timeLine("    renderFilledRow", renderFilledRow_nanos, total) << "  calls=" << renderFilledRow << "\n";
    out << timeLine("  SyntaxCache::ensureValid", syntaxEnsureValid_nanos, total) << "  calls=" << syntaxEnsureValid_calls << "  requested=" << syntaxLinesRequested << "  parsed=" << syntaxLinesParsed << "  highlighter=" << syntaxHighlighterCalls << "\n";
    out << "\n  utf8 buckets (time % of buildScreen):\n";
    // use countLine for details but we want time + calls
    auto bucketLine = [&](const char* label, const Count& c){
        std::ostringstream oss;
        oss << "    " << std::left << std::setw(22) << label
            << " calls:" << std::right << std::setw(6) << c.calls
            << "  time:" << std::setw(8) << std::fixed << std::setprecision(3) << c.nanos/1e6 << " ms"
            << " (" << std::fixed << std::setprecision(1) << (total?100.0*c.nanos/total:0) << "%)"
            << "  bytes:" << std::setw(8) << c.scanned_bytes;
        return oss.str();
    };
    out << bucketLine("range[visibleRaw]", range_visibleRaw) << "\n";
    out << bucketLine("expandTabs[visible]", expandTabs_visible) << "\n";
    out << bucketLine("columnOf[syntax]", columnOf_syntax) << "\n";
    out << bucketLine("range[segments]", range_segments) << "\n";
    out << bucketLine("columnOf[selection]", columnOf_selection) << "\n";
    out << bucketLine("columnOf[bracket]", columnOf_bracket) << "\n";
    out << bucketLine("range[filled]", range_filled) << "\n";
    out << bucketLine("truncate[filled]", truncate_filled) << "\n";
    out << bucketLine("columnOf[filled]", columnOf_filled) << "\n";
    out << "\n  totals:\n";
    out << bucketLine("columnOf total", columnOf) << "\n";
    out << bucketLine("range total", range) << "\n";
    out << bucketLine("truncate total", truncate) << "\n";
    out << bucketLine("expandTabs total", expandTabs) << "\n";
    uint64_t utf8Sum = range_visibleRaw.nanos + expandTabs_visible.nanos + columnOf_syntax.nanos + range_segments.nanos + columnOf_selection.nanos + columnOf_bracket.nanos + range_filled.nanos + truncate_filled.nanos + columnOf_filled.nanos;
    out << "  utf8 buckets sum: " << std::fixed << std::setprecision(3) << utf8Sum/1e6 << " ms (" << (100.0*utf8Sum/total) << "% of buildScreen)\n";
    uint64_t resto = total > utf8Sum ? total - utf8Sum : 0;
    out << "  resto (layout/gutter/syntaxCache/etc): " << std::fixed << std::setprecision(3) << resto/1e6 << " ms (" << (100.0*resto/total) << "%)\n";
    return out.str();
}

std::string FrameMetrics::reportTimedHierarchical() const {
    uint64_t total = buildScreen_nanos ? buildScreen_nanos : 1;
    std::ostringstream out;
    out << "buildScreen " << std::fixed << std::setprecision(3) << total/1e6 << " ms (100%)\n";
    auto pct = [&](uint64_t n){ return total? 100.0*n/total:0; };
    out << " └─ buildEditorBody " << std::fixed << std::setprecision(3) << buildEditorBody_nanos/1e6 << " ms (" << std::fixed << std::setprecision(1) << pct(buildEditorBody_nanos) << "%)\n";
    out << "     └─ renderEditorContent " << renderEditorContent_nanos/1e6 << " ms (" << pct(renderEditorContent_nanos) << "%)\n";
    out << "         └─ renderEditorRow " << renderEditorRow_nanos/1e6 << " ms (" << pct(renderEditorRow_nanos) << "%) calls=" << renderEditorRow << "\n";
    auto line = [&](const char* name, const Count& c){
        std::ostringstream oss;
        oss << "             ├─ " << std::left << std::setw(24) << name
            << " " << std::fixed << std::setprecision(3) << std::setw(7) << c.nanos/1e6 << " ms"
            << " (" << std::fixed << std::setprecision(1) << std::setw(4) << pct(c.nanos) << "%)"
            << " calls=" << std::setw(4) << c.calls;
        return oss.str();
    };
    out << line("range visibleRaw", range_visibleRaw) << "\n";
    out << line("expandTabs", expandTabs_visible) << "\n";
    out << line("columnOf syntax", columnOf_syntax) << "\n";
    out << line("range segments", range_segments) << "\n";
    out << line("columnOf filled", columnOf_filled) << "\n";
    out << "             └─ renderFilledRow " << renderFilledRow_nanos/1e6 << " ms (" << pct(renderFilledRow_nanos) << "%)\n";
    out << "                 ├─ range " << range_filled.nanos/1e6 << " ms (" << pct(range_filled.nanos) << "%)\n";
    out << "                 ├─ truncate " << truncate_filled.nanos/1e6 << " ms (" << pct(truncate_filled.nanos) << "%)\n";
    out << "                 └─ columnOf " << columnOf_filled.nanos/1e6 << " ms (" << pct(columnOf_filled.nanos) << "%)\n";
    return out.str();
}

} // namespace instrument
