# edit — editor de texto de terminal (v0.1)

Editor de texto minimalista en C++17, sin interfaz gráfica, pensado
como base extensible.

## Compilar

```bash
make
```

Esto genera un binario `edit` (requiere Linux/macOS — usa `termios`,
que es POSIX; en Windows habría que adaptar `Terminal.cpp`).

### Probar

```bash
make test
```

Compila y ejecuta la suite de tests en `tests/` (`Document`, `Cursor` y
`Editor`). El runner imprime cada caso y un resumen final
(`N tests, M failure(s)`); sale con código 0 solo si no hay fallos.

## Uso

```bash
./edit archivo.txt
```

Si el archivo no existe, se crea uno nuevo en memoria (se guarda al usar Ctrl+S).

### Teclas

| Tecla          | Acción                     |
|----------------|-----------------------------|
| Flechas        | Mover el cursor              |
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
  test_editor.cpp    -> openFile, undo, redo, guardado y quit

src/
  Document.cpp   -> el texto en sí (vector<string>), sin saber nada
                     de cursor, colores, scroll ni selección
  Cursor.cpp      -> línea/columna + "columna preferida" al moverse
                     verticalmente entre líneas de distinto largo
  Viewport.h      -> qué franja del documento es visible (scroll)
  Terminal.cpp    -> modo raw (termios), lee teclas y las traduce
                     a Event (InsertChar, MoveLeft, Backspace, ...)
  Renderer.cpp    -> Documento -> Renderer -> Terminal. Dibuja,
                     nunca modifica el documento.
  Editor.cpp      -> engine: conecta Document + Cursor + Viewport +
                     Terminal + Renderer, corre el loop principal:

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