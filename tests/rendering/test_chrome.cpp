// Tests del chrome inferior (paso 11). Probamos TtyChrome y ChromeData
// DIRECTAMENTE (render(area, data)), sin pasar por el Editor ni el Renderer:
// el chrome (StatusBar + MessageBar) es un componente propio que solo recibe
// texto/numeros y devuelve la secuencia ANSI.
//
// Casos del plan:
//   - left corto / center corto / right corto        -> cada bloque cabe
//   - left demasiado largo / path demasiado largo     -> se trunca el/los
//     bloque(s) izquierdo(s) sin desbordar
//   - todos demasiado largos                          -> cooperacion de los
//     sacrificios (path -> nombre -> estado -> mensaje)
//   - terminal extremadamente angosto                 -> nada desborda
//
// Invariante central de todos los casos: la barra NUNCA escribe fuera del
// ancho del area; cada fila visible mide a lo sumo `area.width` columnas.
#include <algorithm>
#include <string>
#include <vector>

#include "test_framework.h"
#include "helpers/test_render_utils.h"

#include "layout/Layout.h"
#include "app/Message.h"
#include "rendering/tty/TtyChrome.h"
#include "rendering/tty/TtyRenderer.h"

namespace {

using testutil::stripAnsi;
using testutil::colWidth;
using testutil::contains;

// Fila superior (StatusBar) e inferior (MessageBar) del texto ya sin ANSI,
// separadas por \r\n.
struct Rows {
    std::string fixed;   // barra de estado superior
    std::string message; // MessageBar (fila inferior)
};

Rows rowsOf(const std::string& out) {
    std::string plain = stripAnsi(out);
    size_t sep = plain.find("\r\n");
    if (sep == std::string::npos) return {plain, ""};
    return {plain.substr(0, sep), plain.substr(sep + 2)};
}

// Renderiza con un area 2 filas x `w` columnas y devuelve el par de filas.
Rows renderRows(const ChromeData& data, int w) {
    Rect area;
    area.width = w;
    area.height = 2;
    return rowsOf(TtyChrome().render(area, data));
}

std::string longStr(int n, char c = 'n') {
    return std::string(static_cast<size_t>(n), c);
}

} // namespace

// ---------------------------------------------------------------------------
// left corto: nombre, ruta y estado cortos en una terminal generosa. El
// bloque izquierdo cabe entero y el derecho queda anclado a la derecha.
// ---------------------------------------------------------------------------
TEST(chrome_left_corto) {
    ChromeData d;
    d.statusBar.name = "archivo.txt";
    d.statusBar.path = "/home/usuario";
    d.statusBar.estado = "NAVEGACION";
    d.statusBar.totalLines = 1;

    Rows r = renderRows(d, 80);
    // La fila fija ocupa exactamente el ancho del area (anclado a la
    // derecha: el bloque "pct% (fila,col)" termina en el ultimo caracter).
    CHECK_EQ(colWidth(r.fixed), 80);
    CHECK(contains(r.fixed, "archivo.txt"));
    CHECK(contains(r.fixed, "/home/usuario"));
    CHECK(contains(r.fixed, "NAVEGACION"));
    CHECK(contains(r.fixed, "0% (1,1)"));
    // El bloque derecho queda pegado al borde derecho: tras el ultimo espacio
    // no hay texto, el ")" del (1,1) es lo ultimo.
    CHECK_EQ(r.fixed.back(), ')');
}

