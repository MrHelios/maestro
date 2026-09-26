#pragma once

#include "platform/InputEvent.h"

// Compatibilidad transitoria (decisión 1: el nombre canónico es InputEvent).
//
// `Event`/`EventType` son alias exactos de `InputEvent`/`InputEventType`
// (mismo tipo, no una copia): todo el código existente que incluye este
// header sigue compilando sin cambios. El código nuevo debe incluir
// platform/InputEvent.h y usar los nombres nuevos.
using Event = InputEvent;
using EventType = InputEventType;
