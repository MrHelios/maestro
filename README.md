# edit — editor de texto de terminal (v0.6.4)

Editor de texto minimalista en C++17, sin interfaz gráfica, pensado
como base extensible. Usa un modelo *modal* (Navegación / Interacción /
Selección) estilo editor clásico.

## Compilar

```bash
make
```

Esto genera un binario `edit` (requiere Linux/macOS — usa `terminos`,
que es POSIX; en Windows habría que adaptar `Terminal.cpp`).

### Probar

```bash
make test
```

Compila y ejecuta la suite de tests en `tests/` (`Document`, `Cursor`,
`Editor`, `Selection`, multi-buffer, UTF-8, terminal e invariantes). El
runner imprime
cada caso y un resumen final (`N tests, M failure(s)`); sale con código 0
solo si no hay fallos.

## Uso

```bash
./edit archivo.txt
./edit /home/usuario/Docs/README.md
```

Acepta rutas relativas o absolutas. Si el archivo no existe, se crea
uno nuevo en memoria (se guarda con Ctrl+K, Ctrl+S). Las carpetas no
se pueden abrir todavía: el programa sale con error.

### Modos

El editor es modal. En cada modo las teclas significan cosas distintas:

- **Navegación** (por defecto): no se escribe. Las letras `i`, `s`, `a`,
  `j`/`k`, `c`/`x`, `p` son comandos. Las flechas/Home/End/RePag/AvPag
  mueven el cursor sin seleccionar.
- **Interacción** (con `i`): edición libre; todo carácter se inserta.
  `Esc` vuelve a Navegación.
- **Selección** (con `s`): las flechas, `j`/`k` y RePag/AvPag extienden la
  selección (el *anchor* se fija al entrar y nunca cambia). `Esc`, `c`
  o `x` la terminan. `a` entra al prefijo de "selección total".

### Teclas

| Tecla                      | Acción                                        |
|----------------------------|-----------------------------------------------|
| `i` (en Naveg.)            | Entrar a Interacción (edición)                |
| `s` (en Naveg.)            | Entrar a Selección                            |
| Flechas / Home / End       | Mover el cursor                               |
| RePag / AvPag              | Desplazar una página (cursor + viewport)      |
| `j` / `k` (Naveg./Sel.)    | Mover por *bloques*: previo / siguiente palabra |
| `a` (en Selección)         | Prefijo de selección total (todo el archivo)  |
| `c` / `x` (en Selección)   | Copiar / cortar la selección                  |
| `p` (en Naveg.)            | Pegar el contenido del buffer                 |
| Ctrl+U / Ctrl+Y            | Deshacer / Rehacer                            |
| Ctrl+K, Ctrl+S             | Guardar. Si el buffer no tiene nombre, abre el prompt *Guardar archivo:* |
| Ctrl+K, Ctrl+Q             | Salir                                         |
| Ctrl+K, `n`                | Buffer nuevo sin nombre (y lo activa)         |
| Ctrl+K, `o`                | Explorador de archivos (↑/↓, Enter, Esc)      |
| Ctrl+K, `t`                | Selector de buffers (↑/↓, Enter, Esc)         |
| Ctrl+K, `w`                | Cerrar el buffer activo                       |
| Ctrl+K, otra tecla         | Cancelar el prefijo y descartar la tecla      |
| Backspace / Delete         | Borrar carácter (en Interacción)              |
| Esc                        | Salir de Interacción / cancelar selección     |

#### Prefijo `a` (selección total)

`a` dentro de Selección es un comportamiento temporal (no un modo nuevo):
selecciona el **archivo entero** sin mover el cursor. Mientras está
activo:

- `a` de nuevo → vuelve a la selección anterior (toggle).
- Flecha `→`/`↓` → cursor al EOF; `←`/`↑` → cursor al BOF (termina el
  prefijo y la selección).
