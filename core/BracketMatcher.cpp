#include "core/BracketMatcher.h"
#include "syntax/SyntaxHighlighter.h"
#include "syntax/SyntaxLanguage.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
bool isOpen(char c) { return c == '(' || c == '[' || c == '{'; }
bool isClose(char c) { return c == ')' || c == ']' || c == '}'; }
bool isBracket(char c) { return isOpen(c) || isClose(c); }
char matchingClose(char o) {
    if (o == '(') return ')';
    if (o == '[') return ']';
    if (o == '{') return '}';
    return 0;
}
char matchingOpen(char c) {
    if (c == ')') return '(';
    if (c == ']') return '[';
    if (c == '}') return '{';
    return 0;
}

bool isIgnoredPos(const std::vector<std::vector<SyntaxSpan>>& spansPerLine, Position p) {
    if (p.line < 0 || p.line >= (int)spansPerLine.size()) return false;
    const auto& spans = spansPerLine[p.line];
    for (auto &sp : spans) {
        if (p.col >= (int)sp.begin && p.col < (int)sp.end) {
            if (sp.token == SyntaxToken::String || sp.token == SyntaxToken::Character || sp.token == SyntaxToken::Comment) {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::vector<SyntaxSpan>> buildSpans(const Document& doc, SyntaxLanguage lang) {
    std::vector<std::vector<SyntaxSpan>> out;
    out.resize(doc.lineCount());
    if (lang == SyntaxLanguage::None || doc.lineCount()==0) return out;
    SyntaxHighlighter hl;
    hl.setLanguage(lang);
    SyntaxState state;
    state.inBlockComment = false;
    state.inRawString = false;
    state.rawDelimLen = 0;
    for (int l = 0; l < doc.lineCount(); ++l) {
        std::vector<SyntaxSpan> spans;
        SyntaxState nxt;
        hl.highlight(doc.lineAt(l), state, nxt, spans);
        out[l] = std::move(spans);
        state = nxt;
    }
    return out;
}

std::optional<BracketPair> findEnclosingBracket(const Document& doc, Position pos, const std::vector<std::vector<SyntaxSpan>>& spansPerLine) {
    std::vector<std::pair<char, Position>> stack;
    for (int l = 0; l < doc.lineCount(); ++l) {
        if (l > pos.line) break;
        const std::string& ln = doc.lineAt(l);
        int limit = (l == pos.line) ? pos.col : (int)ln.size();
        for (int c = 0; c < limit; ++c) {
            char ch = ln[c];
            if (!isOpen(ch) && !isClose(ch)) continue;
            Position p{l,c};
            if (isIgnoredPos(spansPerLine, p)) continue;
            if (isOpen(ch)) {
                stack.emplace_back(ch, p);
            } else {
                if (stack.empty()) continue;
                char topChar = stack.back().first;
                if (matchingClose(topChar) == ch) {
                    stack.pop_back();
                } else {
                    return std::nullopt;
                }
            }
        }
        if (l == pos.line) break;
    }
    if (stack.empty()) return std::nullopt;
    auto [openChar, openPos] = stack.back();
    char needClose = matchingClose(openChar);
    std::vector<char> inner;
    for (int l = pos.line; l < doc.lineCount(); ++l) {
        const std::string& ln = doc.lineAt(l);
        int start = (l == pos.line) ? pos.col : 0;
        for (int c = start; c < (int)ln.size(); ++c) {
            Position p{l,c};
            if (isIgnoredPos(spansPerLine, p)) continue;
            char ch = ln[c];
            if (!isBracket(ch)) continue;
            if (isOpen(ch)) {
                inner.push_back(ch);
            } else {
                if (inner.empty()) {
                    if (ch == needClose) {
                        return BracketPair{openPos, p};
                    } else {
                        return std::nullopt;
                    }
                } else {
                    char top = inner.back();
                    if (matchingClose(top) == ch) inner.pop_back();
                    else return std::nullopt;
                }
            }
        }
    }
    return std::nullopt;
}
} // namespace

std::optional<BracketPair> findMatchingBracket(const Document& doc, Position pos, SyntaxLanguage lang, const std::vector<std::vector<SyntaxSpan>>& spansPerLine) {
    (void)lang;
    auto isIgnored = [&](Position p){ return isIgnoredPos(spansPerLine, p); };

    if (doc.lineCount() == 0) return std::nullopt;
    if (pos.line < 0 || pos.line >= doc.lineCount()) return std::nullopt;
    const std::string& line = doc.lineAt(pos.line);
    int col = pos.col;
    if (col < 0) return std::nullopt;
    if (col > (int)line.size()) col = (int)line.size();
    if (col < (int)line.size() && isIgnored(pos)) {
        return std::nullopt;
    }

    char bracket = 0;
    Position bracketPos = pos;
    bool bracketIsOpen = false;
    bool hasBracket = false;
    if (col < (int)line.size() && isBracket(line[col])) {
        Position p{pos.line, col};
        if (!isIgnored(p)) {
            bracket = line[col];
            bracketPos = p;
            hasBracket = true;
        }
    }
    if (!hasBracket && col - 1 >= 0 && col - 1 < (int)line.size() && isBracket(line[col - 1])) {
        Position p{pos.line, col - 1};
        if (!isIgnored(p)) {
            bracket = line[col - 1];
            bracketPos = p;
            hasBracket = true;
        }
    }
    if (!hasBracket) {
        return findEnclosingBracket(doc, pos, spansPerLine);
    }
    bracketIsOpen = isOpen(bracket);

    if (bracketIsOpen) {
        char needClose = matchingClose(bracket);
        std::vector<char> stack;
        for (int l = bracketPos.line; l < doc.lineCount(); ++l) {
            const std::string& ln = doc.lineAt(l);
            int startCol = (l == bracketPos.line) ? bracketPos.col + 1 : 0;
            for (int c = startCol; c < (int)ln.size(); ++c) {
                char ch = ln[c];
                if (!isBracket(ch)) continue;
                Position p{l,c};
                if (isIgnored(p)) continue;
                if (isOpen(ch)) {
                    stack.push_back(ch);
                } else {
                    if (stack.empty()) {
                        if (ch == needClose) {
                            return BracketPair{bracketPos, p};
                        } else {
                            return std::nullopt;
                        }
                    } else {
                        char top = stack.back();
                        char expected = matchingClose(top);
                        if (ch == expected) {
                            stack.pop_back();
                        } else {
                            return std::nullopt;
                        }
                    }
                }
            }
        }
        return std::nullopt;
    } else {
        char needOpen = matchingOpen(bracket);
        std::vector<char> stack;
        for (int l = bracketPos.line; l >= 0; --l) {
            const std::string& ln = doc.lineAt(l);
            int startCol;
            int endCol;
            if (l == bracketPos.line) {
                startCol = bracketPos.col - 1;
                endCol = -1;
            } else {
                startCol = (int)ln.size() - 1;
                endCol = -1;
            }
            for (int c = startCol; c > endCol; --c) {
                char ch = ln[c];
                if (!isBracket(ch)) continue;
                Position p{l,c};
                if (isIgnored(p)) continue;
                if (isClose(ch)) {
                    stack.push_back(ch);
                } else {
                    if (stack.empty()) {
                        if (ch == needOpen) {
                            return BracketPair{{l, c}, bracketPos};
                        } else {
                            return std::nullopt;
                        }
                    } else {
                        char top = stack.back();
                        char expected = matchingOpen(top);
                        if (ch == expected) {
                            stack.pop_back();
                        } else {
                            return std::nullopt;
                        }
                    }
                }
            }
        }
        return std::nullopt;
    }
}

std::optional<BracketPair> findMatchingBracket(const Document& doc, Position pos, SyntaxLanguage lang) {
    auto spansPerLine = buildSpans(doc, lang);
    return findMatchingBracket(doc, pos, lang, spansPerLine);
}

std::optional<BracketPair> findMatchingBracket(const Document& doc, Position pos, const std::string& filename) {
    SyntaxLanguage lang = languageFromFilename(filename);
    if (lang == SyntaxLanguage::None) lang = SyntaxLanguage::Cpp;
    return findMatchingBracket(doc, pos, lang);
}
