#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
#include "layout/BracketMatcher.h"
#include "document/Buffer.h"
#include "document/BufferManager.h"
#include "syntax/SyntaxSpan.h"
#include "platform/clipboard/SystemClipboard.h"
#include "filesystem/FileWatcher.h"
#include "app/CommandMap.h"
#include "app/EditorState.h"
#include "app/FileBrowser.h"
#include "app/Message.h"
#include "rendering/Renderer.h"
#include "platform/Event.h"

// Editor es el "engine": maneja una coleccion de buffers (v0.6.3), un
// buffer activo, el modo, los mensajes y el portapapeles global. Todo lo
// que le pertenece a un documento (Document, Cursor, Viewport, seleccion,
// undo/redo, filename, modified) vive en el Buffer. Traduce Eventos en
// mutaciones sobre el buffer activo. No sabe nada de teclas crudas (eso
// es responsabilidad de Terminal) ni de como se dibuja (eso es
// responsabilidad de Renderer).
// Frontier (12): Editor común — no incluye Terminal/Keymap, no conoce
// pollfd/fd(), no construye X11/Inotify (eso lo hacen las factories y el
// TtyRunLoop). El tamaño de ventana es estado propio (currentRows_/Cols_)
// actualizado por cada EventType::Resize con su payload.
class Editor {
    friend class TtyRunLoop;
public:
    Editor();
    explicit Editor(std::unique_ptr<SystemClipboard> clipboard);
    Editor(std::unique_ptr<SystemClipboard> clipboard, std::unique_ptr<FileWatcher> watcher);
    ~Editor();

    // v0.6.2: true si `path` existe y es una carpeta (absoluta o relativa).
    // Las carpetas no se pueden abrir todavia: solo archivos. Delega en
    // FileBrowser (que es quien sabe de paths).
    static bool isDirectory(const std::string& path);

    // Abre (o crea) el archivo indicado en el buffer ACTIVO. Acepta rutas
    // relativas o absolutas (p.ej. /home/usuario/Docs/README.md). Si
    // `path` es una carpeta, no abre nada, no cambia el estado y devuelve
    // false.
    bool loadIntoActiveBuffer(const std::string& path);

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

    // Método público para testing: permite inyectar eventos directamente
    // Solo debe usarse en tests, no en el flujo normal de la aplicación
    void processEventForTesting(const Event& event) {
        handleEvent(event);
    }

    // Puerta GUI (regla InputEvent/CommandMap): ejecuta un comando nombrado
    // SIN sintetizar eventos de teclado ni pasar por el prefijo. Es el único
    // punto de entrada para botones/acciones GUI; handleEvent queda para el
    // teclado (keymap -> InputEvent -> handling -> CommandMap).
    // Nombre desconocido -> no-op robusto (igual que CommandMap::execute).
    void executeCommand(const std::string& name);
    bool hasCommand(const std::string& name) const;

    // Getters para testing
    State getStateForTesting() const { return state_; }
    std::string getGoToLineQueryForTesting() const { return goToLineQuery_; }
    Buffer& getActiveBufferForTesting() { return active(); }

    // Oraculo del boton fisico del mouse: responde si el boton izquierdo
    // sigue presionado. Existe porque soltar FUERA de la ventana del
    // terminal no entrega ningun evento (el release se pierde) y el tick no
    // tiene forma de notarlo solo con eventos. Por defecto siempre dice
    // "presionado" (tests y entornos sin X11); la app real instala el
    // sondeo X11 en main. El tick lo consulta y desarma como un release.
    void setMouseButtonHeldOracle(std::function<bool()> oracle) {
        mouseButtonHeldOracle_ = std::move(oracle);
    }

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
    // Dimensiones del viewport de un buffer, tomadas del tamaño actual
    // (currentRows_/Cols_, actualizado por el último Resize). run() las
    // fija al arrancar para los buffers que ya existen, pero un buffer
    // creado a mitad de sesion (Ctrl+K n) o reiniciado (Ctrl+K w sobre
    // el ultimo) arranca con el Viewport por defecto (24x80) y no
    // redibujaria toda la pantalla si la terminal es mas grande.
    // Este helper le da sus dimensiones reales.
    void syncViewportSize(Buffer& b);
    // Resize autónomo: aplica el payload del evento (sin consultar
    // ningún backend). Payload inválido (filas/cols <= 0) se ignora.
    void handleResize(int rows, int cols);
    // v0.6.3: Ctrl+K w -> cierra el buffer activo.
    //   - modificado: NO cierra; muestra aviso (hay que guardar o restaurar).
    //   - ultimo buffer: no se elimina; se convierte en vacio sin nombre.
    //   - varios buffers: se elimina y se pasa al selector de buffers.
    void closeActiveBuffer();
    // v0.6.3: activa el buffer `idx` y reconcilia el modo con el estado
    // de su seleccion (Seleccion si tiene rango no vacio, si no Navegacion).
    void activateBuffer(int idx);
    // Cambia al buffer anterior (para Ctrl+K b).
    void switchToPreviousBuffer();

private:
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
    void openFileInBuffer(const std::string& path);

