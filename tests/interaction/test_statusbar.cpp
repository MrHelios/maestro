// Tests de la barra de estado (paso 11). Probamos StatusBar y StatusBarData
// DIRECTAMENTE (render(area, data)), sin pasar por el Editor ni el Renderer:
// la barra es un componente propio que solo recibe texto/numeros y devuelve
// la secuencia ANSI.
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

#include "core/Layout.h"
#include "ui/Message.h"
#include "ui/StatusBar.h"

namespace {

// Quita las secuencias ANSI dejando solo el texto visible.
std::string stripAnsi(const std::string& s) {
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

// Columnas visuales (1 por celda). Suficiente para validar limites de ancho
// sobre texto ASCII; para UTF-8 lo mismo que colCount() del prompt.
int colWidth(const std::string& s) {
    int col = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) col++;
    return col;
}

// Fila 1 y fila 2 (mensajes) del texto ya sin ANSI, separadas por \r\n.
struct Rows {
    std::string fixed;   // barra de estado superior
    std::string message; // fila de mensajes
};

Rows rowsOf(const std::string& out) {
    std::string plain = stripAnsi(out);
    size_t sep = plain.find("\r\n");
    if (sep == std::string::npos) return {plain, ""};
    return {plain.substr(0, sep), plain.substr(sep + 2)};
}

// Renderiza con un area 2 filas x `w` columnas y devuelve el par de filas.
Rows renderRows(const StatusBarData& data, int w) {
    Rect area;
    area.width = w;
    area.height = 2;
    return rowsOf(StatusBar().render(area, data));
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

std::string longStr(int n, char c = 'n') {
    return std::string(static_cast<size_t>(n), c);
}

} // namespace

// ---------------------------------------------------------------------------
// left corto: nombre, ruta y estado cortos en una terminal generosa. El
// bloque izquierdo cabe entero y el derecho queda anclado a la derecha.
// ---------------------------------------------------------------------------
TEST(statusbar_left_corto) {
    StatusBarData d;
    d.name = "archivo.txt";
    d.path = "/home/usuario";
    d.estado = "NAVEGACION";
    d.totalLines = 1;

    Rows r = renderRows(d, 80);
    // La x de la fila fija es exactamente el ancho del area (anclado a la
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
TEST(statusbar_center_corto) {
    StatusBarData d;
    d.name = "archivo.txt"; // 11 columnas
    d.estado = "SELECCION"; // 9 columnas
    d.totalLines = 1;

    // Presupuesto del bloque izquierdo en w=35:
    //   35 - (padL 1 + padR 3 + right 8) = 23 -> caben nombre + " - " + estado.
    // El relleno central queda en cero o una columna de sobra.
    for (int w = 35; w <= 37; ++w) {
        Rows r = renderRows(d, w);
        CHECK_EQ(colWidth(r.fixed), w); // la fila ocupa todo el ancho...
        CHECK(colWidth(r.fixed) <= w);  // ...pero nunca lo excede
        CHECK(colWidth(r.message) <= w);
    }
    // El bloque derecho nunca se pisa con el izquierdo: (1,1) pegado al borde.
    Rows r = renderRows(d, 35);
    CHECK(contains(r.fixed, "archivo.txt - SELECCION"));
    CHECK(contains(r.fixed, "0% (1,1)"));
    CHECK_EQ(r.fixed.back(), ')');
}

// ---------------------------------------------------------------------------
// right corto: override explicito del bloque derecho (pantallas sin
// documento) de pocas columnas; se usa tal cual y se ancla a la derecha.
// ---------------------------------------------------------------------------
TEST(statusbar_right_corto) {
    StatusBarData d;
    d.name = "archivo.txt";
    d.estado = "NAVEGACION";
    d.right = "2/5";               // override: sin documento no hay pct (fila,col)
    d.totalLines = 0;

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
TEST(statusbar_left_demasiado_largo) {
    StatusBarData d;
    d.name = longStr(80); // muy por encima de kNameMax
    d.estado = "NAVEGACION";
    d.totalLines = 1;

    for (int w = 12; w <= 80; w += 7) {
        Rows r = renderRows(d, w);
        CHECK_EQ(colWidth(r.fixed), w); // ocupa todo el ancho
        CHECK(colWidth(r.fixed) <= w);  // sin excederlo
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
TEST(statusbar_path_demasiado_largo) {
    StatusBarData d;
    d.name = "archivo.txt";
    d.path = "/" + longStr(80) + "/cola_final.txt"; // muy larga
    d.estado = "NAVEGACION";
    d.totalLines = 1;

    for (int w = 20; w <= 80; w += 5) {
        Rows r = renderRows(d, w);
        CHECK_EQ(colWidth(r.fixed), w);
        CHECK(colWidth(r.fixed) <= w);
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
TEST(statusbar_todos_demasiado_largos) {
    StatusBarData d;
    d.name = longStr(80);
    d.path = "/" + longStr(80) + "/x.txt";
    d.estado = longStr(40);
    d.message = longStr(120, 'm');
    d.right = longStr(40);
    d.totalLines = 1000;
    d.cursorLine = 256;
    d.cursorCol = 512;

    for (int w = 1; w <= 100; ++w) {
        Rows r = renderRows(d, w);
        CHECK(colWidth(r.fixed) <= w);
        CHECK(colWidth(r.message) <= w);
        CHECK(colWidth(r.fixed) == w); // la barra fija SIEMPRE llena el ancho
    }
}

// ---------------------------------------------------------------------------
// terminal extremadamente angosto: desde 1 columna en adelante la barra
// nunca escribe fuera del ancho, por mas largo que sea el contenido. Es el
// caso que el fix de v1.1 corrigio (antes la fila fija emitia minimo 12
// columnas y la de mensajes 4, desbordando en terminales mas chicas).
// ---------------------------------------------------------------------------
TEST(statusbar_terminal_extremadamente_angosta) {
    StatusBarData d;
    d.name = longStr(80);
    d.path = "/" + longStr(80);
    d.estado = longStr(30);
    d.message = longStr(120, 'm');
    d.totalLines = 1;

    for (int w = 1; w <= 11; ++w) {
        Rows r = renderRows(d, w);
        CHECK(colWidth(r.fixed) == w); // llena exactamente
        CHECK(colWidth(r.fixed) <= w); // nunca desborda
        CHECK(colWidth(r.message) <= w);
        CHECK(!r.fixed.empty());
    }
}

// ---------------------------------------------------------------------------
// Casos limite del bloque derecho: sin totalLines (0 lineas) no hay pct
// porcentual; con varias lineas el pct se calcula. En ambos el ancho se
// respeta y el bloque queda a la derecha.
// ---------------------------------------------------------------------------
TEST(statusbar_right_block_edge_layout) {
    for (int w = 15; w <= 40; w += 5) {
        StatusBarData d;
        d.name = "a.txt";
        d.estado = "NAVEGACION";
        d.totalLines = 5;
        d.cursorLine = 2; // 2/(5-1) = 50%
        Rows r = renderRows(d, w);
        CHECK(colWidth(r.fixed) <= w);
        CHECK(colWidth(r.fixed) == w);
        CHECK_EQ(r.fixed.back(), ')'); // el (fila,col) cabe entero aca
    }
    // Sin documento (totalLines=0): el bloque derecho se calcula igual
    // (pct 0, (1,1)) si no hay override; 0% -> 1 columna, ancho respetado.
    StatusBarData d;
    d.name = "a.txt";
    d.estado = "NAVEGACION";
    d.totalLines = 0;
    Rows r = renderRows(d, 30);
    CHECK_EQ(colWidth(r.fixed), 30);
    CHECK(contains(r.fixed, "0% (1,1)"));
    CHECK_EQ(r.fixed.back(), ')');
}

// ---------------------------------------------------------------------------
// v1.3: el accent de la etiqueta de estado viene de EstadoData.estadoAccent
// (color por estado activo); vacio usa el fallback statusBarAccent del Theme.
// ---------------------------------------------------------------------------
TEST(statusbar_estado_accent_from_data) {
    Theme t = defaultTheme();
    t.statusBarAccent = "\x1b[34m";      // azul (fallback)
    t.accentNavegacion = "\x1b[33m";     // amarillo (estado activo)
    StatusBar bar;
    bar.setTheme(t);
    Rect area; area.width = 40; area.height = 2;

    StatusBarData d;
    d.name = "a.txt";
    d.estado = "NAVEGACION";
    d.totalLines = 1;

    d.estadoAccent = "";
    const std::string fallback = bar.render(area, d);
    CHECK(fallback.find(t.statusBarAccent) != std::string::npos);

    d.estadoAccent = t.accentNavegacion;
    const std::string withAccent = bar.render(area, d);
    CHECK(withAccent.find(t.accentNavegacion) != std::string::npos);
    CHECK(withAccent.find(t.statusBarAccent) == std::string::npos);
    CHECK(withAccent != fallback);
}

// ---------------------------------------------------------------------------
// v1.3: el indicador "[modificado]" se pinta con statusBarModified (no con
// statusBarName), distinto del nombre y nunca presente si no hay cambios.
// ---------------------------------------------------------------------------
TEST(statusbar_modified_indicator_styled) {
    Theme t = defaultTheme();
    t.statusBarModified = "\x1b[1;38;5;200m";
    StatusBar bar;
    bar.setTheme(t);
    Rect area; area.width = 60; area.height = 2;

    StatusBarData d;
    d.name = "x.cc";
    d.modified = true;
    d.estado = "NAVEGACION";
    d.totalLines = 1;

    const std::string out = bar.render(area, d);
    CHECK(out.find(t.statusBarModified + " [modificado]") != std::string::npos);

    StatusBarData c = d;
    c.modified = false;
    CHECK(bar.render(area, c).find(" [modificado]") == std::string::npos);
}

// ---------------------------------------------------------------------------
// v1.3: un mensaje de tipo Prompt se pinta con theme.prompt (negrita);
// los mensajes Info no llevan ese estilo.
// ---------------------------------------------------------------------------
TEST(statusbar_prompt_message_styled) {
    Theme t = defaultTheme();
    // Italica: distintiva, no colisiona con el bold de la etiqueta de estado.
    t.prompt = "\x1b[3m";
    StatusBar bar;
    bar.setTheme(t);
    Rect area; area.width = 60; area.height = 2;

    StatusBarData d;
    d.name = "x";
    d.estado = "NAVEGACION";
    d.totalLines = 1;
    d.message = Message("Guardar archivo: /tmp/x", MessageKind::Prompt,
                        std::nullopt);

    const std::string out = bar.render(area, d);
    CHECK(out.find(t.prompt + "Guardar archivo: /tmp/x") != std::string::npos);

    StatusBarData c = d;
    c.message = Message("ayuda", MessageKind::Info, std::nullopt);
    CHECK(bar.render(area, c).find(t.prompt) == std::string::npos);
}