#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "syntax/SyntaxToken.h"

struct SyntaxSpan {
    size_t begin = 0;
    size_t end = 0;
    SyntaxToken token = SyntaxToken::Keyword;
};

struct SyntaxState {
    bool inBlockComment = false;
    bool inRawString = false;
    char rawDelim[16] = {};
    uint8_t rawDelimLen = 0;

    std::string_view rawDelimView() const { return std::string_view(rawDelim, rawDelimLen); }
    void setRawDelim(std::string_view v) {
        rawDelimLen = static_cast<uint8_t>(v.size() > 16 ? 16 : v.size());
        if (rawDelimLen) std::memcpy(rawDelim, v.data(), rawDelimLen);
        if (rawDelimLen < 16) rawDelim[rawDelimLen] = '\0';
    }
    void clearRawDelim() { rawDelimLen = 0; rawDelim[0] = '\0'; }
    bool operator==(const SyntaxState& o) const {
        if (inBlockComment != o.inBlockComment || inRawString != o.inRawString || rawDelimLen != o.rawDelimLen) return false;
        return std::memcmp(rawDelim, o.rawDelim, rawDelimLen) == 0;
    }
    bool operator!=(const SyntaxState& o) const { return !(*this==o); }
};