    Renderer renderer_;
    // Tamaño actual de la ventana (estado del editor, no del backend).
    // Default 24x80 (igual que el fallback de Terminal sin TTY).
    // Lo actualiza cada EventType::Resize via handleResize(rows, cols):
    // el evento es autónomo (trae su payload) y vale para TTY y GUI
    // sin instalar ningún provider.
    int currentRows_ = 24;
    int currentCols_ = 80;
    // Lectura del tamaño actual (tests: reemplaza al viejo
    // `ed.terminal_.getWindowSize`). No consulta ningún backend.
    void getWindowSize(int& rows, int& cols) const {
        rows = currentRows_;
        cols = currentCols_;
    }
    // Despacho de comandos por nombre. El Editor registra los handlers en
    // el constructor (registerCommands) y los modos resuelven la tecla ->
    // nombre -> handler aqui, en vez de tener cada accion dispersa en
    // bloques de switch.
    CommandMap commands_;

     // Registra bajo nombres los handlers de los comandos de modo (i/s/p/
    // c/x/a/j/k) y de prefijo (n/t/w/o/l). Los CUERPOS quedan registrados
    // como lambdas que capturan este editor.
    void registerCommands();

    State state_ = State::Navegacion;
    bool isDarkTheme_ = true;
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
    // Helpers de portapapeles — solo para testing (acceso via
    // `#define private public` en tests/). En produccion el editor usa
    // `clipboard_` directo; aqui no aparecen usos en ui/*.cpp fuera de
    // sus propias definiciones, lo cual es intencional.
    std::vector<std::string> getClipboardBlock() const;
    void setClipboardBlock(const std::vector<std::string>& block);
    std::string getClipboardText() const;
    bool isClipboardEmpty() const;

    // Indice seleccionado en la pantalla del selector de buffers.
    int bufferSelectorIndex_ = 0;

    // Buffer anterior (para Ctrl+K b: volver al buffer previo).
    // Usamos id estable del Buffer para detectar si fue cerrado.
    struct PreviousBufferInfo {
        bool valid = false;
        int id = -1;
        std::string displayName;
    };
    PreviousBufferInfo previousBuffer_;

    // ---- Explorador de archivos (v0.6.4) ----
    // Estado y navegacion del explorador (ruta actual, entradas, indice y
    // scroll). El Editor no duplica ese estado: solo decide las
    // consecuencias de las acciones del explorador.
    FileBrowser fileBrowser;

    // Ruta escrita por el usuario en el prompt "Guardar archivo:" (modo
    // SaveAs). Relativa o absoluta; se resuelve contra cwd() al confirmar.
    std::string saveAsPath_;

    // ---- Busqueda (v0.8 / feature f) ----
    std::string searchQuery_;
    Position searchOrigin_{0, 0};
    std::optional<Selection> searchHighlight_;
    void startSearch();
    void handleBusquedaEvent(const Event& event);
    void updateSearch();
    void navigateSearch(int dir);
    void updateSearchMessage(bool found, int current, int total);
    std::vector<Position> collectMatches(const std::string& query) const;
    void setSearchHighlight(const Position& pos, int len);
    void clearSearchHighlight();
    void centerViewportOnCursor();

    // ---- Ir a fila (feature g) ----
    std::string goToLineQuery_;
    void startGoToLine();
    void handleIrAFilaEvent(const Event& event);

