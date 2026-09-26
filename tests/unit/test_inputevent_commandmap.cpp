// Contrato InputEvent / CommandMap (decisiones 1-3).
//
// Solo tests: no toca src/. Compila contra el código ACTUAL y fija el
// contrato FUTURO que el src debe cumplir:
//
//  1. `Event`/`EventType` -> `InputEvent`/`InputEventType`
//     (cabecera canónica `platform/InputEvent.h`; `Event.h` es alias).
//  2. Interfaz TTY-only `platform/tty/ITtyKeymap.h`. `TtyKeymap` la
//     implementa y `Terminal` la expone; la GUI futura tendrá su propio
//     keymap sin reutilizarla. `platform/IKeymap.h` fue ELIMINADO:
//     platform/ no depende de platform/tty/.
//  3. Cobertura COMPLETA de CommandMap + bypass GUI:
//     teclado: keymap -> InputEvent -> handling -> CommandMap -> Editor
//     botón GUI: CommandMap -> Editor (sin sintetizar Prefix/teclas).
//
// Convención de este fichero:
//  - Lo que ya existe se verifica con CHECK (rojo si regresa).
//  - Lo futuro se detecta con __has_include / SFINAE: si falta, el test
//    falla con mensaje explícito (rojo hasta que src lo implemente).
//  - La semántica GUI se fija vía `commands_` (truco private/public ya
//    usado en tests/interaction/test_support.h) para no bloquearse hasta
//    que exista `Editor::executeCommand` público.
//  - Eventos con payload (InsertChar.text, MousePress.coords,
//    Resize.rows/cols) NO van a CommandMap por diseño: Handler es void()
//    sin args; viajan por handleEvent. Solo intenciones sin parámetros
//    son comandos.

#include "test_framework.h"

#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

#include "platform/InputEvent.h"
#include "platform/Event.h"  // alias Event/EventType (código en transición)
#include "platform/tty/ITtyKeymap.h"

#include "platform/MouseEvent.h"
#include "platform/tty/Keymap.h"
#include "platform/tty/TtyKeymap.h"
#include "platform/tty/Terminal.h"
#include "app/CommandMap.h"

// Truco establecido en el repo (tests/interaction/test_support.h):
// expone commands_/state_/buffers_ solo en esta TU para fijar contrato.
#define private public
#include "app/Editor.h"
#undef private

// --- Detección SFINAE de la futura API pública de comandos ---
template <typename T, typename = void>
struct has_executeCommand : std::false_type {};
template <typename T>
struct has_executeCommand<
    T, std::void_t<decltype(std::declval<T&>().executeCommand(
           std::declval<const std::string&>()))>> : std::true_type {};

template <typename T, typename = void>
struct has_hasCommand : std::false_type {};
template <typename T>
struct has_hasCommand<
    T, std::void_t<decltype(std::declval<const T&>().hasCommand(
           std::declval<const std::string&>()))>> : std::true_type {};

// Catálogo actual: 23 nombres registrados en Editor::registerCommands.
// Candado anti-regresión: ninguno puede desaparecer con la refactorización.
static const std::vector<std::string> kExistingCommands = {
    "navegacion.interaccion", "navegacion.seleccion", "navegacion.pegar",
    "navegacion.palabra.atras", "navegacion.palabra.adelante",
    "seleccion.total", "navegacion.ir_a_fila", "seleccion.j", "seleccion.k",
    "seleccion.copiar", "seleccion.cortar", "seleccion.indentar",
    "seleccion.desindentar", "navegacion.indentar", "navegacion.desindentar",
    "buffer.nuevo", "buffer.selector", "buffer.cerrar", "buffer.abrir",
    "buffer.anterior", "navegacion.buscar", "theme.toggle", "bracket.jump",
};

