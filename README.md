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

> **AVISO:** `make sanitize` / `make test-sanitize` (build con
> `-fsanitize=address,undefined`) compila la suite **entera** con cada
> objeto sanitizado, lo que tarda muchísimo (varios minutos). Solo correrlo
> si lo pide explícitamente el usuario o lo exige el CI; en el día a día
> basta `make test`.

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
  o `x` la terminan. `a` entra al prefijo de "selección total". `}` y
  `{` tabulan la selección (solo aquí, y solo si hay un rango marcado).

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
| `}` / `{` (en Selección)   | Tabular hacia adentro / quitar tabulación de la selección |
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
  Con varios buffers, el buffer que **hereda la ranura** (misma posición,
  clamp al final si se cerró el último) queda activo de inmediato — cerrar
  no abre el selector. Al cerrar el último buffer, en lugar de eliminarse
  se reinicia a vacío con un nombre nuevo.

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
core/              -> MODELO: estado documental, sin saber nada de UI ni terminal
  Document.h/.cpp    -> el texto en sí (vector<string>), sin saber nada
                        de cursor, colores, scroll ni selección
  Buffer.h/.cpp      -> un buffer: Document + Cursor + viewport + selección +
                        undo/redo + modificado + nombre. Cada buffer es 100%
                        independiente de los demás
  Cursor.h/.cpp      -> línea/columna + "columna preferida" al moverse
                        verticalmente; salto por bloques (j/k) sin partir UTF-8.
                        Trabaja en BYTES (byte-safe)
  BufferManager.h/.cpp-> la colección de buffers + el activo
  Selection.h        -> Selection (anchor/position). La selección es un
                        estado documental del buffer; Document/Cursor no la
                        conocen
  Position.h         -> struct Position (línea/columna)
  Viewport.h         -> qué franja del documento es visible (scroll)
  utf8.h             -> utilitarios UTF-8 byte-safe (columnas, truncado, rango)

ui/                  -> CONTROLADOR + VISTA: el editor y cómo se ve
  Editor.h/.cpp      -> engine: conecta core + Terminal + Renderer, corre el
                        loop principal. Es el dueño del estado de UI global
                        (modo) y del portapapeles (global). Mantiene la
                        colección de Buffer con un activo, el selector de
                        buffers (Ctrl+K t), los comandos Ctrl+K n/w y el
                        explorador de archivos (Ctrl+K o). Define el prefijo 'a'
  EditorState.h      -> enum class State (modos de la máquina de estados).
                        Lo comparte Editor y Renderer; vive aparte para que
                        Renderer NO dependa de Editor.h
  Renderer.h/.cpp    -> Documento -> Renderer -> Terminal. Dibuja, nunca
                        modifica el documento. Resalta la selección con
                        video inverso y posiciona el cursor por COLUMNA
                        visual (no por byte). Solo usa State como etiqueta
  CommandMap.h/.cpp  -> despacho de comandos por nombre (tecla -> comando ->
                        handler), registrado por Editor
  FileBrowser.h/.cpp -> estado y navegación del explorador de archivos
                        (Ctrl+K o). Modal; el Editor decide consecuencias
  main.cpp           -> punto de entrada, abre el archivo pasado por argv

terminal/            -> INPUT: lectura física de teclas y eventos
  Event.h            -> Event + EventType: desacopla "tecla física" de
                        "acción lógica". El Editor nunca sabe qué tecla se
                        apretó, solo qué evento ocurrió
  Terminal.h/.cpp    -> modo raw (termios), lee teclas y las traduce a Event.
                        Los modificadores de las secuencias de escape se ignoran
  Keymap.h/.cpp      -> tabla de datos que traduce teclas crudas a Event.
                        Separada de Terminal para poder reconfigurarse