// ---------------------------------------------------------------------------
// center corto: en un ancho ajustado el relleno central es minimo/cero y el
// bloque derecho sigue anclado a la derecha sin pisarse con el izquierdo.
// ---------------------------------------------------------------------------
TEST(chrome_center_corto) {
    ChromeData d;
    d.statusBar.name = "archivo.txt"; // 11 columnas
    d.statusBar.estado = "SELECCION"; // 9 columnas
    d.statusBar.totalLines = 1;

    // Presupuesto del bloque izquierdo en w=35:
    //   35 - (padL 1 + padR 3 + right 8) = 23 -> caben nombre + " - " + estado.
    // El relleno central queda en cero o una columna de sobra.
    for (int w = 35; w <= 37; ++w) {
        Rows r = renderRows(d, w);
        CHECK_EQ(colWidth(r.fixed), w); // la fila ocupa todo el ancho y nunca lo excede
        CHECK(colWidth(r.message) <= w);
    }
    // El bloque derecho nunca se pisa con el izquierdo: (1,1) pegado al borde.
    Rows r = renderRows(d, 35);
    CHECK(contains(r.fixed, "archivo.txt - SELECCION"));
    CHECK(contains(r.fixed, "0% (1,1)"));
    CHECK_EQ(r.fixed.back(), ')');
}

// ---------------------------------------------------------------------------
// right corto: sobreescritura explicita del bloque derecho (valor forzado, pantallas sin
// documento) de pocas columnas; se usa tal cual y se ancla a la derecha.
// ---------------------------------------------------------------------------
TEST(chrome_right_corto) {
    ChromeData d;
    d.statusBar.name = "archivo.txt";
    d.statusBar.estado = "NAVEGACION";
    d.statusBar.right = "2/5";               // override: sin documento no hay pct (fila,col)
    d.statusBar.totalLines = 0;

    Rows r = renderRows(d, 40);
    CHECK_EQ(colWidth(r.fixed), 40);
    CHECK(contains(r.fixed, "2/5"));
    CHECK(!contains(r.fixed, "% ("));
    CHECK(!contains(r.fixed, "(1,1)"));
    CHECK_EQ(r.fixed.back(), '5');
}

// ---------------------------------------------------------------------------
// left demasiado largo: el nombre solo no cabe ni con el maximo fijo
// (kNameMax=30); se trunca y nunca desborda el ancho.
// ---------------------------------------------------------------------------
TEST(chrome_left_demasiado_largo) {
    ChromeData d;
    d.statusBar.name = longStr(80); // muy por encima de kNameMax
    d.statusBar.estado = "NAVEGACION";
    d.statusBar.totalLines = 1;

    for (int w = 12; w <= 80; w += 7) {
        Rows r = renderRows(d, w);
        CHECK_EQ(colWidth(r.fixed), w); // ocupa todo el ancho sin excederlo
        CHECK(colWidth(r.message) <= w);
    }
    // El estado NO se sacrifica antes que el nombre: se ve entero.
    Rows r = renderRows(d, 80);
    CHECK(contains(r.fixed, "NAVEGACION"));
    CHECK(contains(r.fixed, "0% (1,1)"));
    CHECK(!contains(r.fixed, longStr(80))); // el nombre completo no aparece
}

// ---------------------------------------------------------------------------
// path demasiado largo: la ruta se sacrifica ANTES que el nombre (truncada
// por la IZQUIERDA con "..." al inicio) y el derecho queda intacto.
// ---------------------------------------------------------------------------
TEST(chrome_path_demasiado_largo) {
    ChromeData d;
    d.statusBar.name = "archivo.txt";
    d.statusBar.path = "/" + longStr(80) + "/cola_final.txt"; // muy larga
    d.statusBar.estado = "NAVEGACION";
    d.statusBar.totalLines = 1;

    for (int w = 20; w <= 80; w += 5) {
        Rows r = renderRows(d, w);
        CHECK_EQ(colWidth(r.fixed), w); // ocupa todo el ancho sin excederlo
        CHECK(colWidth(r.message) <= w);
    }
    // En ancho generoso el nombre queda entero y la ruta truncada al frente.
    Rows r = renderRows(d, 80);
    CHECK(contains(r.fixed, "archivo.txt"));
    CHECK(contains(r.fixed, "..."));        // la ruta se corto por la izquierda
    CHECK(contains(r.fixed, "cola_final.txt")); // se conserva la cola de la ruta
    CHECK(contains(r.fixed, "NAVEGACION"));
    CHECK(contains(r.fixed, "0% (1,1)"));
}

