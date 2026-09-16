#include "syntax/CLikeHighlighter.h"

namespace {

bool isIdentStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
bool isIdentCont(char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

bool isType(std::string_view tok) {
    return tok == "void" || tok == "bool" || tok == "char" || tok == "char8_t" || tok == "char16_t" || tok == "char32_t" || tok == "wchar_t" || tok == "short" || tok == "int" || tok == "long" || tok == "float" || tok == "double" || tok == "signed" || tok == "unsigned" || tok == "size_t" || tok == "ssize_t" || tok == "int8_t" || tok == "int16_t" || tok == "int32_t" || tok == "int64_t" || tok == "uint8_t" || tok == "uint16_t" || tok == "uint32_t" || tok == "uint64_t" || tok == "intptr_t" || tok == "uintptr_t" || tok == "ptrdiff_t" || tok == "auto";
}

bool isCKeyword(std::string_view tok) {
    return tok == "auto" || tok == "break" || tok == "case" || tok == "const" || tok == "continue" || tok == "default" || tok == "do" || tok == "else" || tok == "enum" || tok == "extern" || tok == "for" || tok == "goto" || tok == "if" || tok == "inline" || tok == "register" || tok == "restrict" || tok == "return" || tok == "sizeof" || tok == "static" || tok == "struct" || tok == "switch" || tok == "typedef" || tok == "union" || tok == "volatile" || tok == "while" || tok == "_Alignas" || tok == "_Alignof" || tok == "_Atomic" || tok == "_Bool" || tok == "_Complex" || tok == "_Generic" || tok == "_Imaginary" || tok == "_Noreturn" || tok == "_Static_assert" || tok == "_Thread_local" || tok == "_Pragma";
}

bool isCppKeyword(std::string_view tok) {
    return tok == "alignas" || tok == "alignof" || tok == "and" || tok == "and_eq" || tok == "asm" || tok == "bitand" || tok == "bitor" || tok == "break" || tok == "case" || tok == "catch" || tok == "class" || tok == "compl" || tok == "const" || tok == "constexpr" || tok == "const_cast" || tok == "continue" || tok == "co_await" || tok == "co_return" || tok == "co_yield" || tok == "decltype" || tok == "default" || tok == "delete" || tok == "do" || tok == "dynamic_cast" || tok == "else" || tok == "enum" || tok == "explicit" || tok == "export" || tok == "extern" || tok == "false" || tok == "for" || tok == "friend" || tok == "goto" || tok == "if" || tok == "inline" || tok == "mutable" || tok == "namespace" || tok == "new" || tok == "noexcept" || tok == "not" || tok == "not_eq" || tok == "nullptr" || tok == "operator" || tok == "or" || tok == "or_eq" || tok == "private" || tok == "protected" || tok == "public" || tok == "register" || tok == "reinterpret_cast" || tok == "requires" || tok == "return" || tok == "sizeof" || tok == "static" || tok == "static_assert" || tok == "static_cast" || tok == "struct" || tok == "switch" || tok == "template" || tok == "this" || tok == "thread_local" || tok == "throw" || tok == "true" || tok == "try" || tok == "typedef" || tok == "typeid" || tok == "typename" || tok == "union" || tok == "using" || tok == "virtual" || tok == "volatile" || tok == "while" || tok == "xor" || tok == "xor_eq" || tok == "override" || tok == "final";
}

bool isKeyword(std::string_view tok, SyntaxLanguage lang) {
    if (isType(tok)) return false;
    if (lang == SyntaxLanguage::C) return isCKeyword(tok);
    return isCKeyword(tok) || isCppKeyword(tok);
}

}

void CLikeHighlighter::highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, SyntaxLanguage lang, std::vector<SyntaxSpan>& out) const {
    out.clear();
    outNext = state;
    size_t n = line.size();
    size_t i = 0;
    if (outNext.inBlockComment) {
        size_t end = line.find("*/");
        if (end == std::string_view::npos) {
            out.push_back({0, n, SyntaxToken::Comment});
            outNext.inBlockComment = true;
            return;
        } else {
            out.push_back({0, end + 2, SyntaxToken::Comment});
            i = end + 2;
            outNext.inBlockComment = false;
        }
    }
    size_t firstNonSpace = line.find_first_not_of(" \t\r\f\v");
    if (firstNonSpace != std::string_view::npos && line[firstNonSpace] == '#') {
        if (out.empty()) {
            out.push_back({firstNonSpace, n, SyntaxToken::Preprocessor});
            return;
        } else {
            size_t ns = line.find_first_not_of(" \t", i);
            if (ns != std::string_view::npos && line[ns] == '#') {
                out.push_back({ns, n, SyntaxToken::Preprocessor});
                return;
            }
        }
    }
    for (; i < n; ) {
        char c = line[i];
        if (c == '"') {
            size_t start = i;
            ++i;
            while (i < n) {
                if (line[i] == '\\' && i + 1 < n) { i += 2; continue; }
                if (line[i] == '"') { ++i; break; }
                ++i;
            }
            out.push_back({start, i, SyntaxToken::String});
            continue;
        }
        if (c == '\'') {
            size_t start = i;
            ++i;
            while (i < n) {
                if (line[i] == '\\' && i + 1 < n) { i += 2; continue; }
                if (line[i] == '\'') { ++i; break; }
                ++i;
            }
            out.push_back({start, i, SyntaxToken::Character});
            continue;
        }
        if (c == '/' && i + 1 < n && line[i+1] == '/') {
            out.push_back({i, n, SyntaxToken::Comment});
            break;
        }
        if (c == '/' && i + 1 < n && line[i+1] == '*') {
            size_t start = i;
            size_t end = line.find("*/", i + 2);
            if (end == std::string_view::npos) {
                out.push_back({start, n, SyntaxToken::Comment});
                outNext.inBlockComment = true;
                break;
            } else {
                out.push_back({start, end + 2, SyntaxToken::Comment});
                i = end + 2;
                continue;
            }
        }
        if (isSpace(c)) { ++i; continue; }
        if (isIdentStart(c)) {
            size_t start = i;
            ++i;
            while (i < n && isIdentCont(line[i])) ++i;
            std::string_view tok = line.substr(start, i - start);
            if (isType(tok)) out.push_back({start, i, SyntaxToken::Type});
            else if (isKeyword(tok, lang)) out.push_back({start, i, SyntaxToken::Keyword});
            continue;
        }
        if (isDigit(c)) {
            size_t start = i;
            if (c == '0' && i + 1 < n && (line[i+1] == 'x' || line[i+1] == 'X')) {
                i += 2;
                while (i < n && isHexDigit(line[i])) ++i;
                while (i < n && isIdentCont(line[i])) ++i;
                out.push_back({start, i, SyntaxToken::Number});
                continue;
            }
            if (c == '0' && i + 1 < n && (line[i+1] == 'b' || line[i+1] == 'B')) {
                i += 2;
                while (i < n && (line[i] == '0' || line[i] == '1')) ++i;
                while (i < n && isIdentCont(line[i])) ++i;
                out.push_back({start, i, SyntaxToken::Number});
                continue;
            }
            bool hasDot = false;
            bool hasExp = false;
            ++i;
            while (i < n) {
                char d = line[i];
                if (isDigit(d)) { ++i; continue; }
                if (d == '.' && !hasDot && !hasExp) {
                    if (i + 1 < n && isDigit(line[i+1])) { hasDot = true; ++i; continue; }
                    if (i + 1 >= n || !isIdentCont(line[i+1])) { hasDot = true; ++i; continue; }
                    break;
                }
                if ((d == 'e' || d == 'E') && !hasExp) {
                    hasExp = true; ++i;
                    if (i < n && (line[i] == '+' || line[i] == '-')) ++i;
                    continue;
                }
                if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || d == '_' || d == '\'') {
                    ++i; continue;
                }
                break;
            }
            out.push_back({start, i, SyntaxToken::Number});
            continue;
        }
        ++i;
    }
}

std::vector<SyntaxSpan> CLikeHighlighter::highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, SyntaxLanguage lang) const {
    std::vector<SyntaxSpan> tmp;
    highlight(line, state, outNext, lang, tmp);
    return tmp;
}
