#pragma once

#include <memory>
#include <string_view>

#include "platform/InputEvent.h"
#include "platform/IEventSource.h"
#include "platform/tty/ITtyKeymap.h"
#include "platform/tty/TtyKeymap.h"

// Secuencias DECSCUSR de forma del cursor, FUENTE ÚNICA (antes
// hardcodeadas en Terminal.cpp y en el teardown de TtyRunLoop).
// Aptas para write() en signal handlers (puntero + sizeof - 1, sin alloc).
constexpr const char kCursorBlock[] = "\x1b[2 q";    // bloque fijo (raw activo)
constexpr const char kCursorDefault[] = "\x1b[0 q";  // default del emulador

// Estado mínimo compartido con los signal handlers C. Forward declarado
// para no exponer <termios.h>/<signal.h> a los consumidores de este header
// (misma opacidad que origTermios_); la definición vive en
// platform/tty/TtySignalState.h, solo incluida por Terminal.cpp.
struct TtySignalState;

// Encapsula todo lo especifico de la terminal (POSIX/Linux/macOS):
// activar/desactivar el modo "raw", leer teclas crudas, y consultar
// el tamano de la ventana. Nada de esto sabe de Document/Cursor/etc.
//
// Terminal es quien LEE y ENSAMBLA los bytes crudos (distingue un ESC
// suelto de una secuencia, acumula parametros con timeout, arma el
// caracter UTF-8 multibyte), pero NO decide el significado de cada tecla:
// eso vive en el TtyKeymap (remapeable), que Terminal consulta para traducir
// lo leido a un Evento.
class Terminal : public IEventSource {
public:
    Terminal();
    ~Terminal() override;

    // Pone la terminal en modo raw: sin buffer de linea, sin eco,
    // teclas especiales (Ctrl+C, Ctrl+Z, etc) entregadas tal cual.
    void enableRawMode();

    // Restaura la configuracion original de la terminal.
    void disableRawMode();

    // Mouse tracking SGR (?1000h/?1006h): clicks y rueda como eventos.
    // Separado de raw input para testabilidad y para no mezclar
    // responsabilidades (raw vs mouse vs alt-screen).
    void enableMouseTracking();
    void disableMouseTracking();

    // Alternate screen (?1049h): buffer visual propio del editor.
    // No se activa automaticamente con mouse; es modo UI independiente.
    void enterAlternateScreen();
    void leaveAlternateScreen();

    // Bloquea hasta leer una tecla y la traduce a un InputEvent de alto
    // nivel (esta es la unica funcion que "sabe" de teclas). Bloquea
    // indefinidamente.
    InputEvent readEvent();

    // Igual que readEvent(), pero espera a lo sumo `timeoutMs` milisegundos
    // (0 = no bloquea, negativo = indefinido). Devuelve true si se leyo y
    // tradujo una tecla; false si el timeout expiro sin entrada. Es el
    // mecanismo que permite al Editor despertar el ciclo para limpiar un
    // mensaje de accion expirado sin que el usuario aprete ninguna tecla.
    bool readEvent(InputEvent& event, int timeoutMs) override;

    // Tamano actual de la terminal.
    void getWindowSize(int& rows, int& cols);

    bool hasResized();

    // Vuelta de SIGCONT (consumo destructivo, como hasResized): true una
    // sola vez por reanudación. El loop invalida el diff y repinta
    // completo (la pantalla física se perdió aunque no haya resize).
    bool hasResumed();

    // Tabla tecla -> Evento que readEvent() consulta. El usuario puede
    // rebindear las teclas en tiempo de ejecucion
    // (keymapIface().bindSequence(...) / keymapIface().bindControl(...))
    // sin tocar la logica del Editor.
    // Única vía pública: la interfaz TTY (decisión 2). El tipo concreto
    // TtyKeymap es detalle privado de este miembro, nunca se expone.
    ITtyKeymap& keymapIface() { return keymap_; }
    const ITtyKeymap& keymapIface() const { return keymap_; }

    static bool parseMouseSgr(std::string_view seq, InputEvent& e);

    // Observabilidad solo para tests: leen el estado PUBLICADO POR ESTE
    // objeto (su signalState_ miembro), nunca el puntero global. Incluso
    // bajo el supuesto de una sola Terminal viva, el test consulta al
    // objeto que mutó, no a quien el handler ve hoy.
    bool isMouseActiveForTest() const;
    bool isAltActiveForTest() const;
    bool isRawActiveForTest() const;

private:
    bool rawModeEnabled_ = false;
    bool mouseTrackingEnabled_ = false;
    bool altScreenActive_ = false;
    // true si EDIT_DEBUG_KEYS esta definida: vuelca a stderr los bytes
    // crudos de las teclas no reconocidas (util para diagnosticar como
    // emite la terminal las secuencias, p.ej. Shift+Flecha).
    bool debugKeys_ = false;
    void* origTermios_; // puntero opaco a struct termios (evita incluir <termios.h> aqui)

    // Estado mínimo compartido con los signal handlers C. Dueño con
    // lifetime del Terminal (se publica vía puntero crudo solo mientras
    // algún modo está activo). unique_ptr + forward decl: este header no ve
    // <termios.h>/<signal.h>. El handler jamás toca el resto del objeto.
    // El dtor está definido en Terminal.cpp (tipo completo ahí).
    std::unique_ptr<TtySignalState> signalState_;

    // Significado de cada tecla/secuencia. Se consulta en readEvent();
    // remapeable en tiempo de ejecucion via keymapIface().
    TtyKeymap keymap_;

    // Terminal es un recurso unico ligado a la terminal fisica del
    // proceso (modo raw, tamano de la ventana, el mismo STDIN_FILENO).
    // No tiene sentido copiar/mover una instancia: gestiona memoria
    // (new/delete de struct termios), asi que copiarla llevaria a doble
    // delete y comportamiento indefinido. Prohibir copia y movimiento
    // convierte ese bug potencial en un error de compilacion.
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&&) = delete;
    Terminal& operator=(Terminal&&) = delete;
};
