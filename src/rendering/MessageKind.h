#pragma once

// ---------------------------------------------------------------------------
// MessageKind: severidad del MessageBar. Vive en rendering/ (zona pura) para
// que el DTO del chrome (ChromeData/MessageBarData) no arrastre tipos de
// app/. app/Message.h lo reutiliza (app -> rendering, dirección permitida);
// el vencimiento temporal (expiry) queda solo en app::Message, que el
// rendering nunca ve.
// ---------------------------------------------------------------------------
enum class MessageKind {
    Info,     // informacion normal / ayuda / prompt
    Success,  // accion realizada correctamente ("Guardado.", "Pegado.")
    Warning,  // aviso ("Solo hay un buffer.", "Nada para pegar.")
    Error,    // fallo ("Error al guardar:", "No se pudo leer")
    Prompt,   // prompt de entrada ("Guardar archivo:") -> negrita (v1.3)
};
