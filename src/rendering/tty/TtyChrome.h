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
//   renderStatusBar()  -> StatusBar (fila superior fija del chrome)
//   renderMessageBar() -> MessageBar (fila inferior, MessageBarData por tipo)
//   render()/append() -> chrome segun area.height: >=2 ambas filas,
//                        ==1 solo StatusBar, <=0 vacio (ver computeLayout)
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
    // Fila superior del chrome: StatusBar fijo. Devuelve la fila codificada (con "\x1b[K"
    // inicial, sin "\r\n" final). `width` es el ancho del chrome.
    std::string renderStatusBar(int width, const StatusBarData& status,
                             StyleRole accent = StyleRole::StatusAccentDefault) const;
    // Variante sin allocation extra del camino caliente: agrega la fila superior a
    // `out` sin strings intermedios. renderStatusBar() es solo un wrapper para
    // tests (reserva + appendStatusBar).
    void appendStatusBar(std::string& out, int width, const StatusBarData& status,
                      StyleRole accent = StyleRole::StatusAccentDefault) const;
    // Fila inferior del chrome: MessageBar. Devuelve la fila codificada (con "\x1b[K" inicial,
    // sin "\r\n"). El estilo sale de MessageBarData.kind via el TtyTheme.
    std::string renderMessageBar(int width, const MessageBarData& message) const;
    // Variante sin allocation extra: agrega la fila inferior a `out`.
    void appendMessageBar(std::string& out, int width, const MessageBarData& message) const;
    // Chrome completo dentro de `area`, espejo de la politica degenerada de
    // computeLayout: height >= 2 -> StatusBar + MessageBar; height == 1 ->
    // solo StatusBar; height <= 0 -> string vacio (sin chrome).
    // Equivale a renderStatusBar + "\r\n" + renderMessageBar en el caso normal.
    // No toca la terminal; devuelve el string.
    // `accent` es el rol semántico de la etiqueta de estado (el backend lo
    // mapea a ANSI vía themeAnsiFor; default = etiqueta por defecto).
    std::string render(const Rect& area, const ChromeData& data,
                       StyleRole accent = StyleRole::StatusAccentDefault) const;
    // Camino caliente (frame completo): agrega el chrome a `out` con una
    // sola reserva y sin strings intermedios. Mismo contrato de altura que
    // render(): height <= 0 no agrega nada.
    // render() es solo un wrapper para tests (reserva + append).
    void append(std::string& out, const Rect& area, const ChromeData& data,
                StyleRole accent = StyleRole::StatusAccentDefault) const;

    // Tema de colores del chrome (default: defaultTheme()).
    void setTheme(const TtyTheme& t) { theme_ = t; }
    const TtyTheme& theme() const { return theme_; }

private:
    TtyTheme theme_ = defaultTheme();
};