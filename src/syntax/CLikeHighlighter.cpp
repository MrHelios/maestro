/**
 * @file CLikeHighlighter.cpp
 * @brief Autómata por línea para C/C++ sin heap en el caso común.
 *
 * Decisiones clave:
 * - Limitación pragmática: strings normales se consideran intra-línea y no se
 *   propaga estado de string entre líneas (un '"' sin cierre marca hasta EOL);
 *   solo raw strings R"delim(...)delim" y comentarios /\* ... *\/ cruzan y por
 *   eso viven en SyntaxState (inRawString/rawDelim inline [16], inBlockComment).
 * - Preprocesador consume hasta fin de línea y solo si es primer token
 *   no-espacio tras posibles /\*...*\/ líderes (ver fix Bug 1).
 * - Números: autómata con estados hex/bin/decimal + sufijos; valida que
 *   0x/0b tengan al menos 1 dígito (0xG -> solo '0' como Number).
 * - Comparaciones de keywords/tipos via string_view (sin std::string) para
 *   evitar allocations por identificador (bench 80x). La detección de raw
 *   strings usa almacenamiento inline (char[16]) y solo aloca si delim==16
 *   (caso raro); en el path común no hay heap.
 */
#include "syntax/CLikeHighlighter.h"
#include <cstring>

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

bool isBinaryDigit(char c) {
    return c == '0' || c == '1';
}

// Consume dígitos de una base, incluyendo separadores C++ como 1'000.
// El apóstrofo solo se consume si está entre dos dígitos válidos.
size_t consumeDigits(
    std::string_view line,
    size_t i,
    bool (*isValidDigit)(char)
) {
    const size_t n = line.size();

    while (i < n) {
        if (isValidDigit(line[i])) {
            ++i;
            continue;
        }

        if (line[i] == '\'' &&
            i > 0 &&
            i + 1 < n &&
            isValidDigit(line[i - 1]) &&
            isValidDigit(line[i + 1])) {
            ++i;
            continue;
        }

        break;
    }

    return i;
}

size_t consumeIntegerSuffix(std::string_view line, size_t i) {
    while (i < line.size()) {
        const char c = line[i];
        if (c != 'u' && c != 'U' &&
            c != 'l' && c != 'L' &&
            c != 'z' && c != 'Z') {
            break;
        }
        ++i;
    }
    return i;
}

size_t consumeFloatSuffix(std::string_view line, size_t i) {
    if (i < line.size() &&
        (line[i] == 'f' || line[i] == 'F' ||
         line[i] == 'l' || line[i] == 'L')) {
        ++i;
    }
    return i;
}

size_t scanNumberEnd(std::string_view line, size_t start) {
    const size_t n = line.size();

    // Literal decimal que comienza con punto: .5, .25f
    if (line[start] == '.') {
        size_t i = consumeDigits(line, start + 1, isDigit);
        return consumeFloatSuffix(line, i);
    }

    // Hexadecimal: enteros y flotantes como 0xFF, 0x1.fp+2.
    if (line[start] == '0' &&
        start + 1 < n &&
        (line[start + 1] == 'x' || line[start + 1] == 'X')) {

        size_t i = start + 2;
        const size_t integerStart = i;
        i = consumeDigits(line, i, isHexDigit);
        const bool hasIntegerDigits = i > integerStart;

        bool hasDot = false;
        bool hasFractionDigits = false;

        if (i < n && line[i] == '.' &&
            !(i + 1 < n && line[i + 1] == '.')) {
            hasDot = true;
            ++i;

            const size_t fractionStart = i;
            i = consumeDigits(line, i, isHexDigit);
            hasFractionDigits = i > fractionStart;
        }

        // Conserva el comportamiento existente: 0xG resalta solo el 0.
        if (!hasIntegerDigits && !hasFractionDigits) {
            return start + 1;
        }

        bool hasExponent = false;

        if (i < n && (line[i] == 'p' || line[i] == 'P')) {
            size_t exponentDigits = i + 1;

            if (exponentDigits < n &&
                (line[exponentDigits] == '+' ||
                 line[exponentDigits] == '-')) {
                ++exponentDigits;
            }

            // Un exponente solo se consume si tiene dígitos.
            if (exponentDigits < n && isDigit(line[exponentDigits])) {
                i = consumeDigits(line, exponentDigits, isDigit);
                hasExponent = true;
            }
        }

        if (hasDot || hasExponent) {
            return consumeFloatSuffix(line, i);
        }
        return consumeIntegerSuffix(line, i);
    }

    // Binario: 0b1010, 0b10'10U.
    if (line[start] == '0' &&
        start + 1 < n &&
        (line[start + 1] == 'b' || line[start + 1] == 'B')) {

        size_t i = start + 2;
        const size_t digitsStart = i;
        i = consumeDigits(line, i, isBinaryDigit);

        if (i == digitsStart) {
            return start + 1;
        }

        return consumeIntegerSuffix(line, i);
    }

    // Decimal: enteros, flotantes y exponentes.
    size_t i = consumeDigits(line, start, isDigit);
    bool hasDot = false;
    bool hasExponent = false;

    if (i < n && line[i] == '.' &&
        !(i + 1 < n && line[i + 1] == '.')) {
        hasDot = true;
        ++i;
        i = consumeDigits(line, i, isDigit);
    }

    if (i < n && (line[i] == 'e' || line[i] == 'E')) {
        size_t exponentDigits = i + 1;

        if (exponentDigits < n &&
            (line[exponentDigits] == '+' ||
             line[exponentDigits] == '-')) {
            ++exponentDigits;
        }

        if (exponentDigits < n && isDigit(line[exponentDigits])) {
            i = consumeDigits(line, exponentDigits, isDigit);
            hasExponent = true;
        }
    }

    if (hasDot || hasExponent) {
        return consumeFloatSuffix(line, i);
    }
    return consumeIntegerSuffix(line, i);
}

