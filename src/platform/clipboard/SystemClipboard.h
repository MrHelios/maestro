#pragma once
#include <string>
#include <optional>

class SystemClipboard {
public:
    virtual ~SystemClipboard() = default;
    virtual bool copy(const std::string& text) = 0;
    virtual std::optional<std::string> paste() = 0;
    // Heurística best-effort de "mi última copia sigue vigente", NO una
    // garantía de propiedad del portapapeles del SO: la fuerza depende del
    // backend (X11 consulta el owner real de la selección; SDL compara
    // contenido porque no hay query portable de propietario; Null/memoria
    // solo informa bandera local). Un false es fiable (algo cambió); un
    // true significa "nada indica lo contrario" con la precisión del
    // backend. El Editor no la consulta en ningún camino caliente (solo
    // copy/paste explícitos), así que el coste por backend es aceptable.
    virtual bool ownsClipboard() const = 0;
    virtual void processEvents() = 0;
    virtual int fd() const { return -1; }
    virtual bool hasPending() const { return false; }
};
