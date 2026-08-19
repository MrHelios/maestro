// Tests del Theme (v1.2): verifican que el esquema de color es un valor
// inyectable y que los componentes realmente lo usan (no quedan colores
// hardcodeados). Dos propiedades esenciales:
//   1. El Theme por defecto reproduce EXACTAMENTE los colores que estaban
//      hardcodeados en v1.1 (constantes kStatusBar* / kCurrentLineStyle /
//      kSelectionStyle / kMessage*).
//   2. Cambiar el Theme cambia el output: cada componente (Renderer y
//      StatusBar) lee sus colores del Theme, no de constantes globales.
#include <string>

#include "test_framework.h"

#include "core/Theme.h"
#include "core/Document.h"
#include "core/Cursor.h"
#include "core/Layout.h"
#include "core/Viewport.h"
#include "ui/Renderer.h"
#include "ui/StatusBar.h"

namespace {

// Monta un frame del editor de una unica linea con seleccion (para ejercitar
// currentLine + selection) y devuelve el ANSI crudo.
std::string editorFrameWithSelection(const Theme& theme, int width = 200) {
    Document doc;
    doc.restore({"hello world"});
    Viewport vp;
    vp.top = 0;
    vp.height = 2;
    vp.width = width;
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    Renderer r;
    r.setTheme(theme);
    Selection sel;
    sel.anchor = Position{0, 0};
    sel.position = Position{0, 5};
    return r.buildScreen(doc, cur, vp, "/a/b.txt", false, "",
                         State::Navegacion, sel);
}

// Monta el frame del selector de buffers con el primer elemento seleccionado
// (ejercita theme_.selection en la lista).
std::string bufferFrameWithSelection(const Theme& theme, int width = 200) {
    Renderer r;
    r.setTheme(theme);
    return r.buildBufferListScreen({"aa.txt", "bb.txt"}, 0, width, 5);
}

} // namespace

// ---------------------------------------------------------------------------
// El Theme por defecto reproduce los colores de v1.1. Si alguna constante
// cambia de valor sin actualizar defaultTheme, este test lo delata.
// ---------------------------------------------------------------------------
TEST(theme_default_matches_legacy_colors) {
    const Theme t = defaultTheme();
    CHECK_EQ(t.currentLine, std::string(kCurrentLineStyle));
    CHECK_EQ(t.selection, std::string(kSelectionStyle));
    CHECK_EQ(t.statusBar, std::string(kStatusBarStyle));
    CHECK_EQ(t.statusBarName, std::string(kStatusBarName));
    CHECK_EQ(t.statusBarPath, std::string(kStatusBarPath));
    CHECK_EQ(t.statusBarAccent, std::string(kStatusBarCommand));
    CHECK_EQ(t.message, std::string(""));
    CHECK_EQ(t.success, std::string(kMessageSuccess));
    CHECK_EQ(t.warning, std::string(kMessageWarning));
    CHECK_EQ(t.error, std::string(kMessageError));
    CHECK_EQ(t.reset, std::string(kMessageReset));
}

// ---------------------------------------------------------------------------
// El Theme por defecto define los estilos del lenguaje visual v1.3:
// gutter, marcador, item activo de listas, prompts, indicador [modificado]
// y los accents por estado.
// ---------------------------------------------------------------------------
TEST(theme_defaults_new_visual_language) {
    const Theme t = defaultTheme();
    CHECK_EQ(t.lineNumber, std::string(kLineNumberStyle));
    CHECK_EQ(t.gutterCurrent, std::string(kGutterCurrentStyle));
    CHECK_EQ(t.marker, std::string(kMarkerStyle));
    CHECK_EQ(t.listSelected, std::string(kListSelectedStyle));
    CHECK_EQ(t.prompt, std::string(kPromptStyle));
    CHECK_EQ(t.statusBarModified, std::string(kStatusBarModified));
    CHECK_EQ(t.accentNavegacion, std::string(kAccentNavegacion));
    CHECK_EQ(t.accentInteraccion, std::string(kAccentInteraccion));
    CHECK_EQ(t.accentSeleccion, std::string(kAccentSeleccion));
    CHECK_EQ(t.accentComando, std::string(kAccentComando));
    CHECK_EQ(t.accentBuffers, std::string(kAccentBuffers));
    CHECK_EQ(t.accentGuardar, std::string(kAccentGuardar));
    CHECK_EQ(t.accentAbrir, std::string(kAccentAbrir));
    // El lenguaje ACTIVO se unifica: el item de lista usa el mismo gris que
    // la fila del cursor del editor (listSelected == currentLine).
    CHECK_EQ(t.listSelected, t.currentLine);
}

// ---------------------------------------------------------------------------
// Renderer: el color de seleccion y de fila actual salen del Theme, no de
// constantes. Un Theme con colores BIZARROS debe verse en el output.
// ---------------------------------------------------------------------------
TEST(theme_renderer_uses_theme_for_selection_and_currentline) {
    // Fondo gris de fila actual y seleccion roja reconocibles.
    Theme t = defaultTheme();
    t.currentLine = "\x1b[45m";      // fondo magenta
    t.selection = "\x1b[41m";        // fondo rojo
    t.reset = "\x1b[0m";

    const std::string frame = editorFrameWithSelection(t);
    // La fila del cursor (linea 0) lleva el estilo de fila del tema: el
    // bloque seleccionado gana y se abre con t.selection, pero el estilo
    // de fila del tema aparece en el frame (parte antes/despues y relleno).
    CHECK(frame.find(t.currentLine) != std::string::npos);
    // El bloque seleccionado (los primeros 5 chars) lleva el color de
    // seleccion del tema, no el video inverso global de la maquina.
    CHECK(frame.find(t.selection + "hello") != std::string::npos);
    // El default theme SEGUIRIA usando video inverso: el output debe
    // diferir con este tema, demostrando que el Theme se usa.
    CHECK(frame != editorFrameWithSelection(defaultTheme()));
}

