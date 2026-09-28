#pragma once

class Editor;

// Frontier (13): TtyRunLoop.
//
// Dueño del loop TTY: raw mode, alternate screen, mouse tracking,
// SIGWINCH, ppoll sobre stdin/clipboard/watcher y traducción
// señal -> InputEventType::Resize. El Editor común no sabe nada de esto:
// solo recibe InputEvent y pinta Frames.
class TtyRunLoop {
public:
    explicit TtyRunLoop(Editor& editor);
    void run();

private:
    Editor& editor_;
};