/**
 * @brief Tipos builtin; se chequean antes que keywords para no marcar "int" como Keyword.
 * @note "auto" se clasifica como Type incluso en C (donde es storage-class).
 *       Decisión pragmática: en C moderno rara vez se usa y se quiere el mismo
 *       color que un tipo; en C++11+ es placeholder type. Para purismo, mover
 *       "auto" a isCKeyword cuando lang==C.
 */
bool isType(std::string_view tok) {
    return tok == "void" || tok == "bool" || tok == "char" || tok == "char8_t" || tok == "char16_t" || tok == "char32_t" || tok == "wchar_t" || tok == "short" || tok == "int" || tok == "long" || tok == "float" || tok == "double" || tok == "signed" || tok == "unsigned" || tok == "size_t" || tok == "ssize_t" || tok == "int8_t" || tok == "int16_t" || tok == "int32_t" || tok == "int64_t" || tok == "uint8_t" || tok == "uint16_t" || tok == "uint32_t" || tok == "uint64_t" || tok == "intptr_t" || tok == "uintptr_t" || tok == "ptrdiff_t" || tok == "auto";
}

/** @brief Keywords de C; comparación directa string_view (0 alloc). */
bool isCKeyword(std::string_view tok) {
    return tok == "break" || tok == "case" || tok == "const" || tok == "continue" || tok == "default" || tok == "do" || tok == "else" || tok == "enum" || tok == "extern" || tok == "for" || tok == "goto" || tok == "if" || tok == "inline" || tok == "register" || tok == "restrict" || tok == "return" || tok == "sizeof" || tok == "static" || tok == "struct" || tok == "switch" || tok == "typedef" || tok == "union" || tok == "volatile" || tok == "while" || tok == "_Alignas" || tok == "_Alignof" || tok == "_Atomic" || tok == "_Bool" || tok == "_Complex" || tok == "_Generic" || tok == "_Imaginary" || tok == "_Noreturn" || tok == "_Static_assert" || tok == "_Thread_local" || tok == "_Pragma";
}

/** @brief Keywords de C++; idem sin heap. */
bool isCppKeyword(std::string_view tok) {
    return tok == "alignas" || tok == "alignof" || tok == "and" || tok == "and_eq" || tok == "asm" || tok == "bitand" || tok == "bitor" || tok == "break" || tok == "case" || tok == "catch" || tok == "class" || tok == "compl" || tok == "const" || tok == "constexpr" || tok == "const_cast" || tok == "continue" || tok == "co_await" || tok == "co_return" || tok == "co_yield" || tok == "decltype" || tok == "default" || tok == "delete" || tok == "do" || tok == "dynamic_cast" || tok == "else" || tok == "enum" || tok == "explicit" || tok == "export" || tok == "extern" || tok == "false" || tok == "for" || tok == "friend" || tok == "goto" || tok == "if" || tok == "inline" || tok == "mutable" || tok == "namespace" || tok == "new" || tok == "noexcept" || tok == "not" || tok == "not_eq" || tok == "nullptr" || tok == "operator" || tok == "or" || tok == "or_eq" || tok == "private" || tok == "protected" || tok == "public" || tok == "register" || tok == "reinterpret_cast" || tok == "requires" || tok == "return" || tok == "sizeof" || tok == "static" || tok == "static_assert" || tok == "static_cast" || tok == "struct" || tok == "switch" || tok == "template" || tok == "this" || tok == "thread_local" || tok == "throw" || tok == "true" || tok == "try" || tok == "typedef" || tok == "typeid" || tok == "typename" || tok == "union" || tok == "using" || tok == "virtual" || tok == "volatile" || tok == "while" || tok == "xor" || tok == "xor_eq" || tok == "override" || tok == "final";
}

