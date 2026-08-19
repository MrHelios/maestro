// Tests de integracion de las tres pantallas (paso 11): Editor,
// BufferSelector y FileBrowser comparten la MISMA barra comun (StatusBar).
// Cada pantalla solo construye un StatusBarData y lo entrega al componente;
// el chrome (altura, posicion, background, padding, truncamiento y
// comportamiento ante resize) es IDENTICO en las tres.
//
// La unica diferencia entre pantallas debe ser el StatusBarData.
//
// Estrategia: para cada pantalla (a) verificamos los invariantes del chrome
// sobre el frame completo, y (b) reconstruimos el StatusBarData que la
// pantalla produce y comprobamos que las dos filas inferiores de su frame
// son EXACTAMENTE lo que pinta StatusBar::render(area, ese dato). Asi se
// prueba que el chrome es compartido y que solo varia el StatusBarData.
#include <algorithm>
#include <string>
#include <vector>

#include "test_framework.h"

#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Layout.h"
#include "core/Viewport.h"
#include "ui/Message.h"
#include "ui/Renderer.h"
#include "ui/StatusBar.h"

namespace {

// Quita las secuencias ANSI dejando solo el texto visible.
std::string stripAnsiLocal(const std::string& s) {
    std::string out;
    bool inEsc = false;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\x1b') {
            inEsc = true;
            if (i + 1 < s.size() && s[i + 1] == '[') i++;
        } else if (inEsc) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c >= 0x40 && c <= 0x7E) inEsc = false;
        } else {
            out += s[i];
        }
        i++;
    }
    return out;
}

// Columnas visuales (1 por celda; basta para limites de ancho ASCII).
int colWidthLocal(const std::string& s) {
    int col = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) col++;
    return col;
}

// Las filas visibles del frame (ya sin ANSI ni \r).
std::vector<std::string> visibleRows(const std::string& frame) {
    std::string plain = stripAnsiLocal(frame);
    std::vector<std::string> out;
    std::string cur;
    for (char c : plain) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

// Pares de filas de la barra (fija superior + mensajes).
struct BarRows {
    std::string fixed;
    std::string message;
};

bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

// ---------------------------------------------------------------------------
// Frames de cada pantalla. Las tres pantallas reciben `content` FILAS DE
// CONTENIDO (viewport.height) y `width` columnas; la barra comun se suma
// encima (total = content + kStatusBarRows), por eso les pasamos el MISMO
// content/width a las tres para comparar el chrome en igualdad de
// condiciones.
// ---------------------------------------------------------------------------

std::string frameEditor(int content, int width) {
    Document doc;
    doc.restore({"linea uno", "linea dos", "tercera linea"});
    Viewport vp;
    vp.top = 0;
    vp.height = content;
    vp.width = width;
    Cursor cursor;
    cursor.line = 0;
    cursor.col = 0;
    Renderer r;
    return r.buildScreen(doc, cursor, vp, "/ruta/proyecto/archivo.txt",
                         false, "", State::Navegacion, std::nullopt);
}

std::string frameBuffer(int content, int width, int selected = 1) {
    Renderer r;
    return r.buildBufferListScreen({"b0.txt", "b1.txt", "b2.txt", "b3.txt"},
                                   selected, width, content);
}

std::string frameFile(int content, int width) {
    Renderer r;
    return r.buildFileListScreen({"a.txt", "b.txt", "c.txt"}, 0, 0,
                                 "/datos/proyecto", Message("ayuda: direcc de naveg"),
                                 width, content);
}

// Las DOS filas inferiores del frame (la barra comun) en texto visible.
BarRows barOf(const std::string& frame) {
    const auto rows = visibleRows(frame);
    if (rows.size() < 2) return {rows.back(), ""};
    return {rows[rows.size() - 2], rows.back()};
}

// ---------------------------------------------------------------------------
// StatusBarData que cada pantalla deberia estar produciendo (espejo de la
// logica en Renderer.cpp). Al comprobar que la barra del frame ==
// StatusBar::render(area, este dato), probamos que solo varia el dato.
// ---------------------------------------------------------------------------

StatusBarData editorData() {
    const std::string filename = "/ruta/proyecto/archivo.txt";
    size_t slash = filename.find_last_of('/');
    StatusBarData d;
    d.name = filename.substr(slash + 1);              // baseName
    d.path = (slash == 0) ? "/" : filename.substr(0, slash); // dirName
    d.estado = "NAVEGACION";
    d.message = "";
    d.cursorLine = 0;
    d.cursorCol = 0;
    d.totalLines = 3;
    return d;
}

StatusBarData bufferData(int n = 4, int selected = 1) {
    StatusBarData d;
    d.name = "Buffers";
    d.estado = "SELECCIONAR";
    d.right = std::to_string(std::min(selected + 1, n)) + "/" +
              std::to_string(n);
    return d;
}

StatusBarData fileData(int n = 3) {
    StatusBarData d;
    d.name = "/datos/proyecto";
    d.estado = "ABRIR ARCHIVO";
    d.right = "1/" + std::to_string(n);
    d.message = "ayuda: direcc de naveg";
    return d;
}

// Lo que deberia dibujar la barra comun para un dato dado. La geometria de
// la barra para `content` filas de contenido y `width` columnas es
// computeLayout(content + kStatusBarRows, width).statusBar; las tres
// pantallas la calculan igual.
BarRows expectedBar(const StatusBarData& d, int content, int width) {
    Rect area = computeLayout(content + kStatusBarRows, width).statusBar;
    return barOf(StatusBar().render(area, d));
}

} // namespace

