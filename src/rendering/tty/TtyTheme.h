#pragma once

#include <string>

#include "rendering/Style.h"
#include "rendering/Theme.h"

struct TtyTheme {
    int id;
    std::string background;
    std::string foreground;
    std::string lineNumber;
    std::string gutterCurrent;
    std::string marker;
    std::string currentLine;
    std::string selection;
    std::string listSelected;
    std::string statusBar;
    std::string statusBarName;
    std::string statusBarPath;
    std::string statusBarAccent;
    std::string statusBarModified;
    std::string message;
    std::string prompt;
    std::string success;
    std::string warning;
    std::string error;
    std::string reset;

    std::string accentNavegacion;
    std::string accentInteraccion;
    std::string accentSeleccion;
    std::string accentComando;
    std::string accentBuffers;
    std::string accentGuardar;
    std::string accentAbrir;
    std::string syntaxKeyword;
    std::string syntaxType;
    std::string syntaxPreprocessor;
    std::string syntaxString;
    std::string syntaxCharacter;
    std::string syntaxNumber;
    std::string syntaxComment;
    std::string bracketMatch;
};

// Política de representación TTY: truecolor (38;2 / 48;2) desde el RGB
// compartido. No se mantiene un índice ANSI-256 paralelo: una sola fuente de
// verdad y paridad exacta con la GUI. Requiere terminal con truecolor.
namespace ttytheme_detail {

inline std::string fg(const ThemeColor& c) {
    return "\x1b[38;2;" + std::to_string(c.r) + ";" + std::to_string(c.g) +
           ";" + std::to_string(c.b) + "m";
}

inline std::string bg(const ThemeColor& c) {
    return "\x1b[48;2;" + std::to_string(c.r) + ";" + std::to_string(c.g) +
           ";" + std::to_string(c.b) + "m";
}

inline std::string fgFor(const std::optional<ThemeColor>& c) {
    return c ? fg(*c) : std::string();
}

inline std::string bgFor(const std::optional<ThemeColor>& c) {
    return c ? bg(*c) : std::string();
}

inline std::string styleFor(const ThemeStyle& s) {
    return fgFor(s.fg) + bgFor(s.bg);
}

}  // namespace ttytheme_detail

inline TtyTheme ttyThemeFrom(const ThemePalette& p) {
    using ttytheme_detail::bg;
    using ttytheme_detail::fg;
    using ttytheme_detail::fgFor;
    using ttytheme_detail::styleFor;

    TtyTheme t;
    t.id = p.id;
    t.background = bg(p.background);
    t.foreground = fg(p.defaultFg);
    t.lineNumber = fgFor(themeFgFor(p, StyleRole::Gutter));
    t.gutterCurrent = (p.gutterCurrentBold ? "\x1b[1m" : "") +
                      styleFor(themeStyleFor(p, StyleRole::GutterCurrent, true));
    t.marker = fgFor(themeFgFor(p, StyleRole::Marker));
    t.currentLine = styleFor(themeStyleFor(p, StyleRole::CurrentLine, true));
    t.selection = styleFor(themeStyleFor(p, StyleRole::Selection, false));
    t.listSelected = styleFor(themeStyleFor(p, StyleRole::ListSelected, false));
    t.statusBar = fgFor(themeFgFor(p, StyleRole::StatusBase)) +
                  bg(p.statusBackground);
    t.statusBarName = (p.statusNameBold ? "\x1b[1m" : "") +
                      fgFor(themeFgFor(p, StyleRole::StatusName));
    t.statusBarPath = fgFor(themeFgFor(p, StyleRole::StatusPath));
    t.statusBarAccent = (p.statusAccentBold ? "\x1b[1m" : "") +
                        fgFor(themeFgFor(p, StyleRole::StatusAccentDefault));
    t.statusBarModified = (p.statusModifiedBold ? "\x1b[1m" : "") +
                          fgFor(themeFgFor(p, StyleRole::StatusModified));
    t.message = fgFor(themeFgFor(p, StyleRole::MsgInfo));
    t.prompt = (p.promptBold ? "\x1b[1m" : "") +
               fgFor(themeFgFor(p, StyleRole::MsgPrompt));
    t.success = fgFor(themeFgFor(p, StyleRole::MsgSuccess));
    t.warning = fgFor(themeFgFor(p, StyleRole::MsgWarning));
    t.error = fgFor(themeFgFor(p, StyleRole::MsgError));
    t.reset = "\x1b[0m" + t.background + t.foreground;

    const std::string accentPrefix = p.accentsBold ? "\x1b[1m" : "";
    t.accentNavegacion = accentPrefix +
                         fgFor(themeFgFor(p, StyleRole::AccentNavegacion));
    t.accentInteraccion = accentPrefix +
                          fgFor(themeFgFor(p, StyleRole::AccentInteraccion));
    t.accentSeleccion = accentPrefix +
                        fgFor(themeFgFor(p, StyleRole::AccentSeleccion));
    t.accentComando = accentPrefix +
                      fgFor(themeFgFor(p, StyleRole::AccentComando));
    t.accentBuffers = accentPrefix +
                      fgFor(themeFgFor(p, StyleRole::AccentBuffers));
    t.accentGuardar = accentPrefix +
                      fgFor(themeFgFor(p, StyleRole::AccentGuardar));
    t.accentAbrir = accentPrefix +
                    fgFor(themeFgFor(p, StyleRole::AccentAbrir));
    t.syntaxKeyword = fgFor(themeFgFor(p, StyleRole::SyntaxKeyword));
    t.syntaxType = fgFor(themeFgFor(p, StyleRole::SyntaxType));
    t.syntaxPreprocessor = fgFor(themeFgFor(p, StyleRole::SyntaxPreprocessor));
    t.syntaxString = fgFor(themeFgFor(p, StyleRole::SyntaxString));
    t.syntaxCharacter = fgFor(themeFgFor(p, StyleRole::SyntaxCharacter));
    t.syntaxNumber = fgFor(themeFgFor(p, StyleRole::SyntaxNumber));
    t.syntaxComment = fgFor(themeFgFor(p, StyleRole::SyntaxComment));
    t.bracketMatch = styleFor(themeStyleFor(p, StyleRole::BracketMatch, false));
    return t;
}

