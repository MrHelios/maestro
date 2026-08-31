#pragma once

#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include "core/Position.h"

// Resultado de loadFromFile. Diferencia entre un archivo que genuinamente
// no existe (caso legitimo: se abre como documento nuevo) de un error real
// (sin permisos, fallo de E/S) que NO debe tratarse como archivo nuevo por
// que podria hacer perder contenido de un archivo existente.
enum class LoadResult {
    Success,
    // El archivo no existe: el documento queda como uno vacio nuevo, listo
    // para guardarce. No es un error.
    NotFound,
    PermissionDenied,
    IoError,
};

inline std::ostream& operator<<(std::ostream& os, LoadResult r) {
    switch (r) {
        case LoadResult::Success:         return os << "Success";
        case LoadResult::NotFound:        return os << "NotFound";
        case LoadResult::PermissionDenied:return os << "PermissionDenied";
        case LoadResult::IoError:         return os << "IoError";
    }
    return os << "?";
}

// Document representa UNICAMENTE el contenido de texto.
// No sabe nada de cursor, colores, scroll ni seleccion.
// Solo conoce lineas de texto y como mutarlas.
//
// DECISION: Maestro es un editor de texto BINARIAMENTE SEGURO (modelo B).
// El documento guarda BYTES crudos (vector<string>): loadFromFile lee
// verbatim y saveToFile escribe verbatim, sin validar ni re-encodecar
// nada. Eso permite abrir archivos Latin-1, parcialmente corruptos o
// mezclados SIN destruir bytes. El UTF-8 es solo una capa de
// PRESENTACION (utf8.h) y de input (Terminal): las unicas rutinas que
// interpretan bytes son las que navegan/borran/cuentan columnas, y lo
// hacen por CELDAS byte-safe (utf8::isCellStart): una secuencia UTF-8
// valida es una celda; un byte invalido suelto (continuacion huerfana,
// lead invalido) es su propia celda de 1 byte. Ver README (Limitacion de
// UTF-8) y utf8.h.
class Document {
public:
    void setTouchedCallback(std::function<void(int,int)> cb) { touchedCallback_ = std::move(cb); }

    // Terminador de linea detectado al CARGAR un archivo y usado al GUARDAR.
    // Se conserva para que abrir+guardar NO cambie silenciosamente el
    // formato del archivo: un archivo Windows (CRLF) sigue siendo CRLF al
    // guardar, no se traduce a LF (importante para Git, scripts y proyectos
    // multiplataforma).
    //    LF   -> '\n'      (Unix/Linux/macOS)
    //    CRLF -> "\r\n"    (Windows/DOS)
    //    CR   -> '\r'      (legacy Mac OS; NO se auto-detecta en load,
    //                       solo se conserva si se fija con setLineEnding).
    // Un documento nuevo (o un buffer sin nombre) arranca en LF.
    enum class LineEnding { LF, CRLF, CR };

    Document();

    // Terminador de linea actual del documento.
    LineEnding lineEnding() const;
    // Fija el terminador a usar al guardar (p.ej. si el usuario elige
    // convertir a LF/CRLF). loadFromFile lo setea segun lo detectado.
    void setLineEnding(LineEnding e);

    // Nombres para depurar/testear (CHECK_EQ lo usa para imprimir).
    static const char* lineEndingName(LineEnding e) {
        switch (e) {
            case LineEnding::LF:   return "LF";
            case LineEnding::CRLF: return "CRLF";
            case LineEnding::CR:   return "CR";
        }
        return "?";
    }

    // Carga el archivo indicado. Si no existe (NotFound) arranca con un
    // documento vacio (una linea vacia) y recuerda el path para poder
    // guardarlo despues. Ante un error (PermissionDenied / IoError) NO toca
    // el contenido del documento: deja las lineas como estaban.
    LoadResult loadFromFile(const std::string& path);

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

    // true si el contenido actual termina en '\n'. Se mantiene sincronizado
    // en CADA mutacion (ver Document.cpp): un '\n' final se representa O
    // como una ultima linea vacia en lines_ O como este flag, nunca ambos.
    bool endsWithNewline() const;

    // Fija el estado del '\n' final. Lo usa Buffer al restaurar undo/redo:
    // restore() no puede deducir el flag del vector de lineas, asi que el
    // historial guarda el flag junto con las lineas.
    void setEndsWithNewline(bool ends);

