#pragma once

#include <memory>

#include "platform/clipboard/SystemClipboard.h"

// Frontier (9): factory de clipboard.
//
// El Editor común pide un SystemClipboard sin conocer X11/Null.
// El default intenta X11 y cae a Null si no hay display.
std::unique_ptr<SystemClipboard> makeSystemClipboard();
std::unique_ptr<SystemClipboard> makeNullClipboard();
