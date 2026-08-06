#pragma once

#include <string>
#include <vector>

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

    // --- Mutaciones ---
    // Inserta el caracter c en (line, col). col puede ser igual a
    // lineLength(line) (insertar al final de la linea).
    void insertChar(int line, int col, char c);

    // Parte la linea `line` en la posicion `col` en dos lineas.
    // Se usa para la tecla Enter (no forma parte de v0.1, pero el
    // Documento ya queda preparado para soportarlo).
    void insertNewline(int line, int col);

    // Borra el caracter ANTERIOR a (line, col) -- comportamiento de Backspace.
    // Si col == 0 y hay una linea anterior, funde la linea actual con la anterior.
    // Devuelve false si no hay nada que borrar (inicio absoluto del documento).
    bool deleteCharBefore(int line, int col);

    // Borra el caracter en (line, col) -- comportamiento de Delete.
    // Si col == lineLength(line) y hay una linea siguiente, funde la
    // linea siguiente con la actual.
    // Devuelve false si no hay nada que borrar (fin absoluto del documento).
    bool deleteCharAt(int line, int col);

private:
    std::vector<std::string> lines_;
};