// ---------------------------------------------------------------------------
// todos demasiado largos: nombre, ruta, estado, mensaje y bloque derecho
// juntos; cada uno cede lo suyo sin que ninguna fila desborde el ancho.
// ---------------------------------------------------------------------------
TEST(chrome_todos_demasiado_largos) {
    ChromeData d;
    d.statusBar.name = longStr(80);
    d.statusBar.path = "/" + longStr(80) + "/x.txt";
    d.statusBar.estado = longStr(40);
    d.message = longStr(120, 'm');
    d.statusBar.right = longStr(40);
    d.statusBar.totalLines = 1000;
    d.statusBar.cursorLine = 256;
    d.statusBar.cursorCol = 512;

    for (int w = 1; w <= 100; ++w) {
        Rows r = renderRows(d, w);
        CHECK_EQ(colWidth(r.fixed), w); // la barra fija SIEMPRE llena el ancho
        CHECK(colWidth(r.message) <= w);
    }
}

// ---------------------------------------------------------------------------
// terminal extremadamente angosto: desde 1 columna en adelante la barra
// nunca escribe fuera del ancho, por mas largo que sea el contenido. Es el
// caso que el fix de v1.1 corrigio (antes la fila fija emitia minimo 12
// columnas y la de mensajes 4, desbordando en terminales mas chicas).
// ---------------------------------------------------------------------------
TEST(chrome_terminal_extremadamente_angosta) {
    ChromeData d;
    d.statusBar.name = longStr(80);
    d.statusBar.path = "/" + longStr(80);
    d.statusBar.estado = longStr(30);
    d.message = longStr(120, 'm');
    d.statusBar.totalLines = 1;

    for (int w = 1; w <= 11; ++w) {
        Rows r = renderRows(d, w);
        CHECK_EQ(colWidth(r.fixed), w); // llena exactamente y nunca desborda
        CHECK(colWidth(r.message) <= w);
        CHECK(!r.fixed.empty());
    }
}

// ---------------------------------------------------------------------------
// Casos limite del bloque derecho: sin totalLines (0 lineas) no hay pct
// porcentual; con varias lineas el pct se calcula. En ambos el ancho se
// respeta y el bloque queda a la derecha.
// ---------------------------------------------------------------------------
TEST(chrome_right_block_edge_layout) {
    for (int w = 15; w <= 40; w += 5) {
        ChromeData d;
        d.statusBar.name = "a.txt";
        d.statusBar.estado = "NAVEGACION";
        d.statusBar.totalLines = 5;
        d.statusBar.cursorLine = 2; // 2/(5-1) = 50%
        Rows r = renderRows(d, w);
        CHECK_EQ(colWidth(r.fixed), w);
        CHECK_EQ(r.fixed.back(), ')'); // el (fila,col) cabe entero aca
    }
    // Sin documento (totalLines=0): el bloque derecho se calcula igual
    // (pct 0, (1,1)) si no hay override; 0% -> 1 columna, ancho respetado.
    ChromeData d;
    d.statusBar.name = "a.txt";
    d.statusBar.estado = "NAVEGACION";
    d.statusBar.totalLines = 0;
    Rows r = renderRows(d, 30);
    CHECK_EQ(colWidth(r.fixed), 30);
    CHECK(contains(r.fixed, "0% (1,1)"));
    CHECK_EQ(r.fixed.back(), ')');
}

