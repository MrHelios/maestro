#pragma once

#include <cstdint>
#include <optional>

#include "rendering/Style.h"

struct ThemeColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    bool operator==(const ThemeColor& o) const {
        return r == o.r && g == o.g && b == o.b;
    }
    bool operator!=(const ThemeColor& o) const { return !(*this == o); }
};

struct ThemePalette {
    int id = 0;

    ThemeColor defaultFg;
    ThemeColor background;
    ThemeColor statusBackground;
    ThemeColor messageBackground;
    ThemeColor cursor;

    ThemeColor gutter;
    ThemeColor gutterCurrent;
    ThemeColor gutterBlank;
    ThemeColor marker;

    ThemeColor currentLineFg;
    ThemeColor currentLineBg;
    ThemeColor selectionFg;
    ThemeColor selectionBg;
    ThemeColor bracketMatchFg;
    ThemeColor bracketMatchBg;
    ThemeColor listSelectedFg;
    ThemeColor listSelectedBg;

    ThemeColor statusBase;
    ThemeColor statusName;
    ThemeColor statusPath;
    ThemeColor statusModified;
    ThemeColor statusAccentDefault;

    ThemeColor accentNavegacion;
    ThemeColor accentInteraccion;
    ThemeColor accentSeleccion;
    ThemeColor accentComando;
    ThemeColor accentBuffers;
    ThemeColor accentGuardar;
    ThemeColor accentAbrir;

    ThemeColor msgInfo;
    ThemeColor msgSuccess;
    ThemeColor msgWarning;
    ThemeColor msgError;
    ThemeColor msgPrompt;

    ThemeColor syntaxKeyword;
    ThemeColor syntaxType;
    ThemeColor syntaxPreprocessor;
    ThemeColor syntaxString;
    ThemeColor syntaxCharacter;
    ThemeColor syntaxNumber;
    ThemeColor syntaxComment;

    bool statusNameBold = false;
    bool gutterCurrentBold = true;
    bool statusModifiedBold = true;
    bool statusAccentBold = true;
    bool accentsBold = true;
    bool promptBold = true;
};

// Estilo semántico resuelto de un rol. Los opcionales expresan herencia:
// nullopt = usar el valor base del tema (defaultFg / background).
struct ThemeStyle {
    std::optional<ThemeColor> fg;
    std::optional<ThemeColor> bg;
};

inline std::optional<ThemeColor> themeFgFor(const ThemePalette& p,
                                            StyleRole role) {
    switch (role) {
        case StyleRole::Default:
            return std::nullopt;
        case StyleRole::Gutter:
            return p.gutter;
        case StyleRole::GutterCurrent:
            return p.gutterCurrent;
        case StyleRole::GutterBlank:
            return p.gutterBlank;
        case StyleRole::Marker:
            return p.marker;
        case StyleRole::CurrentLine:
            return p.currentLineFg;
        case StyleRole::Selection:
            return p.selectionFg;
        case StyleRole::BracketMatch:
            return p.bracketMatchFg;
        case StyleRole::ListSelected:
            return p.listSelectedFg;
        case StyleRole::StatusBase:
            return p.statusBase;
        case StyleRole::StatusName:
            return p.statusName;
        case StyleRole::StatusPath:
            return p.statusPath;
        case StyleRole::StatusModified:
            return p.statusModified;
        case StyleRole::StatusAccentDefault:
            return p.statusAccentDefault;
        case StyleRole::AccentNavegacion:
            return p.accentNavegacion;
        case StyleRole::AccentInteraccion:
            return p.accentInteraccion;
        case StyleRole::AccentSeleccion:
            return p.accentSeleccion;
        case StyleRole::AccentComando:
            return p.accentComando;
        case StyleRole::AccentBuffers:
            return p.accentBuffers;
        case StyleRole::AccentGuardar:
            return p.accentGuardar;
        case StyleRole::AccentAbrir:
            return p.accentAbrir;
        case StyleRole::MsgInfo:
            return p.msgInfo;
        case StyleRole::MsgSuccess:
            return p.msgSuccess;
        case StyleRole::MsgWarning:
            return p.msgWarning;
        case StyleRole::MsgError:
            return p.msgError;
        case StyleRole::MsgPrompt:
            return p.msgPrompt;
        case StyleRole::SyntaxKeyword:
            return p.syntaxKeyword;
        case StyleRole::SyntaxType:
            return p.syntaxType;
        case StyleRole::SyntaxPreprocessor:
            return p.syntaxPreprocessor;
        case StyleRole::SyntaxString:
            return p.syntaxString;
        case StyleRole::SyntaxCharacter:
            return p.syntaxCharacter;
        case StyleRole::SyntaxNumber:
            return p.syntaxNumber;
        case StyleRole::SyntaxComment:
            return p.syntaxComment;
    }
    return std::nullopt;
}

