// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/bridge/NativeMessagingHost.cpp
//
//  Binario puente "SDMNativeHost" (canal primario, spec 8.1).
//  main() incluido: este fichero solo forma parte del objetivo SDMNativeHost.
//
//  Diseno de recursos:
//   - Lector de STDIN con espera sondeable (poll/PeekNamedPipe): termina de
//     forma determinista en el cierre, nunca cuelga un join.
//   - Hilo escritor de STDOUT con cola acotada (contrapresion): si el
//     navegador no consume, la memoria no crece y la red no se frena.
//   - Reconexión al WebSocket local con backoff exponencial 1 s -> 30 s.
//   - Peticiones del navegador retenidas (con limite) mientras SDM reinicia.
//   - Cierre ordenado: señal de parada, drenaje final y espera acotada.
// ============================================================================
#include "bridge/NativeMessagingHost.h"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketProtocol>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <fcntl.h>
#  include <io.h>
#  include <windows.h>
#else
#  include <cerrno>
#  include <poll.h>
#  include <unistd.h>
#endif

namespace sdm {

namespace {

constexpr quint32 kMaxNativeMessage = 1024 * 1024;    // Limite de Chrome (1 MB)
constexpr qint64 kMaxOutboxBytes = 4 * 1024 * 1024;   // Contrapresion de STDOUT
constexpr int kMaxPendingToSdm = 256;                 // Peticiones retenidas (uds.)
constexpr qint64 kMaxPendingToSdmBytes = 1024 * 1024; // Peticiones retenidas (bytes)
constexpr int kReconnectInitialMs = 1000;             // Backoff inicial
constexpr int kReconnectMaxMs = 30000;                // Backoff maximo
constexpr int kStdinPollMs = 200;                     // Granularidad de salida del lector

std::atomic<bool> g_running{true};
#ifdef _WIN32
std::atomic<HANDLE> g_stdinHandle{nullptr};
#endif

/// Escribe un frame nativo: longitud uint32 LE + carga util.
/// Solo se invoca desde el hilo escritor (serializacion garantizada).
bool writeFrame(const QByteArray& payload) {
    if (payload.size() > static_cast<qsizetype>(kMaxNativeMessage)) {
        std::fprintf(stderr, "SDMNativeHost: mensaje demasiado grande (%d)\n", static_cast<int>(payload.size()));
        return false;
    }
    const quint32 length = static_cast<quint32>(payload.size());
    if (std::fwrite(&length, sizeof(length), 1, stdout) != 1) {
        return false;
    }
    if (length > 0 && std::fwrite(payload.constData(), 1, length, stdout) != length) {
        return false;
    }
    return std::fflush(stdout) == 0;
}

/// Espera hasta `timeoutMs` a que STDIN tenga datos. Devuelve:
///   1 = hay datos, 0 = timeout (revisar g_running), -1 = canal cerrado.
int waitStdin(int timeoutMs) {
#ifdef _WIN32
    const HANDLE handle = g_stdinHandle.load(std::memory_order_relaxed);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    for (int waited = 0; waited < timeoutMs; waited += 25) {
        if (!g_running.load(std::memory_order_relaxed)) {
            return 0;
        }
        DWORD available = 0;
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
            return -1; // pipe roto: el navegador cerro el canal
        }
        if (available > 0) {
            return 1;
        }
        Sleep(25);
    }
    return 0;
#else
    if (!g_running.load(std::memory_order_relaxed)) {
        return 0;
    }
    const int boundedTimeout = std::max(0, std::min(timeoutMs, 200));
    struct pollfd descriptor;
    descriptor.fd = STDIN_FILENO;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    const int rc = poll(&descriptor, 1, boundedTimeout);
    if (rc < 0) {
        return (errno == EINTR) ? 0 : -1;
    }
    if (rc == 0) {
        return 0;
    }
    return (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) ? -1 : 1;
#endif
}

/// Lee todos los bytes disponibles de STDIN (sin bloquear mas de la cuenta).
/// Devuelve false si el canal se cerro.
bool readStdinChunk(std::string* buffer) {
    char chunk[65536];
#ifdef _WIN32
    const HANDLE handle = g_stdinHandle.load(std::memory_order_relaxed);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD available = 0;
    if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
        return false;
    }
    const DWORD wanted = available < sizeof(chunk) ? available : static_cast<DWORD>(sizeof(chunk));
    if (wanted == 0) {
        return true;
    }
    DWORD got = 0;
    if (!ReadFile(handle, chunk, wanted, &got, nullptr) || got == 0) {
        return false;
    }
#else
    const ssize_t gotRaw = read(STDIN_FILENO, chunk, sizeof(chunk));
    if (gotRaw < 0) {
        return (errno == EINTR) ? true : false;
    }
    if (gotRaw == 0) {
        return false; // EOF
    }
    const size_t got = static_cast<size_t>(gotRaw);
#endif
    if (static_cast<size_t>(got) > static_cast<size_t>(kMaxNativeMessage) + 4u -
                                       std::min(buffer->size(), static_cast<size_t>(kMaxNativeMessage) + 4u)) {
        return false; // buffer síncrono limitado a un frame y cabecera
    }
    buffer->append(chunk, got);
    return true;
}