// Cobertura completa (decisión 3): intenciones sin parámetros que hoy son
// llamadas directas y deben promoverse a comandos para que la GUI pueda
// invocarlas sin fingir teclado. Nombres propuestos; si src elige otros,
// actualizar esta lista (el test documenta el contrato, no lo esconde).
static const std::vector<std::string> kRequiredNewCommands = {
    "edicion.deshacer", "edicion.rehacer", "buffer.guardar",
    "buffer.guardar.como", "app.salir", "app.salir.forzado",
    "cursor.mover.izquierda", "cursor.mover.derecha", "cursor.mover.arriba",
    "cursor.mover.abajo", "cursor.mover.inicio", "cursor.mover.fin",
    "cursor.pagina.arriba", "cursor.pagina.abajo", "vista.scroll.arriba",
    "vista.scroll.abajo",
};

// 1. Vocabulario canónico platform/InputEvent.h (decisión 1).
// Event/EventType son alias transitorios del mismo tipo.
TEST(inputcmd_inputevent_header) {
    // El vocabulario semántico se preserva bajo el nuevo nombre.
    InputEvent e;
    e.type = InputEventType::None;
    CHECK(e.type == InputEventType::None);
    CHECK(e.text.empty());
    e.type = InputEventType::MousePress;
    e.setCellPos(CellPos{12, 7});
    CHECK_EQ(e.mouseCol, 12);
    CHECK_EQ(e.mouseRow, 7);
    InputEvent r;
    r.type = InputEventType::Resize;
    r.resizeRows = 30;
    r.resizeCols = 100;
    CHECK_EQ(r.resizeRows, 30);
    CHECK_EQ(r.resizeCols, 100);
    // Enumeradores que la refactorización debe conservar (ni uno menos).
    (void)InputEventType::InsertChar;
    (void)InputEventType::InsertNewline;
    (void)InputEventType::MoveLeft;
    (void)InputEventType::MoveRight;
    (void)InputEventType::MoveUp;
    (void)InputEventType::MoveDown;
    (void)InputEventType::MoveHome;
    (void)InputEventType::MoveEnd;
    (void)InputEventType::PageUp;
    (void)InputEventType::PageDown;
    (void)InputEventType::Backspace;
    (void)InputEventType::Delete;
    (void)InputEventType::Undo;
    (void)InputEventType::Redo;
    (void)InputEventType::Quit;
    (void)InputEventType::Prefix;
    (void)InputEventType::Save;
    (void)InputEventType::Escape;
    (void)InputEventType::ScrollUp;
    (void)InputEventType::ScrollDown;
    (void)InputEventType::MousePress;
    (void)InputEventType::MouseDrag;
    (void)InputEventType::MouseRelease;
    (void)InputEventType::Resize;
    // Alias transitorios: mismo tipo, no copia.
    static_assert(std::is_same<Event, InputEvent>::value, "Event debe ser alias de InputEvent");
    static_assert(std::is_same<EventType, InputEventType>::value, "EventType debe ser alias");
}

// 1b. Forma del vocabulario actual (pasa hoy; debe seguir pasando tras el
// rename, solo cambia el nombre). Fija que el payload viaja en el evento.
TEST(inputcmd_inputevent_shape) {
    Event e;
    CHECK(e.type == EventType::None);
    CHECK(e.text.empty());
    // Handler de CommandMap es void() sin args: el payload NO puede ir por
    // CommandMap; por diseño viaja en el InputEvent (texto/coords/resize).
    static_assert(std::is_same<CommandMap::Handler, std::function<void()>>::value,
                  "CommandMap::Handler debe seguir siendo void()");
    Event t;
    t.type = EventType::InsertChar;
    t.text = "ñ";
    CHECK(t.text == "ñ");
    Event m;
    m.type = EventType::MousePress;
    m.mouseCol = 4;
    m.mouseRow = 2;
    CHECK_EQ(m.mouseCol, 4);
    CHECK_EQ(m.mouseRow, 2);
}

