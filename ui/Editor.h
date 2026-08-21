#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "core/Buffer.h"
#include "core/BufferManager.h"
#include "clipboard/SystemClipboard.h"
#include "ui/CommandMap.h"
#include "ui/EditorState.h"
#include "ui/FileBrowser.h"
#include "ui/Message.h"
#include "ui/Renderer.h"
#include "terminal/Terminal.h"
#include "terminal/Event.h"

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
    explicit Editor(std::unique_ptr<SystemClipboard> clipboard);

    // v0.6.2: true si `path` existe y es una carpeta (absoluta o relativa).
    // Las carpetas no se pueden abrir todavia: solo archivos. Delega en
    // FileBrowser (que es quien sabe de paths).
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

    // Duracion maxima de los mensajes de ACCION (feedback de una accion
    // realizada: "Copiado.", "Seleccion cancelada.", ...). Los mensajes
    // persistentes (ayuda de modo, prompts de comando, informacion de
    // estado) NO expiran: solo los de accion llevan timeout.
    static constexpr auto kActionMessageTimeout = std::chrono::seconds(5);

private:
    // ---- Mensajes al usuario (paso 8) ----
    // Un unico valor `statusMessage_` (ui::Message) lleva el texto, el tipo
    // y el vencimiento del mensaje vigente. `setStatusMessage` muestra un
    // mensaje PERSISTENTE (ayuda de modo, prompts de comando, informacion
    // de estado): se queda hasta que otra cosa lo reemplace y cancela
    // cualquier timeout vigente. `setActionMessage` muestra un mensaje de
    // ACCION (feedback de una accion ya realizada) con timeout de
    // kActionMessageTimeout: se limpia solo pasado ese tiempo, para no
    // quedar pegado en pantalla.
    void setStatusMessage(const std::string& msg, MessageKind kind = MessageKind::Info);
    void setActionMessage(const std::string& msg, MessageKind kind = MessageKind::Info);
    // En el ciclo principal, si hay un mensaje de accion expirado se limpia
    // (statusMessage_ pasa a vacio). Nunca toca los persistentes.
    void clearExpiredActionMessage();

    // ---- Coleccion de buffers (v0.6.3) ----
    // La lista de buffers, el indice activo y el contador de nombres
    // viven en `buffers` (BufferManager). El estado por documento vive
    // dentro de cada Buffer; el Editor SOLO delega.
    BufferManager buffers;

    Buffer& active();
    const Buffer& active() const;

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

    // ---- Explorador de archivos (v0.6.4) ----
    // Ctrl+K o -> startFileBrowser(). El estado y la navegacion viven en
    // fileBrowser (FileBrowser); el Editor decide las consecuencias
    // (abrir archivo, entrar a carpeta, cancelar) sobre state_/statusMessage_.
    void startFileBrowser();
    void handleFileBrowserEvent(const Event& event);
    void fileBrowserEnterSelected();
    // Abre `path` (absoluta) en un buffer NUEVO, o activa el existente
    // si ya hay uno con esa ruta. Sale del explorador a Navegacion.
    void openFileToBuffer(const std::string& path);

    Renderer renderer_;
    Terminal terminal_;
    // Despacho de comandos por nombre. El Editor registra los handlers en
    // el constructor (registerCommands) y los modos resuelven la tecla ->
    // nombre -> handler aqui, en vez de tener cada accion dispersa en
    // bloques de switch.
    CommandMap commands_;

    // Registra bajo nombres los handlers de los comandos de modo (i/s/p/
    // c/x/a/j/k) y de prefijo (n/t/w/o). Los CUERPOS quedan registrados
    // como lambdas que capturan este editor.
    void registerCommands();

    State state_ = State::Navegacion;
    // P0 interaction: grupo de escritura sobre seleccion. Cuando se escribe
    // una letra sobre un rango marcado, el reemplazo se empuja UNA vez y la
    // escritura consecutiva posterior se absorbe en la MISMA entrada de undo,
    // de modo que "reemplazar + teclear" se deshace en una sola operacion.
    // true solo mientras se digita continuamente tras un reemplazo de
    // seleccion; cualquier otra accion (borrar, Enter, ESC, mover, undo/redo)
    // o una escritura normal (sin reemplazo) lo apagan. La escritura normal
    // sigue deshaciendose por caracter.
    bool coalescingTyping_ = false;
    // Estado previo, guardado al entrar en Prefix para volver a el si
    // el prefijo se cancela o ejecuta (p.ej. guardar sin salir de
    // seleccion si el prefijo se abrio estando en Seleccion). Tambien se
    // usa para saber a que modo volver al cancelar el selector con ESC.
    State priorState_ = State::Navegacion;
    bool running_ = true;
    // Mensaje vigente (paso 8): texto + tipo + vencimiento en un solo valor.
    // En lugar de testear actionMessageActive_/actionMessageExpiry_, se
    // pregunta statusMessage_.persistent()/.expired().
    Message statusMessage_;

    std::unique_ptr<SystemClipboard> clipboard_;
    static std::string blockToString(const std::vector<std::string>& block);
    static std::vector<std::string> stringToBlock(const std::string& text);
    std::vector<std::string> getClipboardBlock() const;
    void setClipboardBlock(const std::vector<std::string>& block);
    std::string getClipboardText() const;
    bool isClipboardEmpty() const;

    // Indice seleccionado en la pantalla del selector de buffers.
    int bufferSelectorIndex_ = 0;

    // ---- Explorador de archivos (v0.6.4) ----
    // Estado y navegacion del explorador (ruta actual, entradas, indice y
    // scroll). El Editor no duplica ese estado: solo decide las
    // consecuencias de las acciones del explorador.
    FileBrowser fileBrowser;

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
    // Dibuja el frame actual segun state_ (pantalla normal, selector de
    // buffers o explorador de archivos). Se comparte entre el flujo normal
    // del ciclo y el despertar por timeout de un mensaje de accion.
    void renderFrame();
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

    // Indenta / desindenta el rango seleccionado actual (todas las lineas
    // que toca). `indent` true tabula hacia adentro ('}'), `indent` false
    // quita un nivel ('{'). Solo opera si hay rango NO vacio (hasSelection);
    // si no, avisa y no cambia nada. Empuja una sola entrada de historial y
    // marca modified. No cambia de modo: la seleccion se conserva.
    void indentSelection(bool indent);

    // Borra el rango seleccionado actual (si hay texto marcado) y deja el
    // cursor en el INICIO del rango. Empuja historial de undo y marca
    // modified como una edicion (igual que cortar, pero SIN tocar el
    // portapapeles: la seleccion se elimina sin copiarla). No cambia de
    // modo. Devuelve true si borro algo.
    bool deleteSelection();

    void undo();
    void redo();
};
