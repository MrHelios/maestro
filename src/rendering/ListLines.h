#pragma once

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// ListLines: composición PURA de las líneas visibles de los modales
// (BufferSelector / FileBrowser / SaveAs), compartida por ambos backends.
//
// Es la ÚNICA fuente de la composición textual: "  <nombre>", "/" para
// carpetas y "  ~" de relleno fuera del documento. Cada backend decide la
// representación (ANSI en TTY, rectángulos RGB en GUI); la política de QUÉ
// texto va en cada fila visible y QUÉ filas son relleno vive solo acá.
//
// `filler` viaja explícito: un archivo real llamado "~" produce el mismo
// texto "  ~" que el relleno pero con filler=false (comparar el texto para
// decidir el estilo es una colisión de representación).
//
// REGLA DE CAPAS: zona pura de rendering/ (sin app/, sin ANSI, sin SDL).
// TtyRenderer y GuiRenderer consumen estos builders; ninguno compone
// líneas por su cuenta.
// ---------------------------------------------------------------------------

struct FileListItem;  // rendering/ScreenRenderer.h (se evita el include)

struct ListLine {
    std::string text;
    bool filler = false;
    bool operator==(const ListLine& o) const {
        return filler == o.filler && text == o.text;
    }
    bool operator!=(const ListLine& o) const { return !(*this == o); }
};

// Exactamente `contentH` entradas (contentH < 0 se trata como 0):
// items a partir de `offset` + relleno con filler=true.
// `offset` es el scroll del modo lista: la primera fila visible del
// listado, desacoplada del indice seleccionado. BufferSelector lo usa como
// bufferSelectorScroll_, FileBrowser y SaveAs como scroll_ del listado.
std::vector<ListLine> buildBufferListLines(const std::vector<std::string>& names,
                                           int contentH,
                                           int offset = 0);
std::vector<ListLine> buildFileListLines(const std::vector<FileListItem>& items,
                                         int scroll, int contentH);
