#pragma once

#include <string>
#include "core/Layout.h"
#include "core/Theme.h"
#include "ui/Message.h"

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

// Datos que la barra RECIBE y dibuja. La barra no conoce Editor, Buffer,
// FileBrowser, Document ni Selection: solo recibe texto/numeros y los pinta.
//
// El bloque derecho por defecto es "pct% (fila,col)" calculado de
// cursorLine/cursorCol/totalLines (posicion del cursor sobre el documento).
// Si `right` no esta vacio, se usa ese texto EN LUGAR del calculado: sirve
// para pantallas sin documento (selector, explorador) que muestran un
// contador propio (p.ej. "2/5").
struct StatusBarData {
    std::string name;        // nombre del archivo (izquierda, en blanco)
    std::string path;        // ruta (izquierda, en negro); vacia si no aplica
    std::string estado;      // etiqueta de estado (izquierda, en dorado)
    Message message;         // fila de mensajes (fila propia, coloreada por tipo)
    std::string right;       // overrlde del bloque derecho; vacio = calcular
    bool modified = false;   // fila [modificado] junto al nombre
    int cursorLine = 0;      // fila del cursor (0-indexada, para {fila,col} y %)
    int cursorCol = 0;       // columna del cursor (0-indexada, para {fila,col})
    int totalLines = 0;      // lineas del documento (porcentaje vertical)
};

// Componente de la barra comun. Dibuja la fila fija (barra de estado) y la
// fila de mensajes dentro del area que le da el Renderer. Es la ultima
// fila del frame: Ninguna pantalla decide por si misma donde termina el
// contenido; eso lo resuelve el Layout que calcula el Renderer.
class StatusBar {
public:
    // Construye la secuencia ANSI de la barra completa (fila fija + fila de
    // mensajes) dentro de `area` (espera area.height == 2). No toca la
    // terminal; devuelve el string. Usa el Theme de la instancia.
    std::string render(const Rect& area, const StatusBarData& data);

    // Tema de colores de la barra (default: defaultTheme()).
    void setTheme(const Theme& t) { theme_ = t; }
    const Theme& theme() const { return theme_; }

private:
    Theme theme_ = defaultTheme();
};