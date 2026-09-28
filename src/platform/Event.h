#pragma once

#include "platform/InputEvent.h"

// Shim legacy para tests (decisión 1: el nombre canónico es InputEvent).
//
// `Event`/`EventType` son alias exactos de `InputEvent`/`InputEventType`
// (mismo tipo, no una copia). Solo el código legacy/tests incluye este
// header. Todo `src/` usa platform/InputEvent.h con los nombres nuevos.
using Event = InputEvent;
using EventType = InputEventType;