bool isKeyword(std::string_view tok, SyntaxLanguage lang) {
    if (lang == SyntaxLanguage::C) return isCKeyword(tok);
    return isCKeyword(tok) || isCppKeyword(tok);
}

/**
 * @brief Intenta parsear un raw string en i (prefijos R/u8R/uR/UR/LR).
 * @return true si hay prefijo R" y '('; delim y rawEnd/closed se setean.
 *         closed==false significa que no cierra en esta línea y el caller
 *         debe marcar inRawString. Se usa bool explícito porque rawEnd==n
 *         es ambiguo: también vale cuando cierra justo en n (ej. R"(x)" de
 *         6 chars); sobrecargar rawEnd causaba re-búsqueda frágil.
 *         Implementación sin heap: delim se copia a buffer inline [16].
 */
bool tryParseRawString(std::string_view line, size_t i, size_t n, size_t& rawEnd, char* delimOut, uint8_t& delimLen, bool& closed) {
    size_t quotePos = std::string_view::npos;
    if (i + 1 < n && line[i] == 'R' && line[i + 1] == '"') quotePos = i + 1;
    else if (i + 3 < n && line[i] == 'u' && line[i + 1] == '8' && line[i + 2] == 'R' && line[i + 3] == '"') quotePos = i + 3;
    else if (i + 2 < n && ((line[i] == 'u' && line[i + 1] == 'R') || (line[i] == 'U' && line[i + 1] == 'R') || (line[i] == 'L' && line[i + 1] == 'R')) && line[i + 2] == '"') quotePos = i + 2;
    else return false;
    size_t parenPos = line.find('(', quotePos + 1);
    if (parenPos == std::string_view::npos) return false;
    std::string_view delimView = line.substr(quotePos + 1, parenPos - (quotePos + 1));
    if (delimView.size() > 16) return false;
    for (char d : delimView) {
        if (d == '(' || d == ')' || d == '\\' || d == ' ' || d == '\t' || d == '\r' || d == '\n' || d == '\f' || d == '\v') return false;
    }
    delimLen = static_cast<uint8_t>(delimView.size());
    if (delimLen) std::memcpy(delimOut, delimView.data(), delimLen);
    char closing[18];
    closing[0] = ')';
    if (delimLen) std::memcpy(closing + 1, delimView.data(), delimLen);
    closing[1 + delimLen] = '"';
    std::string_view closingView(closing, 1 + delimLen + 1);
    size_t closePos = line.find(closingView, parenPos + 1);
    if (closePos == std::string_view::npos) {
        rawEnd = n;
        closed = false;
        return true;
    }
    rawEnd = closePos + closingView.size();
    closed = true;
    return true;
}

}

/**
 * @brief Highlight por línea con propagación de estado.
 *
 * Flujo:
 * 1) Continuación de raw string / block comment (estado de entrada).
 * 2) Preprocesador: primer token tras /\*...*\/ líderes y espacios.
 * 3) Autómata principal: strings normales, raw strings, comentarios,
 *    identificadores (tipo vs keyword), números.
 */
