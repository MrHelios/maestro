#pragma once
#include <string>
#include <algorithm>
#include <cctype>

enum class SyntaxLanguage {
    None,
    C,
    Cpp
};

/**
 * @brief Deduce lenguaje por extensión.
 * @note Limitación conocida: todo ".h" se mapea a Cpp (nunca a C). Un header
 *       C real con solo keywords de C recibirá resaltado C++ (ej. "class",
 *       "template" coloreados como Keyword aunque no existan en C). Es una
 *       heurística pragmática para el editor; a futuro podría distinguirse
 *       por contenido o config por proyecto.
 */
inline SyntaxLanguage languageFromFilename(const std::string& filename) {
    if (filename.empty()) return SyntaxLanguage::None;
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return SyntaxLanguage::None;
    std::string ext = filename.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".c") return SyntaxLanguage::C;
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++" ||
        ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".h++" ||
        ext == ".h" || ext == ".ipp") return SyntaxLanguage::Cpp;
    return SyntaxLanguage::None;
}
