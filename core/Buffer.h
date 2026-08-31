#pragma once

#include <optional>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Selection.h"
#include "core/SmallVec.h"
#include "core/Viewport.h"

// Tipo de operacion atomica registrada en una entrada de historial. Cada
// Edit es REVERSIBLE: guarda que hacer y como deshacerlo, sin copiar el
// documento (el texto afectado viaja en `text`, nunca el contenido entero).
//
//   Insert    : se inserto `text` en [start, end)
//   Delete    : se borro `text` de [start, end)
//   SplitLine : Enter partio la linea en `start` ('\n' insertado ahi)
//   MergeLine : se fundio start.line con la linea siguiente ('\n' borrado)
//
// No existe Replace: un reemplazo es un Delete del texto viejo seguido de
// un Insert del nuevo, ambos dentro de la MISMA HistoryEntry, de modo que
// se deshace/rehace como una sola operacion.
enum class EditType {
    Insert,
    Delete,
    SplitLine,
    MergeLine,
};

struct Edit {
    EditType type;
    Position start;
    Position end;
    std::string text;
};

// Una operacion de undo/redo: la lista de edits aplicadas (en orden) mas
// el estado de cursor/seleccion/'\n' final ANTES y DESPUES del grupo.
// Undo aplica las edits en reversa y restaura el "antes"; redo las reaplica
// y restaura el "despues".
struct HistoryEntry {
    // El caso dominante es UNA edit por entrada (cada tecla): el primer
    // Edit vive INLINE en la propia entrada y solo las operaciones
    // multi-edit (reemplazo de seleccion, indentacion, pegado sobre
    // seleccion, ...) piden memoria al heap.
    SmallVec<Edit, 1> edits;

    Position cursorBefore{0, 0};
    Position cursorAfter{0, 0};

    std::optional<Selection> selectionBefore;
    std::optional<Selection> selectionAfter;

    // Estado del '\n' final antes/despues. El flag no se puede deducir
    // del vector de lineas, asi que viaja junto a las edits para que
    // undo/redo no lo desincronice.
    bool endsWithNewlineBefore = false;
    bool endsWithNewlineAfter = false;
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
    Buffer(const Buffer& other);
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(const Buffer& other);
    Buffer& operator=(Buffer&& other) noexcept;

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
    struct WatcherEntry {
        int rowStart;
        int rowEnd;
    };
    std::vector<std::string> originalSnapshot_;
    std::vector<WatcherEntry> watcher_;
    void recordWatch(int rowStart, int rowEnd);

    // Ultimo contenido persistido (o el inicial si nunca se guardo).
    // modified = (contenido actual != originalSnapshot_).
    bool savedEndsWithNewline = false;

    std::vector<HistoryEntry> undoStack;
    std::vector<HistoryEntry> redoStack;

    struct FileIdentity {
        bool valid = false;
        dev_t dev = 0;
        ino_t ino = 0;
        off_t size = 0;
        struct timespec mtime = {0, 0};
        bool operator==(const FileIdentity& o) const {
            if (valid != o.valid) return false;
            if (!valid) return true;
            return dev == o.dev && ino == o.ino && size == o.size &&
                   mtime.tv_sec == o.mtime.tv_sec && mtime.tv_nsec == o.mtime.tv_nsec;
        }
        bool operator!=(const FileIdentity& o) const { return !(*this == o); }
    } savedIdentity;

    void syncSavedState();
    bool isModified() const;
    void recalcModified();

    // Nombre visible del buffer para la barra de estado y el selector:
    // el nombre del archivo (sin directorio) si tiene uno, o el nombre
    // de buffer sin nombre (p.ej. "SinNombre2").
    std::string displayName() const;

    // --- Historial por operaciones reversibles ---
    // Captura el estado ANTES de una mutacion (cursor, seleccion y '\n'
    // final). Se llama ANTES de tocar el documento; las edits se agregan
    // al entry devuelto mientras se muta.
    HistoryEntry beginHistoryEntry() const;

    // Completa el estado DESPUES de la mutacion y apila la entrada en
    // undoStack (limpiando el redo, respetando MAX_UNDO). Una entrada
    // sin edits (mutacion no-op) se descarta: nunca hay entradas vacias.
    void commitHistoryEntry(HistoryEntry entry);

    // Coalescing (grupo de escritura tras un reemplazo de seleccion):
    // agrega una edicion a la ULTIMA entrada del historial en vez de
    // crear una nueva, refrescando su estado "despues". Requiere una
    // entrada previa (contrato del grupo de escritura).
    void extendLastEntry(Edit edit);

    // Deshace la ultima entrada: aplica sus edits en reversa, restaura
    // cursor/seleccion/'\n' del "antes", recalcula modified y mueve la
    // entrada al redoStack. Devuelve false si no hay nada que deshacer.
    bool undo();

    // Rehace: reaplica forward la entrada tope de redoStack, restaura el
    // estado "despues", recalcula modified y devuelve la entrada al
    // undoStack. Devuelve false si no hay nada que rehacer.
    bool redo();

private:
    void rebindCallback();
    // Aplica una edit hacia adelante / en reversa sobre el documento.
    void applyForward(const Edit& e);
    void applyBackward(const Edit& e);

    // Restaura una seleccion descartandola SOLO si quedo fuera de rango
    // tras el undo/redo. Una seleccion degenerada dentro de rango se
    // conserva tal cual (simetria undo/redo).
    void restoreSelection(std::optional<Selection> sel);
};
