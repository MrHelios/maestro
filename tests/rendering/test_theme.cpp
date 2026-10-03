// Tests del TtyTheme (v1.2): verifican que el esquema de color es un valor
// inyectable y que los componentes realmente lo usan (no quedan colores
// hardcodeados). Dos propiedades esenciales:
//   1. El TtyTheme por defecto reproduce EXACTAMENTE los colores que estaban
//      hardcodeados en v1.1 (constantes kStatusBar* / kCurrentLineStyle /
//      kSelectionStyle / kMessage*).
//   2. Cambiar el TtyTheme cambia el output: cada componente (Renderer y
//      StatusBar) lee sus colores del TtyTheme, no de constantes globales.
#include <string>

#include "test_framework.h"
#include "helpers/test_render_utils.h"

#include "rendering/tty/TtyTheme.h"
#include "document/Document.h"
#include "document/Cursor.h"
#include "layout/Layout.h"
#include "layout/Viewport.h"
#include "rendering/tty/TtyRenderer.h"
#include "rendering/tty/TtyChrome.h"

namespace {

constexpr const char* kTestBgMagenta = "\x1b[45m";
constexpr const char* kTestBgRed = "\x1b[41m";
constexpr const char* kTestBgBlue = "\x1b[44m";
constexpr const char* kTestBgGreen = "\x1b[42m";
constexpr const char* kTestFgRed = "\x1b[31m";
constexpr const char* kTestFgMagenta = "\x1b[35m";
constexpr const char* kTestFgYellow = "\x1b[33m";
constexpr const char* kTestFgWhite = "\x1b[37m";
constexpr const char* kTestFgBlack = "\x1b[30m";
constexpr const char* kTestReset = "\x1b[0m";

// Construye un frame minimo que ejercita currentLine y selection.
std::string editorFrameWithSelection(const TtyTheme& theme, int width = 200) {
    Document doc;
    doc.restore({"hello world"});
    Viewport vp;
    vp.top = 0;
    vp.height = 2;
    vp.width = width;
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    TtyRenderer r;
    r.setTheme(theme);
    Selection sel;
    sel.anchor = Position{0, 0};
    sel.position = Position{0, 5};
    return r.buildScreen(doc, cur, vp, "/a/b.txt", false, "",
                         State::Navegacion, sel);
}

// Monta el frame del selector de buffers con el primer elemento seleccionado
// (ejercita theme_.selection en la lista).
std::string bufferFrameWithSelection(const TtyTheme& theme, int width = 200) {
    TtyRenderer tr;
    tr.setTheme(theme);
    return tr.buildBufferListScreen({"aa.txt", "bb.txt"}, 0, width, 5);
}

std::string fileFrameWithSelection(const TtyTheme& theme, int width = 200) {
    TtyRenderer tr;
    tr.setTheme(theme);
    return tr.buildFileListScreen(
        std::vector<FileListItem>{{"aa.txt", false}, {"bb.txt", false}}, 0, 0,
        "/tmp", Message{}, width, 5);
}

} // namespace

// Garantiza que defaultTheme() conserve compatibilidad visual con el
// esquema anterior.
TEST(theme_default_matches_legacy_colors) {
    const TtyTheme t = defaultTheme();
    CHECK_EQ(t.background, std::string(kDarkBackground));
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
    CHECK_EQ(t.reset, std::string(kDarkReset));
}

