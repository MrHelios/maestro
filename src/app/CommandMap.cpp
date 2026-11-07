#include "app/CommandMap.h"

void CommandMap::registerCommand(const std::string& name, Handler handler) {
    commands_[name] = std::move(handler);
}

bool CommandMap::has(const std::string& name) const {
    return commands_.find(name) != commands_.end();
}

void CommandMap::execute(const std::string& name) {
    auto it = commands_.find(name);
    if (it != commands_.end()) {
        it->second();
    }
}

std::vector<std::string> CommandMap::registeredNames() const {
    std::vector<std::string> names;
    names.reserve(commands_.size());
    for (const auto& pair : commands_) {
        names.push_back(pair.first);
    }
    return names;
}