#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include "syntax/SyntaxToken.h"

struct SyntaxSpan {
    size_t begin = 0;
    size_t end = 0;
    SyntaxToken token = SyntaxToken::Keyword;
};

struct SyntaxState {
    // CLike / C/C++
    bool inBlockComment = false;
    bool inRawString = false;
    char rawDelim[16] = {};
    uint8_t rawDelimLen = 0;

    // Markdown
    bool inCodeBlock = false;
    char codeBlockDelimChar = 0;
    uint16_t codeBlockDelimLen = 0;

    // Rust
    std::uint32_t rustBlockCommentDepth = 0;
    bool rustInRawString = false;
    std::uint32_t rustRawHashCount = 0;

    std::string_view rawDelimView() const { return std::string_view(rawDelim, rawDelimLen); }
    
    void setRawDelim(std::string_view v) {
        rawDelimLen = static_cast<uint8_t>(v.size() > 16 ? 16 : v.size());
        if (rawDelimLen) std::memcpy(rawDelim, v.data(), rawDelimLen);
        if (rawDelimLen < 16) rawDelim[rawDelimLen] = '\0';
    }
    
    void clearRawDelim() { 
        rawDelimLen = 0; 
        rawDelim[0] = '\0'; 
    }

    void resetAll() noexcept {
        inBlockComment = false;
        inRawString = false;
        clearRawDelim();
        
        inCodeBlock = false;
        codeBlockDelimChar = 0;
        codeBlockDelimLen = 0;
        
        rustBlockCommentDepth = 0;
        rustInRawString = false;
        rustRawHashCount = 0;
    }

    bool operator==(const SyntaxState& o) const {
        if (inBlockComment != o.inBlockComment || 
            inRawString != o.inRawString || 
            rawDelimLen != o.rawDelimLen) {
            return false;
        }
        
        if (inCodeBlock != o.inCodeBlock || 
            codeBlockDelimChar != o.codeBlockDelimChar || 
            codeBlockDelimLen != o.codeBlockDelimLen) {
            return false;
        }

        if (rustBlockCommentDepth != o.rustBlockCommentDepth ||
            rustInRawString != o.rustInRawString ||
            rustRawHashCount != o.rustRawHashCount) {
            return false;
        }

        // Comparar delim contenido solo si len > 0
        if (rawDelimLen > 0) {
            if (std::memcmp(rawDelim, o.rawDelim, rawDelimLen) != 0) {
                return false;
            }
        }

        return true;
    }
    
    bool operator!=(const SyntaxState& o) const { return !(*this == o); }
};
