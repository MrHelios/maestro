#pragma once

#include <optional>
#include <string>

#include "app/EditorState.h"
#include "app/Message.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "document/Selection.h"
#include "layout/BracketMatcher.h"
#include "layout/Layout.h"
#include "layout/Viewport.h"
#include "rendering/frame/Frame.h"
#include "syntax/SyntaxCache.h"
#include "syntax/SyntaxHighlighter.h"
#include "syntax/SyntaxSpan.h"
#include "syntax/SyntaxToken.h"

// ---------------------------------------------------------------------------
// FrameBuilder (Fase B/C-1): construye el Frame puro desde el Editor.
//
// No emite ANSI/CSI, no toca la terminal, no guarda filas codificadas.
// Solo resuelve geometría, visibilidad, selección, brackets y sintaxis en
// segmentos {texto visible, rol semántico}. El backend (TtyEncoder/GUI)
// decide la representación concreta.
// ---------------------------------------------------------------------------
class FrameBuilder {
public:
    struct EditorGeometry {
        Layout layout;
        int gutterW = 0;
    };

    Frame buildFrame(const Document& doc,
                     const Cursor& cursor,
                     const Viewport& viewport,
                     const std::string& filename,
                     bool modified,
                     const Message& message,
                     State state,
                     const std::optional<Selection>& selection = std::nullopt,
                     const std::optional<Selection>& searchHighlight = std::nullopt,
                     const std::optional<BracketPair>& bracketPair = std::nullopt) const;

    // Una sola fila de contenido (para patches incrementales del backend).
    // Las selecciones ya vienen normalizadas (ver normalizeBracketPair).
    StyledRow buildContentRow(const Document& doc,
                              const Cursor& cursor,
                              const Viewport& viewport,
                              const std::optional<Normalized>& sel,
                              const std::optional<Normalized>& searchSel,
                              const std::optional<Normalized>& bracketOpen,
                              const std::optional<Normalized>& bracketClose,
                              int docLine,
                              int gutterW,
                              int textWidth) const;

    EditorGeometry editorGeometry(const Document& doc,
                                  const Viewport& viewport) const;
    void editorCursorPos(const Document& doc,
                         const Cursor& cursor,
                         const Viewport& viewport,
                         int& outRow, int& outCol) const;
    void editorCursorPos(const Document& doc,
                         const Cursor& cursor,
                         const Viewport& viewport,
                         const EditorGeometry& g,
                         int& outRow, int& outCol) const;

    Layout calculateLayout(int contentRows, int width) const;

    // Sincroniza el lenguaje de sintaxis con el filename. Devuelve true si
    // cambió (el backend debe tratarlo como rebuild total, como hacía el
    // viejo Renderer::updateSyntaxLanguage al limpiar hasCache_).
    bool updateSyntaxLanguage(const std::string& filename) const;

    void setExternalSyntaxCache(SyntaxCache* c) { externalCache_ = c; }
    SyntaxCache* externalSyntaxCache() const { return externalCache_; }
    SyntaxCache& activeCache() const {
        return externalCache_ ? *externalCache_ : syntaxCache_;
    }

    static void normalizeBracketPair(const std::optional<BracketPair>& pair,
                                     std::optional<Normalized>& outOpen,
                                     std::optional<Normalized>& outClose);
    static StyleRole syntaxRoleFor(SyntaxToken tok);
    static std::string stateLabelFor(State state);

    // Payload puro de la barra (barato: sin filas). Lo usan buildFrame y el
    // backend para patches de status sin reconstruir contenido.
    struct StatusPayload {
        StatusBarData data;
        StyleRole accent = StyleRole::StatusAccentDefault;
    };
    StatusPayload buildStatus(const std::string& filename, bool modified,
                              const Message& message, const Cursor& cursor,
                              int totalLines, State state) const;

private:
    mutable SyntaxHighlighter syntaxHighlighter_;
    mutable SyntaxCache syntaxCache_;
    mutable SyntaxCache* externalCache_ = nullptr;

    SyntaxState syntaxStateAt(const Document& doc, int targetLine) const;
};
