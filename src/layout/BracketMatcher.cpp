#include "layout/BracketMatcher.h"
#include "syntax/SyntaxHighlighter.h"
#include "syntax/SyntaxLanguage.h"
#include <algorithm>
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

bool isIgnoredInSpans(const std::vector<SyntaxSpan>& spans, int col) {
    for (const auto& sp : spans) {
        if (col >= (int)sp.begin && col < (int)sp.end) {
            if (sp.token == SyntaxToken::String ||
                sp.token == SyntaxToken::Character ||
                sp.token == SyntaxToken::Comment) {
                return true;
            }
        }
    }
    return false;
}

void ensureLine(const BracketSpanSource& src, int line) {
    if (src.ensure) src.ensure(line);
}

const std::vector<SyntaxSpan>& lineSpans(const BracketSpanSource& src, int line) {
    static const std::vector<SyntaxSpan> empty;
    if (!src.spans) return empty;
    return src.spans(line);
}

// Fuente temporal para overloads sin cache externo.
struct TemporarySyntaxSource {
    const Document* doc_ = nullptr;
    SyntaxLanguage lang_ = SyntaxLanguage::None;
    SyntaxHighlighter hl_;
    SyntaxState state_;
    std::vector<std::vector<SyntaxSpan>> spans_;
    int parsed_ = 0;

    TemporarySyntaxSource(const Document& doc, SyntaxLanguage lang)
        : doc_(&doc), lang_(lang) {
        hl_.setLanguage(lang_);
        state_.inBlockComment = false;
        state_.inRawString = false;
        state_.rawDelimLen = 0;
    }

    void ensure(int line) {
        if (!doc_ || line < 0) return;
        int target = std::min(line + 1, doc_->lineCount());
        while (parsed_ < target) {
            spans_.emplace_back();
            if (lang_ != SyntaxLanguage::None) {
                SyntaxState nxt;
                hl_.highlight(doc_->lineAt(parsed_), state_, nxt, spans_.back());
                state_ = nxt;
            }
            ++parsed_;
        }
    }

    const std::vector<SyntaxSpan>& spansAt(int line) const {
        static const std::vector<SyntaxSpan> empty;
        if (line < 0 || line >= (int)spans_.size()) return empty;
        return spans_[line];
    }

    BracketSpanSource source() {
        BracketSpanSource s;
        s.ensure = [this](int line) { ensure(line); };
        s.spans = [this](int line) -> const std::vector<SyntaxSpan>& {
            return spansAt(line);
        };
        return s;
    }
};

std::optional<BracketPair> scanForwardOpen(
    const Document& doc,
    Position openPos,
    char openChar,
    BracketSpanSource& src,
    int lastLineExclusive
) {
    const int lineCount = doc.lineCount();
    if (openPos.line < 0 || openPos.line >= lineCount) return std::nullopt;

    const int last = std::min(lastLineExclusive, lineCount);
    if (openPos.line >= last) return std::nullopt;

    const char needClose = matchingClose(openChar);
    std::vector<char> stack;

    for (int l = openPos.line; l < last; ++l) {
        ensureLine(src, l);
        const auto& spans = lineSpans(src, l);
        const std::string& ln = doc.lineAt(l);

        int startCol = (l == openPos.line) ? openPos.col + 1 : 0;
        for (int c = startCol; c < (int)ln.size(); ++c) {
            char ch = ln[c];
            if (!isBracket(ch)) continue;
            if (isIgnoredInSpans(spans, c)) continue;

            if (isOpen(ch)) {
                stack.push_back(ch);
            } else {
                if (stack.empty()) {
                    if (ch == needClose) {
                        return BracketPair{openPos, {l, c}};
                    }
                    return std::nullopt;
                }

                char top = stack.back();
                if (matchingClose(top) == ch) {
                    stack.pop_back();
                } else {
                    return std::nullopt;
                }
            }
        }
    }

    return std::nullopt;
}

