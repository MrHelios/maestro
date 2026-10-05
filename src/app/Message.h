#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>

#include "rendering/MessageKind.h"

// ---------------------------------------------------------------------------
// Mensaje al usuario (paso 8). Un solo tipo reemplaza al trio que antes vivia
// repartido en el Editor (statusMessage_ + actionMessageActive_ +
// actionMessageExpiry_): el texto, su tipo y (si no es persistente) el
// vencimiento viven juntos en un mismo valor.
//
// El Editor produce y entrega un Message; el adaptador (FrameBuilder) lo
// traduce a MessageBarData (rendering/, solo texto + tipo) y ChromeData lo
// transporta al MessageBar; el backend traduce MessageKind al estilo visual
// correspondiente. El Editor no dibuja. rendering/ nunca incluye este
// header: la direccion es app -> rendering (Message.h incluye
// rendering/MessageKind.h, nunca al reves).
//
// persistence: un Message SIN `expiry` es PERSISTENTE (ayuda de modo,
// prompts de comando, informacion de estado): se queda hasta que otra cosa
// lo reemplace y nunca se limpia por tiempo. Un Message CON `expiry` es de
// ACCION (feedback de una accion ya realizada): expira solo pasado ese
// momento, para no quedar pegado en pantalla.

struct Message {
    Message() = default;
    // Implictamente convertible desde texto: los callers y tests que pasan
    // un string/literal producen un Message Info persistente.
    Message(const std::string& s) : text(s) {}
    Message(const char* s) : text(s) {}
    // Ctor completo: lo usan setStatusMessage/setActionMessage.
    Message(std::string t, MessageKind k,
            std::optional<std::chrono::steady_clock::time_point> e,
            std::optional<int> c = std::nullopt,
            std::optional<int> boldPrefix = std::nullopt)
        : text(std::move(t)), kind(k), expiry(e), cursor(c), boldPrefix(boldPrefix) {}

    std::string text;
    MessageKind kind = MessageKind::Info;
    // nullopt => persistente (no expira).
    std::optional<std::chrono::steady_clock::time_point> expiry;
    // Byte offset dentro de `text` donde va el cursor de edición cuando el
    // mensaje es un prompt con input (el input vive al inicio: prompt+query,
    // con decoraciones como " - not found" o " (Control+S...)" después).
    // nullopt => al final del texto visible (mensajes sin input).
    std::optional<int> cursor;
    // Longitud en bytes del prefijo a pintar en negrita (etiqueta del
    // prompt: "Find: ", "ir a fila: ", ...). nullopt => todo el texto con
    // el estilo de `kind` (comportamiento anterior). Cuando tiene valor,
    // solo esos primeros bytes van en negrita; el resto (input del usuario
    // + sufijos decorativos) va sin negrita.
    // Contrato: nullopt = legacy; con valor, siempre >= 0 (una longitud no
    // puede ser negativa). Además debe caer en un límite UTF-8 válido del
    // texto: los prompts actuales usan etiquetas fijas ASCII ("Find: ",
    // "ir a fila: ", ...), así que se cumple por construcción. Los
    // productores (ver promptBoldPrefix en Editor.cpp) solo generan valores
    // >= 0; un negativo es un bug.
    std::optional<int> boldPrefix;

    bool persistent() const { return !expiry.has_value(); }
    bool expired() const {
        return expiry && std::chrono::steady_clock::now() >= *expiry;
    }
    bool expired(std::chrono::steady_clock::time_point now) const {
        return expiry && now >= *expiry;
    }

    // Conveniencia "string-like": los tests (y el codigo que solo quiere el
    // texto) comparan/buscan el mensaje sin destillar .text.
    bool empty() const { return text.empty(); }
    std::size_t find(const std::string& needle, std::size_t pos = 0) const {
        return text.find(needle, pos);
    }
    bool operator==(const std::string& o) const { return text == o; }
    bool operator==(const char* o) const { return text == o; }
    bool operator==(const Message& o) const { return text == o.text; }
};

// Para que los CHECK de los tests puedan imprimir el mensaje al fallar.
inline std::ostream& operator<<(std::ostream& os, const Message& m) {
    return os << m.text;
}