// 2. Interfaz TTY-only platform/tty/ITtyKeymap.h (decisión 2).
// platform/ NO depende de platform/tty/: IKeymap.h fue eliminado.
TEST(inputcmd_ittykeymap_header) {
    // TtyKeymap implementa la interfaz TTY y Terminal la expone.
    TtyKeymap km;
    ITtyKeymap& iface = km;
    iface.bindControl(18, EventType::PageDown);
    auto t = iface.control(18);
    CHECK(t.has_value());
    iface.bindSequence("[9~", EventType::PageUp);
    CHECK(iface.sequence("[9~").has_value());
    Terminal term;
    ITtyKeymap& ti = term.keymapIface();
    CHECK(ti.control(17).has_value());
    (void)ti;
}

// 3a. Candado: los 23 comandos actuales no pueden perderse.
TEST(inputcmd_commandmap_existing_catalog) {
    Editor ed;
    for (const auto& name : kExistingCommands) {
        if (!ed.commands_.has(name)) {
            std::cout << "  [MISSING] comando actual perdido: " << name << "\n";
        }
        CHECK(ed.commands_.has(name));
    }
    // Desconocido -> no-op robusto (no tira).
    ed.commands_.execute("no.existe.tal.comando");
    CHECK(true);
}

// 3b. Cobertura completa: todo lo sin-parámetros debe ser comando.
// Hoy falla (los 16 faltan); src debe registrarlos. La GUI los usará vía
// executeCommand sin sintetizar Prefix.
TEST(inputcmd_commandmap_complete_coverage) {
    Editor ed;
    for (const auto& name : kRequiredNewCommands) {
        if (!ed.commands_.has(name)) {
            std::cout << "  [MISSING] cobertura incompleta, falta: " << name << "\n";
        }
        CHECK(ed.commands_.has(name));
    }
}

// 3c. Bypass GUI: `commands_.execute("buffer.nuevo")` equivale al camino
// teclado Prefix+n, sin necesidad de eventos Prefix. Pasa hoy y debe
// seguir pasando cuando exista Editor::executeCommand público.
TEST(inputcmd_gui_bypass_parity) {
    // Vía comando directo (lo que hará el botón GUI).
    Editor viaCmd;
    const int before = viaCmd.buffers.count();
    viaCmd.commands_.execute("buffer.nuevo");
    CHECK_EQ(viaCmd.buffers.count(), before + 1);
    CHECK(viaCmd.getStateForTesting() == State::Navegacion);
    CHECK(viaCmd.getStateForTesting() != State::Prefix);

    // Vía teclado TTY clásico: Prefix + 'n'.
    Editor viaKeys;
    const int beforeK = viaKeys.buffers.count();
    Event prefix;
    prefix.type = EventType::Prefix;
    viaKeys.processEventForTesting(prefix);
    CHECK(viaKeys.getStateForTesting() == State::Prefix);
    Event n;
    n.type = EventType::InsertChar;
    n.text = "n";
    viaKeys.processEventForTesting(n);
    CHECK_EQ(viaKeys.buffers.count(), beforeK + 1);
    CHECK(viaKeys.getStateForTesting() == State::Navegacion);

    // Paridad: mismo efecto final por ambas vías.
    CHECK_EQ(viaCmd.buffers.count(), viaKeys.buffers.count());
}

// Helper template: la rama descartada de if constexpr solo se descarta si
// la condición depende de un parámetro template (en función no-template
// ambas ramas deben compilar aunque el método futuro no exista).
template <typename E>
void checkPublicCommandApi(E& ed) {
    if constexpr (has_executeCommand<E>::value && has_hasCommand<E>::value) {
        CHECK(ed.hasCommand("buffer.nuevo"));
        CHECK(!ed.hasCommand("no.existe.tal.comando"));
        const int before = ed.buffers.count();
        ed.executeCommand("buffer.nuevo");
        CHECK_EQ(ed.buffers.count(), before + 1);
        CHECK(ed.getStateForTesting() != State::Prefix);
        // Desconocido -> no-op, sin cambiar estado ni tirar.
        const State st = ed.getStateForTesting();
        ed.executeCommand("no.existe.tal.comando");
        CHECK(ed.getStateForTesting() == st);
    }
}