std::optional<BracketPair> scanBackwardClose(
    const Document& doc,
    Position closePos,
    char closeChar,
    BracketSpanSource& src,
    int firstLine
) {
    const int lineCount = doc.lineCount();
    if (closePos.line < 0 || closePos.line >= lineCount) return std::nullopt;

    const int first = std::max(0, firstLine);
    if (closePos.line < first) return std::nullopt;

    const char needOpen = matchingOpen(closeChar);
    std::vector<char> stack;

    for (int l = closePos.line; l >= first; --l) {
        ensureLine(src, l);
        const auto& spans = lineSpans(src, l);
        const std::string& ln = doc.lineAt(l);

        int startCol = (l == closePos.line) ? closePos.col - 1 : (int)ln.size() - 1;
        for (int c = startCol; c >= 0; --c) {
            char ch = ln[c];
            if (!isBracket(ch)) continue;
            if (isIgnoredInSpans(spans, c)) continue;

            if (isClose(ch)) {
                stack.push_back(ch);
            } else {
                if (stack.empty()) {
                    if (ch == needOpen) {
                        return BracketPair{{l, c}, closePos};
                    }
                    return std::nullopt;
                }

                char top = stack.back();
                if (matchingOpen(top) == ch) {
                    stack.pop_back();
                } else {
                    return std::nullopt;
                }
            }
        }
    }

    return std::nullopt;
}

std::optional<BracketPair> findEnclosingBackwardThenClose(
    const Document& doc,
    Position pos,
    BracketSpanSource& src,
    int firstLine,
    int lastLineExclusive
) {
    const int lineCount = doc.lineCount();
    if (pos.line < 0 || pos.line >= lineCount) return std::nullopt;

    const int first = std::max(0, firstLine);
    const int last = std::min(lastLineExclusive, lineCount);
    if (pos.line < first || pos.line >= last) return std::nullopt;

    std::vector<char> pendingCloses;
    std::optional<Position> openPos;
    char openChar = 0;

    for (int l = pos.line; l >= first; --l) {
        ensureLine(src, l);
        const auto& spans = lineSpans(src, l);
        const std::string& ln = doc.lineAt(l);

        int startCol = (l == pos.line) ? pos.col - 1 : (int)ln.size() - 1;
        for (int c = startCol; c >= 0; --c) {
            char ch = ln[c];
            if (!isBracket(ch)) continue;
            if (isIgnoredInSpans(spans, c)) continue;

            if (isClose(ch)) {
                pendingCloses.push_back(ch);
            } else {
                if (pendingCloses.empty()) {
                    openPos = Position{l, c};
                    openChar = ch;
                    break;
                }

                if (matchingClose(ch) == pendingCloses.back()) {
                    pendingCloses.pop_back();
                } else {
                    return std::nullopt;
                }
            }
        }

        if (openPos) break;
    }

    if (!openPos) return std::nullopt;

    const char needClose = matchingClose(openChar);
    std::vector<char> inner;

    for (int l = pos.line; l < last; ++l) {
        ensureLine(src, l);
        const auto& spans = lineSpans(src, l);
        const std::string& ln = doc.lineAt(l);

        int startCol = (l == pos.line) ? pos.col : 0;
        for (int c = startCol; c < (int)ln.size(); ++c) {
            char ch = ln[c];
            if (!isBracket(ch)) continue;
            if (isIgnoredInSpans(spans, c)) continue;

            if (isOpen(ch)) {
                inner.push_back(ch);
            } else {
                if (inner.empty()) {
                    if (ch == needClose) {
                        return BracketPair{*openPos, {l, c}};
                    }
                    return std::nullopt;
                }

                char top = inner.back();
                if (matchingClose(top) == ch) {
                    inner.pop_back();
                } else {
                    return std::nullopt;
                }
            }
        }
    }

    return std::nullopt;
}

