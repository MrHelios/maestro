#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>

// ---------------------------------------------------------------------------
// Mensaje al usuario (paso 8). Un solo tipo reemplaza al trio que antes vivia
// repartido en el Editor (statusMessage_ + actionMessageActive_ +
// actionMessageExpiry_): el texto, su tipo y (si no es persistente) el
// vencimiento viven juntos en un mismo valor.
//
// El Editor produces y entrega un Message; el Renderer decide como mostrarlo
// (via StatusBarData): la fila y el color de la fila de mensajes los define
// el tipo. El Editor no dibuja.
//
// persistence: un Message SIN `expiry` es PERSISTENTE (ayuda de modo,
// prompts de comando, informacion de estado): se queda hasta que otra cosa
// lo reemplace y nunca se limpia por tiempo. Un Message CON `expiry` es de
// ACCION (feedback de una accion ya realizada): expira solo pasado ese
// momento, para no quedar pegado en pantalla.
enum class MessageKind {
    Info,     // informacion normal / ayuda / prompt
    Success,  // accion realizada correctamente ("Guardado.", "Pegado.")
    Warning,  // aviso ("Solo hay un buffer.", "Nada para pegar.")
    Error,    // fallo ("Error al guardar:", "No se pudo leer")
};

struct Message {
    Message() = default;
    // Implictamente convertible desde texto: los callers y tests que pasan
    // un string/literal producen un Message Info persistente.
    Message(const std::string& s) : text(s) {}
    Message(const char* s) : text(s) {}
    // Ctor completo: lo usan setStatusMessage/setActionMessage.
    Message(std::string t, MessageKind k,
            std::optional<std::chrono::steady_clock::time_point> e)
        : text(std::move(t)), kind(k), expiry(e) {}

    std::string text;
    MessageKind kind = MessageKind::Info;
    // nullopt => persistente (no expira).
    std::optional<std::chrono::steady_clock::time_point> expiry;

    bool persistent() const { return !expiry.has_value(); }
    bool expired() const {
        return expiry && std::chrono::steady_clock::now() >= *expiry;
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