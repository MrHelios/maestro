#include "rendering/ListLines.h"

#include "rendering/ScreenRenderer.h"  // FileListItem (definición completa)

namespace {

// Misma composición que el viejo TtyRenderer (sin cambios): "  " + nombre,
// "/" en carpetas, "  ~" de relleno.
ListLine bufferLine(const std::string& name) { return ListLine{"  " + name, false}; }

ListLine fileLine(const std::string& name, bool isDirectory) {
    return ListLine{"  " + name + (isDirectory ? "/" : ""), false};
}

}  // namespace

std::vector<ListLine> buildBufferListLines(
    const std::vector<std::string>& names, int contentH) {
    std::vector<ListLine> out;
    if (contentH < 0) contentH = 0;
    out.reserve(static_cast<size_t>(contentH));
    for (int r = 0; r < contentH; ++r) {
        if (r < static_cast<int>(names.size()))
            out.push_back(bufferLine(names[static_cast<size_t>(r)]));
        else
            out.push_back(ListLine{"  ~", true});
    }
    return out;
}

std::vector<ListLine> buildFileListLines(
    const std::vector<FileListItem>& items, int scroll, int contentH) {
    std::vector<ListLine> out;
    if (contentH < 0) contentH = 0;
    out.reserve(static_cast<size_t>(contentH));
    for (int r = 0; r < contentH; ++r) {
        const int idx = scroll + r;
        if (idx >= 0 && idx < static_cast<int>(items.size())) {
            const FileListItem& it = items[static_cast<size_t>(idx)];
            out.push_back(fileLine(it.name, it.isDirectory));
        } else {
            out.push_back(ListLine{"  ~", true});
        }
    }
    return out;
}
