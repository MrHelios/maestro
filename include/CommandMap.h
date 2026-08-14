#pragma once

#include <functional>
#include <map>
#include <string>

// Registro central de comandos nombrados.
//
// Cada comando es un nombre ("navegacion.interaccion", "buffer.nuevo", ...)
// ligado a un handler. El Editor REGISTRA los handlers (los cuerpos) bajo
// esos nombres en su constructor, y el despacho por modo resuelve la tecla
// -> nombre -> handler a traves de CommandMap. Asi el mapeo "que tecla
// dispara que comando" deja de estar disperso en switchs grandes y queda
// centralizado, con nombres reutilizables y testeables de forma aislada.
//
// Los CUERPOS (los handlers) siguen viviendo en el Editor (son lambdas que
// capturan el editor y tocan su estado): CommandMap no reimplementa la
// logica de edicion, solo la encamina por nombre.
class CommandMap {
public:
    using Handler = std::function<void()>;

    // Registra (o reemplaza) el handler para `name`.
    void registerCommand(const std::string& name, Handler handler);

    // true si existe un comando con ese nombre.
    bool has(const std::string& name) const;

    // Ejecuta el comando. Si no existe, no-op (robusto ante errores de
    // registro/renombrado).
    void execute(const std::string& name);

private:
    std::map<std::string, Handler> commands_;
};