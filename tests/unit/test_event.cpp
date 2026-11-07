#include "test_framework.h"

#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

#include "platform/InputEvent.h"
#include "platform/tty/ITtyKeymap.h"
#include "platform/MouseEvent.h"
#include "platform/tty/TtyKeymap.h"
#include "platform/tty/Terminal.h"
#include "app/CommandMap.h"

// Definir lo privado como publico para realizar test
#define private public
#include "app/Editor.h"
#undef private

namespace {

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

static InputEvent inputcmdChar(const std::string& text) {
    InputEvent e;
    
    e.type = InputEventType::InsertChar;
    e.text = text;
    return e;
}

static InputEvent inputcmdKey(InputEventType type) {
    InputEvent e;
    
    e.type = type;
    return e;
}

static std::string inputcmdFileContent(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

} // namespace

TEST(inputcmd_default_state) {
    InputEvent e;
    CHECK(e.type == InputEventType::None);
    CHECK(e.text.empty());
}

TEST(inputcmd_mousepress) {
    InputEvent e;

    e.type = InputEventType::MousePress;
    e.cell = CellPos{12, 7};
    CHECK_EQ(e.cell.col, 12);
    CHECK_EQ(e.cell.row, 7);
}

TEST(inputcmd_resize) {
    InputEvent r;

    r.type = InputEventType::Resize;
    r.resizeRows = 30;
    r.resizeCols = 100;
    CHECK_EQ(r.resizeRows, 30);
    CHECK_EQ(r.resizeCols, 100);
}

TEST(inputcmd_insertchar) {
    InputEvent t;

    t.type = InputEventType::InsertChar;
    t.text = "ñ";
    CHECK(t.text == "ñ");
}

// Enumeradores que la refactorización debe conservar
TEST(inputcmd_header) {
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
    (void)InputEventType::SaveAs;
    (void)InputEventType::Escape;
    (void)InputEventType::ScrollUp;
    (void)InputEventType::ScrollDown;
    (void)InputEventType::MousePress;
    (void)InputEventType::MouseDrag;
    (void)InputEventType::MouseRelease;
    (void)InputEventType::Resize;
}

// Handler de CommandMap es void() sin args: 
// el payload NO puede ir por CommandMap; 
// por diseño viaja en el InputEvent (texto/coords/resize).
TEST(inputcmd_handler_has_no_args) {
    static_assert(std::is_same<CommandMap::Handler, std::function<void()>>::value,
                  "CommandMap::Handler debe seguir siendo void()");
}

// establecemos el bindControl 18 para PageDown
TEST(inputcmd_ittykeymap_implemented_by_ttykeymap) {
    TtyKeymap km;
    ITtyKeymap& iface = km;   // compila solo si hereda de la interfaz

    iface.bindControl(18, InputEventType::PageDown);
    auto c = iface.control(18);
    CHECK(c.has_value());
    CHECK(*c == InputEventType::PageDown);
}

// establecemos la secuencia [9~ para PageUp
TEST(inputcmd_ittykeymap_sequence_binding) {
    TtyKeymap km;
    ITtyKeymap& iface = km;

    iface.bindSequence("[9~", InputEventType::PageUp);
    auto s = iface.sequence("[9~");
    CHECK(s.has_value());
    CHECK(*s == InputEventType::PageUp);
}

// Ctrl+Q: binding por defecto
TEST(inputcmd_terminal_exposes_keymap_with_defaults) {
    Terminal term;
    ITtyKeymap& ti = term.keymapIface();
    CHECK(ti.control(17).has_value());
}

// Verifica que existen todos los comandos
TEST(inputcmd_commandmap_existing_catalog) {
    Editor ed;
    auto nombres = ed.commands_.registeredNames();

    // Debe haber comandos registrados en producción
    CHECK(!nombres.empty());

    for (const auto& name : nombres) {
        CHECK(ed.commands_.has(name));
    }
}

// Verificamos que si ejecuta un comando que no existe no chashea
TEST(inputcmd_no_existe) {
    Editor ed;
    std::string cmd_no_existe = "no.existe.tal.comando";
    
    CHECK(!ed.commands_.has(cmd_no_existe));
    ed.commands_.execute(cmd_no_existe);
    CHECK(true);
}

// Verificamos que existan cada uno de los comando requeridos para la GUI.
TEST(inputcmd_commandmap_complete_coverage) {
    Editor ed;

    // Los nombres vienen de produccion (Editor::requiredCommandNames), no
    // hardcodeados aca. Se verifican todos con un for.
    const auto& nombres = Editor::requiredCommandNames();
    CHECK(!nombres.empty());
    for (const auto& name : nombres) {
        if (!ed.commands_.has(name)) {
            std::cout << "  [MISSING] cobertura incompleta, falta: " << name << "\n";
            CHECK(false);
        }
        CHECK(ed.commands_.has(name));
    }
}

// Creamos un buffer nuevo via comando
TEST(inputcmd_buffers_new_via_command) {
    // Vía comando directo (lo que hará el botón GUI).
    Editor viaCmd;
    const int before = viaCmd.buffers.count();
    
    viaCmd.commands_.execute("buffer.nuevo");
    CHECK_EQ(viaCmd.buffers.count(), before + 1);
    CHECK(viaCmd.getStateForTesting() == State::Navegacion);
}

// Creamos un buffer nuevo via InputEvent
TEST(inputcmd_buffers_new_via_inputevent) {
    Editor viaKeys;
    InputEvent prefix, n;
    const int before = viaKeys.buffers.count();
    
    prefix.type = InputEventType::Prefix;
    viaKeys.processEventForTesting(prefix);
    CHECK(viaKeys.getStateForTesting() == State::Prefix);
    
    n.type = InputEventType::InsertChar;
    n.text = "n";
    viaKeys.processEventForTesting(n);
    CHECK_EQ(viaKeys.buffers.count(), before + 1);
    CHECK(viaKeys.getStateForTesting() == State::Navegacion);
}

// Es el único punto de entrada GUI (handleEvent queda para teclado).
TEST(inputcmd_editor_public_command_api) {
    constexpr bool kHasExec = has_executeCommand<Editor>::value;
    constexpr bool kHasHas = has_hasCommand<Editor>::value;
    if (!kHasExec || !kHasHas) {
        std::cout << "  [PENDING] falta API publica Editor::executeCommand "
                     "/hasCommand (puerta GUI sin fingir teclado)\n";
        CHECK(false);
    }
    CHECK(kHasExec);
    CHECK(kHasHas);
    
    Editor ed;
    checkPublicCommandApi(ed);
}
 
// el comando de movimiento es dueño de la semántica completa
// (begin + mover + update en Seleccion). 
// TTY (evento) y GUI (comando directo)
TEST(inputcmd_movement_parity_seleccion) {
    Editor viaKeys, viaCmd;
    
    // Camino TTY: 's' + MoveRight vía eventos.
    viaKeys.getActiveBufferForTesting().document.restore({"hello world"});
    viaKeys.processEventForTesting(inputcmdChar("s"));
    CHECK(viaKeys.getStateForTesting() == State::Seleccion);
    viaKeys.processEventForTesting(inputcmdKey(InputEventType::MoveRight));
    viaKeys.processEventForTesting(inputcmdKey(InputEventType::MoveDown));

    // Camino GUI: 's' vía evento + comandos directos (botón GUI).
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
    
    CHECK(viaCmd.hasSelection());
    CHECK(viaKeys.hasSelection());
}

// Selección total ('a'):
// la flecha salta al extremo y sale
// TTY (evento) y GUI (comando directo)
TEST(inputcmd_movement_parity_selectall) {
    Editor viaKeys, viaCmd;
    
    // Camino TTY: 's' + 'a' + MoveRight vía eventos.
    viaKeys.getActiveBufferForTesting().document.restore({"aaa", "bb"});
    viaKeys.processEventForTesting(inputcmdChar("s"));
    viaKeys.processEventForTesting(inputcmdChar("a"));
    CHECK(viaKeys.getActiveBufferForTesting().selectAllActive);
    viaKeys.processEventForTesting(inputcmdKey(InputEventType::MoveRight));

    // Camino GUI: 's' + 'a' vía eventos + comando directo.
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

// Creamos un archivo temporal con contenido y lo modificamos y guardamos
TEST(inputcmd_cmd_buffer_guardar_named_behavior) {
    testfw::TempFile tf;
    Editor ed;
    
    tf.write("hola");
    CHECK(ed.loadIntoActiveBuffer(tf.path));
    CHECK(!ed.getActiveBufferForTesting().modified);
    
    // Editar: 'i' (a Interaccion) + 'X' al inicio.
    ed.processEventForTesting(inputcmdChar("i"));
    ed.processEventForTesting(inputcmdChar("X"));
    CHECK(ed.getActiveBufferForTesting().modified);
    ed.processEventForTesting(inputcmdKey(InputEventType::Escape));
    CHECK(ed.hasCommand("buffer.guardar"));
    ed.executeCommand("buffer.guardar");
    CHECK(!ed.getActiveBufferForTesting().modified);
    
    CHECK_EQ(inputcmdFileContent(tf.path), "Xhola");
    CHECK(ed.getStateForTesting() == State::Navegacion);
}

// Para un archivos nuevo, se ejecuta saveAs para guardar.
TEST(inputcmd_cmd_buffer_guardar_unnamed_prompt) {
    Editor ed;
    
    CHECK(ed.getActiveBufferForTesting().filename.empty());
    ed.executeCommand("buffer.guardar");
    CHECK(ed.getStateForTesting() == State::SaveAsFileBrowser);
}

// Guardar un archivo con saveas
TEST(inputcmd_cmd_buffer_guardar_como_prompt) {
    testfw::TempFile tf;
    Editor ed;
    
    tf.write("hola");
    CHECK(ed.loadIntoActiveBuffer(tf.path));
    
    ed.executeCommand("buffer.guardar.como");
    CHECK(ed.getStateForTesting() == State::SaveAsFileBrowser);
    CHECK_EQ(inputcmdFileContent(tf.path), "hola");
}

// Salir sin modificados: salida segura
TEST(inputcmd_cmd_app_salir_safe) {    
    Editor ed;
    
    CHECK(ed.hasCommand("app.salir"));
    CHECK(ed.hasCommand("app.salir.forzado"));
    ed.executeCommand("app.salir");
    CHECK(!ed.running_);
}

// Salir con modificados: forzar cierre
TEST(inputcmd_cmd_app_salir_safe_and_force) {
    Editor ed;
    
    ed.processEventForTesting(inputcmdChar("i"));
    ed.processEventForTesting(inputcmdChar("X"));
    ed.processEventForTesting(inputcmdKey(InputEventType::Escape));
    CHECK(ed.getActiveBufferForTesting().modified);
    ed.executeCommand("app.salir");
    CHECK(ed.running_);
    CHECK(ed.getStateForTesting() == State::Navegacion);
    ed.executeCommand("app.salir.forzado");
    CHECK(!ed.running_);
}

// executeCommand("cursor.mover.derecha") 
// en Seleccion es intención semántica (begin + mover + update), 
// idéntico al evento de teclado.
TEST(inputcmd_cmd_cursor_mover_gui_seleccion) {
    Editor viaKeys, viaGui;
    
    // Camino TTY: 's' + MoveRight vía eventos.
    viaKeys.getActiveBufferForTesting().document.restore({"hello world"});
    viaKeys.processEventForTesting(inputcmdChar("s"));
    viaKeys.processEventForTesting(inputcmdKey(InputEventType::MoveRight));

    // Camino GUI: 's' vía evento + API pública (botón GUI).
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
    else CHECK(false);
}