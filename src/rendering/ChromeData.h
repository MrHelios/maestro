#pragma once

#include "app/Message.h"
#include "rendering/StatusBarData.h"

// ---------------------------------------------------------------------------
// ChromeData: DTO puro del chrome inferior (conjunto StatusBar + MessageBar).
// No conoce TtyTheme ni ANSI. Vive en rendering/ (zona pura). La
// codificacion ANSI vive en rendering/tty/TtyChrome que recibe este DTO.
//
//   ChromeData
//   ├── status  -> StatusBarData (fila 1 fija: nombre, ruta, estado, %)
//   └── message -> Message (fila 2, MessageBar: mensajes/prompts/avisos,
//                           coloreada por MessageKind)
// ---------------------------------------------------------------------------
struct ChromeData {
    StatusBarData status;  // fila 1 (StatusBar)
    Message message;       // fila 2 (MessageBar, fila propia por tipo)
};

inline bool operator==(const ChromeData& a, const ChromeData& b) {
    // Intencional: solo lo observable por el renderer (StatusBar + text/kind
    // del MessageBar). `Message::expiry` se excluye a proposito: no altera
    // los bytes pintados (TtyChrome solo lee text/kind) y el vencimiento se
    // materializa como un Message distinto (Editor::clearExpiredActionMessage
    // lo reemplaza por Message{} -> cambia text). Comparar expiry solo
    // provocaria repintados sin cambio visual. Nota: no usar
    // Message::operator==(Message) aqui: ese solo compara text e ignoraria
    // kind.
    return a.status == b.status && a.message.text == b.message.text &&
           a.message.kind == b.message.kind;
}

inline bool operator!=(const ChromeData& a, const ChromeData& b) {
    return !(a == b);
}