// ---------------------------------------------------------------------------
// Las listas (selector de buffers, explorador) usan el accent del item
// ACTIVO (listSelected), NO el de la seleccion de TEXTO (selection). Asi el
// lenguaje ACTIVO (gris) cubre la fila del cursor y el item de lista, y el
// de SELECCION (video inverso) queda solo para el texto marcado.
// ---------------------------------------------------------------------------
TEST(theme_lists_use_listselected_not_selection) {
    const std::string base = bufferFrameWithSelection(defaultTheme());
    // Un cambio en selection (video inverso -> fondo rojo) NO altera la
    // lista: la lista no lee selection.
    Theme t1 = defaultTheme();
    t1.selection = "\x1b[41m";
    CHECK_EQ(bufferFrameWithSelection(t1), base);
    // Un cambio en listSelected SI altera la lista.
    Theme t2 = defaultTheme();
    t2.listSelected = "\x1b[41m";
    CHECK(bufferFrameWithSelection(t2) != base);
    // Por defecto el item activo lleva el mismo gris que la fila del cursor.
    CHECK(base.find(defaultTheme().listSelected) != std::string::npos);
}

// ---------------------------------------------------------------------------
// StatusBar: la barra tambien lee sus colores del Theme. Un Theme alterado
// debe reflejarse en la fila fija y en los mensajes de cada tipo.
// ---------------------------------------------------------------------------
TEST(theme_statusbar_uses_theme_for_colors) {
    Theme t = defaultTheme();
    t.statusBar = "\x1b[44m";       // fondo azul para la base
    t.statusBarName = "\x1b[31m";   // nombre rojo
    t.statusBarAccent = "\x1b[35m"; // comando magenta
    t.error = "\x1b[41m";           // errores con fondo rojo
    t.reset = "\x1b[0m";

    StatusBar bar;
    bar.setTheme(t);

    Rect area; area.row = 0; area.col = 0; area.width = 60; area.height = 2;
    StatusBarData d;
    d.name = "archivo.txt";
    d.path = "/ruta";
    d.estado = "NAVEGACION";
    d.message = Message("error grave", MessageKind::Error,
                        std::nullopt);
    d.totalLines = 1;

    const std::string out = bar.render(area, d);
    CHECK(out.find(t.statusBar) != std::string::npos);       // base del tema
    CHECK(out.find(t.statusBarName + "archivo.txt") != std::string::npos);
    CHECK(out.find(t.statusBarAccent) != std::string::npos); // - NAVEGACION
    CHECK(out.find(t.error + "error grave") != std::string::npos);
    // Con default theme el output es distinto: se usa el Theme, no constantes.
    StatusBar plainBar;
    CHECK(out != plainBar.render(area, d));
}

// ---------------------------------------------------------------------------
// El Renderer propaga SU Theme a la barra de estado compartida: cambiar el
// Theme del Renderer altera la barra de todas las pantallas.
// ---------------------------------------------------------------------------
TEST(theme_renderer_propagates_to_statusbar) {
    Theme t = defaultTheme();
    t.statusBar = "\x1b[42m";       // fondo verde para la barra
    t.statusBarAccent = "\x1b[31m"; // comando rojo
    t.reset = "\x1b[0m";

    // Editor: la barra del frame usa el Theme del Renderer.
    Renderer r;
    r.setTheme(t);
    Document doc; doc.restore({"x"});
    Viewport vp; vp.top = 0; vp.height = 2; vp.width = 80;
    Cursor cur; cur.line = 0; cur.col = 0;
    std::string ed = r.buildScreen(doc, cur, vp, "/a/b.txt", false, "",
                                   State::Navegacion, std::nullopt);
    CHECK(ed.find(t.statusBar) != std::string::npos);

    // BufferSelector: mismo Theme, misma barra.
    std::string buf = r.buildBufferListScreen({"a.txt"}, 0, 80, 5);
    CHECK(buf.find(t.statusBar) != std::string::npos);

    // FileBrowser: idem.
    std::string file = r.buildFileListScreen({"a.txt"}, 0, 0, "/ruta",
                                             Message("ayuda"), 80, 5);
    CHECK(file.find(t.statusBar) != std::string::npos);

    // Con el default theme la barra usaria otro color: el Theme se propaga.
    Renderer r2;
    // Con el tema custom (barra verde) el frame NO contiene el gris del
    // default theme; con un Renderer sin setTheme si.
    CHECK(ed.find(defaultTheme().statusBar) == std::string::npos);
    std::string edDefault = r2.buildScreen(doc, cur, vp, "/a/b.txt", false, "",
                                           State::Navegacion, std::nullopt);
    CHECK(edDefault.find(defaultTheme().statusBar) != std::string::npos);
    CHECK(ed != edDefault);
}