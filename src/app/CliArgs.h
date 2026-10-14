#pragma once

#include <string>

#include "platform/gui/GuiOptions.h"

struct Args {
    bool gui = false;
    bool help = false;
    std::string file;
    bool bad = false;
    GuiOptions guiOptions;
    bool guiOptionsUsed = false;
};

inline Args parseArgs(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "--gui") {
            a.gui = true;
        } else if (s == "-h" || s == "--help") {
            a.help = true;
        } else if (s == "--font") {
            if (i + 1 >= argc) { a.bad = true; break; }
            a.guiOptions.fontPath = argv[++i];
            a.guiOptionsUsed = true;
        } else if (s == "--font-size") {
            if (i + 1 >= argc) { a.bad = true; break; }
            int sz;
            if (!parseIntStrict(argv[++i], sz, 1, 200)) { a.bad = true; break; }
            a.guiOptions.fontSize = sz;
            a.guiOptionsUsed = true;
        } else if (s == "--theme") {
            if (i + 1 >= argc) { a.bad = true; break; }
            if (!parseTheme(argv[++i], a.guiOptions.theme)) { a.bad = true; break; }
            a.guiOptionsUsed = true;
        } else if (s == "--no-hidpi") {
            a.guiOptions.enableHiDpi = false;
            a.guiOptionsUsed = true;
        } else if (s == "--geometry") {
            if (i + 1 >= argc) { a.bad = true; break; }
            int w, h;
            if (!parseGeometry(argv[++i], w, h)) { a.bad = true; break; }
            a.guiOptions.windowWidth = w;
            a.guiOptions.windowHeight = h;
            a.guiOptionsUsed = true;
        } else if (!s.empty() && s[0] == '-' && a.file.empty()) {
            a.bad = true;
        } else if (a.file.empty()) {
            a.file = s;
        } else {
            a.bad = true;
        }
    }
    if (a.guiOptionsUsed && !a.gui) {
        a.bad = true;
    }
    return a;
}