// ---------------------------------------------------------------------------
// El TtyTheme por defecto define los estilos del lenguaje visual v1.3:
// gutter, marcador, item activo de listas, prompts, indicador [*]
// (v1.4: antes [modificado])
// y los accents por estado.
// ---------------------------------------------------------------------------
TEST(theme_defaults_new_visual_language) {
    const TtyTheme t = defaultTheme();
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

TEST(theme_renderer_uses_theme_for_selection_and_currentline) {
    TtyTheme t = defaultTheme();
    t.currentLine = kTestBgMagenta;
    t.selection = kTestBgRed;
    t.reset = kTestReset;

    const std::string frame = editorFrameWithSelection(t);
    // La fila del cursor (linea 0) lleva el estilo de fila del tema: el
    // bloque seleccionado gana y se abre con t.selection, pero el estilo
    // de fila del tema aparece en el frame (parte antes/despues y relleno).
    CHECK(frame.find(t.currentLine) != std::string::npos);
    // El bloque seleccionado (los primeros 5 chars) lleva el color de
    // seleccion del tema, no el video inverso global de la maquina.
    CHECK(frame.find(t.selection + "hello") != std::string::npos);
    // El default theme SEGUIRIA usando video inverso: el output debe
    // diferir con este tema, demostrando que el TtyTheme se usa.
    CHECK(frame != editorFrameWithSelection(defaultTheme()));
}

TEST(theme_lists_use_listselected_not_selection) {
    const std::string base = bufferFrameWithSelection(defaultTheme());
    TtyTheme t1 = defaultTheme();
    t1.selection = kTestBgRed;
    CHECK_EQ(bufferFrameWithSelection(t1), base);
    TtyTheme t2 = defaultTheme();
    t2.listSelected = kTestBgRed;
    CHECK(bufferFrameWithSelection(t2) != base);
    CHECK(base.find(defaultTheme().listSelected) != std::string::npos);
}

TEST(theme_file_lists_use_listselected_not_selection) {
    const std::string base = fileFrameWithSelection(defaultTheme());
    TtyTheme t1 = defaultTheme();
    t1.selection = kTestBgRed;
    CHECK_EQ(fileFrameWithSelection(t1), base);
    TtyTheme t2 = defaultTheme();
    t2.listSelected = kTestBgRed;
    CHECK(fileFrameWithSelection(t2) != base);
    CHECK(base.find(defaultTheme().listSelected) != std::string::npos);
}

TEST(theme_chrome_uses_theme_for_colors) {
    TtyTheme t = defaultTheme();
    t.statusBar = kTestBgBlue;
    t.statusBarName = kTestFgRed;
    t.statusBarAccent = kTestFgMagenta;
    t.error = kTestBgRed;
    t.reset = kTestReset;

    TtyChrome bar;
    bar.setTheme(t);

    Rect area; area.row = 0; area.col = 0; area.width = 60; area.height = 2;
    ChromeData d;
    d.status.name = "archivo.txt";
    d.status.path = "/ruta";
    d.status.estado = "NAVEGACION";
    d.message = Message("error grave", MessageKind::Error,
                        std::nullopt);
    d.status.totalLines = 1;

    const std::string out = bar.render(area, d);
    CHECK(out.find(t.statusBar) != std::string::npos);       // base del tema
    CHECK(out.find(t.statusBarName + "archivo.txt") != std::string::npos);
    CHECK(out.find(t.statusBarAccent) != std::string::npos); // - NAVEGACION
    CHECK(out.find(t.error + "error grave") != std::string::npos);
    // Con default theme el output es distinto: se usa el TtyTheme, no constantes.
    TtyChrome plainBar;
    CHECK(out != plainBar.render(area, d));
}

// ---------------------------------------------------------------------------
// El backend propaga su TtyTheme a la barra de estado compartida: cambiar el
// tema altera la barra de todas las pantallas (editor y listas).
// ---------------------------------------------------------------------------
TEST(theme_renderer_propagates_to_chrome) {
    TtyTheme t = defaultTheme();
    t.statusBar = kTestBgGreen;
    t.statusBarAccent = kTestFgRed;
    t.reset = kTestReset;

    // Editor: la barra del frame usa el tema del backend.
    TtyRenderer r;
    r.setTheme(t);
    Document doc; doc.restore({"x"});
    Viewport vp; vp.top = 0; vp.height = 2; vp.width = 80;
    Cursor cur; cur.line = 0; cur.col = 0;
    std::string ed = r.buildScreen(doc, cur, vp, "/a/b.txt", false, "",
                                   State::Navegacion, std::nullopt);
    CHECK(ed.find(t.statusBar) != std::string::npos);

    // BufferSelector: mismo tema en el backend, misma barra.
    TtyRenderer tr;
    tr.setTheme(t);
    std::string buf = tr.buildBufferListScreen({"a.txt"}, 0, 80, 5);
    CHECK(buf.find(t.statusBar) != std::string::npos);

    // FileBrowser: idem.
    std::string file = tr.buildFileListScreen(
        std::vector<FileListItem>{{"a.txt", false}}, 0, 0, "/ruta",
        Message("ayuda"), 80, 5);
    CHECK(file.find(t.statusBar) != std::string::npos);

    // Con el default theme la barra usaria otro color: el TtyTheme se propaga.
    TtyRenderer r2;
    // Con el tema custom (barra verde) el frame NO contiene el gris del
    // default theme; con un Renderer sin setTheme si.
    CHECK(ed.find(defaultTheme().statusBar) == std::string::npos);
    std::string edDefault = r2.buildScreen(doc, cur, vp, "/a/b.txt", false, "",
                                           State::Navegacion, std::nullopt);
    CHECK(edDefault.find(defaultTheme().statusBar) != std::string::npos);
    CHECK(ed != edDefault);
}

// Cada reset debe reestablecer el background de la status bar. Esto evita
// que segmentos con estilos propios dejen zonas de la fila sin el background
// del TtyTheme.
TEST(theme_chrome_background_covers_full_width) {
    TtyTheme t = defaultTheme();
    t.statusBar = kTestBgBlue;
    t.statusBarName = kTestFgWhite;
    t.statusBarPath = kTestFgBlack;
    t.statusBarAccent = kTestFgMagenta;
    t.statusBarModified = kTestFgYellow;
    t.reset = kTestReset;

    TtyChrome bar;
    bar.setTheme(t);
    Rect area;
    area.row = 0;
    area.col = 0;
    area.width = 60;
    area.height = 2;
    ChromeData d;
    d.status.name = "archivo.txt";
    d.status.path = "/ruta/larga/que/fuerza/varios/segmentos";
    d.status.estado = "NAVEGACION";
    d.status.totalLines = 100;
    d.status.cursorLine = 10;
    d.status.cursorCol = 5;

    const std::string out = bar.render(area, d);
    size_t nl = out.find("\r\n");
    std::string row = (nl == std::string::npos) ? out : out.substr(0, nl);
    CHECK(row.find(t.statusBar) != std::string::npos);
    size_t pos = 0;
    int resets = 0;
    while (true) {
        size_t f = row.find(t.reset, pos);
        if (f == std::string::npos) break;
        ++resets;
        size_t nxt = row.find(t.reset, f + t.reset.size());
        if (nxt == std::string::npos) {
            CHECK(f + t.reset.size() == row.size());
            break;
        }
        CHECK(row.compare(f + t.reset.size(), t.statusBar.size(), t.statusBar) == 0);
        pos = f + t.reset.size();
    }
    CHECK(resets >= 4);
}