tests/               -> suite de tests (ver "Probar")
```

La separación en capas (`core` / `ui` / `terminal`) deja la puerta abierta
a reutilizar el mismo modelo desde otro frontend: un futuro `Maestro GUI`
(o una variante de terminal) compartiría `core/` + `ui/Editor` sin arrastrar
`terminal/`, que solo sabe leer teclas físicas. `core/` no incluye nada de
UI ni terminal; `ui/Renderer` depende de `EditorState.h`, no de `Editor.h`.

`Event` es la capa que desacopla "tecla física" de "acción lógica":
el `Editor` nunca sabe qué tecla se apretó, solo qué evento ocurrió.
Esto deja la puerta abierta a alimentar el engine desde otro lado
(tests automatizados, macros, otro tipo de input) sin tocar el resto
del código.

El cursor y la selección trabajan en **bytes** dentro de la línea
(modelo byte-safe): los movimientos (`Left`/`Right`, `j`/`k`, páginas)
nunca aterrizan en medio de una celda; el renderer convierte a columnas
visuales al dibujar.

**Limitación de UTF-8 (ámbitos fuera de alcance).** Decisión de modelo:
Maestro es un editor **binariamente seguro** — `Document` guarda *bytes*
crudos y abrir/guardar hace round-trip exacto sin validar ni re-encodecar,
así que archivos Latin-1, parcialmente corruptos o mezclados se pueden
abrir sin destruir bytes. El UTF-8 es solo una capa de *presentación* (y
de input): navegar, borrar y contar columnas opera sobre **celdas**
byte-safe — una secuencia UTF-8 válida es una celda; un byte inválido
suelto (continuación huérfana, lead inválido) es su propia celda de 1
byte. Sobre esa base se asume que cada celda ocupa **una** columna de
terminal. Eso es correcto para texto occidental (acentos, «—», «€»…),
pero una celda no equivale a una columna en general, y hay dos ejes que
quedan sin soportar:

- **Ancho de celda**: caracteres que la terminal pinta en 2 columnas —
  CJK (`中`) y emojis (`🙂`) — se cuentan como 1, así que se renderizan
  apiñados y no alinean contra el margen derecho. La solución correcta
  (tabla de ancho al estilo `wcwidth`) arrastraría al modelo de columna
  de Cursor/Viewport/selección de todo el editor.
- **Cluster de grafemas**: una unidad visual puede ser varias celdas
  que se combinan — `a` + acento combinante (`á`), secuencias ZWJ
  (`👩👩👧`), variation selectors (`e`+`U+FE0F`). Aquí cada celda se
  cuenta como una columna y un salto de cursor; lo correcto sería
  segmentar por grafemas (UAX #29).

**Persistencia: guardado no atómico (trabajo futuro).** Hoy `saveToFile`
abre el archivo con `std::ofstream(path, std::ios::trunc)` y escribe en
su lugar. Un fallo a mitad de escritura (`SIGKILL`, `abort`, corte de
energía) puede dejar el archivo **truncado a medias** y destruir el
contenido original. Para considerarlo un editor de uso serio conviene el
patrón de guardado atómico:

```
archivo.txt.tmp   <- escribir el contenido nuevo aqui
fsync(archivo.txt.tmp)   <- asegurar que llegó a disco
rename(archivo.txt.tmp → archivo.txt)   <- swap atómico dentro del mismo filesystem
```

`rename` en el mismo filesystem es atómico (o falla si está en otro
montaje, caso a resolver). Pendiente de implementar; solo anotado.

**Rutas: normalización y symlinks (trabajo futuro).** Las rutas se guardan
normalizadas contra `cwd()` y con `.`/`..` resueltos (`foo/../bar` →
`bar`; `filesystem::absolute().lexically_normal()`), para que el chequeo
de duplicados de buffers trate igual rutas que escriben el mismo archivo.
Los **symlinks** NO se resuelven: `lexically_normal()` no los deshace y
`filesystem::canonical()` (que sí) exige que el archivo exista (y también
se abren archivos nuevos). Consecuencia: abrir un archivo por su ruta
real y luego por un symlink (o viceversa) crea dos buffers.

Decisión recomendada para el futuro: **no** reemplazar el `filename` por
`canonical`. En su lugar, separar la *clave de duplicación* de la ruta
mostrada/guardada: seguir almacenando para mostrar/guardar la forma
lexical (`absolute().lexically_normal()`), y usar
`canonical(path)` con fallback (`canonical si existe, sino lexical`) solo
para comparar duplicados. Así `tmp/link` y `/real/path` comparten clave
(un solo buffer) y los archivos nuevos siguen pudiendo abrirse. Tema:
guardar la clave junto a cada buffer o recalcularla al comparar.

**Estado global del editor vs estado por buffer (trabajo futuro).** Hoy el
*modo* (`Editor::state_`) es global, pero parte del estado del modo vive en
cada buffer (`Buffer::selection`, `Buffer::selectAllActive`). Por eso
conmutar de buffer exige reconciliar:

```cpp
activateBuffer() { state_ = buffer.selection ? Seleccion : Navegacion; }
```

Funciona, pero significa que el estado del `Editor` no es independiente del
buffer: si el buffer activo tenía selección, el editor "aparece en modo
Selección", aunque el usuario no haya elegido ese modo.

Diseño más limpio para el futuro: que cada buffer guarde **solo estado
documental** (`Document`, `Cursor`, `Selection`, `Viewport`, history,
`filename`, `modified`) y que el `Editor` guarde **estado de UI global**
(modo actual, prompt/modal activo), de modo que cambiar de buffer NO
reescriba automáticamente el modo global. Es una decisión de diseño real
(¿conviene que el modo persista al cambiar de buffer, o que cada buffer
"recuerde" su propio modo?); la semántica actual es correcta, así que solo
se marca para resolver antes de multiplicar los modos.

**Buffer: encapsulación de campos (trabajo futuro).** Hoy `Buffer` expone
casi todo públicamente (`document`, `cursor`, `viewport`, `filename`,
`modified`, las pilas de undo/redo, ...). Para un proyecto pequeño es muy
práctico, pero cualquier parte puede escribir:

```cpp
buffer.cursor.col = -500;
buffer.modified = false;
```

sin pasar por ninguna regla, y el `Editor` tiene que mantener las
invariantes manualmente.

Diseño a considerar más adelante: pasar los campos a `private` y exponer
solo métodos que garanticen invariantes. NO hacerlo prematuramente: una
refactorización sencilla se convertiría en cientos de cambios por todos
los sitios que hoy tocan los campos directamente. Dejar anotado, no
implementado. Pendiente de resolver cuando el editor crezca lo suficiente
como para que el acceso directo empiece a costar.

**Tests: `#define private public` (trabajo futuro).** Varios tests
(`test_editor`, `test_selection`, `test_modes`, `test_invariants`,
`test_buffers`, `test_filebrowser`, `test_clipboard`) comienzan con:

