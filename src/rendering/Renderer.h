#pragma once

#include <optional>
#include <string>

#include "document/Cursor.h"
#include "document/Document.h"
#include "document/Selection.h"
#include "layout/BracketMatcher.h"
#include "layout/Layout.h"
#include "layout/Viewport.h"
#include "rendering/frame/Frame.h"
#include "rendering/frame/FrameBuilder.h"
#include "syntax/SyntaxCache.h"

// ---------------------------------------------------------------------------
// Renderer puro (Fase E paso 6): construye el Frame, sin ANSI, sin terminal.
//
// No conoce ningún backend: no incluye sus headers ni los de plataforma,
// no emite secuencias, no toca Sink, no conoce Theme. Solo resuelve
// geometría + contenido en roles semánticos vía su FrameBuilder (el builder
// único que el backend TTY comparte por referencia).
//
// El backend de terminal vive en su carpeta propia (TtyRenderer: posee
// encoder, diff, theme y listas, y consume este Renderer para el Frame).
// ---------------------------------------------------------------------------
class Renderer {
public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void setExternalSyntaxCache(SyntaxCache* c) {
        frameBuilder_.setExternalSyntaxCache(c);
    }
    SyntaxCache* externalSyntaxCache() const {
        return frameBuilder_.externalSyntaxCache();
    }
    SyntaxCache& activeCache() const { return frameBuilder_.activeCache(); }

    // Acceso al builder único para el backend TTY (TtyDiff lo comparte por
    // referencia: un solo dueño del estado de sintaxis).
    FrameBuilder& frameBuilder() { return frameBuilder_; }
    const FrameBuilder& frameBuilder() const { return frameBuilder_; }

private:
    mutable FrameBuilder frameBuilder_;
};
