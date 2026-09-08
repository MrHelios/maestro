# Maestro

Editor de texto modal, liviano — pensado para usarse con o sin entorno gráfico en Linux/Ubuntu X11. 
Mayoritariamente *vibecode* con supervisión del creador.

> **La idea del editor es que tenga la mayor cantidad de funcionalidades posibles pero que a su vez sea lo más liviana posible.**

## Estado actual

- **Versión:** `v0.9` (alpha)
- **Estado:** en desarrollo activo
- **Plataforma:** Linux/Ubuntu X11

## Cambios: v0.9

- se corrigieron todos los test: documetnacion, nombres y Bugs
- hay muchos test que glitchean feo cuando se los ejecuta (problema menor)
- falta optimizar utf-8 (critico)
- entender mejor como se renderiza, para proponer mejoras (ha mejorado pero aun no es top)
- agregar el comando g: para ir a determinada fila (solo util en navegacion)
- poder hacer sangria o quitar sangria en navegacion , solo afectaria a fila (mas practico)
- cuando inicias el editor muestra un mensaje para los comando. 
- los mensajes no deben tener colores
- mediante el comando: contro+k l, se puede alternar tema oscuro y tema claro
- control+k b, no funciona como se espera, hay que revisar
- se optimizo parcialmente el metodo de guardado de archivos.
- hay dos carpetas de testing, una en raiz y otra en test, remover la que esta en raiz y verificar que todo los test funcionan
- nuevo comando: control+k b y vuelve al anterior archivo (si es que se abrio uno nuevo)
- cursor: cuando estas en modo navegacion, debe permanecer fijo, pero si estas en modo interaccion, debe parpadear.
- cuando usas comando para abrir un nuevo archivo, te debe poner donde en la direccion del actual archivo. actualmente se utiliza la direccion donde se ejecuta el editor.

## Características

- Editor modal (Navegación / Interacción / Selección / Prefijo / BufferSelector / SaveAs / FileBrowser / Búsqueda)
- Multi-buffer 
- File browser
- Búsqueda incremental (`Busqueda`)
- Undo/Redo
- UTF-8 byte-safe
- Clipboard X11

## Compilar

```bash
make
```

Genera `build/maestro`. Requiere `g++` C++17, `libX11` y `pthread`.

## Ejecutar

```bash
./build/maestro
./build/maestro archivo.txt
./build/maestro /ruta/absoluta/archivo.txt
```

Si el archivo no existe se crea en memoria (guardar con `Ctrl+K s`). Las carpetas no se abren como archivo.
Wrapper opcional `./maestro` (si existe en la raíz) delega en `make`/`build/maestro`.

## Instalación

Instalación local sin privilegios de administrador (no modifica archivos fuera del home del usuario):

```bash
make install
```

* Compila Maestro si es necesario.
* Crea `~/.local/bin` si no existe.
* Instala el ejecutable en `~/.local/bin/maestro` con permisos de ejecución.
* No requiere `sudo` ni escribe en `/usr/bin` o `/usr/local/bin`.

Ejecución tras instalar (agregar `~/.local/bin` al `PATH` si se desea invocarlo como `maestro`):

```bash
~/.local/bin/maestro
~/.local/bin/maestro archivo.txt
```

## Desinstalación

```bash
make uninstall
```

Elimina únicamente `~/.local/bin/maestro` y conserva `~/.local/bin` para no afectar otros binarios del usuario.

## Tests

```bash
make test            # suite completa
make test-sanitize   # con ASan+UBSan (lento, varios minutos)
```

Runner imprime cada caso y resumen `N tests, M failure(s)`; exit 0 solo si todo pasa.

## Modos

| Modo                  | Entrada                       | Descripción
---------------------------------------------------------------------------------------------------------------------------------------------
| **Navegación**        | por defecto                   | No se escribe. Comandos `i`/`s`/`j`/`k`/`c`/`x`/`p`. Flechas mueven sin seleccionar
| **Interacción**       | `i` en Navegación             | Edición libre, todo inserta. `Esc` vuelve a Navegación
| **Selección**         | `s` en Navegación             | Flechas/`j`/`k`/RePag/AvPag extienden selección (anchor fijo). `c`/`x`/`Esc` salen
| **Prefijo**           | `Ctrl+K`                      | Siguiente tecla decide: `s` guardar, `Ctrl+S` guardar en otra ubicación, `q` salir verificando guardado, `Ctrl+Q` salir forzado, `n`/`t`/`w`/`o` buffers/archivos
| **BufferSelector**    | `Ctrl+K t`                    | Lista modal de buffers (`↑`/`↓`, `Enter`, `Esc`)
| **SaveAs**            | `Ctrl+K s` sin nombre / `Ctrl+K Ctrl+S` | Prompt `Guardar archivo:` (`Enter` guarda, `Esc` cancela; en `Ctrl+K Ctrl+S` la ruta inicial es editable)
| **FileBrowser**       | `Ctrl+K o`                    | Explorador modal (`↑`/`↓`, `Enter` carpeta/archivo, `Esc`); arranca en el directorio del archivo activo o en el directorio de trabajo si el buffer no tiene ruta
| **Búsqueda**          | `Ctrl+K f` / `/`              | Búsqueda incremental, `Enter`/`Esc`

## Atajos principales

| Tecla                     | Acción
--------------------------------------------------------------------
| `i`                       | Navegación → Interacción
| `s`                       | Navegación → Selección
| Flechas / Home / End      | Mover cursor
| RePag / AvPag             | Página (cursor + viewport)
| `j` / `k`                 | Bloque previo/siguiente palabra
| `a` (en Selección)        | Selección total
| `c` / `x` (en Selección)  | Copiar / cortar
| `}` / `{` (en Selección)  | Indentar / desindentar selección
| `p`                       | Pegar
| `Ctrl+U` / `Ctrl+Y`       | Deshacer / Rehacer
| `Ctrl+K s`                | Guardar (abre SaveAs si sin nombre)
| `Ctrl+K Ctrl+S`           | Guardar en otra ubicación (prompt con ruta editable; `Enter` crea y mueve, `Esc` cancela)
| `Ctrl+K q`                | Salir verificando guardado (bloquea si hay archivos sin guardar)
| `Ctrl+K Ctrl+Q`           | Salir forzado (sin comprobar guardado)
| `Ctrl+K n`                | Buffer nuevo sin nombre
| `Ctrl+K o`                | Explorador de archivos (directorio del archivo activo, o cwd si sin ruta)
| `Ctrl+K t`                | Selector de buffers
| `Ctrl+K w`                | Cerrar buffer activo
| `Esc`                     | Salir de Interacción/Selección/Busqueda
| `Backspace` / `Delete`    | Borrar (en Interacción)

## Documentación

Aun no implementado

## Licencia

De libre uso.
No nos hacemos responsables de los riesgos tomados por el usuario.
