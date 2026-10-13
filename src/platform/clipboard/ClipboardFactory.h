#pragma once

#include <memory>

#include "platform/clipboard/SystemClipboard.h"

// Frontier (9): factory de clipboard.
//
// El Editor común pide un SystemClipboard sin conocer X11/SDL/Null.
// El default intenta X11 y cae a Null si no hay display.
// makeSdlClipboard() es el clipboard del modo --gui (SDL_Get/SetClipboardText,
// síncrono: fd -1, sin heartbeat INCR). Sin HAVE_SDL2 degrada a memoria.
std::unique_ptr<SystemClipboard> makeSystemClipboard();
std::unique_ptr<SystemClipboard> makeNullClipboard();
std::unique_ptr<SystemClipboard> makeSdlClipboard();
