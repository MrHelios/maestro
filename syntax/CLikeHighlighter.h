#pragma once
#include <string_view>
#include <vector>
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"

class CLikeHighlighter {
public:
    void highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, SyntaxLanguage lang, std::vector<SyntaxSpan>& out) const;
    std::vector<SyntaxSpan> highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, SyntaxLanguage lang) const;
};
