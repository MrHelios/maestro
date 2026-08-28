#pragma once

#include "core/Cursor.h"
#include "core/Document.h"
#include "core/utf8.h"

class Viewport {
public:
    int top = 0;
    int left = 0;
    int height = 24;
    int width = 80;

    void scrollToCursor(const Cursor& cursor) {
        if (cursor.line < top) {
            top = cursor.line;
        } else if (cursor.line >= top + height) {
            top = cursor.line - height + 1;
        }
        if (top < 0) top = 0;
        if (left < 0) left = 0;
    }

    void scrollToCursor(const Cursor& cursor, int absoluteCol, int textWidth) {
        if (cursor.line < top) {
            top = cursor.line;
        } else if (cursor.line >= top + height) {
            top = cursor.line - height + 1;
        }
        if (top < 0) top = 0;
        if (textWidth <= 0) {
            left = 0;
            return;
        }
        if (absoluteCol < left) {
            left = absoluteCol;
        } else if (absoluteCol >= left + textWidth) {
            left = absoluteCol - textWidth + 1;
        }
        if (left < 0) left = 0;
    }

    void scrollToCursor(const Cursor& cursor, const Document& doc, int textWidth) {
        int absoluteCol = 0;
        if (cursor.line >= 0 && cursor.line < doc.lineCount()) {
            absoluteCol = utf8::columnOf(doc.lineAt(cursor.line), cursor.col);
        }
        scrollToCursor(cursor, absoluteCol, textWidth);
    }
};
