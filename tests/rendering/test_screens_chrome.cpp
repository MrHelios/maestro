// Tests de integracion de las tres pantallas (paso 11): Editor,
// BufferSelector y FileBrowser comparten el MISMO chrome inferior
// (StatusBar + MessageBar).
// Cada pantalla solo construye un ChromeData y lo entrega al componente;
// el chrome (altura, posicion, background, padding, truncamiento y
// comportamiento ante resize) es IDENTICO en las tres.
//
// La unica diferencia entre pantallas debe ser el ChromeData.
//
// Estrategia: para cada pantalla (a) verificamos los invariantes del chrome
// sobre el frame completo, y (b) reconstruimos el ChromeData que la
// pantalla produce y comprobamos que las dos filas inferiores de su frame
// son EXACTAMENTE lo que pinta TtyChrome::render(area, ese dato). Asi se
// prueba que el chrome es compartido y que solo varia el ChromeData.
#include <algorithm>
#include <string>
#include <vector>

#include "test_framework.h"
#include "helpers/test_render_utils.h"

#include "document/Document.h"
#include "document/Cursor.h"
#include "layout/Layout.h"
#include "layout/Viewport.h"
#include "app/Message.h"
#include "rendering/Style.h"
#include "rendering/tty/TtyRenderer.h"
#include "rendering/tty/TtyChrome.h"

namespace {

using testutil::stripAnsi;
using testutil::colWidth;
using testutil::visibleRows;
using testutil::startsWith;

// Pares de filas del chrome (StatusBar superior + MessageBar).
struct BarRows {
    std::string fixed;
    std::string message;
};

// ---------------------------------------------------------------------------
// Frames de cada pantalla. Las tres pantallas reciben `content` FILAS DE
// CONTENIDO (viewport.height) y `width` columnas; el chrome se suma
// encima (total = content + kChromeRows), por eso les pasamos el MISMO
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
    TtyRenderer r;
    return r.buildScreen(doc, cursor, vp, "/ruta/proyecto/archivo.txt",
                         false, "", State::Navegacion, std::nullopt);
}

std::string frameBuffer(int content, int width, int selected = 1) {
    TtyRenderer tr;
    return tr.buildBufferListScreen({"b0.txt", "b1.txt", "b2.txt", "b3.txt"},
                                       selected, width, content);
}

std::string frameFile(int content, int width) {
    TtyRenderer tr;
    return tr.buildFileListScreen(
        std::vector<FileListItem>{
            {"a.txt", false}, {"b.txt", false}, {"c.txt", false}},
        0, 0, "/datos/proyecto", Message("ayuda: direcc de naveg"),
        width, content);
}

// Las DOS filas inferiores del frame (el chrome) en texto visible.
BarRows barOf(const std::string& frame) {
    const auto rows = visibleRows(frame);
    if (rows.size() < 2) return {rows.back(), ""};
    return {rows[rows.size() - 2], rows.back()};
}

// ---------------------------------------------------------------------------
// ChromeData que cada pantalla deberia estar produciendo (espejo de la
// logica en Renderer.cpp - si cambia Renderer, actualizar aqui). Al comprobar
// que el chrome del frame == TtyChrome::render(area, este dato), probamos que
// solo varia el dato; no prueba por si solo que los valores sean
// funcionalmente correctos (requiere cobertura de chrome unit).
// ---------------------------------------------------------------------------

ChromeData editorData() {
    const std::string filename = "/ruta/proyecto/archivo.txt";
    size_t slash = filename.find_last_of('/');
    ChromeData d;
    d.statusBar.name = filename.substr(slash + 1);              // baseName
    d.statusBar.path = (slash == 0) ? "/" : filename.substr(0, slash); // dirName
    d.statusBar.estado = "NAVEGACION";
    d.message = "";
    d.statusBar.cursorLine = 0;
    d.statusBar.cursorCol = 0;
    d.statusBar.totalLines = 3;
    return d;
}

ChromeData bufferData(int n = 4, int selected = 1) {
    ChromeData d;
    d.statusBar.name = "Buffers";
    d.statusBar.estado = "SELECCIONAR";
    d.statusBar.right = std::to_string(std::min(selected + 1, n)) + "/" +
              std::to_string(n);
    return d;
}