/// Extrae frames completos del buffer acumulado. Devuelve false si un frame
/// excede el limite (el emparejamiento de frames no admite resincronizacion).
bool extractFrames(std::string* buffer, const std::function<void(const QByteArray&)>& emitFrame) {
    while (buffer->size() >= 4) {
        quint32 length = 0;
        std::memcpy(&length, buffer->data(), sizeof(length)); // LE en orden nativo
        if (length == 0) {
            buffer->erase(0, 4);
            continue;
        }
        if (length > kMaxNativeMessage) {
            std::fprintf(stderr, "SDMNativeHost: frame demasiado grande (%u), se aborta la lectura\n", length);
            return false;
        }
        if (buffer->size() < 4u + static_cast<size_t>(length)) {
            break; // frame incompleto: se esperan mas bytes
        }
        emitFrame(QByteArray(buffer->data() + 4, static_cast<qsizetype>(length)));
        buffer->erase(0, 4u + static_cast<size_t>(length));
    }
    return true;
}

} // namespace

NativeMessagingHost::NativeMessagingHost(QObject* parent) : QObject(parent) {
    m_reconnectDelayMs = kReconnectInitialMs;
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &NativeMessagingHost::tryConnect);

    // Hilo escritor dedicado: las escrituras bloqueantes de STDOUT (cuando el
    // navegador no consume) nunca detienen el bucle de eventos ni la red.
    m_writer = std::make_shared<WriterState>();
    m_writerThread = std::thread([state = m_writer]() { pumpStdout(state); });
}

NativeMessagingHost::~NativeMessagingHost() {
    m_exiting = true;
    g_running.store(false);
    if (m_reconnectTimer != nullptr) {
        m_reconnectTimer->stop();
    }
    if (m_socket != nullptr) {
        m_socket->abort();
    }
    // Parada ordenada del hilo escritor: señal + drenaje final + espera acotada.
    {
        std::lock_guard<std::mutex> lock(m_writer->mutex);
        m_writer->stop = true;
    }
    m_writer->cv.notify_all();
    {
        std::unique_lock<std::mutex> lock(m_writer->mutex);
        const bool finished =
            m_writer->cv.wait_for(lock, std::chrono::seconds(2), [this]() { return m_writer->finished; });
        if (!finished) {
            // El hilo conserva shared_ptr a WriterState y solo accede al stream
            // del proceso; se desacopla si el navegador dejó bloqueada la tubería.
            m_writerThread.detach();
            return;
        }
    }
    if (m_writerThread.joinable()) {
        m_writerThread.join();
    }
}

bool NativeMessagingHost::launchSdm(const char* reason) {
    static bool attempted = false;
    if (attempted) {
        return false;
    }
    attempted = true;
#ifdef _WIN32
    const QString executable =
        QCoreApplication::applicationDirPath() + QStringLiteral("/SDM.exe");
#else
    const QString executable =
        QCoreApplication::applicationDirPath() + QStringLiteral("/SDM");
#endif
    if (!QFileInfo::exists(executable)) {
        std::fprintf(stderr, "SDMNativeHost: %s; no se encontro %s\n", reason,
                     executable.toUtf8().constData());
        return false;
    }
    const bool started = QProcess::startDetached(executable, {QStringLiteral("--minimized")});
    std::fprintf(stderr, "SDMNativeHost: %s; lanzando SDM%s\n", reason,
                 started ? "" : " (fallo)");
    return started;
}

