#pragma once

class Editor;

// TtyRunLoop: dueño del loop TTY.
//
// Dueño del loop TTY: raw mode, alternate screen, mouse tracking,
// SIGWINCH, ppoll sobre stdin/clipboard/watcher y traducción
// señal -> resize(). El Editor común no sabe nada de esto:
// solo expone la fachada (handleEvent/resize/renderFrame/tick).
// Sin friend: este loop solo usa la API pública de Editor.
class TtyRunLoop {
public:
    explicit TtyRunLoop(Editor& editor);
    void run();

private:
    Editor& editor_;
};