ChromeData fileData(int n = 3) {
    ChromeData d;
    d.statusBar.name = "/datos/proyecto";
    d.statusBar.estado = "ABRIR ARCHIVO";
    d.statusBar.right = "1/" + std::to_string(n);
    d.message = "ayuda: direcc de naveg";
    return d;
}

// Lo que deberia dibujar el chrome para un dato dado. La geometria del
// la barra para `content` filas de contenido y `width` columnas es
// computeLayout(content + kChromeRows, width).chrome; las tres
// pantallas la calculan igual.
BarRows expectedBar(const ChromeData& d, int content, int width) {
    Rect area = computeLayout(content + kChromeRows, width).chrome;
    return barOf(TtyChrome().render(area, d));
}

} // namespace

// ---------------------------------------------------------------------------
// Misma altura y misma posicion de la barra en las tres pantallas:
// siempre las DOS filas finales del frame (computeLayout reserva
// kChromeRows) y ocupan el mismo rango de filas.
// ---------------------------------------------------------------------------
TEST(integration_height_and_position_same_across_screens) {
    for (int content : {3, 8, 22}) {
        for (int width : {40, 80}) {
            // Total de filas = content (contenido) + kChromeRows (chrome)
            // en las tres pantallas.
            const int total = content + kChromeRows;
            const Layout layout = computeLayout(total, width);
            // Cada pantalla le pasa `content` filas de contenido y produce
            // `total` filas: las ultimas kChromeRows son el chrome.
            CHECK_EQ((int)visibleRows(frameEditor(content, width)).size(), total);
            CHECK_EQ((int)visibleRows(frameBuffer(content, width)).size(), total);
            CHECK_EQ((int)visibleRows(frameFile(content, width)).size(), total);
            // La barra arranca en la MISMA fila en las tres pantallas y
            // ocupa exactamente kChromeRows filas (las ultimas del frame).
            CHECK_EQ(layout.chrome.height, kChromeRows);
            CHECK_EQ(layout.chrome.row, content);
            CHECK_EQ(layout.chrome.row + layout.chrome.height, total);
        }
    }
}

// ---------------------------------------------------------------------------
// Las tres pantallas usan el chrome comun: sus dos filas inferiores coinciden
// EXACTAMENTE con TtyChrome::render(area, ChromeData). Es decir, el frame
// despliega exactamente el chrome que pinta el componente compartido, y solo
// cambia el ChromeData que cada pantalla produce.
// ---------------------------------------------------------------------------
TEST(integration_chrome_is_exactly_shared) {
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
// Mismo background: el StatusBar SIEMPRE lleva kStatusBarStyle
// en las tres pantallas; el MessageBar no lleva ese fondo
// (su ancho nunca excede `width`).
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
        CHECK_EQ(colWidth(barOf(frame).fixed), width);
    }
    // El MessageBar no lleva el fondo del StatusBar (sin relleno de ancho
    // completo: solo su contenido + paddings).
    for (const std::string& frame : screens) {
        CHECK(colWidth(barOf(frame).message) <= width);
    }
}