// ---------------------------------------------------------------------------
// v1.3/evolución Fase E: el accent de la etiqueta de estado viaja como
// StyleRole (parámetro, nunca en el DTO); el default usa statusBarAccent.
// ---------------------------------------------------------------------------
TEST(chrome_estado_accent_from_role) {
    TtyTheme t = defaultTheme();
    t.statusBarAccent = "\x1b[34m";      // azul (default)
    t.accentNavegacion = "\x1b[33m";     // amarillo (estado activo)
    TtyChrome bar;
    bar.setTheme(t);
    Rect area; area.width = 40; area.height = 2;

    ChromeData d;
    d.statusBar.name = "a.txt";
    d.statusBar.estado = "NAVEGACION";
    d.statusBar.totalLines = 1;

    const std::string fallback = bar.render(area, d);
    CHECK(fallback.find(t.statusBarAccent) != std::string::npos);

    const std::string withAccent =
        bar.render(area, d, StyleRole::AccentNavegacion);
    CHECK(withAccent.find(t.accentNavegacion) != std::string::npos);
    CHECK(withAccent.find(t.statusBarAccent) == std::string::npos);
    CHECK(withAccent != fallback);
}

// ---------------------------------------------------------------------------
// Fase E paso 4: las pantallas de listas usan sus roles (no el default).
// Antes lo garantizaba el campo estadoAccent del DTO; ahora el rol viaja
// como parámetro. Tema custom con los tres ANSI distintos para que el
// test distinga rol vs default.
// ---------------------------------------------------------------------------
TEST(chrome_list_screens_use_their_roles) {
    TtyTheme t = defaultTheme();
    t.statusBarAccent = "\x1b[34m";
    t.accentBuffers = "\x1b[31m";
    t.accentAbrir = "\x1b[32m";
    TtyRenderer tr;
    tr.setTheme(t);

    const std::string buf = tr.buildBufferListScreen({"a.txt"}, 0, 80, 5);
    CHECK(buf.find(t.accentBuffers) != std::string::npos);
    CHECK(buf.find(t.statusBarAccent) == std::string::npos);

    const std::string file = tr.buildFileListScreen(
        std::vector<FileListItem>{{"a.txt", false}}, 0, 0, "/ruta",
        Message("ayuda"), 80, 5);
    CHECK(file.find(t.accentAbrir) != std::string::npos);
    CHECK(file.find(t.statusBarAccent) == std::string::npos);
}

// ---------------------------------------------------------------------------
// v1.4: el indicador "[*]" se pinta con statusBarModified (no con
// statusBarName), distinto del nombre y nunca presente si no hay cambios.
// ---------------------------------------------------------------------------
TEST(chrome_modified_indicator_styled) {
    TtyTheme t = defaultTheme();
    t.statusBarModified = "\x1b[1;38;5;200m";
    TtyChrome bar;
    bar.setTheme(t);
    Rect area; area.width = 60; area.height = 2;

    ChromeData d;
    d.statusBar.name = "x.cc";
    d.statusBar.modified = true;
    d.statusBar.estado = "NAVEGACION";
    d.statusBar.totalLines = 1;

    const std::string out = bar.render(area, d);
    CHECK(out.find(t.statusBarModified + " [*]") != std::string::npos);

    ChromeData c = d;
    c.statusBar.modified = false;
    CHECK(bar.render(area, c).find(" [*]") == std::string::npos);
}

// ---------------------------------------------------------------------------
// v1.3: un mensaje de tipo Prompt se pinta con theme.prompt;
// los mensajes Info no llevan ese estilo.
// ---------------------------------------------------------------------------
TEST(chrome_prompt_message_styled) {
    TtyTheme t = defaultTheme();
    // Italica: distintiva, no colisiona con el bold de la etiqueta de estado.
    t.prompt = "\x1b[3m";
    TtyChrome bar;
    bar.setTheme(t);
    Rect area; area.width = 60; area.height = 2;

    ChromeData d;
    d.statusBar.name = "x";
    d.statusBar.estado = "NAVEGACION";
    d.statusBar.totalLines = 1;
    d.message = Message("Guardar archivo: /tmp/x", MessageKind::Prompt,
                        std::nullopt);

    const std::string out = bar.render(area, d);
    CHECK(out.find(t.prompt + "Guardar archivo: /tmp/x") != std::string::npos);

    ChromeData c = d;
    c.message = Message("ayuda", MessageKind::Info, std::nullopt);
    CHECK(bar.render(area, c).find(t.prompt) == std::string::npos);
}

