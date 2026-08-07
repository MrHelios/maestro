# edit — editor de texto de terminal (v0.2)

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

Si el archivo no existe, se crea uno nuevo en memoria (se guarda al usar Ctrl+S).

### Teclas

| Tecla          | Acción                     |
|----------------|-----------------------------|
| Flechas        | Mover el cursor              |
| Shift+Flechas  | Seleccionar texto            |
| Home / End     | Ir al inicio/fin de la línea |
| Backspace      | Borrar carácter anterior     |
| Delete         | Borrar carácter siguiente    |
| Cualquier letra| Insertar carácter            |
| Ctrl+S         | Guardar                      |
| Ctrl+Q         | Salir                        |

## Arquitectura

```
tests/
  test_framework.h  -> micro-framework de tests (CHECK/CHECK_EQ, runner)
  test_main.cpp      -> punto de entrada del runner
  test_document.cpp  -> carga/guardado, insertar, newline, backspace, delete
  test_cursor.cpp    -> movimiento horizontal/vertical, home/end, invariantes
  test_event.cpp     -> Event transporte el estado de Shift (Move + shift=true)
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
                     a Event (InsertChar, MoveLeft, Backspace, ...)
                     incl. Shift+Flecha -> event.shift = true
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