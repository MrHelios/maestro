#pragma once

#include <string>
#include <vector>

#include "document/Buffer.h"

// Resultado de BufferManager::closeActive(): un enum con el desenlace de
// la operacion para que el Editor decida el modo/mensaje correspondiente
// (cerrar un buffer es una operacion que tambien toca la UI del editor).
enum class CloseResult {
    ModifiedBlocked,  // el buffer estaba modificado: NO se cerro
    ResetLast,        // era el unico buffer: se reinicio vacio y sin nombre
    Removed,          // varios buffers: se elimino el activo
};

// Maneja la coleccion de buffers (v0.6.3): la lista, el indice del buffer
// activo y el contador de nombres "SinNombre". Todo el estado que le
// pertenece a un documento (Document, Cursor, Viewport, seleccion,
// undo/redo, filename, modified) vive en cada Buffer; aqui SOLO la
// coleccion y sus operaciones.
//
// ORDEN MRU: `buffers_` conserva el orden fisico de creacion (estable para
// los escaneos internos y los IDs), mientras que `mru_` guarda los IDs en
// orden de uso: el frente es el buffer ACTIVO y la posicion 1 es el buffer
// abierto anteriormente. Toda activacion real pasa por activateId(), que es
// la UNICA operacion que mueve un ID al frente (una sola vez). El selector
// de buffers usa posiciones MRU, nunca indices fisicos.
//
// Aislado del Editor: no sabe de terminal ni de MessageBar. Las
// operaciones que alteran el modo del Editor (p.ej. pasar al selector al
// cerrar el ultimo buffer) devuelven un resultado (CloseResult) que el
// Editor traduce a su state_/statusMessage_.
class BufferManager {
public:
    // Constructor: arranca con un unico buffer sin nombre activo
    // (invariante 1 y 2: siempre existe al menos un buffer y exactamente
    // uno activo) y consume "SinNombre" del contador.
    BufferManager();

    // Buffer activo (el indice activeBuffer_).
    Buffer& active();
    const Buffer& active() const;

    int count() const;
    Buffer& at(int idx);
    const Buffer& at(int idx) const;
    // Indice activo (exposicion utilitaria; los enfoques deben usar at()).
    int activeIndex() const;

    // Agrega un buffer y lo activa. Devuelve el indice nuevo.
    // Requiere rvalue: llamar con std::move(buf) o temporal (Buffer{}).
    // No acepta lvalues para evitar copias implicitas costosas de Buffer
    // (Document + undo/redo). Para duplicar: Buffer copy = orig; push(std::move(copy));
    int push(Buffer&& buffer);

    // Nombre "SinNombre[n]" de la sesion. Nunca se reutiliza un nombre ya
    // entregado (aunque se cierre el buffer que lo llevaba).
    std::string nextUnnamedName();

    // Ctrl+K n -> crea una buffer nuevo SIN NOMBRE y lo activa. El buffer
    // arranca con el viewport dado (dimensiones de la terminal) para que
    // redibuje toda la pantalla. Devuelve el indice del buffer nuevo.
    int createBuffer(int rows, int cols);

    // Ctrl+K w -> cierra el buffer activo. Vease CloseResult.
    //   - modificado: NO cierra (ModifiedBlocked).
    //   - ultimo buffer: se reinicia vacio y sin nombre (ResetLast).
    //   - varios buffers: se elimina y devuelve Removed (deja el indice
    //     en 0; el Editor abre el selector).
    CloseResult closeActive(int rows, int cols);

    // Activa el buffer en el indice FISICO `idx`. Devuelve true si ese
    // buffer tiene seleccion NO vacia (para que el Editor reconcilie el
    // modo global: Seleccion si hay rango, Navegacion si no).
    // Uso interno/tests; la UI debe usar activateMruPosition().
    bool activateIndex(int idx);

    // Activa el buffer en la posicion `pos` del orden MRU. Devuelve true
    // si el buffer activado tiene seleccion NO vacia. Es el unico punto
    // que usa la UI (selector y Ctrl+K b).
    bool activateMruPosition(int pos);

    // Posicion de `id` en la MRU, o -1 si no existe.
    int mruPositionOf(int id) const;

    // Posicion 1 (buffer anterior) si hay mas de un buffer; 0 si no.
    int previousMruPosition() const;

    // Nombres visibles de todos los buffers en orden MRU, para el
    // selector. Los buffers modificados se marcan con " *" al final.
    std::vector<std::string> mruNames() const;

    // Fija el viewport de un buffer con las dimensiones dadas de la
    // terminal (height = filas util menos la barra de estado y la fila
    // de mensajes). Publication utilitaria para que el Editor no duplique
    // la ecuacion de tamaños.
    static void fitViewport(Buffer& b, int rows, int cols);

private:
    // Indice fisico de `id` en buffers_, o -1 si no existe.
    int indexOfId(int id) const;
    // Unico camino de activacion: fija activeBuffer_ y mueve el ID al
    // frente de la MRU (no-op si ya es el activo). Devuelve hasSelection.
    bool activateId(int id);
    // Mueve/inserta `id` al frente de la MRU.
    void touchMru(int id);
    // Elimina `id` de la MRU (si esta).
    void removeFromMru(int id);
    // Deja la MRU con un unico ID (reinicio del ultimo buffer).
    void resetMru(int id);

    std::vector<Buffer> buffers_;
    // IDs en orden de uso: [activo, anterior, ...]. Invariante:
    // mru_.front() == active().id y es una permutacion de buffers_.
    std::vector<int> mru_;
    int activeBuffer_ = 0;
    int nextBufferId_ = 1;
    // Contador global de la sesion para los nombres de buffer sin nombre.
    int unnamedCounter_ = 1; // el buffer inicial ya gasto "SinNombre"
};
