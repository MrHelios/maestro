#pragma once

#include <string>
#include <vector>

// Una entrada del explorador de archivos. `name` es lo que se muestra
// ("..", una carpeta o un archivo), `isDirectory` true para carpetas y
// para "..", y `fullPath` es la ruta absoluta que representa.
struct FileBrowserEntry {
    std::string name;
    bool isDirectory = false;
    std::string fullPath;
};

// Explorador de archivos (v0.6.4): estado + navegacion de un listado de
// directorio, aislado del Editor. No sabe de terminal ni de fila de
// mensajes: las consecuencias (abrir un archivo, entrar a una carpeta,
// cancelar, mensajes de estado) las decide el Editor a partir de lo que
// devuelven reload() y enter().
class FileBrowser {
public:
    // true si `path` existe y es una carpeta (absoluta o relativa).
    static bool isDirectory(const std::string& path);

    // Directorio de trabajo actual (vacio si getcwd falla).
    static std::string getCwd();

    // Directorio padre de `path` (para la entrada ".."). Para "/" vuelve a "/".
    static std::string parentPath(const std::string& path);

    // Lista las entradas ordenadas: ".." (si no es raiz), carpetas y luego
    // archivos, ambos alfabeticos case-insensitive. `error` queda set si no
    // se pudo leer el directorio. Los symlinks rotos se omiten.
    static std::vector<FileBrowserEntry> listDirectory(const std::string& path,
                                                       std::string& error);

    // Empieza en el directorio de trabajo con la seleccion al inicio.
    void start();
    void startAt(const std::string& path);

    // (Re)lista path_ en entries_/displayNames_. Devuelve un mensaje de
    // error si no se pudo leer el directorio ("" si fue bien).
    std::string reload();

    // Ajusta scroll_ para que index_ quede siempre dentro de la ventana
    // de `page` filas.
    void clampScroll(int page);

    // Movimiento. Devuelve true si el indice seleccionado cambio.
    bool moveUp();
    bool moveDown();

    // Enter sobre la entrada seleccionada.
    enum class EnterResult {
        None,               // no hay entradas
        EnteredDirectory,   // entro a una carpeta (".." o una subcarpeta)
        OpenedFile,         // la entrada es un archivo: pendingPath() tiene la ruta
    };
    EnterResult enter();

    // Tras OpenedFile, la ruta absoluta del archivo a abrir.
    const std::string& pendingPath() const { return pendingPath_; }

    // Estado del explorador (accesible para los tests).
    std::string path_;
    std::vector<FileBrowserEntry> entries_;
    std::vector<std::string> displayNames_;
    int index_ = 0;   // indice de la entrada seleccionada
    int scroll_ = 0;  // offset de la ventana visible

private:
    std::string pendingPath_;
};