bool NativeMessagingHost::start(const QString& bridgeInfoPath, QString* error) {
    g_running.store(true, std::memory_order_relaxed);
    m_exiting = false;

    // Si SDM no esta en ejecucion el navegador ya nos ha lanzado: se arranca el
    // gestor (minimizado) para que la captura funcione sin pasos manuales.
    QFile infoFile(bridgeInfoPath);
    QJsonDocument doc;
    const auto readBridge = [&infoFile, &doc]() -> bool {
        if (!infoFile.isOpen() && !infoFile.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QByteArray raw = infoFile.readAll();
        infoFile.close();
        doc = QJsonDocument::fromJson(raw);
        return doc.isObject();
    };

    if (!readBridge() && launchSdm("SDM no estaba en ejecucion")) {
        // Espera acotada (6 s) a que SDM escriba el puente recien arrancado.
        for (int attempt = 0; attempt < 15 && !readBridge(); ++attempt) {
            QThread::msleep(400);
        }
    }
    if (!doc.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral(
                         "No se pudo leer el puente de SDM (%1). ¿Esta SDM instalado y en ejecucion?")
                         .arg(bridgeInfoPath);
        }
        return false;
    }
    const QJsonObject object = doc.object();
    const int configuredPort = object.value(QStringLiteral("port")).toInt(0);
    m_token = object.value(QStringLiteral("token")).toString();
    if (configuredPort <= 0 || configuredPort > 65535) {
        if (error != nullptr) {
            *error = QStringLiteral("bridge.json contiene un puerto inválido");
        }
        return false;
    }
    m_port = static_cast<quint16>(configuredPort);
    if (m_token.size() < 32 || m_token.size() > 256) {
        if (error != nullptr) {
            *error = QStringLiteral("bridge.json sin token");
        }
        return false;
    }

    m_socket = new QWebSocket(QStringLiteral("sdm://native-host"), QWebSocketProtocol::VersionLatest, this);
    m_socket->setMaxAllowedIncomingFrameSize(1024 * 1024);
    connect(m_socket, &QWebSocket::connected, this, &NativeMessagingHost::onConnected);
    connect(m_socket, &QWebSocket::disconnected, this, &NativeMessagingHost::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &NativeMessagingHost::onTextMessage);
    connect(m_socket, &QWebSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                // Fallo de conexion (SDM aun no disponible): arrancarlo una vez
                // y reintentar con backoff exponencial.
                if (!m_wsConnected && !m_exiting) {
                    launchSdm("puerto del puente cerrado");
                    scheduleReconnect();
                }
            });

    tryConnect();
    if (!m_wsConnected) {
        scheduleReconnect(); // reintenta mientras el navegador espera
    }
    return true;
}

void NativeMessagingHost::tryConnect() {
    if (m_socket == nullptr || m_wsConnected || m_exiting) {
        return;
    }
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        // Intento en curso: no se abren conexiones en paralelo, pero el ciclo
        // de reintentos no se pierde (el timer single-shot se reprograma).
        scheduleReconnect();
        return;
    }
    QUrl url(QStringLiteral("ws://127.0.0.1:%1/").arg(m_port));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("token"), m_token);
    url.setQuery(query);
    m_socket->open(url);
}

void NativeMessagingHost::onConnected() {
    m_wsConnected = true;
    QJsonObject payload;
    payload.insert(QStringLiteral("token"), m_token);
    QJsonObject hello;
    hello.insert(QStringLiteral("type"), QStringLiteral("hello"));
    hello.insert(QStringLiteral("payload"), payload);
    const QByteArray greeting = QJsonDocument(hello).toJson(QJsonDocument::Compact);
    if (m_socket != nullptr) {
        m_socket->sendTextMessage(QString::fromUtf8(greeting));
    }
    m_reconnectDelayMs = kReconnectInitialMs; // backoff listo para la proxima caida
    m_reconnectTimer->stop();
    // El saludo autentica primero; flushPendingToSdm se hará tras helloAck.
}

void NativeMessagingHost::onDisconnected() {
    m_wsConnected = false;
    if (!m_exiting) {
        scheduleReconnect();
    }
}

void NativeMessagingHost::scheduleReconnect() {
    if (m_exiting || m_reconnectTimer->isActive()) {
        return; // ya programado: no se acumulan reintentos
    }
    m_reconnectTimer->start(m_reconnectDelayMs);
    // Backoff exponencial con tope: 1 s, 2 s, 4 s ... hasta 30 s.
    m_reconnectDelayMs = (std::min)(m_reconnectDelayMs * 2, kReconnectMaxMs);
}

