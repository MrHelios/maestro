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
make WITH_SDL2=1              # GUI con texto (requiere libsdl2-dev y libsdl2-ttf-dev)
make WITH_SDL2=0              # Solo TTY (binario menor, sin dependencia SDL)
```

SDL_ttf es **obligatorio** para una GUI utilizable: sin texto no se ve el
documento y el editor queda ciego, así que `WITH_SDL2=1` sin
`libsdl2-ttf-dev` (o sin `libsdl2-dev`) falla el build con error explícito
que nombra el paquete faltante. `WITH_SDL2_TTF=0` no está permitido con
GUI. Además se necesita una fuente en runtime (`MAESTRO_FONT` o una del
sistema como DejaVuSansMono, ver "Selección de fuente").

En un binario compilado `WITH_SDL2=0`, ejecutar `./build/maestro --gui`
**no** abre ventana ni degrada en silencio: informa
`maestro: --gui requiere compilar con SDL2 (WITH_SDL2=1, falta
libsdl2-dev).` y termina con código de salida `1`.

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

#### Ayuda

`--help` (o `-h`) funciona **en cualquier posición** de la línea de
comandos, incluso conviviendo con argumentos que serían inválidos: imprime
la ayuda y sale con código `0` sin inicializar SDL ni abrir ventana.

#### Opciones GUI

| Opción | Descripción |
|--------|-------------|
| `--font <ruta>` | Fuente TTF monoespaciada (precede a `MAESTRO_FONT` y a los candidatos del sistema; si no abre, **error terminal sin fallback**). `--font=ruta` también es válido y es la forma para rutas con guion inicial (ej. `--font=-mi-fuente.ttf`) |
| `--font-size <px>` | Píxeles lógicos base (default 16; misma altura visual en cualquier monitor). Solo dígitos decimales entre 1 y 200 |
| `--theme dark\|light` | Tema inicial (default `dark`) |
| `--no-hidpi` | Desactiva `SDL_WINDOW_ALLOW_HIGHDPI` |
| `--geometry WxH` | Coords iniciales de ventana, no píxeles físicos (ej. `1200x800`; dimensiones 1–10000, área total ≤ 12000000) |

Un valor de opción que empieza con `-` se interpreta como otra opción
(por ejemplo `--font --theme` es error, no la ruta `--theme`); para esos
valores use la forma `--opt=VALOR`. Cada argumento inválido produce un
diagnóstico que nombra la opción y el valor recibido.

#### Variables de entorno y selección de fuente

Orden (única fuente de verdad, `src/rendering/gui/FontOrder.h`):

1. `--font` (CLI) — si la fuente explícita no abre: **error y salida ≠ 0,
   sin fallback** (una fuente explícita nunca cae silenciosamente a otra).
2. `MAESTRO_FONT` — si no abre: **aviso por stderr (una sola vez por
   proceso)** y se continúa con el siguiente candidato.
3. Candidatos del sistema: DejaVuSansMono, LiberationMono, NotoSansMono.
4. `$HOME/.local/share/fonts/JetBrainsMono-Regular.ttf` y
   `$HOME/.fonts/JetBrainsMono-Regular.ttf`.

`MAESTRO_FONT` vacía o sin definir usa el comportamiento predeterminado.
La fuente (cualquier origen) debe ser monoespaciada: la celda se mide con
el avance de `'M'`.

#### Otras variables de entorno

- `SDL_VIDEODRIVER` — Fuerza driver SDL (`x11`, `wayland`, `dummy` para tests)

#### Atajos GUI adicionales

| Tecla | Acción |
|-------|--------|
| Click izq | Posicionar cursor / Seleccionar (drag) |
| Rueda | Scroll ±3 líneas |
| Doble click | Seleccionar palabra |
| `Ctrl+K` luego `l` | Toggle tema (dark/light). Es una **secuencia**: prefijo `Ctrl+K`, seguido de `l` |

#### Ejemplos

```bash
# Ventana 1200x800 con tema claro y fuente personalizada
./build/maestro --gui --theme light --font /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf --geometry 1200x800 archivo.cpp

# Sin HiDPI (útil en algunos entornos Wayland/XWayland)
./build/maestro --gui --no-hidpi archivo.txt

# Ayuda en cualquier posición (salida inmediata, sin ventana)
./build/maestro --gui --help
./build/maestro --font --help
```

#### Nota Wayland: captura de mouse y arrastre fuera de la ventana

`SDL_CaptureMouse(SDL_TRUE)` se pide en cada click como best-effort, pero
en Wayland el compositor puede denegarla (devuelve `-1`). Eso es una
**limitación del entorno, no un error fatal**: el editor sigue funcionando
con la selección coherente hasta la última celda válida.

- **Con captura (X11 / Wayland que la concede):** el `motion`/`up` fuera de
  la ventana igual llega: la selección se extiende al borde + autoscroll y
  el release cierra el gesto.
- **Sin captura (Wayland que la deniega):** el `motion` de fuera no llega y
  la selección **se congela en su último borde válido dentro** (sin rangos
  espurios ni crash). El gesto se cierra por el release entregado dentro al
  re-entrar, por el release sintético de `FOCUS_LOST`, o por el oráculo del
  tick (`SDL_GetGlobalMouseState`); el autoscroll por timer puede frenar
  antes donde el driver no pueda consultar el botón al OS (los pasos por
  evento de drag no se afectan).

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