    // --- Mutaciones ---
    // NOTA sobre posiciones: las mutaciones que INSERTAN devuelven la
    // posicion final logica resultante (la que debe tomar el cursor). El
    // calculo de esa posicion es responsabilidad de Document (que conoce
    // el modelo de celdas byte-safe y los '\n'), nunca del llamador: asi
    // UTF-8 multibyte y texto multilinea se resuelven en un unico lugar.

    // Inserta el caracter c en (line, col). col puede ser igual a
    // lineLength(line) (insertar al final de la linea).
    void insertChar(int line, int col, char c);

    // Inserta una secuencia de BYTES en (line, col). `text` puede ser un
    // caracter UTF-8 completo (1-4 bytes), una corrida de ellos o texto
    // MULTILINEA ('\n' como separador): en ese caso parte lineas igual
    // que insertBlock. Devuelve la posicion final tras la insercion.
    Position insertText(int line, int col, const std::string& text);

    // Parte la linea `line` en la posicion `col` en dos lineas.
    // Se usa para la tecla Enter. Es la mitad positiva del par
    // simetrico splitLine <-> mergeLine.
    void splitLine(int line, int col);

    // Inversa de splitLine: funde la linea `line` con la SIGUIENTE
    // (borra el '\n' que las separa). Devuelve true si fusiono algo;
    // false si `line` es la ultima o es invalida.
    bool mergeLine(int line);

    // --- Consultas de celdas (modelo byte-safe, ver utf8.h) ---
    // Bytes de la celda ANTERIOR a (line, col) y de la celda QUE COMIENZA
    // en (line, col). Espejo de solo-lectura de lo que borran
    // deleteCharBefore/deleteCharAt respectivamente: permiten capturar el
    // texto borrado ANTES de borrarlo sin duplicar el recorrido de celdas
    // en la capa de edicion. Devuelven "" si no hay tal celda.
    std::string cellTextBefore(int line, int col) const;
    std::string cellTextAt(int line, int col) const;

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

    // Indenta / desindenta una linea COMPLETA por su comienzo, sin tocar
    // el resto del contenido (es la primitiva usada por la tabulacion sobre
    // una seleccion). `indent` true anade `indentLen` espacios al comienzo;
    // `indent` false quita un nivel de indentacion: si la linea arranca con
    // un tabulador lo quita entero (un solo caracter, un nivel); si no,
    // quita hasta `indentLen` espacios INICIALES (en mezclas espacio+tab
    // solo cuenta la corrida de espacios antes del tab).
    //
    // Devuelve el DELTA de bytes que el cambio movio el comienzo de la
    // linea respecto de la posicion 0: un valor positivo (+indentLen) tras
    // indentar, un valor negativo (el numero de bytes quitados) tras
    // desindentar, y 0 si no hubo cambio (linea invalida, `indentLen` <= 0,
    // o desindentar una linea sin indentacion). Este delta permite al
    // Editor corregir cursor/seleccion tras tabular. La linea no cambia
    // si devuelve 0.
    int indentLine(int line, bool indent, int indentLen);

private:
    std::vector<std::string> lines_;
    std::function<void(int,int)> touchedCallback_;
    void notifyTouched(int a,int b) { if (touchedCallback_) touchedCallback_(a,b); }

    // Terminador de linea usado al guardar. Se detecta en loadFromFile
    // (LF vs CRLF) y se conserva hasta el siguiente guardado.
    LineEnding lineEnding_ = LineEnding::LF;

    // true si el contenido actual termina en salto de linea ('\n'). El
    // modelo de lineas no representa esa nueva linea final (una "linea
    // vacia" al final equivale a no tenerla), asi que sin este flag la
    // guardar y abrir un archivo bien formado perderia su '\n' final.
    // Se respeta al escribir para que abrir+guardar no altere el archivo.
    bool endsWithNewline_ = false;

    // Mantiene el flag sincronizado tras una mutacion: si lines_ termina
    // en una linea vacia, ESA linea ya aporta el '\n' al serializar, asi
    // que el flag debe quedar en false (si no, saveToFile escribiria dos
    // '\n' seguidos). Se llama al final de toda mutacion estructural.
    void normalizeEndsWithNewline();
};

// Permite CHECK_EQ(d.lineEnding(), ...) y depurar con ostream.
inline std::ostream& operator<<(std::ostream& os, Document::LineEnding e) {
    return os << Document::lineEndingName(e);
}
