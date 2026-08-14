#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Selection.h"
#include "core/Viewport.h"

// Estado guardado en cada entrada de undo/redo de un buffer: el
// contenido completo, el cursor y la seleccion vigente en ese momento.
struct HistoryState {
    std::vector<std::string> lines;
    int line = 0;
    int col = 0;
    // Estado del '\n' final en ese momento. El flag no se puede deducir
    // del vector de lineas (restore no lo sabe), asi que se guarda junto
    // con el contenido para que undo/redo no lo desincronice.
    bool endsWithNewline = false;
    // Seleccion vigente en ese momento (si habia). Se restaura en
    // undo/redo para que una seleccion borrada/reemplazada regrese
    // a su estado original.
    std::optional<Selection> selection;
};

// Buffer representa el estado completo y AUTOCONTENIDO de un archivo
// (o de un buffer sin nombre) que el usuario esta editando.
//
// Cada buffer tiene SU PROPIO Documento, Cursor, Viewport, seleccion,
// historial de undo/redo, nombre de archivo, bandera modified_ y
// estado de "seleccion total". Cambiar de buffer NUNCA mezcla estos
// estados: todo lo que le pertenece a un documento viaja con el buffer.
//
// El Editor mantiene una coleccion de Buffer y un buffer activo; Buffer
// no sabe nada del Editor ni de la terminal: es un holder de estado mas
// los metodos de historial que operan sobre ese estado.
class Buffer {
public:
    static constexpr size_t MAX_UNDO = 1000;

    Buffer();

    Document document;
    Cursor cursor;
    Viewport viewport;
    // Ruta absoluta del archivo; vacia => buffer sin nombre.
    std::string filename;
    // Nombre asignado al buffer sin nombre ("SinNombre", "SinNombre1",
    // "SinNombre2", ...). Solo tiene sentido cuando filename esta vacio.
    std::string unnamedName;
    bool modified = false;
    std::optional<Selection> selection;
    // 'a' (seleccion total): selecciona el archivo entero sin mover el
    // cursor; selectAllPrevious_ guarda la seleccion previa para el toggle.
    bool selectAllActive = false;
    std::optional<Selection> selectAllPrevious;
    // Ultimo contenido persistido (o el inicial si nunca se guardo).
    // modified = (contenido actual != savedLines).
    std::vector<std::string> savedLines;

    std::vector<HistoryState> undoStack;
    std::vector<HistoryState> redoStack;

    // Nombre visible del buffer para la barra de estado y el selector:
    // el nombre del archivo (sin directorio) si tiene uno, o el nombre
    // de buffer sin nombre (p.ej. "SinNombre2").
    std::string displayName() const;

    // Empuja el estado ACTUAL a la pila de undo (antes de una mutacion)
    // y limpia el redo. Respeta MAX_UNDO descartando la entrada mas vieja.
    void pushHistory();

    // Restaura el documento, el cursor y la seleccion de un HistoryState.
    // La seleccion restaurada se descarta si quedo fuera de rango, y
    // modified se recalcula contra savedLines.
    void applyState(const HistoryState& state);
};