// 3d. API pública para la GUI: Editor::executeCommand/hasCommand.
// Es el único punto de entrada GUI (handleEvent queda para teclado).
TEST(inputcmd_editor_public_command_api) {
    constexpr bool kHasExec = has_executeCommand<Editor>::value;
    constexpr bool kHasHas = has_hasCommand<Editor>::value;
    if (!kHasExec || !kHasHas) {
        std::cout << "  [PENDING] falta API publica Editor::executeCommand "
                     "/hasCommand (puerta GUI sin fingir teclado)\n";
    }
    CHECK(kHasExec);
    CHECK(kHasHas);
    Editor ed;
    checkPublicCommandApi(ed);
}

static Event inputcmdChar(const std::string& text) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = text;
    return e;
}

static Event inputcmdKey(EventType type) {
    Event e;
    e.type = type;
    return e;
}

// Punto crítico: el comando de movimiento es dueño de la semántica completa
// (begin + mover + update en Seleccion). TTY (evento) y GUI (comando directo)
// deben producir cursor + selección + estado idénticos.
TEST(inputcmd_movement_parity_seleccion) {
    // Camino TTY: 's' + MoveRight vía eventos.
    Editor viaKeys;
    viaKeys.getActiveBufferForTesting().document.restore({"hello world"});
    viaKeys.processEventForTesting(inputcmdChar("s"));
    CHECK(viaKeys.getStateForTesting() == State::Seleccion);
    viaKeys.processEventForTesting(inputcmdKey(EventType::MoveRight));
    viaKeys.processEventForTesting(inputcmdKey(EventType::MoveDown));

    // Camino GUI: 's' vía evento + comandos directos (botón GUI).
    Editor viaCmd;
    viaCmd.getActiveBufferForTesting().document.restore({"hello world"});
    viaCmd.processEventForTesting(inputcmdChar("s"));
    CHECK(viaCmd.getStateForTesting() == State::Seleccion);
    viaCmd.commands_.execute("cursor.mover.derecha");
    viaCmd.commands_.execute("cursor.mover.abajo");

    // Paridad total: estado, cursor y selección normalizada.
    CHECK(viaCmd.getStateForTesting() == viaKeys.getStateForTesting());
    CHECK_EQ(viaCmd.getActiveBufferForTesting().cursor.line,
             viaKeys.getActiveBufferForTesting().cursor.line);
    CHECK_EQ(viaCmd.getActiveBufferForTesting().cursor.col,
             viaKeys.getActiveBufferForTesting().cursor.col);
    CHECK(viaCmd.hasSelection() == viaKeys.hasSelection());
    auto selCmd = viaCmd.selection();
    auto selKeys = viaKeys.selection();
    CHECK(selCmd.has_value() == selKeys.has_value());
    if (selCmd && selKeys) {
        CHECK_EQ(selCmd->start.line, selKeys->start.line);
        CHECK_EQ(selCmd->start.col, selKeys->start.col);
        CHECK_EQ(selCmd->end.line, selKeys->end.line);
        CHECK_EQ(selCmd->end.col, selKeys->end.col);
    }
    // La selección realmente se extendió (no fue un mover pelado).
    CHECK(viaCmd.hasSelection());
}

