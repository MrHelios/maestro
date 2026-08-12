#pragma once

#include "Document.h"

// El Cursor tiene reglas propias: sabe en que linea y columna esta,
// y recuerda una "columna preferida" para que moverse verticalmente
// entre lineas de distinto largo se sienta natural (igual que en
// editores como Vim, Nano, VSCode, etc).
class Cursor {
public:
    int line = 0;
    int col = 0;

    void moveLeft(const Document& doc);
    void moveRight(const Document& doc);
    void moveUp(const Document& doc);
    void moveDown(const Document& doc);

    void moveHome();
    void moveEnd(const Document& doc);

    // Movimiento por BLOQUES (v0.6, teclas j/k). Un bloque o "palabra" es
    // una corrida maxima de caracteres NO separadores (espacio ' ' o
    // tabulador). Todo whitespace ASCII separa, incl. el final de linea.
    //   j : posiciona el cursor al FINAL del siguiente bloque (hacia
    //       adelante), cruzando lineas si hace falta, sin quedarse corto.
    //   k : posiciona el cursor al COMIENZO del bloque anterior (hacia
    //       atras), cruzando lineas si hace falta.
    // Los multibyte (utf-8) NUNCA se parten: solo los separadores ascii
    // ' '/'\t' detienen el escaneo, asi que los cortes caen siempre sobre
    // un limite de caracter valido.
    void moveToNextWord(const Document& doc);
    void moveToPreviousWord(const Document& doc);

    // Ajusta col para que nunca quede fuera de los limites de la
    // linea actual (por ejemplo, tras borrar caracteres).
    void clampToLine(const Document& doc);

private:
    // Columna "deseada" al moverse verticalmente. Se actualiza cada vez
    // que el usuario se mueve horizontalmente, y se preserva (sin
    // tocarla) durante movimientos verticales sucesivos.
    int preferredCol_ = 0;

    void applyPreferredCol(const Document& doc);
};
