#pragma once

#include <optional>
#include <string>
#include <vector>
#include "Buffer.h"
#include "Renderer.h"
#include "Terminal.h"
#include "Event.h"

// Maquina de estados interna del editor (v0.5: modalidad tipo modal).
//   Navegacion:  estado por defecto. No se puede escribir. Tecla 'i' entra
//                a Interaccion (edicion), 's' a Seleccion. Las flechas
//                se mueven libremente sin iniciar seleccion.
//   Interaccion: edicion libre de texto (todo se inserta tal cual). ESC
//                vuelve a Navegacion. Antes era el comportamiento "Normal".
//   Seleccion:   modo seleccion (activo con 's'). Las flechas/Home/End
//                extienden la seleccion; ESC o 'c'/'x' la terminan y
//                vuelven a Navegacion. Un caracter ya NO la reemplaza.
//   Prefix:      tras Ctrl+K; el siguiente evento decide (Ctrl+S guarda,
//                Ctrl+Q sale, Ctrl+K n/t/w operan sobre buffers, cualquier
//                otra cosa lo cancela y se descarta).
//   BufferSelector (v0.6.3): pantalla modal de listado de buffers tras
//                Ctrl+K t (o tras cerrar con Ctrl+K w). Solo se aceptan
//                ↑/↓, Enter y ESC; todo lo demas es no-op.
//   SaveAs (v0.7): prompt "Guardar archivo:" tras Ctrl+K Ctrl+S sobre un
//                buffer SIN NOMBRE (p.ej. creado con Ctrl+K n). El usuario
//                escribe una ruta en la fila de mensajes; Enter guarda,
//                ESC cancela. Modal: solo se aceptan caracteres, Backspace,
//                Enter y ESC.
//
// IMPORTANTE: estas son DOS cosas distintas que NO hay que confundir.
//   state_ == State::Seleccion  == "el modo seleccion esta ACTIVO" (modo).
//   hasSelection()              == "existe un rango de texto seleccionado",
//                                  es decir un rango NO vacio (anchor != position).
//
// Al entrar al modo ('s') sin haber movido el cursor, beginSelection()
// fija anchor == position, asi que es posible tener:
//     state_ == State::Seleccion   y   hasSelection() == false
// (modo seleccion listo pero todavia sin texto marcado). Eso es correcto
// por diseño: el modo es la capacidad de extender; hasSelection() es el
// resultado concreto. Un estado solo significa lo que anuncia: "modo"
// NO implica "texto seleccionado".
enum class State {
    Navegacion,
    Interaccion,
    Seleccion,
    Prefix,
    BufferSelector,
    SaveAs,
};

// Editor es el "engine": maneja una coleccion de buffers (v0.6.3), un
// buffer activo, el modo, los mensajes y el portapapeles global. Todo lo
// que le pertenece a un documento (Document, Cursor, Viewport, seleccion,
// undo/redo, filename, modified) vive en el Buffer. Traduce Eventos en
// mutaciones sobre el buffer activo. No sabe nada de teclas crudas (eso
// es responsabilidad de Terminal) ni de como se dibuja (eso es
// responsabilidad de Renderer).
class Editor {
public:
    Editor();

    // v0.6.2: true si `path` existe y es una carpeta (absoluta o relativa).
    // Las carpetas no se pueden abrir todavia: solo archivos.
    static bool isDirectory(const std::string& path);

    // Abre (o crea) el archivo indicado en el buffer ACTIVO. Acepta rutas
    // relativas o absolutas (p.ej. /home/usuario/Docs/README.md). Si
    // `path` es una carpeta, no abre nada, no cambia el estado y devuelve
    // false.
    bool openFile(const std::string& path);

    // Corre el ciclo principal:
    //   mientras siga abierto:
    //     leer evento
    //     actualizar estado
    //     renderizar
    void run();

    // --- Consultas sobre la seleccion ---
    // true solo si hay un rango NO vacio seleccionado (anchor != position).
    // No confundir con state_ == State::Seleccion (modo activo), que puede ser
    // true aunque no haya texto marcado todavia. "Seleccion activa" (modo)
    // y "texto seleccionado" (rango) son cosas distintas.
    bool hasSelection() const;
    // Seleccion actual, si la hay (normalizada: start antes que end).
    std::optional<Normalized> selection() const;

    static constexpr size_t MAX_UNDO = Buffer::MAX_UNDO;

private:
    // ---- Coleccion de buffers (v0.6.3) ----
    // La lista de buffers y el indice del buffer activo. El estado por
    // documento vive dentro de cada Buffer; aqui SOLO la coleccion.
    std::vector<Buffer> buffers_;
    int activeBuffer_ = 0;

    Buffer& active();
    const Buffer& active() const;

    // Contador global de la sesion para los nombres de buffer sin nombre:
    // "SinNombre", "SinNombre1", "SinNombre2", ... Nunca se reutiliza un
    // nombre ya entregado, para evitar colisiones y hacer el orden
    // predecible (aunque se cierre el buffer que lo llevaba).
    int unnamedCounter_ = 0;
    std::string nextUnnamedName();