// Punto crítico en selección total ('a'): la flecha salta al extremo y sale
// del prefijo 'a' tanto por evento como por comando directo.
TEST(inputcmd_movement_parity_selectall) {
    // Camino TTY: 's' + 'a' + MoveRight vía eventos.
    Editor viaKeys;
    viaKeys.getActiveBufferForTesting().document.restore({"aaa", "bb"});
    viaKeys.processEventForTesting(inputcmdChar("s"));
    viaKeys.processEventForTesting(inputcmdChar("a"));
    CHECK(viaKeys.getActiveBufferForTesting().selectAllActive);
    viaKeys.processEventForTesting(inputcmdKey(EventType::MoveRight));

    // Camino GUI: 's' + 'a' vía eventos + comando directo.
    Editor viaCmd;
    viaCmd.getActiveBufferForTesting().document.restore({"aaa", "bb"});
    viaCmd.processEventForTesting(inputcmdChar("s"));
    viaCmd.processEventForTesting(inputcmdChar("a"));
    CHECK(viaCmd.getActiveBufferForTesting().selectAllActive);
    viaCmd.commands_.execute("cursor.mover.derecha");

    // Paridad: salto a EOF, sin 'a' activo, en Seleccion, sin rango.
    for (Editor* ed : {&viaKeys, &viaCmd}) {
        CHECK_EQ(ed->getActiveBufferForTesting().cursor.line, 1);
        CHECK_EQ(ed->getActiveBufferForTesting().cursor.col, 2);
        CHECK(!ed->getActiveBufferForTesting().selectAllActive);
        CHECK(ed->getStateForTesting() == State::Seleccion);
        CHECK(!ed->hasSelection());
    }
}

static std::string inputcmdFileContent(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

// Comportamiento real (no solo registro) de buffer.nuevo por API pública:
// crea y activa un buffer vacío sin tocar los existentes.
TEST(inputcmd_cmd_buffer_nuevo_behavior) {
    Editor ed;
    ed.getActiveBufferForTesting().document.restore({"quedate"});
    const int before = ed.buffers.count();
    CHECK(ed.hasCommand("buffer.nuevo"));
    ed.executeCommand("buffer.nuevo");
    CHECK_EQ(ed.buffers.count(), before + 1);
    CHECK(ed.getStateForTesting() == State::Navegacion);
    // El nuevo quedó activo y vacío; el anterior intacto.
    CHECK_EQ(ed.getActiveBufferForTesting().document.lineAt(0), "");
    CHECK(ed.getActiveBufferForTesting().filename.empty());
    ed.executeCommand("buffer.nuevo");
    CHECK_EQ(ed.buffers.count(), before + 2);
}

// buffer.guardar con nombre por API pública: persiste, limpia modified y
// no altera el modo (nunca pasa por Prefix).
TEST(inputcmd_cmd_buffer_guardar_named_behavior) {
    testfw::TempFile tf;
    tf.write("hola");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(tf.path));
    CHECK(!ed.getActiveBufferForTesting().modified);
    // Editar: 'i' (a Interaccion) + 'X' al inicio.
    ed.processEventForTesting(inputcmdChar("i"));
    ed.processEventForTesting(inputcmdChar("X"));
    CHECK(ed.getActiveBufferForTesting().modified);
    ed.processEventForTesting(inputcmdKey(EventType::Escape));
    CHECK(ed.hasCommand("buffer.guardar"));
    ed.executeCommand("buffer.guardar");
    CHECK(!ed.getActiveBufferForTesting().modified);
    CHECK_EQ(inputcmdFileContent(tf.path), "Xhola");
    CHECK(ed.getStateForTesting() == State::Navegacion);
    CHECK(ed.getStateForTesting() != State::Prefix);
    CHECK(ed.getStateForTesting() != State::SaveAs);
}

// buffer.guardar sin nombre por API pública: abre el prompt SaveAs en vez
// de fallar con "Archivo sin nombre".
TEST(inputcmd_cmd_buffer_guardar_unnamed_prompt) {
    Editor ed;  // SinNombre, sin ruta
    CHECK(ed.getActiveBufferForTesting().filename.empty());
    ed.executeCommand("buffer.guardar");
    CHECK(ed.getStateForTesting() == State::SaveAs);
}

