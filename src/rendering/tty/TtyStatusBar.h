#pragma once

#include <string>
#include "layout/Layout.h"
#include "rendering/StatusBarData.h"
#include "rendering/Style.h"
#include "rendering/tty/TtyTheme.h"

// ---------------------------------------------------------------------------
// TtyStatusBar: codificador TTY de la barra común (vive en rendering/tty/).
// Recibe el DTO puro StatusBarData (rendering/) y lo traduce a ANSI con el
// TtyTheme (tabla ANSI de ESTE backend). rendering/ nunca incluye este header;
// opera con StatusBarData + StyleRole.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// La barra toma sus colores del TtyTheme (statusBar como base,
// statusBarName/statusBarPath/statusBarModified para los fragmentos y el
// acento de la etiqueta por rol via themeAnsiFor): ya no hay estilos
// hardcodeados. La base se emite una sola vez al inicio; cada fragmento
// solo cambia color/atributos del texto y restaura la base con
// T.reset + T.statusBar, manteniendo el fondo.
// ---------------------------------------------------------------------------

// ---- Padding de la barra de estado ----
inline constexpr int kStatusBarPadLeft  = 1;  // espacio inicial antes del nombre
inline constexpr int kStatusBarPadRight = 3;  // margen derecho: bloque (%, fila,col) no pegado al borde

// Componente de la barra comun. Dibuja la fila fija (barra de estado) y la
// fila de mensajes dentro del area que le da el Renderer. Es la ultima
// fila del frame: Ninguna pantalla decide por si misma donde termina el
// contenido; eso lo resuelve el Layout que calcula el Renderer.
class TtyStatusBar {
public:
    // Construye la secuencia ANSI de la barra completa (fila fija + fila de
    // mensajes) dentro de `area` (espera area.height == 2). No toca la
    // terminal; devuelve el string. Usa el TtyTheme de la instancia.
    // `accent` es el rol semántico de la etiqueta de estado (el backend lo
    // mapea a ANSI vía themeAnsiFor; default = etiqueta por defecto).
    std::string render(const Rect& area, const StatusBarData& data,
                       StyleRole accent = StyleRole::StatusAccentDefault);

    // Tema de colores de la barra (default: defaultTheme()).
    void setTheme(const TtyTheme& t) { theme_ = t; }
    const TtyTheme& theme() const { return theme_; }

private:
    TtyTheme theme_ = defaultTheme();
};