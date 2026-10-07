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
};

// Exactamente `contentH` entradas (contentH < 0 se trata como 0):
// items + relleno con filler=true. BufferSelector sin scroll (desde 0);
// FileBrowser/SaveAs con scroll (idx = scroll + fila).
std::vector<ListLine> buildBufferListLines(const std::vector<std::string>& names,
                                           int contentH);
std::vector<ListLine> buildFileListLines(const std::vector<FileListItem>& items,
                                         int scroll, int contentH);