```cpp
#define private public
#include "Editor.h"
#undef private
```

Esto convierte los campos `private` en públicos durante la compilación,
dejando acceder a los internos dentro del test. No es elegante desde el
punto de vista académico (abusa del preprocesador y rompe el encapsulado
en la unidad de traducción del test), pero para un proyecto pequeño es
muy práctico: permite testar invariantes reales sin meter getters
artificiales en la API solo por los tests.

Alternativa más limpia a futuro, si el proyecto crece: una declaración
`friend` de una clase o suite de test, o mantener getters `internal`
limitados. Mientras tanto se deja anotado, no se cambia: funciona y la
API de producción no se contamina. Pendiente de resolver si el acceso
directo a internos empieza a requerirse desde fuera de los tests.

**Buffer: `modified` / `savedLines` (trabajo futuro).** Hoy `Buffer`
tiene dos campos (`bool modified` y `std::vector<std::string> savedLines`)
con `modified = snapshot() != savedLines`, y se recalcula en `applyState`
(`Buffer.cpp`). Una alternativa aparentemente más elegante sería un par
de revisiones (`revision_ != savedRevision_`, cada mutación `revision_++`,
guardar `savedRevision_ = revision_`), evitando las dos fuentes de verdad.

**Pero un RevisionId puro rompe undo/redo.** La comparación de contenido
es la que detecta "volví exactamente al estado guardado", y eso ocurre en
casos reales:

```cpp
// guardar "ab" (savedRevision = 2)
// escribir "c", borrar "c", ... -> contenido "ab" de nuevo pero revision 6
modified = revision_ != savedRevision_;  // true, INCORRECTO
```

Un contador monótono no sabe cuándo hubo un `savedRevision_ = revision_`
intermedio; comparar `snapshot() != savedLines` sí captura "la última
mutación (incluida una secuencia de undo/redo) me devolvió al guardado".
Además el costo de la comparación se paga solo en `applyState` (undo/redo,
poco frecuente), no por tecla: `modified` sigue siendo un `bool` de lectura
barata en el render. Decisión: mantener `savedLines`; anotado para no
reintroducir el bug. Pendiente de revisar si alguna vez el proyecto
necesita comparar otros snapshots con `savedLines` (entonces sí
convendría extraer una utilidad común). Dejar anotado, no implementado.

**Editor: dos semánticas de "abrir" (trabajo futuro).** `Editor` tiene dos
formas de abrir un archivo con significados distintos:

- `openFile(path)` — público, devuelve `bool`. **Sobrescribe el buffer
  activo** (`Buffer& b = active(); b.filename = ...`). Lo usa `main.cpp`
  para el archivo inicial (`edit file.txt`) y muchos tests como setup, pero
  semánticamente "reemplaza el documento actual" aunque el nombre sugiera
  abrir de forma general.
- `openFileToBuffer(path)` — privado. **Crea/activa un buffer nuevo**. Solo
  lo usa el FileBrowser (`Editor.cpp`).

Ambos son razonables, pero la API es confusa: "abrir" significa dos cosas
según el método. La revisión del FileBrowser (`Editor.cpp:318`) usa el
segundo camino; el comando inicial (`main.cpp`) usa el primero.

Discusión de nombres (anotado, no implementado): renombrar
`openFile()` hacia "reemplaza el buffer actual" y `openFileToBuffer()`
hacia "crea un buffer nuevo". Nombres candidatos más explícitos:
`loadIntoCurrentBuffer()` / `openInNewBuffer()`; u `openInitialFile()` /
`openFileInBuffer()`. Nota: `openInitialFile` sería engañoso porque los
~52 usos de los tests tratan `openFile()` como setup ("abrir y editar"),
no como abrir solo el primer archivo. Renombrar toca ~53 sitios en 7
archivos de tests + `main` + el Editor interno, así que se deja anotado
mientras no aporte más que claridad. Pendiente si la semántica llega a
diferenciarse funcionalmente (p. ej. preguntar antes de sobrescribir un
buffer modificado).