void NativeMessagingHost::onTextMessage(const QString& message) {
    if (message.size() > 1024 * 1024) {
        std::fprintf(stderr, "SDMNativeHost: mensaje WebSocket supera el límite, desconectando\n");
        if (m_socket != nullptr) {
            m_socket->abort();
        }
        return;
    }
    const QByteArray raw = message.toUtf8();
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (!document.isObject()) {
        return;
    }
    const QString type = document.object().value(QStringLiteral("type")).toString();
    if (type == QLatin1String("error") && m_pendingToSdm.isEmpty() && m_wsConnected) {
        // Error previo al saludo aceptado: el token/compatibilidad no coincide.
        if (m_socket != nullptr) {
            m_socket->abort();
        }
        return;
    }
    if (type == QLatin1String("helloAck")) {
        flushPendingToSdm();
        sendToBrowser(raw);
        return;
    }
    sendToBrowser(raw);
}

void NativeMessagingHost::sendToBrowser(const QByteArray& jsonPayload) {
    if (jsonPayload.isEmpty()) {
        return;
    }
    if (jsonPayload.size() > static_cast<qsizetype>(kMaxNativeMessage) ||
        !QJsonDocument::fromJson(jsonPayload).isObject()) {
        std::fprintf(stderr, "SDMNativeHost: mensaje hacia el navegador inválido o demasiado grande, descartado\n");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_writer->mutex);
        if (m_writer->stop) {
            return; // cierre en curso: no se aceptan mas mensajes
        }
        // Contrapresion: si el navegador no consume, se descartan los frames
        // mas antiguos (notificaciones de estado) y se contabilizan.
        while (!m_writer->queue.isEmpty() && m_writer->bytes + jsonPayload.size() > kMaxOutboxBytes) {
            const QByteArray dropped = m_writer->queue.takeFirst();
            m_writer->bytes -= dropped.size();
            ++m_writer->dropped;
        }
        if (m_writer->bytes + jsonPayload.size() > kMaxOutboxBytes) {
            ++m_writer->dropped;
            return;
        }
        if (m_writer->dropped > 0 && m_writer->dropped % 100 == 1) {
            std::fprintf(stderr, "SDMNativeHost: cola STDOUT saturada, %d frames descartados\n",
                         m_writer->dropped);
        }
        m_writer->queue.append(jsonPayload);
        m_writer->bytes += jsonPayload.size();
    }
    m_writer->cv.notify_one();
}

void NativeMessagingHost::forwardToSdm(const QByteArray& jsonPayload) {
    if (jsonPayload.isEmpty()) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(jsonPayload);
    if (!document.isObject()) {
        std::fprintf(stderr, "SDMNativeHost: JSON inválido desde el navegador, descartado\n");
        return;
    }
    if (jsonPayload.size() > static_cast<qsizetype>(kMaxNativeMessage)) {
        std::fprintf(stderr, "SDMNativeHost: petición hacia SDM excede 1 MB, descartada\n");
        return;
    }
    if (m_wsConnected && m_socket != nullptr) {
        if (m_socket->bytesToWrite() + jsonPayload.size() > kMaxPendingToSdmBytes) {
            if (m_pendingToSdm.size() < kMaxPendingToSdm &&
                m_pendingToSdmBytes + jsonPayload.size() <= kMaxPendingToSdmBytes) {
                m_pendingToSdm.append(jsonPayload);
                m_pendingToSdmBytes += jsonPayload.size();
            }
            m_socket->abort();
            onDisconnected();
            std::fprintf(stderr, "SDMNativeHost: cola WebSocket saturada, reintento programado\n");
            return;
        }
        m_socket->sendTextMessage(QString::fromUtf8(jsonPayload));
        return;
    }
    // SDM reiniciandose: la peticion del usuario se retiene (con limite) en
    // vez de perderse; se entregara al reconectar.
    if (m_pendingToSdm.size() >= kMaxPendingToSdm ||
        m_pendingToSdmBytes + jsonPayload.size() > kMaxPendingToSdmBytes) {
        std::fprintf(stderr, "SDMNativeHost: cola de retencion llena, peticion descartada\n");
        return;
    }
    m_pendingToSdm.append(jsonPayload);
    m_pendingToSdmBytes += jsonPayload.size();
}

