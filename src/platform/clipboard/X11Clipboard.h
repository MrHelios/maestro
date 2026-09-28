#pragma once
#include "platform/clipboard/SystemClipboard.h"
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
#include <unordered_set>
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
    // ---- Estado de PROCESO (todo lo global que queda; impuesto por Xlib) --
    // Xlib mantiene UN error handler por proceso y el callback no recibe
    // userdata, así que este grupo es inevitablemente estático:
    //   previousHandler_/refCount_: instalan/restauran el handler una vez
    //     (adquisición/release contada).
    //   liveInstances_: registro de instancias vivas para que el handler
    //     estático pueda consultar el estado funcional (por-instancia).
    //     No es estado funcional: solo el puente que Xlib nos niega.
    // Mismo supuesto single-thread que refCount_.
    static int refCount_;
    static XErrorHandler previousHandler_;
    static std::unordered_set<X11Clipboard*> liveInstances_;
    // Adquisición/release contada del handler de proceso + registro.
    static void acquireProcessState(X11Clipboard* self);
    static void releaseProcessState(X11Clipboard* self);
    static int handleX11Error(Display* display, XErrorEvent* error);

    // ---- Estado FUNCIONAL (por instancia: un clipboard concreto) ----------
    struct RequestorInfo {
        int count = 0;
        std::chrono::steady_clock::time_point last;
    };
    // Rastrea requestors activos (ventanas), no transferencias individuales.
    // Si una misma ventana hace múltiples solicitudes simultáneas, se cuenta
    // como una sola entrada con contador. No eliminar demasiado pronto: una
    // transferencia INCR mantiene el requestor hasta completar/expirar.
    // NOTA derivación vs incrSends_ (ambos por-instancia ahora): todo
    // requestor con un INCR en curso está en activeRequestors_
    // (keepRegistered=true al servir el chunk inicial; unregister al
    // completar/expirar en handlePropertyNotify y purgeStaleIncrSends). El
    // camino no-INCR registra y desregistra dentro del mismo
    // handleSelectionRequest. Conjunto de ventanas con transferencia viva ⊆
    // claves de activeRequestors_; la inversa no vale en ventanas síncronas
    // transitorias. Derivar el filtro desde incrSends_ exigiría acoplar el
    // handler a transferencias en vez de ventanas; se mantiene el mapa
    // dedicado con timeout como protección anti-abandono.
    std::unordered_map<unsigned long, RequestorInfo> activeRequestors_;
    int absorbedErrorCount_ = 0;
    bool isExpectedClipboardError(const XErrorEvent& error) const;
    void registerRequestor(unsigned long win);
    void unregisterRequestor(unsigned long win);
    void purgeStaleRequestors();
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

    // Nota: data se copia por requestor (std::string copy). Para payloads grandes
    // y muchos requestors concurrentes esto duplica memoria. Tradeoff: simpleza
    // (ownership independiente, sin shared_ptr/COW) vs memoria. Casos típicos
    // (texto/código < 1MB, 1-2 requestors) son irrelevantes; si aparecen
    // payloads masivos + muchos requestors, se puede optimizar con
    // shared_ptr<string> + copy-on-write.
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