    // ---- Bracket matching (feature h) ----
    std::optional<BracketPair> bracketPair_;
    enum class BracketJumpTarget { Open, Close };
    BracketJumpTarget nextBracketJump_ = BracketJumpTarget::Open;
    bool bracketJumpPendingPreserve_ = false;
    Position lastBracketCursor_{-1, -1};
    std::uint64_t lastBracketVersion_{UINT64_MAX};
    std::uint64_t lastBracketInstanceId_{0};
    int lastBracketViewportTop_ = -1;
    int lastBracketViewportBottom_ = -1;
    BracketSpanSource makeBracketSpanSource(Buffer& buf, SyntaxLanguage lang);
    BracketSpanSource makeViewportSpanSource(Buffer& buf, SyntaxLanguage lang, int firstLine, int lastLine);
    // Caché propio del path de brackets (siempre en Cpp, ver makeBracketSpanSource).
    // Separado de Buffer::syntaxCache (idioma real del archivo, lo usa el renderer):
    // compartirlo con políticas de idioma distintas lo invalidaba cada frame
    // (thrash Cpp<->None en archivos .md/.txt + reparse 0..cursor).
    SyntaxCache bracketCache_;
    void updateBracketHighlight();
    void refreshBracketAfterJump();

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
    // Click izquierdo en el viewport: si venia de Seleccion cancela el
    // highlight EN EL PRESS (valido o en ~/statusbar) y vuelve a
    // Navegacion; luego arma el gesto (mueve el cursor y guarda
    // dragAnchor_) sin crear rango. El drag posterior crea la nueva
    // seleccion, el release sin drag es no-op.
    void handleMousePress(const Event& event);
    // Arrastre con boton presionado: solo valido si un press previo lo
    // armo (mouseGestureActive_). El primer drag efectivo entra a Seleccion
    // (o descarta el rango previo si ya estaba) con anchor=dragAnchor_;
    // los siguientes extienden selection.position. Incluye autoscroll
    // de 1 linea / 1 celda por evento en bordes.
    void handleMouseDrag(const Event& event);
    // Cierre del gesto: sin drag previo es no-op (el press ya cancelo la
    // seleccion si venia de Seleccion); con drag previo permanece en
    // Seleccion. Siempre desarma el gesto. Se conserva la rama historica
    // pressState_==Seleccion por robustez.
    void handleMouseRelease(const Event& event);
    // Resuelve la posicion de un MouseDrag: dentro del viewport usa
    // screenToCursor(); fuera calcula scroll ±1 + posicion de borde, y si
    // ya esta en el limite (scroll imposible) devuelve la posicion
    // clampada al borde (siempre via byteForColumn + alignStart + clamp).
    // Devuelve nullopt solo si no hay posicion resoluble. Como efecto
    // lateral actualiza la intencion de autoscroll temporal (arriba: la
    // primera fila —contenido— se trata como intencion por decision, ya que
    // el fuera hacia arriba no es reportable; abajo: solo el fuera real en
    // statusbar; el resto apaga); solo util durante el gesto de mouse.
    std::optional<Position> resolveMouseDragPosition(int mouseRow, int mouseCol);
    // Aplica una posicion de drag a cursor+seleccion: primer drag efectivo
    // entra a Seleccion (o reinicia el rango), los siguientes extienden
    // selection.position. Cola compartida de handleMouseDrag y del tick.
    void applyMouseDragPosition(const Position& pos);
    // true si hay autoscroll temporal activo: direccion armada + gesto de
    // mouse en curso + seleccion iniciada + estado valido. Chequeo completo
    // para no dejar estado zombie que despierte el loop.
    bool mouseAutoscrollActive() const;
    // Un paso de autoscroll temporal (como maximo 1 linea, sin ponerse al
    // dia con deuda acumulada): re-ejecuta el ultimo drag fuera y extiende
    // la seleccion; en el limite sostiene el borde. `now` inyectable para
    // tests deterministas. Devuelve true si produjo posicion.
    bool tickMouseAutoscroll(std::chrono::steady_clock::time_point now);
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
    // Scroll de rueda: desplaza exclusivamente el viewport (±3 lineas).
    // No reutiliza navegacion de cursor (MoveUp/PageUp) y no debe llamar
    // a logica de movimiento de cursor; cursor permanece quieto (puede
    // quedar off-screen). Clampeado en [0, maxTop]. Activa
    // suppressScrollToCursor_ para que renderFrame no lo deshaga via
    // scrollToCursor en el siguiente frame.
    void applyScroll(int delta);
    // Salto a extremo durante seleccion total (prefijo 'a' activo): replica
    // la rama de flechas de handleSelectAllEvent (cursor al extremo,
    // seleccion degenerada sin rango, sale del prefijo 'a'). `toEnd` false
    // = BOF, true = EOF. Vive aquí para que los comandos cursor.mover.*
    // sean dueños de la semántica completa por modo (regla InputEvent /
    // CommandMap: el handling solo resuelve el nombre).
    void jumpSelectAllEdge(bool toEnd);
    bool suppressScrollToCursor_ = false;