- `c` / `x` → copian / cortan todo el archivo.
- Esc → cancela.
- Cualquier otra tecla → no hace nada.

### Barra de estado

La barra de estado ocupa las dos últimas filas de la terminal:

- **Fila 1 (fija)** en video inverso, a ancho completo: bloque izquierdo
  `nombre - ruta - ESTADO` (`NAVEGACION` / `INTERACCION` / `SELECCION` /
  `COMANDO` / `BUFFERS` / `GUARDAR` / `ABRIR`), con `[modificado]` junto al nombre si hay cambios
  sin guardar, y la ruta resuelta siempre a absoluta. Bloque derecho
  `Linea: N Col: M`, anclado a la derecha y nunca truncado. Ante una
  terminal chica se sacrifica primero la ruta (con `...` al inicio) y
  luego el nombre.
- **Fila 2 (mensajes)**: `statusMessage` (ayudas, avisos de undo/guardado…)
  sin inverso, truncada a su propio ancho, reservada aunque esté vacía.

### Modo selección

Con `s` en Navegación se activa el modo selección. La selección es un
rango `anchor` → `position` del que es dueño el Editor (ni `Document` ni
`Cursor` la conocen). Dentro de Selección:

- Flechas / Home / End, `j`/`k` y RePag/AvPag extienden o encogen la
  selección; el **anchor permanece fijo** y solo se mueve el extremo.
- `c` copia el rango al buffer y vuelve a Navegación.
- `x` corta (copia y borra) y vuelve a Navegación.
- `a` activa la selección total (ver arriba).
- Esc sale del modo selección sin tocar el texto.
- El contenido se resalta en video inverso (soporta UTF-8).

### Multi-buffer (Ctrl+K)

El editor trabaja sobre una colección de buffers; exactamente uno está
activo. Cada buffer tiene su propio `Document`, `Cursor` (incluida la
columna preferida), `viewport`, selección, historial undo/redo, flag de
modificado y nombre:

- `Ctrl+K n`: crea un buffer sin nombre y lo activa inmediatamente. Los
  nombres genéricos son `SinNombre`, `SinNombre1`, `SinNombre2`… con un
  contador de sesión que no reutiliza nombres.
- Guardar un buffer **sin nombre** (`Ctrl+K Ctrl+S`): en lugar de fallar,
  se abre el prompt *Guardar archivo:* en la fila de mensajes. Se escribe
  la ruta destino (relativa o absoluta), `Enter` guarda y `Esc` cancela.
  Una vez guardado, el buffer conserva ese nombre y `Ctrl+K Ctrl+S` persiste
  normal.
- `Ctrl+K t`: abre el selector de buffers (lista en video inverso).
  `↑`/`↓` navegan, `Enter` activa el buffer bajo el cursor, `Esc` vuelve
  al buffer y modo anterior sin cambiar nada.
- `Ctrl+K w`: cierra el buffer activo. Si está modificado, **se bloquea**
  con un mensaje (`Buffer modificado: guarda con Ctrl+K s o restaura.`).
  Al cerrar el último buffer, en lugar de eliminarse se reinicia a vacío
  con un nombre nuevo.

El portapapeles (`c`/`x`/`p`) es global a todos los buffers. El selector
es de solo lectura: no modifica ningún buffer.

### Explorador de archivos (Ctrl+K `o`)

`Ctrl+K o` abre un explorador modal que arranca en el directorio de
trabajo actual (`getcwd()`). Se navega como en el selector de buffers:

- `↑`/`↓` se mueven por la lista (con scroll si no cabe en pantalla).
- `Enter` sobre una **carpeta** entra en ella; sobre `..` sube un nivel.
- `Enter` sobre un **archivo** lo abre: agrega un buffer nuevo y, si el
  archivo ya está abierto, activa el buffer existente (no duplica).
- `Esc` (o `Ctrl+K`) cancela y vuelve al modo anterior sin tocar nada.