inline ThemeColor themeBgFor(const ThemePalette& p, RowBgKind kind) {
    switch (kind) {
        case RowBgKind::CurrentLine:
            return p.currentLineBg;
        case RowBgKind::Selection:
            return p.selectionBg;
        case RowBgKind::Bracket:
            return p.bracketMatchBg;
        case RowBgKind::List:
            return p.listSelectedBg;
        case RowBgKind::Content:
        default:
            return p.background;
    }
}

inline ThemeStyle themeStyleFor(const ThemePalette& p, StyleRole role,
                                bool isCurrentLine) {
    return ThemeStyle{themeFgFor(p, role),
                      themeBgFor(p, rowBgKindFor(role, isCurrentLine))};
}

inline ThemePalette darkPalette() {
    ThemePalette p;
    p.id = 0;

    p.defaultFg = ThemeColor{0xD4, 0xD4, 0xD4};
    p.background = ThemeColor{0x12, 0x13, 0x14};
    p.statusBackground = ThemeColor{0x19, 0x1A, 0x1B};
    p.messageBackground = ThemeColor{0x12, 0x13, 0x14};
    p.cursor = ThemeColor{0xD4, 0xD4, 0xD4};

    p.gutter = ThemeColor{0x6C, 0x6C, 0x6C};
    p.gutterCurrent = ThemeColor{0x51, 0xAF, 0xEF};
    p.gutterBlank = p.background;
    p.marker = ThemeColor{0x5F, 0x87, 0x5F};

    p.currentLineFg = p.defaultFg;
    p.currentLineBg = ThemeColor{0x3A, 0x3A, 0x3A};
    p.selectionFg = ThemeColor{0xFF, 0xFF, 0xFF};
    p.selectionBg = ThemeColor{0x5F, 0x5F, 0x87};
    p.bracketMatchFg = ThemeColor{0x1E, 0x1E, 0x1E};
    p.bracketMatchBg = ThemeColor{0xFF, 0xD7, 0x5F};
    p.listSelectedFg = ThemeColor{0xFF, 0xFF, 0xFF};
    p.listSelectedBg = p.currentLineBg;

    p.statusBase = ThemeColor{0x8C, 0x8C, 0x8C};
    p.statusName = ThemeColor{0x8C, 0x8C, 0x8C};
    p.statusPath = ThemeColor{0x8C, 0x8C, 0x8C};
    p.statusModified = ThemeColor{0xDD, 0xDD, 0x00};
    p.statusAccentDefault = ThemeColor{0x51, 0xAF, 0xEF};

    p.accentNavegacion = ThemeColor{0x51, 0xAF, 0xEF};
    p.accentInteraccion = p.accentNavegacion;
    p.accentSeleccion = p.accentNavegacion;
    p.accentComando = p.accentNavegacion;
    p.accentBuffers = p.accentNavegacion;
    p.accentGuardar = p.accentNavegacion;
    p.accentAbrir = p.accentNavegacion;

    p.msgInfo = p.defaultFg;
    p.msgSuccess = p.defaultFg;
    p.msgWarning = p.defaultFg;
    p.msgError = p.defaultFg;
    p.msgPrompt = ThemeColor{0xFF, 0xFF, 0xFF};

    p.syntaxKeyword = ThemeColor{0x51, 0xAF, 0xEF};
    p.syntaxType = ThemeColor{0xAF, 0x87, 0xFF};
    p.syntaxPreprocessor = ThemeColor{0xFF, 0x87, 0x00};
    p.syntaxString = ThemeColor{0x87, 0xD7, 0x87};
    p.syntaxCharacter = p.syntaxString;
    p.syntaxNumber = ThemeColor{0xFF, 0xAF, 0x00};
    p.syntaxComment = ThemeColor{0x6C, 0x6C, 0x6C};

    p.statusNameBold = false;
    return p;
}

