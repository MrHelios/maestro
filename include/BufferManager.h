#pragma once

#include <string>
#include <vector>

#include "Buffer.h"

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
// Aislado del Editor: no sabe de terminal ni de fila de mensajes. Las
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
    int push(Buffer buffer);

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

    // Activa el buffer `idx`. Devuelve true si ese buffer tiene seleccion
    // NO vacia (para que el Editor reconcilie el modo global: Seleccion
    // si hay rango, Navegacion si no).
    bool activate(int idx);

    // Nombres visibles de todos los buffers, para el selector. Los
    // buffers modificados se marcan con " *" al final.
    std::vector<std::string> names() const;

    // Fija el viewport de un buffer con las dimensiones dadas de la
    // terminal (height = filas util menos la barra de estado y la fila
    // de mensajes). Publication utilitaria para que el Editor no duplique
    // la ecuacion de tamaños.
    static void fitViewport(Buffer& b, int rows, int cols);

private:
    std::vector<Buffer> buffers_;
    int activeBuffer_ = 0;
    // Contador global de la sesion para los nombres de buffer sin nombre.
    int unnamedCounter_ = 1; // el buffer inicial ya gasto "SinNombre"
};