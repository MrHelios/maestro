# Maestro — Reglas de arquitectura

## 1. Separación `InputEvent` / `CommandMap`

Son dos niveles distintos y no deben colapsarse en una abstracción genérica.

- **`InputEvent`** (`src/platform/InputEvent.h`) = representación semántica
  de entrada ("el usuario hizo click en esta celda", "mover a la derecha").
  Lo producen los keymaps; lo consume el *semantic handling* del Editor.
- ***Semantic handling*** (`Editor::handleEvent` por modo) = traducción
  `InputEvent + State -> nombre de comando`.
- **`CommandMap`** (`src/app/CommandMap.h`) = traducción de
  intención/comando a acción de aplicación (`nombre -> handler`). No sabe
  nada de teclas, secuencias ni prefijos.

Flujo común (teclado):

```text
                   ┌── TTY Keymap ──┐
keyboard ──────────┤                ├── InputEvent ──┐
                   └── GUI Keymap ──┘                │
                                                     ↓
                                              semantic handling
                                                     ↓
                                                CommandMap
                                                     ↓
                                                   Editor
```

Atajo GUI (sin fingir teclado): un botón "Nuevo buffer" llama directo a
`Editor::executeCommand("buffer.nuevo")`, sin fabricar artificialmente
`KeyEvent(Prefix)` + `KeyEvent(...)`:

```text
GUI button
   ↓
CommandMap
   ↓
Editor
```

## 2. Neutralidad de backend de `InputEvent`

**Regla:** los backends convierten su representación física a la
representación semántica de `InputEvent`. `InputEvent` no debe exponer
formatos de transporte propios de TTY, GUI, SGR, keycodes, etc.

- `InputEvent` = "el usuario hizo click en esta celda" (común).
- Bytes de control, contenidos de secuencias ESC, keycodes crudos,
  `pollfd`/`fd()` = transporte del backend (TTY). Nunca suben al
  vocabulario común. Las coordenadas viajan como `CellPos` 0-based: en
  entrada el decoder TTY (`decodeMouseSgr` en `platform/tty/TtyMouse.h`)
  convierte SGR 1-based restando 1 (único lugar que conoce el offset SGR
  en entrada); en salida `FrameBuilder` resta 1 al producir `CellPos` y
  `TtyEncoder` suma 1 al emitir CUP 1-based ANSI.

### Deuda conocida (no bloquea; parche futuro)

POSIX transport leak — tolerated during migration: `FileWatcher::fd()`
(`int`, `-1` si no hay nada que sondear) expone transporte POSIX en la
interfaz común, igual que `SystemClipboard::fd()`. Es la misma clase de
fuga que los antiguos `int` SGR de `InputEvent` (ya eliminados), y contrasta con `IEventSource`,
que deliberadamente no expone `fd`. El camino neutral ya existe
(`pollEvents()`); `fd()` lo consume solo el loop TTY
(`TtyRunLoop`, vía `ppoll`). No bloquea FASE B; en un parche futuro el
loop debe depender solo de `pollEvents()` (o de una abstracción de
wake-up neutral) y `fd()` debe salir de la interfaz común.

## 3. Keymaps por backend, no compartidos

- `platform/tty/ITtyKeymap.h` = interfaz TTY-only
  (`byte de control / secuencia ESC -> InputEventType`). `TtyKeymap` la
  implementa; `Terminal` la expone.
- La futura `GuiKeymap` (`keycode + modifiers -> InputEventType`) tendrá su
  propia interfaz: nunca reutiliza la TTY. Ambas emiten el mismo `InputEvent`.
- `platform/` no depende de `platform/tty/` (`platform/IKeymap.h` fue
  eliminada por introducir exactamente esa dependencia).

## 4. `assert` como contrato: no compilar con NDEBUG

La app no está prevista para compilar con `-DNDEBUG`: los `assert` son
precondiciones necesarias (`Editor`, `BufferManager`, `sink()`, etc.) y
NDEBUG los desactiva. A futuro se pueden reemplazar por una guarda
activa; hoy simplemente no se usa NDEBUG.

## 5. Anti-patrones prohibidos

- Sintetizar `Prefix` + letra desde GUI para invocar comandos.
- Exponer en `InputEvent` bytes crudos, keycodes, secuencias ESC, fds o
  structs de `termios`.
- Resolver `InputEvent -> acción` fuera del *semantic handling* del Editor
  (los keymaps solo traducen; no conocen modos ni estado).

## 6. Frontera `app/` / `rendering/` (tipos puros, traducción en `app/`)

**Regla:** ningún header de `rendering/` incluye nada de `app/`.
`Frame` (`rendering/frame/Frame.h`), `ChromeData`/`MessageBarData`
(`rendering/ChromeData.h`), `ChromeRequest` (`rendering/ChromeRequest.h`),
`StatusBarData` y `StyleRole` (`rendering/Style.h`) más `MessageKind`
(`rendering/MessageKind.h`) son zona pura: ni `expiry` temporal ni `State`
del Editor viajan en ellos, ni siquiera como parámetros (los puertos
`ScreenRenderer`, `FrameBuilder`, `TtyDiff` y `TtyRenderer` reciben
`ChromeRequest`/`MessageBarData` ya resueltos).

El `app::Message` (texto + tipo + `expiry`) y el `State` se traducen en
`app/ChromePresentation.h`, punto común de traducción:

- `toMessageBar`: `Message -> MessageBarData` (solo texto + tipo; el `expiry`
  nunca entra al rendering porque no es estado visual).
- `makeChromeRequest`: `(Message, State) -> ChromeRequest` (mensaje +
  etiqueta + acento + forma del cursor + visibilidad por modo). `app/`
  (hoy: `Editor::renderFrame`) la invoca antes de llamar al rendering.

Los tests que ejercitan el rendering directamente arman el `ChromeRequest`
con `makeChromeRequest` (pueden incluir `app/`: son tests, no la frontera).
