#pragma once
#include <functional>
#include <optional>
#include <string>
#include "document/Document.h"

namespace filesystem {

using LoadHook = std::function<std::optional<LoadResult>(const std::string&)>;

void setLoadHook(LoadHook hook);
void clearLoadHook();
std::optional<LoadResult> callLoadHook(const std::string& path);

}