// buffer.guardar.como por API pública: siempre abre el prompt, incluso con
// nombre (equivale a Ctrl+K Ctrl+S del teclado).
TEST(inputcmd_cmd_buffer_guardar_como_prompt) {
    testfw::TempFile tf;
    tf.write("hola");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(tf.path));
    ed.executeCommand("buffer.guardar.como");
    CHECK(ed.getStateForTesting() == State::SaveAs);
    // No guardó nada de más: el archivo sigue intacto.
    CHECK_EQ(inputcmdFileContent(tf.path), "hola");
}

// app.salir / app.salir.forzado por API pública: la salida segura bloquea
// con modificados, la forzada nunca bloquea.
TEST(inputcmd_cmd_app_salir_safe_and_forced) {
    // Sin modificados: salida segura cierra.
    {
        Editor ed;
        CHECK(ed.hasCommand("app.salir"));
        CHECK(ed.hasCommand("app.salir.forzado"));
        ed.executeCommand("app.salir");
        CHECK(!ed.running_);
    }
    // Con modificados: segura bloquea, forzada cierra.
    {
        Editor ed;
        ed.processEventForTesting(inputcmdChar("i"));
        ed.processEventForTesting(inputcmdChar("X"));
        ed.processEventForTesting(inputcmdKey(EventType::Escape));
        CHECK(ed.getActiveBufferForTesting().modified);
        ed.executeCommand("app.salir");
        CHECK(ed.running_);
        CHECK(ed.getStateForTesting() == State::Navegacion);
        ed.executeCommand("app.salir.forzado");
        CHECK(!ed.running_);
    }
}

// Decisivo: executeCommand("cursor.mover.derecha") en Seleccion es intención
// semántica (begin + mover + update), idéntico al evento de teclado. Si el
// comando fuera la primitiva pelada, cursor y selección divergerían aquí.
TEST(inputcmd_cmd_cursor_mover_gui_seleccion) {
    // Camino TTY: 's' + MoveRight vía eventos.
    Editor viaKeys;
    viaKeys.getActiveBufferForTesting().document.restore({"hello world"});
    viaKeys.processEventForTesting(inputcmdChar("s"));
    viaKeys.processEventForTesting(inputcmdKey(EventType::MoveRight));

    // Camino GUI: 's' vía evento + API pública (botón GUI).
    Editor viaGui;
    viaGui.getActiveBufferForTesting().document.restore({"hello world"});
    viaGui.processEventForTesting(inputcmdChar("s"));
    CHECK(viaGui.hasCommand("cursor.mover.derecha"));
    viaGui.executeCommand("cursor.mover.derecha");

    // Idénticos: el comando extendió la selección, no movió pelado.
    CHECK(viaGui.getStateForTesting() == viaKeys.getStateForTesting());
    CHECK(viaGui.getStateForTesting() == State::Seleccion);
    CHECK_EQ(viaGui.getActiveBufferForTesting().cursor.line,
             viaKeys.getActiveBufferForTesting().cursor.line);
    CHECK_EQ(viaGui.getActiveBufferForTesting().cursor.col,
             viaKeys.getActiveBufferForTesting().cursor.col);
    CHECK_EQ(viaGui.getActiveBufferForTesting().cursor.col, 1);
    CHECK(viaGui.hasSelection());
    CHECK(viaGui.hasSelection() == viaKeys.hasSelection());
    auto selGui = viaGui.selection();
    auto selKeys = viaKeys.selection();
    CHECK(selGui.has_value() && selKeys.has_value());
    if (selGui && selKeys) {
        CHECK_EQ(selGui->start.line, 0);
        CHECK_EQ(selGui->start.col, 0);
        CHECK_EQ(selGui->end.line, selKeys->end.line);
        CHECK_EQ(selGui->end.col, selKeys->end.col);
    }
}
