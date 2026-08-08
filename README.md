# edit — editor de texto de terminal (v0.4)

Editor de texto minimalista en C++17, sin interfaz gráfica, pensado
como base extensible.

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
`Editor`, `Selection` e invariantes). El runner imprime cada caso y un
resumen final (`N tests, M failure(s)`); sale con código 0 solo si no
hay fallos.

## Uso

```bash
./edit archivo.txt
```

Si el archivo no existe, se crea uno nuevo en memoria (se guarda con Ctrl+K, Ctrl+S).

### Teclas

| Tecla                | Acción                                        |
|----------------------|-----------------------------------------------|
| Flechas              | Mover el cursor                                |
| Ctrl+S               | Entrar al modo selección                       |
| Ctrl+S + flechas/Home/End | Extender la selección (v0.3: ya no hace falta Shift) |
| Ctrl+K, Ctrl+S       | Guardar (funciona también dentro del modo selección) |
| Ctrl+K, Ctrl+Q       | Salir                                          |
| Ctrl+K, otra tecla   | Cancelar el prefijo y descartar la tecla       |
| Ctrl+U / Ctrl+Y      | Deshacer / Rehacer                             |
| Home / End           | Ir al inicio/fin de la línea                   |
| Backspace            | Borrar carácter anterior                       |
| Delete               | Borrar carácter siguiente                      |
| Cualquier letra      | Insertar carácter                              |
| Esc                  | Salir del modo selección                       |

### Barra de estado (v0.4)

La barra de estado ocupa las dos últimas filas de la terminal:

- **Fila 1 (fija)** en video inverso, a ancho completo: bloque izquierdo
  `nombre - ruta - ESTADO` (`NORMAL` / `SELECCION` / `COMANDO`), con
  `[modificado]` junto al nombre si hay cambios sin guardar, y la ruta
  resuelta siempre a absoluta. Bloque derecho `Linea: N Col: M`, anclado
  a la derecha y nunca truncado. Ante una terminal chica se sacrifica
  primero la ruta (con `...` al inicio) y luego el nombre.
- **Fila 2 (mensajes)**: `statusMessage` (ayudas, avisos de undo/guardado…)
  sin inverso, truncada a su propio ancho, reservada aunque esté vacía.

### Modo selección

Con `Ctrl+S` se activa el modo selección sin necesidad de mantener
pulsado Shift. Dentro de él:

- Las flechas / Home / End extienden la selección.
- Un carácter, espacio o Enter reemplaza el texto seleccionado y sale
  del modo.
- Backspace / Delete borran la selección y salen del modo.
- Esc sale del modo selección sin tocar el texto.
- Ctrl+S se ignora (ya estás en selección).
- Ctrl+K, Ctrl+S guarda el archivo sin salir del modo selección.
- Ctrl+K, Ctrl+Q sale del programa.

## Arquitectura

```
tests/
  test_framework.h  -> micro-framework de tests (CHECK/CHECK_EQ, runner)
  test_main.cpp      -> punto de entrada del runner
  test_document.cpp  -> carga/guardado, insertar, newline, backspace, delete
  test_cursor.cpp    -> movimiento horizontal/vertical, home/end, invariantes
  test_event.cpp     -> Event transporte un solo EventType (sin campo shift)
  test_editor.cpp    -> openFile, undo, redo, guardado y quit
  test_selection.cpp -> seleccion de texto (Paso 1 de v0.2: anchor/position)

src/
  Document.cpp   -> el texto en sí (vector<string>), sin saber nada
                     de cursor, colores, scroll ni selección
  Cursor.cpp      -> línea/columna + "columna preferida" al moverse
                     verticalmente entre líneas de distinto largo
  Selection.h     -> struct Position + Selection (anchor/position). La
                     selección ES del Editor: ni Document ni Cursor la
                     conocen
  Viewport.h      -> qué franja del documento es visible (scroll)
  Terminal.cpp    -> modo raw (termios), lee teclas y las traduce
                     a Event (InsertChar, MoveLeft, Prefix, Select,
                     Backspace, ...). Los modificadores (Shift/Ctrl/Alt)
                     de las secuencias de escape se ignoran
  Renderer.cpp    -> Documento -> Renderer -> Terminal. Dibuja,
                     nunca modifica el documento. Resalta la selección
                     con video inverso
  Editor.cpp      -> engine: conecta Document + Cursor + Viewport +
                     Terminal + Renderer, corre el loop principal. Es
                     el dueño de la selección (ni Document ni Cursor
                     la conocen).
                     Corre el loop principal:

                         mientras siga abierto:
                             leer evento
                             actualizar estado
                             renderizar

  main.cpp        -> punto de entrada, abre el archivo pasado por argv
```

`Event` es la capa que desacopla "tecla física" de "acción lógica":
el `Editor` nunca sabe qué tecla se apretó, solo qué evento ocurrió.
Esto deja la puerta abierta a alimentar el engine desde otro lado
(tests automatizados, macros, otro tipo de input) sin tocar el resto
del código.