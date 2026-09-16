#include "syntax/SyntaxHighlighter.h"

std::vector<SyntaxSpan> SyntaxHighlighter::highlight(std::string_view line) const {
    if (lang_ == SyntaxLanguage::None) return {};
    SyntaxState s; SyntaxState out;
    std::vector<SyntaxSpan> tmp;
    cLike_.highlight(line, s, out, lang_, tmp);
    return tmp;
}

std::vector<SyntaxSpan> SyntaxHighlighter::highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext) const {
    if (lang_ == SyntaxLanguage::None) { outNext = state; return {}; }
    std::vector<SyntaxSpan> tmp;
    cLike_.highlight(line, state, outNext, lang_, tmp);
    return tmp;
}

void SyntaxHighlighter::highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, std::vector<SyntaxSpan>& out) const {
    if (lang_ == SyntaxLanguage::None) { outNext = state; out.clear(); return; }
    cLike_.highlight(line, state, outNext, lang_, out);
}

void SyntaxHighlighter::highlight(std::string_view line, std::vector<SyntaxSpan>& out) const {
    if (lang_ == SyntaxLanguage::None) { out.clear(); return; }
    SyntaxState s; SyntaxState nxt;
    cLike_.highlight(line, s, nxt, lang_, out);
}
