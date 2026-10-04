#pragma once

#include <string>

#include "rendering/MessageKind.h"
#include "rendering/StatusBarData.h"

// ---------------------------------------------------------------------------
// ChromeData: DTO puro del chrome inferior (conjunto StatusBar + MessageBar).
// No conoce TtyTheme ni ANSI. Vive en rendering/ (zona pura). La
// codificacion ANSI vive en rendering/tty/TtyChrome que recibe este DTO.
//
//   ChromeData
//   ├── statusBar -> StatusBarData (fila superior fija: nombre, ruta, estado, %)
//   └── message  -> MessageBarData (fila inferior, MessageBar: mensajes/prompts/
//                                  avisos, coloreada por MessageKind)
//
// REGLA DE CAPAS: este DTO no incluye nada de app/. El mensaje vigente del
// Editor (app::Message, con texto + tipo + vencimiento temporal) se traduce
// a MessageBarData (solo texto + tipo) en app/ (ver
// app/ChromePresentation.h::toMessageBar) antes de invocar al rendering.
// El `expiry` nunca viaja al rendering: no forma parte del estado visual.
// ---------------------------------------------------------------------------
struct MessageBarData {
    MessageBarData() = default;
    MessageBarData(const std::string& s) : text(s) {}
    MessageBarData(const char* s) : text(s) {}
    MessageBarData(std::string t, MessageKind k) : text(std::move(t)), kind(k) {}

    std::string text;
    MessageKind kind = MessageKind::Info;

    bool empty() const { return text.empty(); }
};

inline bool operator==(const MessageBarData& a, const MessageBarData& b) {
    return a.text == b.text && a.kind == b.kind;
}

inline bool operator!=(const MessageBarData& a, const MessageBarData& b) {
    return !(a == b);
}

struct ChromeData {
    StatusBarData statusBar;      // fila superior del chrome (StatusBar)
    MessageBarData message;       // fila inferior del chrome (MessageBar)
};

inline bool operator==(const ChromeData& a, const ChromeData& b) {
    return a.statusBar == b.statusBar && a.message == b.message;
}

// Igualdad visual: ¿producirian A y B los mismos bytes en TtyChrome?
// Compara el estado observable por el renderer (StatusBar + text/kind del
// MessageBar). Se conserva como alias de operator== para los callers del
// diff: el DTO ya no lleva `expiry`, asi que la igualdad estructural ES la
// visual (antes habia que ignorar expiry a proposito).
inline bool sameRenderedChrome(const ChromeData& a, const ChromeData& b) {
    return a.statusBar == b.statusBar && a.message.text == b.message.text &&
           a.message.kind == b.message.kind;
}

inline bool operator!=(const ChromeData& a, const ChromeData& b) {
    return !(a == b);
}
