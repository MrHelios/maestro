#include "syntax/RustHighlighter.h"
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace {

bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }
bool isHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool isOctDigit(char c) { return c >= '0' && c <= '7'; }
bool isBinDigit(char c) { return c == '0' || c == '1'; }
bool isIdentStart(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return u == '_' || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u >= 0x80;
}
bool isIdentCont(char c) { return isIdentStart(c) || isAsciiDigit(c); }

bool isRustPrimitiveType(std::string_view tok) {
    return tok == "bool" || tok == "char" || tok == "str" ||
           tok == "i8" || tok == "i16" || tok == "i32" || tok == "i64" || tok == "i128" || tok == "isize" ||
           tok == "u8" || tok == "u16" || tok == "u32" || tok == "u64" || tok == "u128" || tok == "usize" ||
           tok == "f32" || tok == "f64";
}

bool isRustKeyword(std::string_view tok) {
    // Strict
    if (tok == "_" || tok == "as" || tok == "async" || tok == "await" || tok == "break" ||
        tok == "const" || tok == "continue" || tok == "crate" || tok == "dyn" || tok == "else" ||
        tok == "enum" || tok == "extern" || tok == "false" || tok == "fn" || tok == "for" ||
        tok == "if" || tok == "impl" || tok == "in" || tok == "let" || tok == "loop" ||
        tok == "match" || tok == "mod" || tok == "move" || tok == "mut" || tok == "pub" ||
        tok == "ref" || tok == "return" || tok == "self" || tok == "Self" || tok == "static" ||
        tok == "struct" || tok == "super" || tok == "trait" || tok == "true" || tok == "type" ||
        tok == "unsafe" || tok == "use" || tok == "where" || tok == "while") {
        return true;
    }
    // Reserved
    if (tok == "abstract" || tok == "become" || tok == "box" || tok == "do" || tok == "final" ||
        tok == "gen" || tok == "macro" || tok == "override" || tok == "priv" || tok == "try" ||
        tok == "typeof" || tok == "unsized" || tok == "virtual" || tok == "yield") {
        return true;
    }
    return false;
}

bool isNumericSuffix(std::string_view s) {
    return s == "i8" || s == "i16" || s == "i32" || s == "i64" || s == "i128" || s == "isize" ||
           s == "u8" || s == "u16" || s == "u32" || s == "u64" || s == "u128" || s == "usize" ||
           s == "f32" || s == "f64";
}

// Busca el cierre de un raw string. Retorna índice EXCLUSIVO (posición después del último #)
std::size_t findRawStringClose(std::string_view line, std::size_t start, std::uint32_t hashCount) {
    const std::size_t n = line.size();
    for (std::size_t p = start; p < n; ++p) {
        if (line[p] != '"') continue;
        
        std::size_t q = p + 1;
        std::uint32_t k = 0;
        while (k < hashCount && q < n && line[q] == '#') {
            ++k;
            ++q;
        }
        
        if (k == hashCount) {
            return q; // Exclusivo
        }
    }
    return std::string_view::npos;
}