// ---------------------------------------------------------------------------
// Contrato de composicion: ChromeData = StatusBar + MessageBar independientes.
// Cambio solo en statusBar -> solo fila superior; cambio solo en message -> solo fila inferior.
// Congela que renderStatusBar/renderMessageBar no se pisan entre si.
// ---------------------------------------------------------------------------
TEST(chrome_status_and_message_change_isolated_rows) {
    TtyChrome bar;
    ChromeData base;
    base.statusBar.name = "a.txt";
    base.statusBar.path = "/ruta";
    base.statusBar.estado = "NAVEGACION";
    base.statusBar.totalLines = 3;
    base.message = Message("msg uno", MessageKind::Info, std::nullopt);

    // Solo StatusBar: cambia el nombre, mismo Message.
    ChromeData onlyStatusBar = base;
    onlyStatusBar.statusBar.name = "b.txt";
    CHECK(bar.renderStatusBar(80, onlyStatusBar.statusBar) !=
          bar.renderStatusBar(80, base.statusBar));
    CHECK_EQ(bar.renderMessageBar(80, onlyStatusBar.message),
             bar.renderMessageBar(80, base.message));
    Rows rs = rowsOf(bar.render({0, 0, 80, 2}, onlyStatusBar));
    Rows r0 = rowsOf(bar.render({0, 0, 80, 2}, base));
    CHECK(rs.fixed != r0.fixed);
    CHECK_EQ(rs.message, r0.message);

    // Solo MessageBar: mismo StatusBar, cambia el texto.
    ChromeData onlyMsg = base;
    onlyMsg.message = Message("msg dos", MessageKind::Info, std::nullopt);
    CHECK_EQ(bar.renderStatusBar(80, onlyMsg.statusBar),
             bar.renderStatusBar(80, base.statusBar));
    CHECK(bar.renderMessageBar(80, onlyMsg.message) !=
          bar.renderMessageBar(80, base.message));
    Rows rm = rowsOf(bar.render({0, 0, 80, 2}, onlyMsg));
    CHECK_EQ(rm.fixed, r0.fixed);
    CHECK(rm.message != r0.message);
}

// ---------------------------------------------------------------------------
// Contrato de altura (espejo de computeLayout): height == 1 -> solo StatusBar
// (byte a byte igual a renderStatusBar, sin separador de filas); height == 0 ->
// sin chrome (string vacio, append no toca el buffer).
// ---------------------------------------------------------------------------
TEST(chrome_height_one_renders_only_status) {
    TtyChrome bar;
    ChromeData d;
    d.statusBar.name = "a.txt";
    d.statusBar.estado = "NAVEGACION";
    d.statusBar.totalLines = 3;
    d.message = Message("msg visible", MessageKind::Info, std::nullopt);

    for (int w : {1, 20, 80}) {
        const std::string full = bar.render({0, 0, w, 1}, d);
        CHECK_EQ(full, bar.renderStatusBar(w, d.statusBar));
        CHECK(full.find("\r\n") == std::string::npos);
        CHECK(!full.empty());
    }
}

TEST(chrome_height_zero_renders_empty) {
    TtyChrome bar;
    ChromeData d;
    d.statusBar.name = "a.txt";
    d.statusBar.estado = "NAVEGACION";
    d.statusBar.totalLines = 3;
    d.message = Message("msg", MessageKind::Info, std::nullopt);

    for (int w : {0, 1, 80}) {
        CHECK(bar.render({0, 0, w, 0}, d).empty());
        std::string out = "prefijo";
        bar.append(out, {0, 0, w, 0}, d);
        CHECK_EQ(out, "prefijo");
    }
}
