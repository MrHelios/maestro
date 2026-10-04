#pragma once

#include <string>

#include "rendering/ChromeData.h"
#include "rendering/Style.h"
#include "rendering/frame/Frame.h"

// ---------------------------------------------------------------------------
// ChromeRequest: presentación ya resuelta para el chrome + cursor (zona pura).
// Agrupa lo que el rendering necesita y app/ ya tradujo:
//
//   message            -> MessageBarData (texto + tipo, sin vencimiento)
//   estado             -> etiqueta de la StatusBar ("NAVEGACION", ...)
//   accent             -> rol de acento de la etiqueta
//   cursorShape        -> forma del cursor (el DTO ya no conoce el modo)
//   cursorVisibleByMode-> visibilidad aportada por el modo (Búsqueda oculta);
//                         el viewport aporta la suya (editorCursorPos) y el
//                         Frame/diff combinan ambas con AND.
//
// REGLA DE CAPAS: este struct no incluye nada de app/. Lo construye app/
// (ver app/ChromePresentation.h::makeChromeRequest) antes de invocar al
// puerto ScreenRenderer o al FrameBuilder. rendering/ nunca traduce State.
// ---------------------------------------------------------------------------
struct ChromeRequest {
    MessageBarData message;
    std::string estado;
    StyleRole accent = StyleRole::StatusAccentDefault;
    FrameCursorShape cursorShape = FrameCursorShape::Block;
    bool cursorVisibleByMode = true;
};