void CLikeHighlighter::highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, SyntaxLanguage lang, std::vector<SyntaxSpan>& out) const {
    out.clear();
    outNext = state;
    size_t n = line.size();
    size_t i = 0;
    // 1) Continuación de raw string cruzando líneas: solo en Cpp.
    if (lang == SyntaxLanguage::Cpp && outNext.inRawString) {
        std::string_view delim(outNext.rawDelim, outNext.rawDelimLen);
        char closing[18];
        closing[0] = ')';
        if (delim.size()) std::memcpy(closing + 1, delim.data(), delim.size());
        closing[1 + delim.size()] = '"';
        std::string_view closingView(closing, 1 + delim.size() + 1);
        size_t end = line.find(closingView);
        if (end == std::string_view::npos) {
            if (n>0)
                out.push_back({0, n, SyntaxToken::String});
            return;
        } else {
            out.push_back({0, end + closingView.size(), SyntaxToken::String});
            i = end + closingView.size();
            outNext.inRawString = false;
            outNext.clearRawDelim();
        }
    }
    // 1b) Continuación de /\* ... *\/.
    if (outNext.inBlockComment) {
        size_t end = line.find("*/");
        if (end == std::string_view::npos) {
            if (n>0)
                out.push_back({0, n, SyntaxToken::Comment});
            outNext.inBlockComment = true;
            return;
        } else {
            out.push_back({0, end + 2, SyntaxToken::Comment});
            i = end + 2;
            outNext.inBlockComment = false;
        }
    }
    // 2) Preprocesador: consume hasta fin de línea. Se permite que líderes
    // /*...*/ en la misma línea precedan al '#', por eso el loop.
    size_t pos = i;
    while (true) {
        size_t ns = line.find_first_not_of(" \t\r\f\v", pos);
        if (ns == std::string_view::npos) break;
        if (ns + 1 < n && line[ns] == '/' && line[ns + 1] == '*') {
            size_t end = line.find("*/", ns + 2);
            if (end == std::string_view::npos) {
                out.push_back({ns, n, SyntaxToken::Comment});
                outNext.inBlockComment = true;
                return;
            }
            out.push_back({ns, end + 2, SyntaxToken::Comment});
            pos = end + 2;
            continue;
        }
        if (line[ns] == '#') {
            out.push_back({ns, n, SyntaxToken::Preprocessor});
            return;
        }
        break;
    }
    i = pos;
    for (; i < n; ) {
        char c = line[i];
        // Raw string tiene prioridad sobre '"' normal y sobre 'R' como ident.
        // Guard barato evita construir delimitador y llamar a tryParse por cada
        // espacio/operador; solo R/u/U/L pueden iniciar R" / u8R" / uR" / UR" / LR".
        // Solo en Cpp: en C no existe y debe tratarse como código normal.
        if (lang == SyntaxLanguage::Cpp && (c == 'R' || c == 'u' || c == 'U' || c == 'L')) {
            size_t rawEnd = 0;
            char delimBuf[16] = {};
            uint8_t delimLen = 0;
            bool closed = false;
            if (tryParseRawString(line, i, n, rawEnd, delimBuf, delimLen, closed)) {
                if (!closed) {
                    out.push_back({i, n, SyntaxToken::String});
                    outNext.inRawString = true;
                    outNext.rawDelimLen = delimLen;
                    if (delimLen) std::memcpy(outNext.rawDelim, delimBuf, delimLen);
                    if (delimLen < 16) outNext.rawDelim[delimLen] = '\0';
                    return;
                }
                out.push_back({i, rawEnd, SyntaxToken::String});
                i = rawEnd;
                continue;
            }
        }
        
        // Literales con prefijo: u8"...", u"...", U"...", L"..."
        // y sus equivalentes de carácter.
        size_t quotePos = std::string_view::npos;
        if (c == 'u' && i + 2 < n && line[i + 1] == '8' &&
            (line[i + 2] == '"' || line[i + 2] == '\'')) {
            quotePos = i + 2;
        } else if ((c == 'u' || c == 'U' || c == 'L') &&
                   i + 1 < n &&
                   (line[i + 1] == '"' || line[i + 1] == '\'')) {
            quotePos = i + 1;
        }
        
        if (quotePos != std::string_view::npos) {
            const size_t start = i;
            const char quote = line[quotePos];
            i = quotePos + 1;
        
            while (i < n) {
                if (line[i] == '\\' && i + 1 < n) {
                    i += 2;
                    continue;
                }
        
                if (line[i] == quote) {
                    ++i;
                    break;
                }
        
                ++i;
            }
        
            out.push_back({
                start,
                i,
                quote == '"' ? SyntaxToken::String : SyntaxToken::Character
            });
            continue;
        }
        // Limitación pragmática: strings normales intra-línea; un '"' sin cierre marca hasta EOL.
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
        // Autómata de números:
        // - 0x + [hexDigits>=1] + sufijo ident (u/l) -> si 0 dígitos, solo '0'
        // - 0b + [binDigits>=1] + sufijo -> idem
        // - decimal: dígitos, punto opcional, exponente e/E, sufijos ' y ident
        if (isDigit(c) ||
            (c == '.' && i + 1 < n && isDigit(line[i + 1]))) {
            const size_t start = i;
            i = scanNumberEnd(line, start);
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
