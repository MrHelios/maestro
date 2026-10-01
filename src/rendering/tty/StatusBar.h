#pragma once

#include <string>
#include "layout/Layout.h"
#include "rendering/StatusBarData.h"
#include "rendering/Style.h"
#include "rendering/tty/Theme.h"

// ---------------------------------------------------------------------------
// StatusBar: codificador TTY de la barra común (vive en rendering/tty/).
// Recibe el DTO puro StatusBarData (rendering/) y lo traduce a ANSI con el
// Theme (tabla ANSI de ESTE backend). rendering/ nunca incluye este header;
// opera con StatusBarData + StyleRole.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// La barra usa un Theme para sus colores (v1.2): ya no tiene estilos
// hardcodeados. La paleta por defecto vive en core/Theme.h (kStatusBarStyle
// = texto negro sobre fondo gris 60%, RGB(102,102,102); kStatusBarName
// blanco; kStatusBarPath negro; kStatusBarCommand bold dorado).
//
// El Theme tambien documenta el aspecto de v1.0/v1.1, que era:
//  - kStatusBarStyle se aplica UNA sola vez al inicio; los fragmentos solo
//    cambian el color/atributos del texto manteniendo ese fondo.
//  - kStatusBarName:   nombre (y ruta) del archivo en blanco.
//  - kStatusBarReset:  vuelve a la base (negro sobre gris 60%).
//  - kStatusBarCommand: etiqueta de estado (comando) en negrita dorada.
//  - kStatusBarPath:   ruta del archivo en negro.

// ---- Padding de la barra de estado ----
inline constexpr int kStatusBarPadLeft  = 1;  // espacio inicial antes del nombre
inline constexpr int kStatusBarPadRight = 3;  // margen derecho: bloque (%, fila,col) no pegado al borde

// Componente de la barra comun. Dibuja la fila fija (barra de estado) y la
// fila de mensajes dentro del area que le da el Renderer. Es la ultima
// fila del frame: Ninguna pantalla decide por si misma donde termina el
// contenido; eso lo resuelve el Layout que calcula el Renderer.
class StatusBar {
public:
    // Construye la secuencia ANSI de la barra completa (fila fija + fila de
    // mensajes) dentro de `area` (espera area.height == 2). No toca la
    // terminal; devuelve el string. Usa el Theme de la instancia.
    // `accent` es el rol semántico de la etiqueta de estado (el backend lo
    // mapea a ANSI vía themeAnsiFor; default = etiqueta por defecto).
    std::string render(const Rect& area, const StatusBarData& data,
                       StyleRole accent = StyleRole::StatusAccentDefault);

    // Tema de colores de la barra (default: defaultTheme()).
    void setTheme(const Theme& t) { theme_ = t; }
    const Theme& theme() const { return theme_; }

private:
    Theme theme_ = defaultTheme();
};