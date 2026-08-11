#pragma once

#include <string>
#include <vector>

#include "Position.h"

// Document representa UNICAMENTE el contenido de texto.
// No sabe nada de cursor, colores, scroll ni seleccion.
// Solo conoce lineas de texto y como mutarlas.
class Document {
public:
    Document();

    // Carga el archivo indicado. Si no existe, arranca con un documento
    // vacio (una linea vacia) y recuerda el path para poder guardarlo despues.
    bool loadFromFile(const std::string& path);

    // Guarda el contenido actual en el path indicado.
    bool saveToFile(const std::string& path) const;

    // --- Consultas ---
    int lineCount() const;
    int lineLength(int line) const;
    const std::string& lineAt(int line) const;

    // Devuelve una copia de todas las lineas (util para undo/redo).
    std::vector<std::string> snapshot() const;

    // Reemplaza todo el contenido por las lineas dadas.
    void restore(const std::vector<std::string>& lines);

    // --- Mutaciones ---
    // Inserta el caracter c en (line, col). col puede ser igual a
    // lineLength(line) (insertar al final de la linea).
    void insertChar(int line, int col, char c);

    // Inserta una secuencia de BYTES UTF-8 en (line, col). `text` es un
    // caracter UTF-8 completo (1-4 bytes): ASCII o un codepoint multibyte.
    // col puede ser igual a lineLength(line).
    void insertText(int line, int col, const std::string& text);

    // Parte la linea `line` en la posicion `col` en dos lineas.
    // Se usa para la tecla Enter (no forma parte de v0.1, pero el
    // Documento ya queda preparado para soportarlo).
    void insertNewline(int line, int col);

    // Borra el caracter ANTERIOR a (line, col) -- comportamiento de Backspace.
    // Si col == 0 y hay una linea anterior, funde la linea actual con la anterior.
    // Devuelve la cantidad de bytes borrados dentro de una linea (0 si no se
    // borro ningun byte, p.ej. al fundir lineas o en el inicio absoluto). El
    // valor es el largo del caracter UTF-8 completo, para que el Editor pueda
    // reposicionar el cursor sin recalcular el limite del caracter.
    int deleteCharBefore(int line, int col);

    // Borra el caracter en (line, col) -- comportamiento de Delete.
    // Si col == lineLength(line) y hay una linea siguiente, funde la
    // linea siguiente con la actual.
    // Devuelve la cantidad de bytes borrados dentro de una linea (0 si no se
    // borro ningun byte, p.ej. al fundir lineas o en el final absoluto).
    int deleteCharAt(int line, int col);

    // Borra el texto comprendido entre (sl, sc) y (el, ec), con sl <= el
    // y sc <= ec, donde el extremo es exclusivo (igual que un substr).
    // Si (sl, sc) == (el, ec) no borra nada y devuelve false. El cursor
    // debe quedar despues en (sl, sc). Es la primitiva usada para borrar
    // una seleccion completa en una sola operacion.
    bool deleteRange(int sl, int sc, int el, int ec);

    // Extrae el texto comprendido entre (sl, sc) y (el, ec) como un bloque
    // de lineas SIN modificar el documento. Hermana de solo lectura de
    // deleteRange: comparte la misma validacion de limites (rango invertido,
    // fuera de linea, etc.) y ante un rango invalido o vacio devuelve un
    // vector vacio. Es la primitiva usada por copiar/cortar.
    //   - una sola linea: vector de un elemento con el substring [sc, ec).
    //   - multilinea:     1er el. = cola de sl desde sc; intermedios = lineas
    //                      completas; ultimo el. = cabeza de el hasta ec.
    std::vector<std::string> extractRange(int sl, int sc, int el, int ec) const;

    // Inserta un bloque de lineas en (line, col), partiendo la linea actual
    // en col si hace falta. Equivale a insertar el bloque completo en un
    // solo paso. Devuelve la posicion donde queda el cursor tras la insercion
    // (el final de la ultima linea del bloque insertado), para que el Editor
    // no tenga que recomputarla. No toca history (eso es del Editor).
    // Si `block` esta vacio o (line, col) es invalido, no cambia nada y
    // devuelve (line, col) sin modificar.
    Position insertBlock(int line, int col, const std::vector<std::string>& block);

private:
    std::vector<std::string> lines_;

    // true si el archivo original terminaba en salto de linea ('\n'). El
    // modelo de lineas no representa esa nueva linea final (una "linea
    // vacia" al final equivale a no tenerla), asi que sin este flag la
    // guardar y abrir un archivo bien formado perderia su '\n' final.
    // Se respeta al escribir para que abrir+guardar no altere el archivo.
    bool trailingNewline_ = false;
};
