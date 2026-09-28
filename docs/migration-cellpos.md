# Migración CellPos 1-based → 0-based — inventario (Fase A)

Objetivo: que el paso "eliminar shim" tenga checklist. Todo lo que toca
coordenadas SGR 1-based o `mouseCol`/`mouseRow` legacy está listado acá.
Estado: Fase A (shim) — el código productivo sigue en 1-based.

## Guardia

```sh
rg -o '\.mouse(Col|Row)' tests/ | wc -l
```

Hoy: **46**. Solo puede bajar. (Incluye 2 menciones en comentarios del
propio `tests/helpers/event_shim.h`.)

## Contrato del shim (tests que deben seguir verdes)

- `tests/unit/test_event_shim.cpp`:
  `shim_mouse_roundtrip_and_legacy_ints`,
  `shim_sgr_entry_point_is_identity_today`,
  `shim_fake_resize_payload`.
- Punto único de conversión 1-based: `testshim::cellFromSgr(cx, cy)`
  (hoy identidad, luego `{cx-1, cy-1}`). `makeMouseEvent(type, CellPos)`
  recibe dominio destino, no es conversión.
- Helpers ya migrados a la variante SGR: `mousePress/Drag/Release` en
  `tests/unit/test_mouse_press.cpp` → `makeMouseEventSgr`.

## A. Construcción con ints crudos (bypasean el shim — migrar)

| Archivo | Líneas | Detalle |
|---|---|---|
| `tests/unit/test_inputevent_commandmap.cpp` | 155-156 | `m.mouseCol = 4; m.mouseRow = 2;` (`inputcmd_inputevent_shape`) |
| `tests/interaction/test_mouse_autoscroll_tick.cpp` | 21-22, 29-30, 37-38 | helpers locales `e.mouseCol/mouseRow =` |
| `tests/interaction/test_mouse_autoscroll_release.cpp` | 17-18, 25-26 | helpers locales `e.mouseCol/mouseRow =` |
| `tests/interaction/test_mouse_autoscroll_limits.cpp` | 32-33, 39-40, 72-73, 79-80 | `press./drag.mouseCol/mouseRow =` literales |

## B. Lecturas de compat (asserts sobre salida del decoder — válidas hasta la migración, después reescribir a `cellPos()`)

| Archivo | Líneas | Detalle |
|---|---|---|
| `tests/unit/test_mouse_press.cpp` | 61-62, 74-75, 84-85, 99-100 | sección SGR (`CHECK_EQ(e.mouseCol/Row, …)`) |
| `tests/unit/test_terminal_event.cpp` | 351-352 | `parse_mouse_sgr_direct_*` |
| `tests/unit/test_frontier.cpp` | 36-37, 52-53, 142-143, 177-178 | roundtrip CellPos + decoder |
| `tests/unit/test_inputevent_commandmap.cpp` | 101-102, 157-158 | roundtrip `setCellPos` / forma del vocabulario |

## C. Overload legacy `screenToCursor(int mouseRow, int mouseCol)` (no pasa por el shim)

| Archivo | Líneas | Detalle |
|---|---|---|
| `src/app/Editor.cpp` | 1128, 1279 | `handleMousePress`, `resolveMouseDragPosition` (pueden usar `event.cellPos()`) |
| `tests/unit/test_frontier.cpp` | 90, 99 | `frontier_screen_to_cursor_cellpos` (contrasta ambas formas a propósito) |
| `tests/unit/test_mouse_press.cpp` | 15 sitios / 9 TESTs mapper puro | detalle línea por línea en el comentario LEGACY de `src/layout/ScreenToCursor.h` |

## D. Verificados sin restos (nada que migrar)

- `tests/interaction/test_buffers.cpp` (~271, 286): son los tests de
  viewport `ctrl_k_n/w_*`; el archivo no tiene ningún uso de
  mouse/`CellPos`/`screenToCursor`.

## Checklist "eliminar shim"

1. Tabla A en 0 (toda construcción por shim o fábricas `MouseEvent.h`).
2. Tabla C solo en forma `CellPos` (borrar overload legacy).
3. Tabla B reescrita a `cellPos()`.
4. Suite verde tras `cellFromSgr → {cx-1, cy-1}` + decoder + `ScreenToCursor`.
5. Borrar `tests/helpers/event_shim.h`, este doc (o archivarlo) y los
   campos `mouseCol`/`mouseRow` de `InputEvent`.
