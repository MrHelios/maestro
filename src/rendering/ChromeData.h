#pragma once

#include "app/Message.h"
#include "rendering/StatusBarData.h"

// ---------------------------------------------------------------------------
// ChromeData: DTO puro del chrome inferior (conjunto StatusBar + MessageBar).
// No conoce TtyTheme ni ANSI. Vive en rendering/ (zona pura). La
// codificacion ANSI vive en rendering/tty/TtyChrome que recibe este DTO.
//
//   ChromeData
//   ├── statusBar -> StatusBarData (fila superior fija: nombre, ruta, estado, %)
//   └── message  -> Message (fila inferior, MessageBar: mensajes/prompts/avisos,
//                            coloreada por MessageKind)
// ---------------------------------------------------------------------------
struct ChromeData {
    StatusBarData statusBar;  // fila superior del chrome (StatusBar)
    Message message;          // fila inferior del chrome (MessageBar, fila propia por tipo)
};

inline bool operator==(const ChromeData& a, const ChromeData& b) {
    // Igualdad estructural: todos los campos, incluido `Message::expiry`.
    // No reutiliza Message::operator==(Message): ese solo compara text e
    // ignoraria kind y expiry.
    return a.statusBar == b.statusBar && a.message.text == b.message.text &&
           a.message.kind == b.message.kind &&
           a.message.expiry == b.message.expiry;
}

// Igualdad visual: ¿producirian A y B los mismos bytes en TtyChrome?
// Compara solo el estado observable por el renderer (StatusBar + text/kind
// del MessageBar). `Message::expiry` no forma parte del estado visual:
// TtyChrome solo lee text/kind, asi que dos ChromeData que difieren solo en
// expiry se pintan identico. El diff usa esta comparacion para evitar
// repintados sin cambio visual.
inline bool sameRenderedChrome(const ChromeData& a, const ChromeData& b) {
    return a.statusBar == b.statusBar && a.message.text == b.message.text &&
           a.message.kind == b.message.kind;
}

inline bool operator!=(const ChromeData& a, const ChromeData& b) {
    return !(a == b);
}
