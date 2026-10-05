#pragma once

#include <optional>
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
    // Byte offset dentro de `text` donde va el cursor de edición (prompts
    // con input al inicio + decoraciones después). nullopt => al final del
    // texto visible. Lo pone app/ (ver app/Message.h::cursor); el rendering
    // solo lo posiciona (ver chrome::messageBarCursorCell).
    std::optional<int> cursor;
    // Longitud en bytes del prefijo en negrita (etiqueta del prompt).
    // nullopt => todo el texto con el estilo de `kind`. Con valor: solo
    // esos primeros bytes en negrita (theme.prompt); el resto sin negrita
    // (input del usuario + sufijos decorativos).
    // Contrato: nullopt = legacy; con valor, siempre >= 0 y sobre un límite
    // UTF-8 válido del texto (las etiquetas de prompt son ASCII, así que se
    // cumple por construcción). Un valor mayor que el texto visible se
    // recorta a lo visible (terminal angosta); un negativo, o un corte en
    // medio de un carácter multibyte, es un bug del productor, nunca un caso
    // a reparar aquí.
    std::optional<int> boldPrefix;

    bool empty() const { return text.empty(); }
};

// Padding del MessageBar (fuente neutra compartida entre TtyChrome y el
// cálculo del cursor en el MessageBar). TtyChrome.h mantiene aliases.
inline constexpr int kMessageBarPadLeft = 1;
inline constexpr int kMessageBarPadRight = 3;

inline bool operator==(const MessageBarData& a, const MessageBarData& b) {
    return a.text == b.text && a.kind == b.kind && a.cursor == b.cursor &&
           a.boldPrefix == b.boldPrefix;
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

// NOTA: no hay `sameRenderedChrome` separado: el DTO solo lleva estado
// visual (StatusBar + text/kind del MessageBar), asi que la igualdad
// estructural ES la igualdad visual. El diff usa operator== directo.

inline bool operator!=(const ChromeData& a, const ChromeData& b) {
    return !(a == b);
}
