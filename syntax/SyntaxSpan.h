#pragma once
#include <cstddef>
#include "syntax/SyntaxToken.h"

struct SyntaxSpan {
    size_t begin = 0;
    size_t end = 0;
    SyntaxToken token = SyntaxToken::Keyword;
};

struct SyntaxState {
    bool inBlockComment = false;
};
