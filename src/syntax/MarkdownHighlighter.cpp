#include "syntax/MarkdownHighlighter.h"

namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isDelimiterChar(char c) {
    return c == '*' || c == '-' || c == '_';
}

size_t countDelimiterRun(std::string_view line, size_t start, char delim) {
    size_t count = 0;
    size_t i = start;
    while (i < line.size() && line[i] == delim) {
        ++count;
        ++i;
    }
    return count;
}

size_t findClosingParen(std::string_view line, size_t openPos, size_t end) {
    int depth = 1;
    for (size_t i = openPos + 1; i < end; ++i) {
        char c = line[i];
        if (c == '\\') {
            if (i + 1 < end) ++i;
            continue;
        }
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::string_view::npos;
}

size_t findEmphasisClose(std::string_view line, size_t start, size_t end,
                         char delim, size_t openLen) {
    for (size_t k = start; k < end; ) {
        if (line[k] != delim) {
            ++k;
            continue;
        }
        size_t run = countDelimiterRun(line, k, delim);
        if (k > start && run == openLen) return k;
        k += run;
    }
    return std::string_view::npos;
}

bool isValidURL(std::string_view s) {
    size_t pos = s.find("://");
    if (pos == std::string_view::npos || pos == 0) return false;
    for (size_t i = 0; i < pos; ++i) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'))
            return false;
    }
    if (pos + 3 >= s.size()) return false;
    for (size_t i = pos + 3; i < s.size(); ++i) {
        if (isSpace(s[i])) return false;
    }
    return true;
}

bool isValidEmail(std::string_view s) {
    size_t at = s.find('@');
    if (at == std::string_view::npos || at == 0 || at == s.size() - 1) return false;
    if (s.find('@', at + 1) != std::string_view::npos) return false;
    std::string_view domain = s.substr(at + 1);
    size_t dot = domain.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot == domain.size() - 1) return false;
    for (char c : s) if (isSpace(c)) return false;
    return true;
}

void parseInline(std::string_view line, size_t begin, size_t end,
                 std::vector<SyntaxSpan>& out, int depth) {
    size_t i = begin;
    while (i < end) {
        char c = line[i];

        if (c == '`') {
            size_t start = i;
            size_t openLen = countDelimiterRun(line, i, '`');
            size_t closePos = std::string_view::npos;
            for (size_t k = i + openLen; k < end; ) {
                if (line[k] == '`') {
                    size_t closeLen = countDelimiterRun(line, k, '`');
                    if (closeLen == openLen) {
                        closePos = k;
                        break;
                    }
                    k += closeLen;
                } else {
                    ++k;
                }
            }
            if (closePos != std::string_view::npos) {
                out.push_back({start, closePos + openLen, SyntaxToken::Code});
                i = closePos + openLen;
            } else {
                i = start + 1;
            }
            continue;
        }

        if (c == '*' || c == '_') {
            char delim = c;
            bool isBold = (i + 1 < end && line[i + 1] == delim);
            size_t openLen = isBold ? 2 : 1;
            size_t start = i;
            size_t contentStart = i + openLen;
            size_t closeStart = findEmphasisClose(line, contentStart, end, delim, openLen);
            if (closeStart != std::string_view::npos) {
                out.push_back({start, closeStart + openLen,
                               isBold ? SyntaxToken::Bold : SyntaxToken::Italic});
                if (depth < 64) {
                    parseInline(line, contentStart, closeStart, out, depth + 1);
                }
                i = closeStart + openLen;
            } else {
                i = contentStart;
            }
            continue;
        }

        if (c == '[') {
            size_t bracketStart = i;
            size_t bracketEnd = line.find(']', i + 1);
            if (bracketEnd != std::string_view::npos && bracketEnd < end) {
                size_t parenStart = bracketEnd + 1;
                if (parenStart < end && line[parenStart] == '(') {
                    size_t parenEnd = findClosingParen(line, parenStart, end);
                    if (parenEnd != std::string_view::npos) {
                        bool isImage = (bracketStart > begin && line[bracketStart - 1] == '!');
                        size_t tokenStart = isImage ? bracketStart - 1 : bracketStart;
                        out.push_back({tokenStart, parenEnd + 1,
                                       isImage ? SyntaxToken::Image : SyntaxToken::Link});
                        if (!isImage) {
                            out.push_back({parenStart + 1, parenEnd, SyntaxToken::LinkUrl});
                        }
                        i = parenEnd + 1;
                        continue;
                    }
                }
            }
        }

        if (c == '<') {
            size_t close = line.find('>', i + 1);
            if (close != std::string_view::npos && close < end) {
                std::string_view url = line.substr(i + 1, close - i - 1);
                if (isValidURL(url) || isValidEmail(url)) {
                    out.push_back({i, close + 1, SyntaxToken::Link});
                    out.push_back({i + 1, close, SyntaxToken::LinkUrl});
                    i = close + 1;
                    continue;
                }
            }
        }

        ++i;
    }
}

