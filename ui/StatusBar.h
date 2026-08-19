#pragma once

#include <string>
#include "core/Layout.h"
#include "ui/Message.h"

// ---------------------------------------------------------------------------
// Estilos de la barra de estado (v1.0). Antes vivian en Renderer.h; se
// mudan aqui porque la barra es un componente propio, no parte del Renderer.
//
// kStatusBarStyle: texto negro sobre fondo gris 60%. El gris se mide con
// 100% = negro y 0% = blanco: 60% => nivel 0.4*255 = 102, RGB(102,102,102)
// en truecolor. Reemplaza al video inverso que se usaba para marcar la
// fila fija.
//
// El fondo (kStatusBarStyle) se aplica UNA sola vez al inicio; los
// fragmentos solo cambian el color/atributos del texto manteniendo ese
// fondo:
//  - kStatusBarName:    nombre (y ruta) del archivo en blanco.
//  - kStatusBarReset:   vuelve a la base (negro sobre gris 60%).
//  - kStatusBarCommand: etiqueta de estado (comando) en negrita dorada
//                       opaca (dorado de la paleta 256, 38;5;178, + bold).
//  - kStatusBarPath:    ruta del archivo en negro, distinguiendola del
//                       nombre (blanco) y del comando (dorado).
inline constexpr const char* kStatusBarStyle = "\x1b[30m\x1b[48;2;102;102;102m";
inline constexpr const char* kStatusBarName = "\x1b[37m";                        // blanco
inline constexpr const char* kStatusBarPath = "\x1b[30m";                          // negro
inline constexpr const char* kStatusBarReset =
    "\x1b[0m\x1b[30m\x1b[48;2;102;102;102m";                                      // base
inline constexpr const char* kStatusBarCommand = "\x1b[1m\x1b[38;5;178m";        // bold + dorado

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
    // terminal; devuelve el string.
    std::string render(const Rect& area, const StatusBarData& data);
};