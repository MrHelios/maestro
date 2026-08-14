#include "ui/CommandMap.h"

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