Las carpetas se listan primero (con `/` al final) y luego los archivos,
ambos en orden alfabético case-insensitive; se muestran también los
ocultos. En la raíz (`/`) no aparece `..`. Como es modal, otras teclas
dentro (incl. Ctrl+K) no filtran a los buffers.

## Arquitectura

```
tests/
  test_framework.h     -> micro-framework de tests (CHECK/CHECK_EQ, runner)
  test_main.cpp         -> punto de entrada del runner
  test_document.cpp     -> carga/guardado, insertar, newline, backspace, delete
  test_cursor.cpp       -> movimiento horizontal/vertical, home/end, j/k, invariantes
  test_event.cpp        -> Event transporte un solo EventType (sin campo shift)
  test_editor.cpp       -> openFile, undo, redo, guardado y quit
  test_terminal_event.cpp -> traducción de bytes/secuencias de escape a Event
  test_selection.cpp    -> selección (anchor/position), c/x, prefijo 'a', j/k, páginas
  test_modes.cpp        -> máquina de estados (Navegación/Interacción/Selección/Prefix)
  test_invariants.cpp   -> invariantes de estado tras una secuencia determinista
  test_buffers.cpp      -> multi-buffer: aislamiento, Ctrl+K n/t/w, selector, clipboard
  test_filebrowser.cpp  -> explorador Ctrl+K o: navegación, carpetas, open, raíz, scroll
  test_utf8*.cpp        -> utilitarios UTF-8 (columnas, truncado, rango)

src/
  Buffer.cpp      -> un buffer: Document + Cursor + viewport + selección +
                     undo/redo + modificado + nombre. Cada buffer es 100%
                     independiente de los demás
  Document.cpp    -> el texto en sí (vector<string>), sin saber nada
                     de cursor, colores, scroll ni selección
  Cursor.cpp      -> línea/columna + "columna preferida" al moverse
                     verticalmente; salto por bloques (j/k) sin partir UTF-8
  Cursor.h        -> declaraciones; el cursor trabaja en BYTES (utf-8)
  Selection.h     -> struct Position + Selection (anchor/position). La
                     selección ES del Editor: ni Document ni Cursor la
                     conocen
  Viewport.h      -> qué franja del documento es visible (scroll)
  Terminal.cpp    -> modo raw (termios), lee teclas y las traduce
                     a Event (InsertChar, MoveLeft, PageUp, ...). Los
                     modificadores (Shift/Ctrl/Alt) de las secuencias de
                     escape se ignoran
  Renderer.cpp    -> Documento -> Renderer -> Terminal. Dibuja, nunca
                     modifica el documento. Resalta la selección con
                     video inverso y posiciona el cursor por COLUMNA
                     visual (no por byte)
  Editor.cpp      -> engine: conecta Document + Cursor + Viewport +
                     Terminal + Renderer, corre el loop principal. Es
                     el dueño de la selección y del portapapeles
                     (global). Mantiene la colección de `Buffer` con un
                     activo, el selector de buffers (Ctrl+K t), los
                     comandos Ctrl+K n/w y el explorador de archivos
                     (Ctrl+K o). Define los modos y el prefijo 'a'.

                     Loop principal:
                         leer evento
                         actualizar estado (applyPage, selección, ...)
                         renderizar
  main.cpp        -> punto de entrada, abre el archivo pasado por argv
```

`Event` es la capa que desacopla "tecla física" de "acción lógica":
el `Editor` nunca sabe qué tecla se apretó, solo qué evento ocurrió.
Esto deja la puerta abierta a alimentar el engine desde otro lado
(tests automatizados, macros, otro tipo de input) sin tocar el resto
del código.

El cursor y la selección trabajan en **bytes** dentro de la línea
(modelo UTF-8): los movimientos (`Left`/`Right`, `j`/`k`, páginas)
nunca aterrizan en medio de un carácter multibyte; el renderer convierte
a columnas visuales al dibujar.