// ---------------------------------------------------------------------------
// Mismo padding: StatusBar y MessageBar arrancan con kChromePadLeft
// espacios (alineacion del texto) en las tres pantallas.
// ---------------------------------------------------------------------------
TEST(integration_same_padding_across_screens) {
    const int content = 22;
    const int width = 80;
    const std::string pad = std::string(kChromePadLeft, ' ');
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
// (incluidos muy angostos) el chrome de cada pantalla nunca escribe fuera del
// ancho; el StatusBar SIEMPRE llena exactamente `width` columnas y el
// MessageBar jamas lo excede.
// ---------------------------------------------------------------------------
TEST(integration_same_resize_behavior_across_screens) {
    for (int content : {4, 10, 22}) {
        for (int width = 1; width <= 60; ++width) {
            std::string a = frameEditor(content, width);
            std::string b = frameBuffer(content, width);
            std::string c = frameFile(content, width);

            for (const std::string& frame : {a, b, c}) {
                BarRows r = barOf(frame);
                CHECK_EQ(colWidth(r.fixed), width);
                CHECK(colWidth(r.message) <= width);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// v1.4: el ciclo de vida del frame (ocultar cursor / limpiar / home /
// mostrar) es responsabilidad del frame global, no de cada pantalla. Las tres
// pantallas arrancan con la MISMA secuencia de preludio y cierran con la
// misma de epilogo.
// ---------------------------------------------------------------------------
TEST(integration_frame_lifecycle_shared) {
    const std::string prelude = "\x1b[?25l\x1b[48;2;18;19;20m\x1b[2J\x1b[H";
    const std::string epilogue = "\x1b[?25h";
    for (const std::string& frame :
         {frameEditor(5, 80), frameBuffer(5, 80), frameFile(5, 80)}) {
        CHECK(frame.compare(0, prelude.size(), prelude) == 0);
        CHECK(frame.compare(frame.size() - epilogue.size(), epilogue.size(),
                            epilogue) == 0);
    }
}

// ---------------------------------------------------------------------------
// Pantalla "Guardar como" (v0.9): mismo chrome compartido que abrir, con
// estado GUARDAR COMO. El input del nombre vive en el MessageBar
// (debajo del StatusBar), compuesto por el Editor en el Message: se trunca
// al ancho (nunca desborda) y el accent semantico es el de guardar.
// ---------------------------------------------------------------------------
std::string frameSaveAs(int content, int width, const Message& message) {
    TtyRenderer tr;
    return tr.buildSaveAsFileListScreen(
        std::vector<FileListItem>{
            {"sub", true}, {"a.txt", false}, {"b.txt", false}},
        0, 0, "/datos/proyecto", message, width, content);
}

Message saveAsInput(const std::string& fileName) {
    return Message{"Nombre del Archivo: " + fileName + " (Control+S para Guardar)",
                   MessageKind::Prompt, std::nullopt};
}

TEST(saveas_screen_shows_input_line_and_estado) {
    const std::string frame = frameSaveAs(8, 80, saveAsInput("notas.txt"));
    const std::string plain = stripAnsi(frame);
    CHECK(plain.find("Nombre del Archivo: notas.txt") != std::string::npos);
    CHECK(plain.find("Control+S para Guardar") != std::string::npos);
    CHECK(plain.find("GUARDAR COMO") != std::string::npos);
    // Los items del listado se siguen viendo igual que en abrir.
    CHECK(plain.find("a.txt") != std::string::npos);
    CHECK(plain.find("sub/") != std::string::npos);
}

TEST(saveas_screen_input_lives_in_messagebar_below_statusbar) {
    const std::string frame = frameSaveAs(8, 80, saveAsInput("notas.txt"));
    BarRows bar = barOf(frame);
    // El StatusBar conserva el estado; el MessageBar lleva el input.
    CHECK(bar.fixed.find("GUARDAR COMO") != std::string::npos);
    CHECK(bar.message.find("Nombre del Archivo: notas.txt") != std::string::npos);
    // Posición: el input es la ÚLTIMA fila (debajo del StatusBar) y no
    // aparece en ninguna fila de contenido por encima del chrome.
    const auto rows = visibleRows(frame);
    CHECK(rows.size() >= 3);
    CHECK_EQ(rows.back(), bar.message);
    for (size_t i = 0; i + 2 < rows.size(); ++i)
        CHECK(rows[i].find("Nombre del Archivo:") == std::string::npos);
}

TEST(saveas_screen_truncates_long_input_to_width) {
    const int width = 40;
    const std::string frame =
        frameSaveAs(8, width, saveAsInput(std::string(200, 'x')));
    const std::string plain = stripAnsi(frame);
    // El nombre completo NO viaja en los bytes: el MessageBar se
    // trunco al ancho, pero el prefijo del input sigue visible.
    CHECK(plain.find(std::string(200, 'x')) == std::string::npos);
    CHECK(plain.find("Nombre del Archivo: ") != std::string::npos);
    for (const std::string& row : visibleRows(frame))
        CHECK(colWidth(row) <= width);
}

TEST(saveas_screen_accent_role_is_guardar) {
    CHECK(accentRoleFor(State::SaveAsFileBrowser) == StyleRole::AccentGuardar);
}