    // v0.6.3: Ctrl+K n -> crea un buffer nuevo SIN NOMBRE y lo activa
    // inmediatamente. El buffer nuevo arranca en Navegacion, vacio.
    void createBuffer();
    // Dimensiones del viewport de un buffer, tomadas de la terminal en
    // ese momento. run() las fija al arrancar para los buffers que ya
    // existen, pero un buffer creado a mitad de sesion (Ctrl+K n) o
    // reiniciado (Ctrl+K w sobre el ultimo) arranca con el Viewport por
    // defecto (24x80) y no redibujaria toda la pantalla si la terminal
    // es mas grande. Este helper le da sus dimensiones reales.
    void syncViewportSize(Buffer& b);
    // v0.6.3: Ctrl+K w -> cierra el buffer activo.
    //   - modificado: NO cierra; muestra aviso (hay que guardar o restaurar).
    //   - ultimo buffer: no se elimina; se convierte en vacio sin nombre.
    //   - varios buffers: se elimina y se pasa al selector de buffers.
    void closeActiveBuffer();
    // v0.6.3: activa el buffer `idx` y reconcilia el modo con el estado
    // de su seleccion (Seleccion si tiene rango no vacio, si no Navegacion).
    void activateBuffer(int idx);
    // Nombres visibles de todos los buffers, para el selector. Los buffers
    // modificados se marcan con " *" al final.
    std::vector<std::string> bufferNames() const;

    // Maneja los eventos mientras state_ == State::BufferSelector.
    void handleBufferSelectorEvent(const Event& event);

    Renderer renderer_;
    Terminal terminal_;

    State state_ = State::Navegacion;
    // Estado previo, guardado al entrar en Prefix para volver a el si
    // el prefijo se cancela o ejecuta (p.ej. guardar sin salir de
    // seleccion si el prefijo se abrio estando en Seleccion). Tambien se
    // usa para saber a que modo volver al cancelar el selector con ESC.
    State priorState_ = State::Navegacion;
    bool running_ = true;
    std::string statusMessage_;

    // Buffer de copiar/cortar/pegar (v0.55). Contenido del ultimo rango
    // copiado/cortado, como bloque de lineas. Vive FUERA de HistoryState:
    // nunca se guarda en un pushHistory ni se restaura con undo/redo. Es
    // estado de la UI (que tiene el usuario "en la mano"), no del documento,
    // asi que deshacer una edicion NO debe deshacer el portapapeles. Si no
    // parece participar del historial no es un olvido: es la decision de
    // diseno del punto 3 de v0.5. Es GLOBAL al editor: compartido por
    // todos los buffers (v0.6.3, invariante 11).
    std::vector<std::string> clipboard_;

    // Indice seleccionado en la pantalla del selector de buffers.
    int bufferSelectorIndex_ = 0;

    // Ruta escrita por el usuario en el prompt "Guardar archivo:" (modo
    // SaveAs). Relativa o absoluta; se resuelve contra cwd() al confirmar.
    std::string saveAsPath_;

    // ---- Seleccion total ('a') ----
    // Nota: selectAllActive_/selectAllPrevious_ viven en el Buffer (cada
    // buffer tiene su propio estado de seleccion total). Los helpers de
    // seleccion operan sobre el buffer activo.
    // Seleccion que cubre el documento entero: [BOF, EOF].
    std::optional<Selection> selectAllSelection() const;
    // Maneja los eventos mientras selectAllActive_ es true.
    void handleSelectAllEvent(const Event& event);

    // ---- Helpers de seleccion ----
    // Si no hay seleccion, la inicia poniendo el anchor en la posicion
    // actual del cursor (se llama ANTES de mover el cursor).
    void beginSelection();
    // Sincroniza el extremo de la seleccion con la posicion del cursor.
    void updateSelectionPosition();
    void clearSelection();

    void handleEvent(const Event& event);
    void save();
    // Procesa el siguiente evento cuando el editor esta en modo Prefix
    // (tras Ctrl+K). Ctrl+S/Guardar persiste, Ctrl+Q sale, Ctrl+K n crea
    // buffer, Ctrl+K t abre el selector, Ctrl+K w cierra buffer; cualquier
    // otra tecla descarta el evento y cancela el prefijo.
    void handlePrefixKey(const Event& event);

    // ---- Guardar como (v0.7) ----
    // Ctrl+K Ctrl+S sobre un buffer sin nombre (p.ej. creado con Ctrl+K n)
    // ya no falla con "Archivo sin nombre": en su lugar se abre el prompt
    // "Guardar archivo:" en la fila de mensajes, donde se escribe la ruta
    // destino. Enter confirma (commitSaveAs), ESC cancela.
    void startSaveAs();
    // Maneja los eventos mientras state_ == State::SaveAs.
    void handleSaveAsEvent(const Event& event);
    // Resuelve la ruta escrita (relativa -> absoluta contra cwd), rechaza
    // carpetas y persiste el buffer con su nuevo nombre. Ante exito sale
    // del prompt; ante error se queda para corregir la ruta.
    void commitSaveAs();

    // --- Despacho por modo ---
    void handleNavegacionEvent(const Event& event);
    void handleInteraccionEvent(const Event& event);
    void handleSeleccionEvent(const Event& event);

    // RePag/AvPag: desplaza el viewport y el cursor la misma cantidad de
    // paginas (viewport.height lineas), conservando la posicion relativa
    // del cursor dentro del viewport. `dir` = -1 retrocede (RePag) y +1
    // avanza (AvPag). Antes de los bordes el cursor se clampa para que
    // nunca quede fuera del documento ni el viewport mas alla del EOF.
    void applyPage(int dir);

    void undo();
    void redo();
};