void NativeMessagingHost::flushPendingToSdm() {
    if (m_socket == nullptr) {
        return;
    }
    QList<QByteArray> pending = m_pendingToSdm;
    m_pendingToSdm.clear();
    m_pendingToSdmBytes = 0;
    for (const QByteArray& payload : pending) {
        if (!m_wsConnected || m_socket->bytesToWrite() + payload.size() > kMaxPendingToSdmBytes) {
            m_pendingToSdm.append(payload);
            m_pendingToSdmBytes += payload.size();
            continue;
        }
        m_socket->sendTextMessage(QString::fromUtf8(payload));
    }
    if (!m_pendingToSdm.isEmpty()) {
        scheduleReconnect();
    } else if (!pending.isEmpty()) {
        std::fprintf(stderr, "SDMNativeHost: %d peticiones retenidas entregadas a SDM\n",
                     static_cast<int>(pending.size()));
    }
}

void NativeMessagingHost::pumpStdout(const std::shared_ptr<WriterState>& state) {
    for (;;) {
        QByteArray payload;
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->cv.wait(lock, [&state]() { return state->stop || !state->queue.isEmpty(); });
            if (state->queue.isEmpty()) {
                if (state->stop) {
                    break; // parada ordenada: cola vacia, fin del hilo
                }
                continue;
            }
            payload = state->queue.takeFirst();
            state->bytes -= payload.size();
        }
        // Fuera del lock y del bucle de eventos: la escritura puede bloquearse
        // si el navegador no consume, pero nunca frena la red ni la interfaz.
        if (!writeFrame(payload)) {
            // El canal con el navegador esta roto (STDIO cerrado): sin salida
            // no hay puente util, se termina limpiamente.
            std::fprintf(stderr, "SDMNativeHost: STDOUT no disponible, terminando\n");
            if (QCoreApplication::instance() != nullptr) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), []() { QCoreApplication::quit(); },
                                          Qt::QueuedConnection);
            }
            break;
        }
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->finished = true;
    }
    state->cv.notify_all();
}

} // namespace sdm

// ---------------------------------------------------------------------------
// Punto de entrada
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Sin transformacion de texto: los frames son binarios.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    sdm::g_stdinHandle.store(GetStdHandle(STD_INPUT_HANDLE), std::memory_order_relaxed);
#endif

    QCoreApplication app(argc, argv);
    sdm::g_running.store(true, std::memory_order_relaxed);
    QCoreApplication::setOrganizationName(QStringLiteral("SDM"));
    QCoreApplication::setApplicationName(QStringLiteral("S-Download Manager"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QString bridgePath;
    const QStringList arguments = app.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QLatin1String("--bridge") && i + 1 < arguments.size()) {
            bridgePath = arguments.at(i + 1);
        }
    }
    if (bridgePath.isEmpty()) {
        bridgePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                     QStringLiteral("/bridge.json");
    }

    sdm::NativeMessagingHost host;
    QString error;
    if (!host.start(bridgePath, &error)) {
        std::fprintf(stderr, "SDMNativeHost: %s\n", error.toUtf8().constData());
        return 1;
    }

    // Hilo lector de STDIN (formato nativo: uint32 LE + JSON). Sondea el canal
    // en lugar de bloquearse: la parada del proceso es siempre determinista.
    std::thread reader([&host]() {
        std::string buffer;
        while (sdm::g_running.load(std::memory_order_relaxed)) {
            const int ready = sdm::waitStdin(sdm::kStdinPollMs);
            if (ready < 0) {
                break; // EOF: el navegador cerro el canal
            }
            if (ready == 0) {
                continue; // timeout: se reevalua g_running
            }
            if (!sdm::readStdinChunk(&buffer)) {
                break; // cierre del canal durante la lectura
            }
            const bool framesOk = sdm::extractFrames(&buffer, [&host](const QByteArray& payload) {
                QMetaObject::invokeMethod(
                    &host, [&host, payload]() { emit host.fromBrowser(payload); }, Qt::QueuedConnection);
            });
            if (!framesOk) {
                break; // frame malformado: el emparejamiento no es resincronizable
            }
        }
        sdm::g_running.store(false);
        QMetaObject::invokeMethod(&host, []() { QCoreApplication::quit(); }, Qt::QueuedConnection);
    });

    // Todo mensaje del navegador se reenvía al WebSocket local (hacia SDM).
    QObject::connect(&host, &sdm::NativeMessagingHost::fromBrowser, &host,
                     [&host](const QByteArray& payload) { host.forwardToSdm(payload); },
                     Qt::QueuedConnection);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &host, []() {
        sdm::g_running.store(false, std::memory_order_relaxed);
    });
    const int exitCode = app.exec();
    sdm::g_running.store(false);
    if (reader.joinable()) {
        reader.join(); // waitStdin revisa el flag y termina en el siguiente sondeo
    }
    return exitCode;
}
