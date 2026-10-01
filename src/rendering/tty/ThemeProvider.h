#pragma once

#include "rendering/tty/TtyTheme.h"

// ---------------------------------------------------------------------------
// ThemeProvider (Fase E paso 6): dueño único del tema activo del backend.
//
// Fuente única: todo camino que cambie el tema pasa por acá (setTheme o
// toggle); el resto consulta theme()/isDark(). Arranca en oscuro, igual que
// el viejo isDarkTheme_=true del Editor.
// ---------------------------------------------------------------------------
class ThemeProvider {
public:
    ThemeProvider() : theme_(darkTheme()) {}

    void setTheme(const TtyTheme& t) {
        theme_ = t;
        dark_ = (t.id == darkTheme().id);
    }
    void toggle() { setTheme(dark_ ? lightTheme() : darkTheme()); }

    const TtyTheme& theme() const { return theme_; }
    bool isDark() const { return dark_; }

private:
    TtyTheme theme_ = darkTheme();
    bool dark_ = true;
};