std::optional<BracketPair> findEnclosingForwardFull(
    const Document& doc,
    Position pos,
    BracketSpanSource& src
) {
    const int lineCount = doc.lineCount();
    if (pos.line < 0 || pos.line >= lineCount) return std::nullopt;

    std::vector<std::pair<char, Position>> stack;

    for (int l = 0; l <= pos.line; ++l) {
        ensureLine(src, l);
        const auto& spans = lineSpans(src, l);
        const std::string& ln = doc.lineAt(l);

        int limit = (l == pos.line) ? pos.col : (int)ln.size();
        for (int c = 0; c < limit; ++c) {
            char ch = ln[c];
            if (!isBracket(ch)) continue;
            if (isIgnoredInSpans(spans, c)) continue;

            if (isOpen(ch)) {
                stack.emplace_back(ch, Position{l, c});
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
    }

    if (stack.empty()) return std::nullopt;

    auto [openChar, openPos] = stack.back();
    const char needClose = matchingClose(openChar);
    std::vector<char> inner;

    for (int l = pos.line; l < lineCount; ++l) {
        ensureLine(src, l);
        const auto& spans = lineSpans(src, l);
        const std::string& ln = doc.lineAt(l);

        int startCol = (l == pos.line) ? pos.col : 0;
        for (int c = startCol; c < (int)ln.size(); ++c) {
            char ch = ln[c];
            if (!isBracket(ch)) continue;
            if (isIgnoredInSpans(spans, c)) continue;

            if (isOpen(ch)) {
                inner.push_back(ch);
            } else {
                if (inner.empty()) {
                    if (ch == needClose) {
                        return BracketPair{openPos, {l, c}};
                    }
                    return std::nullopt;
                }

                char top = inner.back();
                if (matchingClose(top) == ch) {
                    inner.pop_back();
                } else {
                    return std::nullopt;
                }
            }
        }
    }

    return std::nullopt;
}

enum class EnclosingStrategy {
    ForwardFull,
    BackwardFromCursor
};

std::optional<BracketPair> findMatchingBracketGeneric(
    const Document& doc,
    Position pos,
    BracketSpanSource& src,
    int firstLine,
    int lastLineExclusive,
    EnclosingStrategy strategy
) {
    const int lineCount = doc.lineCount();
    if (lineCount == 0) return std::nullopt;
    if (pos.line < 0 || pos.line >= lineCount) return std::nullopt;

    const int first = std::max(0, firstLine);
    const int last = std::min(lastLineExclusive, lineCount);
    if (first > last) return std::nullopt;

    if (strategy == EnclosingStrategy::BackwardFromCursor) {
        if (pos.line < first || pos.line >= last) return std::nullopt;
    }

    ensureLine(src, pos.line);
    const auto& posSpans = lineSpans(src, pos.line);

    const std::string& line = doc.lineAt(pos.line);
    int col = pos.col;
    if (col < 0) return std::nullopt;
    if (col > (int)line.size()) col = (int)line.size();

    if (col < (int)line.size() && isIgnoredInSpans(posSpans, col)) {
        return std::nullopt;
    }

    char bracket = 0;
    Position bracketPos = pos;
    bool hasBracket = false;

    if (col < (int)line.size() && isBracket(line[col])) {
        if (!isIgnoredInSpans(posSpans, col)) {
            bracket = line[col];
            bracketPos = {pos.line, col};
            hasBracket = true;
        }
    }

    if (!hasBracket && col - 1 >= 0 && col - 1 < (int)line.size() && isBracket(line[col - 1])) {
        const int c = col - 1;
        if (!isIgnoredInSpans(posSpans, c)) {
            bracket = line[col - 1];
            bracketPos = {pos.line, c};
            hasBracket = true;
        }
    }

    if (!hasBracket) {
        if (strategy == EnclosingStrategy::ForwardFull) {
            return findEnclosingForwardFull(doc, pos, src);
        }
        return findEnclosingBackwardThenClose(doc, pos, src, first, last);
    }

    if (isOpen(bracket)) {
        return scanForwardOpen(doc, bracketPos, bracket, src, last);
    }

    return scanBackwardClose(doc, bracketPos, bracket, src, first);
}

} // namespace

std::optional<BracketPair> findMatchingBracketBounded(
    const Document& doc,
    Position pos,
    SyntaxLanguage lang,
    BracketSpanSource& src,
    int firstLine,
    int lastLineExclusive
) {
    (void)lang;
    return findMatchingBracketGeneric(
        doc,
        pos,
        src,
        firstLine,
        lastLineExclusive,
        EnclosingStrategy::BackwardFromCursor
    );
}

std::optional<BracketPair> findMatchingBracketFrom(
    const Document& doc,
    Position pos,
    SyntaxLanguage lang,
    BracketSpanSource& src
) {
    (void)lang;
    return findMatchingBracketGeneric(
        doc,
        pos,
        src,
        0,
        doc.lineCount(),
        EnclosingStrategy::BackwardFromCursor
    );
}

std::optional<Position> findMatchingOpenBackward(
    const Document& doc,
    Position pos,
    SyntaxLanguage lang,
    BracketSpanSource& src,
    int firstLine
) {
    (void)lang;
    const int lineCount = doc.lineCount();
    if (pos.line < 0 || pos.line >= lineCount) return std::nullopt;
    const int first = std::max(0, firstLine);

    ensureLine(src, pos.line);
    const auto& posSpans = lineSpans(src, pos.line);
    const std::string& line = doc.lineAt(pos.line);
    int col = std::clamp(pos.col, 0, (int)line.size());

    char bracket = 0;
    Position bracketPos = pos;
    bool hasBracket = false;

    auto checkClose = [&](int c) {
        if (c >= 0 && c < (int)line.size() && isClose(line[c]) && !isIgnoredInSpans(posSpans, c)) {
            bracket = line[c];
            bracketPos = {pos.line, c};
            hasBracket = true;
        }
    };
    if (col < (int)line.size()) checkClose(col);
    if (!hasBracket) checkClose(col - 1);

    if (hasBracket) {
        auto pair = scanBackwardClose(doc, bracketPos, bracket, src, first);
        return pair ? std::optional<Position>(pair->open) : std::nullopt;
    }

    std::vector<char> pendingCloses;
    for (int l = pos.line; l >= first; --l) {
        ensureLine(src, l);
        const auto& spans = lineSpans(src, l);
        const std::string& ln = doc.lineAt(l);
        int startCol = (l == pos.line) ? col - 1 : (int)ln.size() - 1;
        for (int c = startCol; c >= 0; --c) {
            char ch = ln[c];
            if (!isBracket(ch) || isIgnoredInSpans(spans, c)) continue;
            if (isClose(ch)) {
                pendingCloses.push_back(ch);
            } else {
                if (pendingCloses.empty()) {
                    return Position{l, c};
                }
                if (matchingClose(ch) == pendingCloses.back()) {
                    pendingCloses.pop_back();
                } else {
                    return std::nullopt;
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<Position> findMatchingCloseForward(
    const Document& doc,
    Position pos,
    SyntaxLanguage lang,
    BracketSpanSource& src,
    int lastLineExclusive
) {
    (void)lang;
    const int lineCount = doc.lineCount();
    if (pos.line < 0 || pos.line >= lineCount) return std::nullopt;
    const int last = std::min(lastLineExclusive, lineCount);

    ensureLine(src, pos.line);
    const auto& posSpans = lineSpans(src, pos.line);
    const std::string& line = doc.lineAt(pos.line);
    int col = std::clamp(pos.col, 0, (int)line.size());

    char bracket = 0;
    Position bracketPos = pos;
    bool hasBracket = false;

    auto checkOpen = [&](int c) {
        if (c >= 0 && c < (int)line.size() && isOpen(line[c]) && !isIgnoredInSpans(posSpans, c)) {
            bracket = line[c];
            bracketPos = {pos.line, c};
            hasBracket = true;
        }
    };
    if (col < (int)line.size()) checkOpen(col);
    if (!hasBracket && col > 0) checkOpen(col - 1);

    if (hasBracket) {
        auto pair = scanForwardOpen(doc, bracketPos, bracket, src, last);
        return pair ? std::optional<Position>(pair->close) : std::nullopt;
    }

    auto openPos = findMatchingOpenBackward(doc, pos, lang, src, 0);
    if (!openPos) return std::nullopt;

    ensureLine(src, openPos->line);
    const std::string& openLine = doc.lineAt(openPos->line);
    char openChar = openLine[openPos->col];
    auto pair = scanForwardOpen(doc, *openPos, openChar, src, last);
    return pair ? std::optional<Position>(pair->close) : std::nullopt;
}

std::optional<BracketPair> findMatchingBracket(
    const Document& doc,
    Position pos,
    SyntaxLanguage lang,
    const std::vector<std::vector<SyntaxSpan>>& spansPerLine
) {
    (void)lang;

    BracketSpanSource src;
    src.ensure = [](int) {};
    src.spans = [&spansPerLine](int line) -> const std::vector<SyntaxSpan>& {
        static const std::vector<SyntaxSpan> empty;
        if (line < 0 || line >= (int)spansPerLine.size()) return empty;
        return spansPerLine[line];
    };

    return findMatchingBracketGeneric(
        doc,
        pos,
        src,
        0,
        doc.lineCount(),
        EnclosingStrategy::ForwardFull
    );
}

std::optional<BracketPair> findMatchingBracket(
    const Document& doc,
    Position pos,
    SyntaxLanguage lang
) {
    TemporarySyntaxSource tmp(doc, lang);
    BracketSpanSource src = tmp.source();

    return findMatchingBracketGeneric(
        doc,
        pos,
        src,
        0,
        doc.lineCount(),
        EnclosingStrategy::ForwardFull
    );
}

std::optional<BracketPair> findMatchingBracket(
    const Document& doc,
    Position pos,
    const std::string& filename
) {
    SyntaxLanguage lang = languageFromFilename(filename);
    if (lang == SyntaxLanguage::None) lang = SyntaxLanguage::Cpp;
    return findMatchingBracket(doc, pos, lang);
}