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
- `mouseCol/mouseRow` 1-based según SGR, keycodes crudos, bytes de control,
  contenidos de secuencias ESC, `pollfd`/`fd()` = transporte del backend
  (TTY). Nunca suben al vocabulario común.

### Deuda conocida (no bloquea; parche futuro)

`InputEvent.mouseCol/mouseRow` (`int`, documentados como "1-based de
terminal", es decir formato SGR) todavía exponen transporte TTY. La
representación neutral ya existe y es `CellPos`
(`cellPos()`/`setCellPos()`): los backends deben traducir su formato físico
a ella. En un parche futuro `CellPos` debe quedar como única
representación de celda y los `int` SGR deben eliminarse de `InputEvent`.

## 3. Keymaps por backend, no compartidos

- `platform/tty/ITtyKeymap.h` = interfaz TTY-only
  (`byte de control / secuencia ESC -> InputEventType`). `TtyKeymap` la
  implementa; `Terminal` la expone.
- La futura `GuiKeymap` (`keycode + modifiers -> InputEventType`) tendrá su
  propia interfaz: nunca reutiliza la TTY. Ambas emiten el mismo `InputEvent`.
- `platform/` no depende de `platform/tty/` (`platform/IKeymap.h` fue
  eliminada por introducir exactamente esa dependencia).

## 4. Anti-patrones prohibidos

- Sintetizar `Prefix` + letra desde GUI para invocar comandos.
- Exponer en `InputEvent` bytes crudos, keycodes, secuencias ESC, fds o
  structs de `termios`.
- Resolver `InputEvent -> acción` fuera del *semantic handling* del Editor
  (los keymaps solo traducen; no conocen modos ni estado).