bool isOnlyWhitespace(std::string_view line, size_t start) {
    for (size_t i = start; i < line.size(); ++i) {
        if (!isSpace(line[i])) return false;
    }
    return true;
}

size_t leadingIndent(std::string_view line, size_t& i) {
    size_t indent = 0;
    while (i < line.size() && isSpace(line[i])) {
        if (line[i] == '\t') indent += 4 - (indent % 4);
        else ++indent;
        ++i;
    }
    return indent;
}

bool checkHorizontalRule(std::string_view line, size_t start, char delim) {
    size_t count = 0;
    size_t i = start;
    while (i < line.size()) {
        if (line[i] == delim) {
            ++count;
            ++i;
        } else if (isSpace(line[i])) {
            ++i;
        } else {
            break;
        }
    }
    return count >= 3 && isOnlyWhitespace(line, i);
}

}

void MarkdownHighlighter::highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, std::vector<SyntaxSpan>& out) const {
    out.clear();
    outNext = state;

    size_t n = line.size();
    if (n == 0) return;

    if (outNext.inCodeBlock) {
        char delimChar = outNext.codeBlockDelimChar;
        size_t delimLen = outNext.codeBlockDelimLen;

        size_t i = 0;
        size_t indent = leadingIndent(line, i);

        size_t closeLen = countDelimiterRun(line, i, delimChar);
        if (indent <= 3 && delimChar != 0 && closeLen >= delimLen &&
            isOnlyWhitespace(line, i + closeLen)) {
            out.push_back({0, n, SyntaxToken::CodeBlock});
            outNext.inCodeBlock = false;
            outNext.codeBlockDelimChar = 0;
            outNext.codeBlockDelimLen = 0;
            return;
        }
        out.push_back({0, n, SyntaxToken::CodeBlock});
        return;
    }

    size_t i = 0;
    size_t indent = leadingIndent(line, i);

    bool isHeading = false;
    size_t headingStart = 0;
    if (i < n && line[i] == '#') {
        headingStart = i;
        uint8_t level = 0;
        while (i < n && line[i] == '#' && level < 6) { ++i; ++level; }
        if (i == n || isSpace(line[i])) {
            isHeading = true;
        } else {
            i = headingStart;
        }
    }

    bool isHorizontalRule = false;
    if (i + 2 < n) {
        char c0 = line[i];
        if (isDelimiterChar(c0)) {
            if (checkHorizontalRule(line, i, c0)) {
                isHorizontalRule = true;
            }
        }
        if (!isHorizontalRule && indent <= 3 && (c0 == '~' || c0 == '`')) {
            char delimChar = c0;
            size_t delimLen = countDelimiterRun(line, i, delimChar);
            if (delimLen >= 3) {
                out.push_back({i, n, SyntaxToken::CodeBlock});
                outNext.inCodeBlock = true;
                outNext.codeBlockDelimChar = delimChar;
                outNext.codeBlockDelimLen = static_cast<uint16_t>(delimLen > 65535u ? 65535u : delimLen);
                return;
            }
        }
    }

    bool isBlockQuote = false;
    if (i < n && line[i] == '>') {
        isBlockQuote = true;
    }

    bool isListItem = false;
    size_t listItemEnd = 0;
    if (i < n && (line[i] == '-' || line[i] == '*' || line[i] == '+')) {
        size_t markerStart = i;
        ++i;
        if (i < n && isSpace(line[i])) {
            isListItem = true;
            listItemEnd = markerStart + 1;
        }
        i = markerStart;
    }

    bool isOrderedListItem = false;
    size_t orderedListItemEnd = 0;
    if (i < n && isDigit(line[i])) {
        size_t markerStart = i;
        size_t digitCount = 0;
        while (i < n && isDigit(line[i])) {
            ++i;
            ++digitCount;
        }
        if (digitCount <= 9 && i < n && (line[i] == '.' || line[i] == ')') && i + 1 < n && isSpace(line[i + 1])) {
            isOrderedListItem = true;
            orderedListItemEnd = i + 2;
        }
        i = markerStart;
    }

    if (isHeading) {
        out.push_back({headingStart, n, SyntaxToken::Heading});
    }
    if (isHorizontalRule) {
        out.push_back({i, n, SyntaxToken::HorizontalRule});
        return;
    }
    if (isBlockQuote) {
        out.push_back({i, n, SyntaxToken::BlockQuote});
    }
    if (isListItem) {
        out.push_back({i, listItemEnd, SyntaxToken::ListItem});
    }
    if (isOrderedListItem) {
        out.push_back({i, orderedListItemEnd, SyntaxToken::ListItem});
    }

    parseInline(line, 0, n, out, 0);
}

std::vector<SyntaxSpan> MarkdownHighlighter::highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext) const {
    std::vector<SyntaxSpan> tmp;
    highlight(line, state, outNext, tmp);
    return tmp;
}