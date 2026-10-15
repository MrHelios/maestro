# Maestro

Editor de texto modal, liviano — pensado para usarse con o sin entorno gráfico en Linux/Ubuntu X11. 
Mayoritariamente *vibecode* con supervisión del creador.

> **La idea del editor es que tenga la mayor cantidad de funcionalidades posibles pero que a su vez sea lo más liviana posible.**

## Estado actual

- **Versión:** `v0.11` (alpha)
- **Estado:** Activo
- **Plataforma:** Linux/Ubuntu X11

## Cambios: v0.11

- se implemento la gui
- ahora se puede hacer ~/.local/bin/maestro --gui

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

### Compilación con GUI

```bash
make WITH_SDL2=1              # Habilita GUI (requiere libsdl2-dev)
make WITH_SDL2=1 WITH_SDL2_TTF=1  # Con texto (recomendado, requiere libsdl2-ttf-dev)
make WITH_SDL2=1 WITH_SDL2_TTF=0  # GUI sin texto (modo degradado forzado)
make WITH_SDL2=0              # Solo TTY (binario menor, sin dependencia SDL)
```

`WITH_SDL2_TTF` es la opción pública (default: autodetección). Sin
`libsdl2-ttf-dev` la GUI compila en **modo degradado**: fondo sólido sin
renderizado de texto. Forzar `WITH_SDL2_TTF=1` sin la biblioteca instalada
falla el build con error explícito (igual que `WITH_SDL2_TTF=1` con
`WITH_SDL2=0`).

## Ejecutar

```bash
./build/maestro
./build/maestro archivo.txt
./build/maestro /ruta/absoluta/archivo.txt
```

Si el archivo no existe se crea en memoria (guardar con `Ctrl+K s`). Las carpetas no se abren como archivo.
Wrapper opcional `./maestro` (si existe en la raíz) delega en `make`/`build/maestro`.

### Modo GUI (--gui)

```bash
./build/maestro --gui [opciones-gui] [archivo]
```

#### Opciones GUI

| Opción | Descripción |
|--------|-------------|
| `--font <ruta>` | Fuente TTF monoespaciada (override `MAESTRO_FONT`) |
| `--font-size <pt>` | Tamaño base en puntos (default 16, escala HiDPI) |
| `--theme dark\|light` | Tema inicial (default `dark`) |
| `--no-hidpi` | Desactiva `SDL_WINDOW_ALLOW_HIGHDPI` |
| `--geometry WxH` | Tamaño inicial ventana en puntos (ej. `1200x800`) |

#### Variables de entorno

- `MAESTRO_FONT` — Ruta a fuente TTF monoespaciada (prioridad sobre candidatos del sistema)
- `SDL_VIDEODRIVER` — Fuerza driver SDL (`x11`, `wayland`, `dummy` para tests)

#### Atajos GUI adicionales

| Tecla | Acción |
|-------|--------|
| Click izq | Posicionar cursor / Seleccionar (drag) |
| Rueda | Scroll ±3 líneas |
| Doble click | Seleccionar palabra |
| `Ctrl+K l` | Toggle tema (dark/light) |

#### Ejemplos

```bash
# Ventana 1200x800 con tema claro y fuente personalizada
./build/maestro --gui --theme light --font /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf --geometry 1200x800 archivo.cpp

# Sin HiDPI (útil en algunos entornos Wayland/XWayland)
./build/maestro --gui --no-hidpi archivo.txt
```

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
| `Ctrl+K Ctrl+S`           | Guardar en otra ubicación (prompt con ruta editable; `Enter` crea una copia y conserva ambos buffers —si el destino existe pide un segundo `Enter` para sobrescribir—, `Esc` cancela)
| `Ctrl+K q`                | Salir verificando guardado (bloquea si hay archivos sin guardar)
| `Ctrl+K Ctrl+Q`           | Salir forzado (sin comprobar guardado)
| `Ctrl+K n`                | Buffer nuevo sin nombre
| `Ctrl+K o`                | Explorador de archivos (directorio del archivo activo, o cwd si sin ruta)
| `Ctrl+K t`                | Selector de buffers
| `Ctrl+K w`                | Cerrar buffer activo
| `Esc`                     | Salir de Interacción/Selección/Busqueda
| `Backspace` / `Delete`    | Borrar (en Interacción)

## Documentación

Ver `docs/architecture.md` (reglas InputEvent/CommandMap, neutralidad de
backend, keymaps por backend).

## Licencia

De libre uso.
No nos hacemos responsables de los riesgos tomados por el usuario.