// ---------------------------------------------------------------------------
// Misma altura y misma posicion de la barra en las tres pantallas:
// siempre las DOS filas finales del frame (computeLayout reserva
// kStatusBarRows) y ocupan el mismo rango de filas.
// ---------------------------------------------------------------------------
TEST(integration_height_and_position_same_across_screens) {
    for (int content : {3, 8, 22}) {
        for (int width : {40, 80}) {
            // Total de filas = content (contenido) + kStatusBarRows (chrome)
            // en las tres pantallas.
            const int total = content + kStatusBarRows;
            const Layout layout = computeLayout(total, width);
            // Cada pantalla le pasa `content` filas de contenido y produce
            // `total` filas: las ultimas kStatusBarRows son el chrome.
            CHECK_EQ((int)visibleRows(frameEditor(content, width)).size(), total);
            CHECK_EQ((int)visibleRows(frameBuffer(content, width)).size(), total);
            CHECK_EQ((int)visibleRows(frameFile(content, width)).size(), total);
            // La barra arranca en la MISMA fila en las tres pantallas y
            // ocupa exactamente kStatusBarRows filas (las ultimas del frame).
            CHECK_EQ(layout.statusBar.height, kStatusBarRows);
            CHECK_EQ(layout.statusBar.row, content);
            CHECK_EQ(layout.statusBar.row + layout.statusBar.height, total);
        }
    }
}

// ---------------------------------------------------------------------------
// Las tres pantallas usan la barra comun: sus dos filas inferiores coinciden
// EXACTAMENTE con StatusBar::render(area, StatusBarData). Es decir, el frame
// despliega exactamente el chrome que pinta el componente compartido, y solo
// cambia el StatusBarData que cada pantalla produce.
// ---------------------------------------------------------------------------
TEST(integration_chrome_is_exactly_shared_statusbar) {
    for (int content : {6, 22}) {
        for (int width : {30, 80}) {
            BarRows ed = expectedBar(editorData(), content, width);
            BarRows bd = expectedBar(bufferData(), content, width);
            BarRows fd = expectedBar(fileData(), content, width);

            BarRows ef = barOf(frameEditor(content, width));
            BarRows bf = barOf(frameBuffer(content, width));
            BarRows ff = barOf(frameFile(content, width));

            CHECK_EQ(ef.fixed, ed.fixed);
            CHECK_EQ(ef.message, ed.message);
            CHECK_EQ(bf.fixed, bd.fixed);
            CHECK_EQ(bf.message, bd.message);
            CHECK_EQ(ff.fixed, fd.fixed);
            CHECK_EQ(ff.message, fd.message);
        }
    }
}

// ---------------------------------------------------------------------------
// Mismo background: la fila fija SIEMPRE lleva el estilo de fondo gris 60%
// (kStatusBarStyle) en las tres pantallas; la fila de mensajes jamas lo
// lleva (es texto plano coloreado por tipo, sin fondo).
// ---------------------------------------------------------------------------
TEST(integration_same_background_across_screens) {
    const int content = 22;
    const int width = 80;
    const std::string screens[] = {
        frameEditor(content, width),
        frameBuffer(content, width),
        frameFile(content, width),
    };
    for (const std::string& frame : screens) {
        CHECK(frame.find(std::string(kStatusBarStyle)) != std::string::npos);
        // El texto fijo (ya sin ANSI) llena todo el ancho con ese fondo.
        CHECK_EQ(colWidthLocal(barOf(frame).fixed), width);
    }
    // La fila de mensajes no lleva el fondo de la barra (sin relleno de ancho
    // completo: solo su contenido + paddings).
    for (const std::string& frame : screens) {
        CHECK(colWidthLocal(barOf(frame).message) <= width);
    }
}

// ---------------------------------------------------------------------------
// Mismo padding: las dos filas de la barra arrancan con kStatusBarPadLeft
// espacios (alineacion del texto) en las tres pantallas.
// ---------------------------------------------------------------------------
TEST(integration_same_padding_across_screens) {
    const int content = 22;
    const int width = 80;
    const std::string pad = std::string(kStatusBarPadLeft, ' ');
    for (const std::string& frame :
         {frameEditor(content, width), frameBuffer(content, width),
          frameFile(content, width)}) {
        BarRows r = barOf(frame);
        CHECK(startsWith(r.fixed, pad));
        CHECK(startsWith(r.message, pad));
    }
}

// ---------------------------------------------------------------------------
// Mismo truncamiento y mismo comportamiento ante resize: en cualquier ancho
// (incluidos muy angostos) la barra de cada pantalla nunca escribe fuera del
// ancho; la fila fija SIEMPRE llena exactamente `width` columnas y la fila
// de mensajes jamas lo excede.
// ---------------------------------------------------------------------------
TEST(integration_same_resize_behavior_across_screens) {
    for (int content : {4, 10, 22}) {
        for (int width = 1; width <= 60; ++width) {
            std::string a = frameEditor(content, width);
            std::string b = frameBuffer(content, width);
            std::string c = frameFile(content, width);

            for (const std::string& frame : {a, b, c}) {
                BarRows r = barOf(frame);
                CHECK_EQ(colWidthLocal(r.fixed), width);
                CHECK(colWidthLocal(r.fixed) <= width);
                CHECK(colWidthLocal(r.message) <= width);
            }
        }
    }
}