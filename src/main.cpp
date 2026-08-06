#include <cstdio>
#include "Editor.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Uso: %s <archivo>\n", argv[0]);
        return 1;
    }

    Editor editor;
    editor.openFile(argv[1]);
    editor.run();

    return 0;
}
