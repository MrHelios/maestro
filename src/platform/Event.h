#pragma once

#include <string>

#include "platform/CellPos.h"

// En vez de razonar en terminos de "teclas", el Engine razona en
// terminos de EVENTOS. Esto hace que Editor sea completamente
// reutilizable: podria alimentarse desde un teclado real, desde un
// test automatizado, desde una macro grabada, etc.
// Diseño: Event transporta un único EventType, sin campo "shift".
// Desde v0.5 la selección se activa con la letra 's' en modo Navegación
// (un InsertChar que el Editor interpreta), no con un evento propio ni
// con modificador Shift. No existe EventType::Select; Prefix (Ctrl+K) y
// Save (Ctrl+S) modelan el guardado con prefijo.
enum class EventType {
    None,
    InsertChar,
    InsertNewline,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    MoveHome,
    MoveEnd,
    // RePag / AvPag (Page Up / Page Down): el viewport se desplaza una
    // pagina y el cursor la misma cantidad, conservando su posicion
    // relativa dentro del viewport. Trabajan en Navegacion, Interaccion
    // (movimiento libre) y Seleccion (extienden la seleccion como una
    // flecha). Se ignoran durante el prefijo 'a'.
    PageUp,
    PageDown,
    Backspace,
    Delete,
    Undo,
    Redo,
    Quit,
    // Ctrl+K: entra en "modo prefijo"; el siguiente evento decide
    // (Ctrl+S guarda, Ctrl+Q sale, cualquier otra cosa lo cancela).
    Prefix,
    // Ctrl+S: guardar. Solo tiene efecto tras el prefijo (Ctrl+K); fuera
    // de el se ignora. La entrada a seleccion ya NO es por Ctrl+S: desde
    // v0.5 se hace con la letra 's' dentro del modo Navegacion.
    Save,
    // ESC suelto (no seguido de una secuencia de flecha/Home/...).
    // Tipico uso: cancelar la seleccion activa o salir de Interaccion.
    Escape,
    // Rueda del mouse. Mueve exclusivamente el viewport (±3 lineas) sin
    // cambiar la posicion del cursor. No reutiliza navegacion de cursor
    // (MoveUp/Down/PageUp) y debe mantener cursor quieto. Clampeado en
    // BOF/EOF (top en [0, maxTop]), no configurable por ahora. Valido en
    // Navegacion/Interaccion/Seleccion (solo viewport, sin extender
    // seleccion); en FileBrowser/BufferSelector mueve indice y en
    // Busqueda se ignora (no rompe el modo).
    ScrollUp,
    ScrollDown,
    // Click izquierdo (press SGR `...M`). Fase 1: solo existe el press del
    // boton izquierdo; release (`...m`), medio/derecho, drag y motion se
    // traducen a None. Las coordenadas van en mouseRow/mouseCol (1-based
    // de terminal), nunca en `text`.
    MousePress,
    // Arrastre con boton izquierdo (SGR Cb=32 + 'M', con ?1002h). Solo se
    // emite mientras un press previo lo armo (mouseGestureActive_); el Editor
    // lo usa para iniciar/extender la seleccion (1 linea / 1 celda por
    // evento, con autoscroll en bordes). Coordenadas igual que MousePress.
    MouseDrag,
    // Release del boton izquierdo (SGR Cb=3 + 'm'). Cierra el gesto:
    // sin drag previo aplica la conducta de click simple; con drag previo
    // permanece en Seleccion. No cambia el modo por si solo.
    MouseRelease,
    // Resize de ventana (SIGWINCH traducido por el loop TTY, o tamaño
    // enviado por la GUI). Payload AUTÓNOMO en resizeRows/resizeCols:
    // el Editor lo aplica tal cual (handleResize(rows, cols)) sin
    // consultar ningún backend ni provider.
    Resize,
};

struct Event {
    EventType type = EventType::None;
    // Texto a insertar (solo relevante para InsertChar). Guarda los
    // BYTES UTF-8 de un solo caracter: un ASCII (1 byte) o un caracter
    // multibyte (2-4 bytes, p.ej. "á", "ñ", "—", "😀"). Asi el Editor
    // recibee el caracter completo, no byte por byte.
    std::string text;
    // Coordenadas 1-based de terminal, solo validas si type == MousePress.
    // (columna, fila) tal como las emite SGR (Cx, Cy).
    int mouseCol = 0;
    int mouseRow = 0;
    // Tamaño nuevo, solo válido si type == Resize.
    int resizeCols = 0;
    int resizeRows = 0;
    // Vista común (Frontier 1): misma coordenada que mouseCol/Row.
    CellPos cellPos() const { return CellPos{mouseCol, mouseRow}; }
    void setCellPos(CellPos p) { mouseCol = p.col; mouseRow = p.row; }
    // Nota: desde v0.3 no hay campo "shift". La seleccion no depende del
    // modificador Shift (que cada terminal emite de forma distinta); la
    // entrada a seleccion se hace con la letra 's' en modo Navegacion.
};