inline ThemePalette lightPalette() {
    ThemePalette p;
    p.id = 1;

    p.defaultFg = ThemeColor{0x1E, 0x1E, 0x1E};
    p.background = ThemeColor{0xFF, 0xFF, 0xFF};
    p.statusBackground = ThemeColor{0xEC, 0xEC, 0xEC};
    p.messageBackground = ThemeColor{0xFF, 0xFF, 0xFF};
    p.cursor = ThemeColor{0x1E, 0x1E, 0x1E};

    p.gutter = ThemeColor{0x94, 0x94, 0x94};
    p.gutterCurrent = ThemeColor{0x00, 0x5F, 0xA0};
    p.gutterBlank = p.background;
    p.marker = ThemeColor{0x6E, 0x6E, 0x6E};

    p.currentLineFg = p.defaultFg;
    p.currentLineBg = ThemeColor{0xE4, 0xE4, 0xE4};
    p.selectionFg = ThemeColor{0x1E, 0x1E, 0x1E};
    p.selectionBg = ThemeColor{0xBD, 0xBD, 0xFF};
    p.bracketMatchFg = ThemeColor{0x1E, 0x1E, 0x1E};
    p.bracketMatchBg = ThemeColor{0xFF, 0xD7, 0x5F};
    p.listSelectedFg = ThemeColor{0x1E, 0x1E, 0x1E};
    p.listSelectedBg = p.currentLineBg;

    p.statusBase = ThemeColor{0x26, 0x26, 0x26};
    p.statusName = ThemeColor{0x1E, 0x1E, 0x1E};
    p.statusPath = ThemeColor{0x6E, 0x6E, 0x6E};
    p.statusModified = ThemeColor{0xD7, 0x00, 0x00};
    p.statusAccentDefault = ThemeColor{0x00, 0x5F, 0xA0};

    p.accentNavegacion = ThemeColor{0x00, 0x5F, 0xA0};
    p.accentInteraccion = p.accentNavegacion;
    p.accentSeleccion = p.accentNavegacion;
    p.accentComando = p.accentNavegacion;
    p.accentBuffers = p.accentNavegacion;
    p.accentGuardar = p.accentNavegacion;
    p.accentAbrir = p.accentNavegacion;

    p.msgInfo = p.defaultFg;
    p.msgSuccess = p.defaultFg;
    p.msgWarning = p.defaultFg;
    p.msgError = p.defaultFg;
    p.msgPrompt = ThemeColor{0x1E, 0x1E, 0x1E};

    p.syntaxKeyword = ThemeColor{0x00, 0x5F, 0xA0};
    p.syntaxType = ThemeColor{0x5F, 0x00, 0x87};
    p.syntaxPreprocessor = ThemeColor{0x8A, 0x4B, 0x00};
    p.syntaxString = ThemeColor{0x00, 0x5F, 0x00};
    p.syntaxCharacter = p.syntaxString;
    p.syntaxNumber = ThemeColor{0x8A, 0x4B, 0x00};
    p.syntaxComment = ThemeColor{0x94, 0x94, 0x94};

    p.statusNameBold = true;
    return p;
}
