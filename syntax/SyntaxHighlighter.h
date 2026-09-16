#pragma once
#include <string_view>
#include <vector>
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"
#include "syntax/CLikeHighlighter.h"

class SyntaxHighlighter {
public:
    void setLanguage(SyntaxLanguage lang) { lang_ = lang; }
    SyntaxLanguage language() const { return lang_; }
    std::vector<SyntaxSpan> highlight(std::string_view line) const;
    std::vector<SyntaxSpan> highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext) const;
    void highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, std::vector<SyntaxSpan>& out) const;
    void highlight(std::string_view line, std::vector<SyntaxSpan>& out) const;
private:
    SyntaxLanguage lang_ = SyntaxLanguage::None;
    CLikeHighlighter cLike_;
};