bool tryRawStringOpen(std::string_view line, std::size_t i, std::size_t n, 
                      std::size_t& contentStart, std::uint32_t& hashCount) {
    std::size_t p = i;
    
    if (p + 1 < n && line[p] == 'b' && line[p+1] == 'r') {
        p += 2;
    } else if (p + 1 < n && line[p] == 'c' && line[p+1] == 'r') {
        p += 2;
    } else if (line[p] == 'r') {
        p += 1;
    } else {
        return false;
    }
    
    std::size_t hashStart = p;
    while (p < n && line[p] == '#') {
        ++p;
    }
    
    std::size_t hashSize = p - hashStart;
    if (hashSize > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    hashCount = static_cast<std::uint32_t>(hashSize);
    
    if (p >= n || line[p] != '"') {
        return false;
    }
    
    contentStart = p + 1;
    return true;
}

std::size_t parseSimpleStringFromQuote(std::string_view line, std::size_t quotePos) {
    const std::size_t n = line.size();
    if (quotePos >= n) return n;
    std::size_t i = quotePos + 1;
    while (i < n) {
        if (line[i] == '\\' && i + 1 < n) { i += 2; continue; }
        if (line[i] == '"') { ++i; break; }
        ++i;
    }
    return i;
}

std::size_t parseCharEscape(std::string_view line, std::size_t p) {
    const std::size_t n = line.size();
    if (p + 1 >= n) return std::string_view::npos;
    const char e = line[p + 1];
    switch (e) {
        case 'n': case 'r': case 't': case '0': case '\'': case '\\':
            return p + 2;
        case 'x': {
            std::size_t q = p + 2;
            std::size_t digits = 0;
            while (q < n && digits < 2 && isHexDigit(line[q])) { ++q; ++digits; }
            return digits == 0 ? std::string_view::npos : q;
        }
        case 'u': {
            std::size_t q = p + 2;
            if (q >= n || line[q] != '{') return std::string_view::npos;
            ++q;
            std::size_t digits = 0;
            while (q < n && digits < 6 && isHexDigit(line[q])) { ++q; ++digits; }
            if (digits == 0 || q >= n || line[q] != '}') return std::string_view::npos;
            return q + 1;
        }
        default:
            return std::string_view::npos;
    }
}

std::size_t consumeUtf8Char(std::string_view line, std::size_t p) {
    const std::size_t n = line.size();
    if (p >= n) return std::string_view::npos;
    const unsigned char lead = static_cast<unsigned char>(line[p]);
    std::size_t len = 1;
    if (lead < 0x80) len = 1;
    else if ((lead & 0xE0) == 0xC0) len = 2;
    else if ((lead & 0xF0) == 0xE0) len = 3;
    else if ((lead & 0xF8) == 0xF0) len = 4;
    else return std::string_view::npos;
    
    if (p + len > n) return std::string_view::npos;
    for (std::size_t k = 1; k < len; ++k) {
        const unsigned char cb = static_cast<unsigned char>(line[p + k]);
        if ((cb & 0xC0) != 0x80) return std::string_view::npos;
    }
    return p + len;
}

std::size_t tryParseCharLiteral(std::string_view line, std::size_t apostrophePos) {
    const std::size_t n = line.size();
    if (apostrophePos >= n) return std::string_view::npos;
    std::size_t p = apostrophePos + 1;
    if (p >= n) return std::string_view::npos;
    
    if (line[p] == '\\') {
        p = parseCharEscape(line, p);
        if (p == std::string_view::npos) return std::string_view::npos;
    } else {
        if (line[p] == '\'') return std::string_view::npos;
        p = consumeUtf8Char(line, p);
        if (p == std::string_view::npos) return std::string_view::npos;
    }
    
    if (p >= n || line[p] != '\'') return std::string_view::npos;
    return p + 1;
}

std::size_t tryParseLifetime(std::string_view line, std::size_t apostrophePos) {
    const std::size_t n = line.size();
    if (apostrophePos >= n) return std::string_view::npos;
    std::size_t p = apostrophePos + 1;
    if (p >= n) return std::string_view::npos;
    
    if (line[p] == '_' || isIdentStart(line[p])) {
        ++p;
        while (p < n && isIdentCont(line[p])) ++p;
        return p;
    }
    return std::string_view::npos;
}

std::size_t consumeDecimalDigits(std::string_view line, std::size_t p) {
    const std::size_t n = line.size();
    while (p < n) {
        if (isAsciiDigit(line[p])) ++p;
        else if (line[p] == '_' && p + 1 < n && isAsciiDigit(line[p + 1])) p += 2;
        else break;
    }
    return p;
}

std::size_t consumeHexDigits(std::string_view line, std::size_t p) {
    const std::size_t n = line.size();
    while (p < n) {
        if (isHexDigit(line[p])) ++p;
        else if (line[p] == '_' && p + 1 < n && isHexDigit(line[p + 1])) p += 2;
        else break;
    }
    return p;
}

std::size_t consumeOctDigits(std::string_view line, std::size_t p) {
    const std::size_t n = line.size();
    while (p < n) {
        if (isOctDigit(line[p])) ++p;
        else if (line[p] == '_' && p + 1 < n && isOctDigit(line[p + 1])) p += 2;
        else break;
    }
    return p;
}

std::size_t consumeBinDigits(std::string_view line, std::size_t p) {
    const std::size_t n = line.size();
    while (p < n) {
        if (isBinDigit(line[p])) ++p;
        else if (line[p] == '_' && p + 1 < n && isBinDigit(line[p + 1])) p += 2;
        else break;
    }
    return p;
}

std::size_t consumeKnownNumericSuffix(std::string_view line, std::size_t p) {
    const std::size_t n = line.size();
    if (p >= n || !isIdentStart(line[p])) return p;
    std::size_t q = p;
    while (q < n && isIdentCont(line[q])) ++q;
    if (isNumericSuffix(line.substr(p, q - p))) return q;
    return p;
}

std::size_t parseNumber(std::string_view line, std::size_t start) {
    const std::size_t n = line.size();
    if (start >= n) return start;
    
    if (line[start] == '0' && start + 1 < n) {
        const char prefix = line[start + 1];
        if (prefix == 'x' || prefix == 'X') {
            std::size_t dEnd = consumeHexDigits(line, start + 2);
            if (dEnd == start + 2) return start + 1;
            return consumeKnownNumericSuffix(line, dEnd);
        }
        if (prefix == 'o' || prefix == 'O') {
            std::size_t dEnd = consumeOctDigits(line, start + 2);
            if (dEnd == start + 2) return start + 1;
            return consumeKnownNumericSuffix(line, dEnd);
        }
        if (prefix == 'b' || prefix == 'B') {
            std::size_t dEnd = consumeBinDigits(line, start + 2);
            if (dEnd == start + 2) return start + 1;
            return consumeKnownNumericSuffix(line, dEnd);
        }
    }
    
    std::size_t p = consumeDecimalDigits(line, start);
    
    if (p < n && line[p] == '.' && p + 1 < n && isAsciiDigit(line[p + 1])) {
        ++p;
        p = consumeDecimalDigits(line, p);
    }
    
    if (p < n && (line[p] == 'e' || line[p] == 'E')) {
        std::size_t q = p + 1;
        if (q < n && (line[q] == '+' || line[q] == '-')) ++q;
        if (q < n && isAsciiDigit(line[q])) {
            p = consumeDecimalDigits(line, q);
        }
    }
    
    return consumeKnownNumericSuffix(line, p);
}

} // namespace

