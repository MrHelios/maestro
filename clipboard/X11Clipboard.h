#pragma once
#include "clipboard/SystemClipboard.h"
#pragma push_macro("Cursor")
#pragma push_macro("Success")
#pragma push_macro("None")
#define Cursor X11Cursor
#include <X11/Xlib.h>
#pragma pop_macro("None")
#pragma pop_macro("Success")
#pragma pop_macro("Cursor")
#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include <chrono>
class X11Clipboard : public SystemClipboard {
public:
    X11Clipboard();
    ~X11Clipboard() override;
    X11Clipboard(const X11Clipboard&) = delete;
    X11Clipboard& operator=(const X11Clipboard&) = delete;
    X11Clipboard(X11Clipboard&&) = delete;
    X11Clipboard& operator=(X11Clipboard&&) = delete;
    bool copy(const std::string& text) override;
    std::optional<std::string> paste() override;
    bool ownsClipboard() const override;
    void processEvents() override;
    int fd() const override;
    bool hasPending() const override { return !incrSends_.empty(); }
    bool isAvailable() const { return display_ != nullptr; }
private:
    // Xlib mantiene el error handler a nivel global de proceso.
    // Este contador asume que la creación/destrucción de X11Clipboard
    // ocurre desde un único thread.
    struct RequestorInfo {
        int count = 0;
        std::chrono::steady_clock::time_point last;
    };
    static int refCount_;
    static XErrorHandler previousHandler_;
    // Rastrea requestors activos (ventanas), no transferencias individuales.
    // Si una misma ventana hace múltiples solicitudes simultáneas, se cuenta
    // como una sola entrada con contador. No eliminar demasiado pronto: una
    // transferencia INCR mantiene el requestor hasta completar/expirar.
    // NOTA: existe duplicación de estado con incrSends_ (timeout). Idealmente
    // activeRequestors_ debería derivarse del estado real de transferencias y
    // su timeout ser solo protección contra estados abandonados. No blocker ahora.
    static std::unordered_map<unsigned long, RequestorInfo> activeRequestors_;
    static int handleX11Error(Display* display, XErrorEvent* error);
    static bool isExpectedClipboardError(const XErrorEvent& error);
    static void registerRequestor(unsigned long win);
    static void unregisterRequestor(unsigned long win);
    static void purgeStaleRequestors();
    void handleSelectionRequest(void* ev);
    std::optional<std::string> readProperty(unsigned long win, unsigned long prop);
    void deleteProperty(unsigned long win, unsigned long prop);
    std::optional<std::string> fetchProperty(unsigned long win, unsigned long prop);
    std::optional<std::string> readIncrProperty(unsigned long win, unsigned long prop);
    bool waitForSelectionNotify(unsigned long target, unsigned long property, int timeoutMs);
    void handlePropertyNotify(void* ev);
    // Descarta transferencias INCR que dejaron de recibir actividad hace
    // mas de kIncrStaleTimeout: el requestor puede desaparecer o dejar de
    // continuar el protocolo (no borra la propiedad para pedir el
    // siguiente chunk) y sin esto la entrada en incrSends_ (con una copia
    // completa del texto copiado) quedaria viva para siempre. Se llama
    // desde processEvents(), asi que corre cada vez que se drena la cola
    // de eventos X11.
    void purgeStaleIncrSends();

    // Tamano de chunk/umbral de INCR: se calculan en runtime a partir de
    // XMaxRequestSize() del servidor conectado (ver ctor), en vez de una
    // constante fija que podria exceder lo que ese servidor acepta en una
    // sola request (XChangeProperty fallaria con BadLength).
    size_t incrChunkSize_ = 0;
    size_t incrThreshold_ = 0;

    // Un requestor que no continua el protocolo INCR (no dispara el
    // PropertyNotify/PropertyDelete esperado) durante mas de este tiempo
    // se considera abandonado. ICCCM no fija un numero; 5s es holgado
    // frente a clientes lentos y acota la vida de una transferencia
    // fantasma.
    static constexpr std::chrono::seconds kIncrStaleTimeout{5};

    struct IncrSend {
        unsigned long requestor = 0;
        unsigned long property = 0;
        unsigned long target = 0;
        std::string data;
        size_t offset = 0;
        // Momento del ultimo chunk servido (o de creacion, si todavia no
        // se sirvio ninguno). Usado por purgeStaleIncrSends().
        std::chrono::steady_clock::time_point lastActivity;
    };
    std::vector<IncrSend> incrSends_;
    Display* display_ = nullptr;
    unsigned long window_ = 0;
    unsigned long clipboardAtom_ = 0;
    unsigned long utf8Atom_ = 0;
    unsigned long textAtom_ = 0;
    unsigned long stringAtom_ = 0;
    unsigned long targetsAtom_ = 0;
    unsigned long incrAtom_ = 0;
    unsigned long propertyAtom_ = 0;
    std::string ownedText_;
    bool ownsClipboard_ = false;
};