inline TtyTheme darkTheme() {
    return ttyThemeFrom(darkPalette());
}

inline TtyTheme lightTheme() {
    return ttyThemeFrom(lightPalette());
}

inline TtyTheme defaultTheme() {
    return darkTheme();
}

inline bool operator==(const TtyTheme& a, const TtyTheme& b) {
    return a.id == b.id;
}

inline bool operator!=(const TtyTheme& a, const TtyTheme& b) {
    return !(a == b);
}

inline const std::string& themeAnsiFor(const TtyTheme& t, StyleRole role) {
    static const std::string kEmpty;
    switch (role) {
        case StyleRole::Default:     return kEmpty;
        case StyleRole::Gutter:      return t.lineNumber;
        case StyleRole::GutterCurrent: return t.gutterCurrent;
        case StyleRole::GutterBlank: return kEmpty;
        case StyleRole::Marker:      return t.marker;
        case StyleRole::CurrentLine: return t.currentLine;
        case StyleRole::Selection:   return t.selection;
        case StyleRole::BracketMatch: return t.bracketMatch;
        case StyleRole::ListSelected: return t.listSelected;
        case StyleRole::StatusBase:  return t.statusBar;
        case StyleRole::StatusName:  return t.statusBarName;
        case StyleRole::StatusPath:  return t.statusBarPath;
        case StyleRole::StatusModified: return t.statusBarModified;
        case StyleRole::StatusAccentDefault: return t.statusBarAccent;
        case StyleRole::AccentNavegacion:  return t.accentNavegacion;
        case StyleRole::AccentInteraccion: return t.accentInteraccion;
        case StyleRole::AccentSeleccion:   return t.accentSeleccion;
        case StyleRole::AccentComando:     return t.accentComando;
        case StyleRole::AccentBuffers:     return t.accentBuffers;
        case StyleRole::AccentGuardar:     return t.accentGuardar;
        case StyleRole::AccentAbrir:       return t.accentAbrir;
        case StyleRole::MsgInfo:     return t.message;
        case StyleRole::MsgSuccess:  return t.success;
        case StyleRole::MsgWarning:  return t.warning;
        case StyleRole::MsgError:    return t.error;
        case StyleRole::MsgPrompt:   return t.prompt;
        case StyleRole::SyntaxKeyword:      return t.syntaxKeyword;
        case StyleRole::SyntaxType:         return t.syntaxType;
        case StyleRole::SyntaxPreprocessor: return t.syntaxPreprocessor;
        case StyleRole::SyntaxString:       return t.syntaxString;
        case StyleRole::SyntaxCharacter:    return t.syntaxCharacter;
        case StyleRole::SyntaxNumber:       return t.syntaxNumber;
        case StyleRole::SyntaxComment:      return t.syntaxComment;
    }
    return kEmpty;
}
