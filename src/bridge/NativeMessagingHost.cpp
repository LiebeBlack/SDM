// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/bridge/NativeMessagingHost.cpp
//
//  Binario puente "SDMNativeHost" (canal primario, spec 8.1).
//  main() incluido: este fichero solo forma parte del objetivo SDMNativeHost.
// ============================================================================
#include "bridge/NativeMessagingHost.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

#ifdef _WIN32
#  include <fcntl.h>
#  include <io.h>
#endif

namespace sdm {

namespace {

constexpr quint32 kMaxNativeMessage = 1024 * 1024; // Limite de Chrome (1 MB)
std::atomic<bool> g_running{true};

/// Escribe un frame nativo: longitud uint32 LE + carga util.
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

} // namespace

NativeMessagingHost::NativeMessagingHost(QObject* parent) : QObject(parent) {
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(2000);
    connect(m_reconnectTimer, &QTimer::timeout, this, &NativeMessagingHost::tryConnect);
}

NativeMessagingHost::~NativeMessagingHost() {
    g_running.store(false);
    if (m_socket != nullptr) {
        m_socket->abort();
    }
}

bool NativeMessagingHost::start(const QString& bridgeInfoPath, QString* error) {
    QFile infoFile(bridgeInfoPath);
    if (!infoFile.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("No se encontró bridge.json (%1). ¿Esta SDM en ejecucion?")
                         .arg(bridgeInfoPath);
        }
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(infoFile.readAll());
    infoFile.close();
    if (!doc.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("bridge.json corrupto");
        }
        return false;
    }
    const QJsonObject object = doc.object();
    m_port = static_cast<quint16>(object.value(QStringLiteral("port")).toInt(9010));
    m_token = object.value(QStringLiteral("token")).toString();
    if (m_token.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("bridge.json sin token");
        }
        return false;
    }

    m_socket = new QWebSocket(QStringLiteral("SDM Native Host"), QWebSocket::Client, this);
    connect(m_socket, &QWebSocket::connected, this, &NativeMessagingHost::onConnected);
    connect(m_socket, &QWebSocket::disconnected, this, &NativeMessagingHost::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &NativeMessagingHost::onTextMessage);

    tryConnect();
    if (!m_wsConnected) {
        m_reconnectTimer->start(); // reintenta mientras el navegador espera
    }
    return true;
}

void NativeMessagingHost::tryConnect() {
    if (m_socket == nullptr || m_wsConnected) {
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
    m_reconnectTimer->stop();
}

void NativeMessagingHost::onDisconnected() {
    m_wsConnected = false;
    if (!m_exiting) {
        m_reconnectTimer->start();
    }
}

void NativeMessagingHost::onTextMessage(const QString& message) {
    sendToBrowser(message.toUtf8());
}

void NativeMessagingHost::sendToBrowser(const QByteArray& jsonPayload) {
    if (jsonPayload.isEmpty()) {
        return;
    }
    writeFrame(jsonPayload);
}

void NativeMessagingHost::pumpStdout() {
    // Reservado para futuros mensajes encolados.
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
#endif

    QCoreApplication app(argc, argv);
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

    // Hilo lector de STDIN (formato nativo: uint32 LE + JSON).
    std::thread reader([&host]() {
        while (sdm::g_running.load(std::memory_order_relaxed)) {
            quint32 length = 0;
            const size_t readLength = std::fread(&length, sizeof(length), 1, stdin);
            if (readLength != 1) {
                break; // EOF: el navegador cerro el canal
            }
            if (length == 0) {
                continue;
            }
            if (length > sdm::kMaxNativeMessage) {
                std::fprintf(stderr, "SDMNativeHost: frame demasiado grande, se ignora\n");
                break;
            }
            std::string buffer(static_cast<size_t>(length), '\0');
            const size_t readPayload = std::fread(buffer.data(), 1, length, stdin);
            if (readPayload != length) {
                break;
            }
            const QByteArray payload(buffer.data(), static_cast<qsizetype>(readPayload));
            QMetaObject::invokeMethod(
                &host, [&host, payload]() { emit host.fromBrowser(payload); }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(&host, []() { QCoreApplication::quit(); }, Qt::QueuedConnection);
    });

    // Todo mensaje del navegador se reenvia al puerto local de SDM.
    QObject::connect(&host, &sdm::NativeMessagingHost::fromBrowser, &host,
                     [&host](const QByteArray& payload) { host.sendToBrowser(payload); },
                     Qt::QueuedConnection);

    const int exitCode = app.exec();
    sdm::g_running.store(false);
    if (reader.joinable()) {
        reader.detach();
    }
    return exitCode;
}