    // ---- Gesto de seleccion por mouse (press cancela vs drag crea) ----
    // press en Seleccion limpia el highlight de inmediato (valido o en
    // ~/statusbar) y vuelve a Navegacion; luego arma (mueve cursor +
    // guarda anchor). El primer drag efectivo crea la nueva seleccion;
    // release sin drag es no-op y con drag permanece en Seleccion.
    bool mouseGestureActive_ = false;
    bool mouseDragStarted_ = false;
    std::optional<Position> dragAnchor_;
    State pressState_ = State::Navegacion;

    // ---- Autoscroll temporal de seleccion por mouse (solo vertical) ----
    // Intencion, no coordenadas: hacia abajo el mouse sigue fuera del area
    // de edicion (statusbar); hacia arriba no hay fuera reportable, asi que
    // por DECISION la primera fila (contenido) cuenta como intencion, con
    // la consecuencia conocida de que un drag terminado justo ahi scrollea.
    // El loop despierta por timeout y tickMouseAutoscroll() da un paso por
    // intervalo, sin necesidad de mover el mouse.
    enum class MouseAutoscrollDirection { None, Up, Down };
    MouseAutoscrollDirection mouseAutoscrollDirection_ =
        MouseAutoscrollDirection::None;
    // Ultimo drag en zona de scroll (coords 1-based de terminal): fuera
    // real por abajo (statusbar) o primera fila por sustitucion hacia
    // arriba (ver DECISION en el bloque de armado). El tick lo re-ejecuta
    // para dar un paso.
    int mouseAutoscrollRow_ = 0;
    int mouseAutoscrollCol_ = 0;
    // Oraculo del boton fisico (ver setter): por defecto "presionado".
    std::function<bool()> mouseButtonHeldOracle_ = [] { return true; };
    // Instante del ultimo paso (o del armado): el primer tick mueve solo
    // tras el intervalo, sin salto inmediato al salir del viewport.
    std::chrono::steady_clock::time_point mouseAutoscrollLastStep_{};
    static constexpr auto kMouseAutoscrollInterval =
        std::chrono::milliseconds(100);

    // Indenta / desindenta el rango seleccionado actual (todas las lineas
    // que toca). `indent` true tabula hacia adentro ('}'), `indent` false
    // quita un nivel ('{'). Solo opera si hay rango NO vacio (hasSelection);
    // si no, avisa y no cambia nada. Empuja una sola entrada de historial y
    // marca modified. No cambia de modo: la seleccion se conserva.
    void indentSelection(bool indent);

    // Indenta / desindenta la linea actual del cursor en modo Navegacion.
    // `indent` true tabula hacia adentro ('}'), `indent` false quita un nivel
    // ('{'). No requiere seleccion; opera sobre la linea donde esta el cursor.
    // Empuja una entrada de historial y marca modified. No cambia de modo.
    void indentCurrentLine(bool indent);

    // Borra el rango seleccionado actual (si hay texto marcado) y deja el
    // cursor en el INICIO del rango. Empuja historial de undo y marca
    // modified como una edicion (igual que cortar, pero SIN tocar el
    // portapapeles: la seleccion se elimina sin copiarla). No cambia de
    // modo. Devuelve true si borro algo.
    bool deleteSelection();

    void undo();
    void redo();

    void toggleTheme();

    std::unique_ptr<FileWatcher> watcher_;
    std::unordered_set<std::string> watchedFiles_;
    void watchFile(const std::string& path);
    void unwatchFile(const std::string& path);
    void handleFileChange(const FileChangeEvent& ev);
};
