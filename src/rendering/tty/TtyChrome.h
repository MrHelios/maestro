#pragma once

#include <string>
#include "layout/Layout.h"
#include "rendering/ChromeData.h"
#include "rendering/Style.h"
#include "rendering/tty/TtyTheme.h"

// ---------------------------------------------------------------------------
// TtyChrome: codificador TTY del chrome inferior (vive en rendering/tty/).
// Recibe el DTO puro ChromeData (rendering/) y lo traduce a ANSI con el
// TtyTheme (tabla ANSI de ESTE backend). rendering/ nunca incluye este header;
// opera con ChromeData + StyleRole.
//   renderStatus()  -> fila 1 (StatusBar fija)
//   renderMessage() -> fila 2 (MessageBar, Message por tipo)
//   render()        -> chrome completo (StatusBar + MessageBar)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// El chrome toma sus colores del TtyTheme (statusBar como base,
// statusBarName/statusBarPath/statusBarModified para los fragmentos y el
// acento de la etiqueta por rol via themeAnsiFor): ya no hay estilos
// hardcodeados. La base se emite una sola vez al inicio; cada fragmento
// solo cambia color/atributos del texto y restaura la base con
// T.reset + T.statusBar, manteniendo el fondo.
// ---------------------------------------------------------------------------

// ---- Padding por fila (StatusBar y MessageBar comparten valores) ----
inline constexpr int kChromePadLeft = 1;   // espacio inicial antes del nombre
inline constexpr int kChromePadRight = 3;   // margen derecho: bloque (%, fila,col) no pegado al borde

// Componente del chrome inferior. Dibuja la fila fija (StatusBar) y la
// fila del MessageBar dentro del area que le da el Renderer. Es la ultima
// parte del frame: ninguna pantalla decide por si misma donde termina el
// contenido; eso lo resuelve el Layout que calcula el Renderer.
class TtyChrome {
public:
    // Fila 1: StatusBar fijo. Devuelve la fila codificada (con "\x1b[K"
    // inicial, sin "\r\n" final). `width` es el ancho del chrome.
    std::string renderStatus(int width, const StatusBarData& status,
                             StyleRole accent = StyleRole::StatusAccentDefault) const;
    // Fila 2: MessageBar. Devuelve la fila codificada (con "\x1b[K" inicial,
    // sin "\r\n"). El estilo sale de Message.kind via el TtyTheme.
    std::string renderMessage(int width, const Message& message) const;
    // Chrome completo (StatusBar + MessageBar) dentro de `area`
    // (area.height == kChromeRows en el caso normal; con height < 2 el
    // MessageBar se omite). Equivale a renderStatus + "\r\n" + renderMessage.
    // No toca la terminal; devuelve el string.
    // `accent` es el rol semántico de la etiqueta de estado (el backend lo
    // mapea a ANSI vía themeAnsiFor; default = etiqueta por defecto).
    std::string render(const Rect& area, const ChromeData& data,
                       StyleRole accent = StyleRole::StatusAccentDefault) const;

    // Tema de colores del chrome (default: defaultTheme()).
    void setTheme(const TtyTheme& t) { theme_ = t; }
    const TtyTheme& theme() const { return theme_; }

private:
    TtyTheme theme_ = defaultTheme();
};