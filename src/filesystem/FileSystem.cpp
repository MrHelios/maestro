#include "filesystem/FileSystem.h"

namespace filesystem {

namespace {
LoadHook g_hook;
}

void setLoadHook(LoadHook hook) {
    g_hook = std::move(hook);
}

void clearLoadHook() {
    g_hook = nullptr;
}

std::optional<LoadResult> callLoadHook(const std::string& path) {
    if (!g_hook) return std::nullopt;
    return g_hook(path);
}

}
