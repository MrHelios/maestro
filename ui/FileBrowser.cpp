#include "ui/FileBrowser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

bool FileBrowser::isDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

std::string FileBrowser::getCwd() {
    char buf[4096];
    if (!getcwd(buf, sizeof buf)) return "";
    return std::string(buf);
}

std::string FileBrowser::parentPath(const std::string& path) {
    if (path.empty() || path == "/") return "/";
    const std::string parent = std::filesystem::path(path).parent_path().string();
    // parent_path() devuelve vacio si `path` es relativo sin directorio:
    // no hay a donde subir, se ancla a la raiz.
    return parent.empty() ? "/" : parent;
}

// Compara dos nombres de forma case-insensitive para el orden alfabetico
// de las entradas del explorador (v0.6.4, decision de diseno).
static bool entryLess(const FileBrowserEntry& a, const FileBrowserEntry& b) {
    std::string sa = a.name, sb = b.name;
    std::transform(sa.begin(), sa.end(), sa.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::transform(sb.begin(), sb.end(), sb.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return sa < sb;
}

std::vector<FileBrowserEntry> FileBrowser::listDirectory(const std::string& path,
                                                         std::string& error) {
    std::vector<FileBrowserEntry> dirs, files;
    error.clear();

    std::error_code ec;
    std::filesystem::directory_iterator it(path, ec);
    if (ec) {
        // Sin permiso de lectura / no existe: la lista queda solo con ".."
        // (si la hay) y el error se muestra en la fila de mensajes.
        error = "No se pudo leer: " + path;
    } else {
        std::filesystem::directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; } // entrada no listable -> saltar
            const std::filesystem::directory_entry& e = *it;
            std::error_code sec;
            std::filesystem::file_status st = e.status(sec);
            if (sec) continue; // symlink roto etc. -> omitir
            FileBrowserEntry entry;
            entry.name = e.path().filename().string();
            entry.fullPath = e.path().string();
            entry.isDirectory = std::filesystem::is_directory(st);
            if (entry.isDirectory) {
                dirs.push_back(std::move(entry));
            } else {
                files.push_back(std::move(entry));
            }
        }
    }

    std::sort(dirs.begin(), dirs.end(), entryLess);
    std::sort(files.begin(), files.end(), entryLess);

    std::vector<FileBrowserEntry> out;
    if (path != "/") out.push_back(FileBrowserEntry{"..", true, parentPath(path)});
    out.insert(out.end(), dirs.begin(), dirs.end());
    out.insert(out.end(), files.begin(), files.end());
    return out;
}

void FileBrowser::start() {
    path_ = getCwd();
    index_ = 0;
    scroll_ = 0;
}

void FileBrowser::startAt(const std::string& path) {
    if (path.empty()) { start(); return; }
    path_ = path;
    index_ = 0;
    scroll_ = 0;
}

std::string FileBrowser::reload() {
    std::string err;
    entries_ = listDirectory(path_, err);
    displayNames_.clear();
    displayNames_.reserve(entries_.size());
    for (const FileBrowserEntry& e : entries_) {
        displayNames_.push_back(e.isDirectory ? e.name + "/" : e.name);
    }
    return err;
}

void FileBrowser::clampScroll(int page) {
    const int n = static_cast<int>(entries_.size());
    if (n == 0) { scroll_ = 0; return; }
    if (index_ < scroll_)
        scroll_ = index_;
    if (scroll_ + page > n)
        scroll_ = std::max(0, n - page);
    if (index_ - scroll_ >= page)
        scroll_ = index_ - page + 1;
}

bool FileBrowser::moveUp() {
    if (index_ > 0) { index_--; return true; }
    return false;
}

bool FileBrowser::moveDown() {
    if (index_ + 1 < static_cast<int>(entries_.size())) { index_++; return true; }
    return false;
}

FileBrowser::EnterResult FileBrowser::enter() {
    if (entries_.empty()) return EnterResult::None;
    const FileBrowserEntry& e = entries_[static_cast<size_t>(index_)];

    if (e.isDirectory) {
        // Carpeta (o ".."): entrar, recargar y volver al inicio.
        path_ = e.fullPath;
        index_ = 0;
        scroll_ = 0;
        return EnterResult::EnteredDirectory;
    }

    pendingPath_ = e.fullPath;
    return EnterResult::OpenedFile;
}