void RustHighlighter::highlight(std::string_view line,
                                const SyntaxState& state,
                                SyntaxState& outNext,
                                std::vector<SyntaxSpan>& out) const {
    out.clear();
    outNext = state;

    const std::size_t n = line.size();
    if (n == 0) return;

    std::size_t i = 0;

    // 1) Continuación de raw string multilínea
    if (outNext.rustInRawString) {
        const std::uint32_t hashes = outNext.rustRawHashCount;
        const std::size_t close = findRawStringClose(line, 0, hashes);

        if (close == std::string_view::npos) {
            out.push_back({0, n, SyntaxToken::String});
            return;
        }

        out.push_back({0, close, SyntaxToken::String});
        outNext.rustInRawString = false;
        outNext.rustRawHashCount = 0;
        i = close;
    }

    // 2) Continuación de comentario de bloque anidado
    if (outNext.rustBlockCommentDepth > 0) {
        const std::size_t start = i;
        std::uint32_t depth = outNext.rustBlockCommentDepth;
        std::size_t p = i;
        bool closed = false;

        while (p < n) {
            if (p + 1 < n && line[p] == '/' && line[p + 1] == '*') {
                ++depth;
                p += 2;
            } else if (p + 1 < n && line[p] == '*' && line[p + 1] == '/') {
                --depth;
                p += 2;
                if (depth == 0) {
                    closed = true;
                    break;
                }
            } else {
                ++p;
            }
        }

        if (!closed) {
            out.push_back({start, n, SyntaxToken::Comment});
            outNext.rustBlockCommentDepth = depth;
            return;
        }

        out.push_back({start, p, SyntaxToken::Comment});
        outNext.rustBlockCommentDepth = 0;
        i = p;
    }

    // 3) Análisis principal
    for (; i < n; ) {
        const char c = line[i];

        // --- RAW STRINGS (Prioridad máxima sobre idents/comentarios) ---
        if (c == 'r' || c == 'b' || c == 'c') {
            std::size_t contentStart = 0;
            std::uint32_t hashes = 0;

            if (tryRawStringOpen(line, i, n, contentStart, hashes)) {
                const std::size_t close = findRawStringClose(line, contentStart, hashes);

                if (close == std::string_view::npos) {
                    out.push_back({i, n, SyntaxToken::String});
                    outNext.rustInRawString = true;
                    outNext.rustRawHashCount = hashes;
                    return;
                }

                out.push_back({i, close, SyntaxToken::String});
                i = close;
                continue;
            }
            
            // Byte String normal: b"..."
            if (c == 'b' && i + 1 < n && line[i + 1] == '"') {
                const std::size_t end = parseSimpleStringFromQuote(line, i + 1);
                out.push_back({i, end, SyntaxToken::String});
                i = end;
                continue;
            }
            
            // Byte Char: b'...'
            if (c == 'b' && i + 1 < n && line[i + 1] == '\'') {
                const std::size_t charEnd = tryParseCharLiteral(line, i + 1);
                if (charEnd != std::string_view::npos) {
                    out.push_back({i, charEnd, SyntaxToken::Character});
                    i = charEnd;
                    continue;
                }
                out.push_back({i, n, SyntaxToken::Character});
                return;
            }
            
            // C String normal: c"..."
            if (c == 'c' && i + 1 < n && line[i + 1] == '"') {
                const std::size_t end = parseSimpleStringFromQuote(line, i + 1);
                out.push_back({i, end, SyntaxToken::String});
                i = end;
                continue;
            }
        }

        // --- STRING NORMAL ---
        if (c == '"') {
            const std::size_t end = parseSimpleStringFromQuote(line, i);
            out.push_back({i, end, SyntaxToken::String});
            i = end;
            continue;
        }

        // --- CHAR LITERAL / LIFETIME ---
        if (c == '\'') {
            const std::size_t charEnd = tryParseCharLiteral(line, i);
            if (charEnd != std::string_view::npos) {
                out.push_back({i, charEnd, SyntaxToken::Character});
                i = charEnd;
                continue;
            }

            const std::size_t lifetimeEnd = tryParseLifetime(line, i);
            if (lifetimeEnd != std::string_view::npos) {
                i = lifetimeEnd;
                continue;
            }

            ++i;
            continue;
        }

        // --- COMENTARIOS ---
        if (c == '/' && i + 1 < n) {
            if (line[i + 1] == '/') {
                out.push_back({i, n, SyntaxToken::Comment});
                return;
            }

            if (line[i + 1] == '*') {
                const std::size_t start = i;
                std::size_t p = i + 2;
                std::uint32_t depth = 1;
                bool closed = false;

                while (p < n) {
                    if (p + 1 < n && line[p] == '/' && line[p + 1] == '*') {
                        ++depth;
                        p += 2;
                    } else if (p + 1 < n && line[p] == '*' && line[p + 1] == '/') {
                        --depth;
                        p += 2;
                        if (depth == 0) {
                            closed = true;
                            break;
                        }
                    } else {
                        ++p;
                    }
                }

                if (!closed) {
                    out.push_back({start, n, SyntaxToken::Comment});
                    outNext.rustBlockCommentDepth = depth;
                    return;
                }

                out.push_back({start, p, SyntaxToken::Comment});
                i = p;
                continue;
            }
        }
        
        // --- RAW IDENTIFIER ---
        if (c == 'r' && i + 1 < n && line[i + 1] == '#' && i + 2 < n && isIdentStart(line[i + 2])) {
             std::size_t p = i + 2;
             while (p < n && isIdentCont(line[p])) ++p;
             i = p;
             continue;
        }

        // --- IDENTIFICADORES / KEYWORDS / TYPES ---
        if (isIdentStart(c)) {
            const std::size_t start = i;
            ++i;
            while (i < n && isIdentCont(line[i])) ++i;
            
            const std::string_view tok = line.substr(start, i - start);
            
            if (isRustPrimitiveType(tok)) {
                out.push_back({start, i, SyntaxToken::Type});
            } else if (isRustKeyword(tok)) {
                out.push_back({start, i, SyntaxToken::Keyword});
            }
            continue;
        }

        // --- NUMEROS ---
        if (isAsciiDigit(c)) {
            const std::size_t end = parseNumber(line, i);
            out.push_back({i, end, SyntaxToken::Number});
            i = end;
            continue;
        }

        // --- RESTO ---
        ++i;
    }
}

std::vector<SyntaxSpan> RustHighlighter::highlight(std::string_view line,
                                                   const SyntaxState& state,
                                                   SyntaxState& outNext) const {
    std::vector<SyntaxSpan> tmp;
    highlight(line, state, outNext, tmp);
    return tmp;
}
