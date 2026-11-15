// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/bridge/WebSocketServer.cpp
// ============================================================================
#include "bridge/WebSocketServer.h"

#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketServer>

#include <utility>

namespace sdm {

namespace {

// Limites del canal local: un cliente hostil (o un fallo de la extension) no
// puede agotar memoria con frames gigantes ni bloquear el puerto con
// conexiones a medio autenticar.
constexpr qint64 kMaxWsMessageBytes = 1 << 20; // 1 MiB por frame
constexpr int kMaxPendingSockets = 16;         // conexiones sin token a la vez
constexpr int kHandshakeTimeoutMs = 5000;
constexpr int kAuthWindowMs = 5000;

} // namespace

// ---------------------------------------------------------------------------
// BridgeInfo / autenticacion
// ---------------------------------------------------------------------------

QString BridgeInfo::connectionKey() const {
    return QString::number(port) + QLatin1Char(':') + token;
}

bool BridgeInfo::parseConnectionKey(const QString& key, quint16* port, QString* token) {
    const qsizetype colon = key.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == key.size() - 1) {
        return false;
    }
    bool okPort = false;
    const int parsedPort = key.left(colon).toInt(&okPort);
    const QString parsedToken = key.mid(colon + 1).trimmed();
    if (!okPort || parsedPort <= 0 || parsedPort > 65535 || parsedToken.size() < 32) {
        return false;
    }
    if (port != nullptr) {
        *port = static_cast<quint16>(parsedPort);
    }
    if (token != nullptr) {
        *token = parsedToken;
    }
    return true;
}

namespace auth {

// Declaracion previa (orden de definicion en este fichero).
bool verifyToken(const QString& expected, const QString& token);

bool writeBridgeInfo(const BridgeInfo& info) {
    if (info.filePath.isEmpty()) {
        return false;
    }
    QDir().mkpath(QFileInfo(info.filePath).absolutePath());

    QJsonObject object;
    object.insert(QStringLiteral("port"), static_cast<int>(info.port));
    object.insert(QStringLiteral("token"), info.token);
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);

    // Escritura atomica (tmp + rename) para no dejar el fichero a medias.
    const QString temporary = info.filePath + QStringLiteral(".tmp");
    QFile out(temporary);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        log::error("No se pudo escribir {}", log::utf8(info.filePath));
        return false;
    }
    out.write(payload);
    out.flush();
    out.close();
    QFile::remove(info.filePath);
    if (!QFile::rename(temporary, info.filePath)) {
        return QFile::copy(temporary, info.filePath);
    }
    return true;
}

BridgeInfo loadOrCreate(const QString& dir) {
    BridgeInfo info;
    QDir().mkpath(dir);
    info.filePath = dir + QStringLiteral("/bridge.json");
    info.port = 9010;

    QFile in(info.filePath);
    if (in.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(in.readAll());
        in.close();
        if (doc.isObject()) {
            const QJsonObject object = doc.object();
            const int port = object.value(QStringLiteral("port")).toInt(9010);
            const QString token = object.value(QStringLiteral("token")).toString();
            if (port > 0 && port <= 65535) {
                info.port = static_cast<quint16>(port);
            }
            if (token.size() >= 32) {
                info.token = token;
            }
        }
    }

    if (info.token.isEmpty()) {
        // 32 bytes aleatorios del generador criptografico del sistema.
        QString token;
        token.reserve(64);
        for (int i = 0; i < 8; ++i) {
            token += QString::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, QLatin1Char('0'));
        }
        info.token = token;
        writeBridgeInfo(info);
        log::info("Token de puente local generado");
    } else if (!QFile::exists(info.filePath)) {
        writeBridgeInfo(info);
    }
    return info;
}

bool verifyToken(const BridgeInfo& info, const QString& token) {
    return verifyToken(info.token, token);
}

bool verifyToken(const QString& expected, const QString& token) {
    if (expected.isEmpty() || token.size() != expected.size()) {
        return false;
    }
    int diff = 0;
    for (qsizetype i = 0; i < expected.size(); ++i) {
        diff |= expected.at(i).unicode() ^ token.at(i).unicode();
    }
    return diff == 0;
}

} // namespace auth

// ---------------------------------------------------------------------------
// Servidor
// ---------------------------------------------------------------------------

WebSocketServer::WebSocketServer(QObject* parent) : QObject(parent) {
}

WebSocketServer::~WebSocketServer() {
    close();
}

bool WebSocketServer::listen(quint16 preferredPort, const QString& token, QString* error) {
    m_token = token;
    close();

    m_server = new QWebSocketServer(QStringLiteral("S-Download Manager"), QWebSocketServer::NonSecureMode, this);
    m_server->setMaxAllowedIncomingMessageSize(kMaxWsMessageBytes);
    m_server->setHandshakeTimeout(kHandshakeTimeoutMs);

    bool bound = false;
    if (preferredPort > 0) {
        // Puerto fijo con hasta 10 alternativas consecutivas.
        for (int offset = 0; offset < 10 && !bound; ++offset) {
            const quint16 candidate = static_cast<quint16>(preferredPort + offset);
            bound = m_server->listen(QHostAddress::LocalHost, candidate);
        }
    }
    if (!bound) {
        // Puerto efimero del sistema (siempre libre).
        bound = m_server->listen(QHostAddress::LocalHost, 0);
    }
    if (!bound) {
        const QString message =
            QStringLiteral("No se pudo abrir el servidor local del puente: %1").arg(m_server->errorString());
        if (error != nullptr) {
            *error = message;
        }
        log::error("{}", log::utf8(message));
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }

    connect(m_server, &QWebSocketServer::newConnection, this, &WebSocketServer::onNewConnection);
    log::info("Puente WebSocket escuchando en 127.0.0.1:{}", static_cast<int>(m_server->serverPort()));
    return true;
}

void WebSocketServer::close() {
    for (QWebSocket* client : std::as_const(m_clients)) {
        closeSocket(client, static_cast<int>(QWebSocketProtocol::CloseCodeNormal),
                    QStringLiteral("servidor detenido"));
        client->deleteLater();
    }
    m_clients.clear();
    for (QWebSocket* pending : std::as_const(m_pending)) {
        closeSocket(pending, static_cast<int>(QWebSocketProtocol::CloseCodeNormal),
                    QStringLiteral("servidor detenido"));
        pending->deleteLater();
    }
    m_pending.clear();

    if (m_server != nullptr) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}

bool WebSocketServer::isListening() const {
    return m_server != nullptr && m_server->isListening();
}

quint16 WebSocketServer::port() const {
    return m_server != nullptr ? m_server->serverPort() : 0;
}

int WebSocketServer::clientCount() const {
    return static_cast<int>(m_clients.size());
}

void WebSocketServer::broadcast(const QByteArray& payload) {
    if (m_clients.isEmpty() || payload.isEmpty()) {
        return;
    }
    const QString text = QString::fromUtf8(payload);
    for (QWebSocket* client : std::as_const(m_clients)) {
        if (client != nullptr && client->isValid()) {
            client->sendTextMessage(text);
        }
    }
}

void WebSocketServer::sendTo(QObject* sender, const QByteArray& payload) {
    auto* socket = qobject_cast<QWebSocket*>(sender);
    if (socket == nullptr || payload.isEmpty()) {
        return;
    }
    if (socket->isValid()) {
        socket->sendTextMessage(QString::fromUtf8(payload));
    }
}

void WebSocketServer::closeSocket(QWebSocket* socket, int code, const QString& reason) {
    if (socket == nullptr) {
        return;
    }
    socket->close(static_cast<QWebSocketProtocol::CloseCode>(code), reason);
}

void WebSocketServer::dropClient(QWebSocket* socket) {
    if (m_clients.remove(socket)) {
        emit clientDisconnected();
    }
    m_pending.remove(socket);
}

void WebSocketServer::onNewConnection() {
    while (m_server != nullptr && m_server->hasPendingConnections()) {
        QWebSocket* socket = m_server->nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }

        // 1) Exclusivamente bucle de retorno local (spec 8.2).
        const QHostAddress peer = socket->peerAddress();
        if (!peer.isLoopback()) {
            log::warn("Puente WS rechazado: origen no local ({})", log::utf8(peer.toString()));
            closeSocket(socket, static_cast<int>(QWebSocketProtocol::CloseCodePolicyViolated),
                        QStringLiteral("solo acceso local"));
            socket->deleteLater();
            continue;
        }

        // 1b) Contrapresion: tope de conexiones pendientes de autenticar.
        if (m_pending.size() >= kMaxPendingSockets) {
            log::warn("Puente WS rechazado: demasiados clientes sin autenticar ({})",
                      static_cast<int>(m_pending.size()));
            closeSocket(socket, static_cast<int>(QWebSocketProtocol::CloseCodeGoingAway),
                        QStringLiteral("demasiadas conexiones pendientes"));
            socket->deleteLater();
            continue;
        }

        socket->setMaxAllowedIncomingMessageSize(kMaxWsMessageBytes);

        // 2) Token via query string (?token=...).
        const QUrlQuery query(socket->requestUrl().query());
        const QString queryToken = query.queryItemValue(QStringLiteral("token"));
        bool authenticated = false;
        if (!queryToken.isEmpty()) {
            authenticated = auth::verifyToken(m_token, queryToken);
        }

        // 3) Desconexion al conectarse.
        connect(socket, &QWebSocket::disconnected, this, [this, socket]() {
            const bool wasClient = m_clients.contains(socket);
            dropClient(socket); // emite clientDisconnected() si procedia
            socket->deleteLater();
            Q_UNUSED(wasClient)
        });

        connect(socket, &QWebSocket::textMessageReceived, this,
                [this, socket](const QString& message) {
                    const QByteArray raw = message.toUtf8();
                    if (m_clients.contains(socket)) {
                        emit messageReceived(raw, socket);
                        return;
                    }
                    // Cliente pendiente: primer mensaje = {"type":"hello","token":...}
                    // QJsonDocument expone object() (toObject() es de QJsonValue).
                    const QString helloToken =
                        QJsonDocument::fromJson(raw).object().value(QStringLiteral("token")).toString();
                    if (!helloToken.isEmpty() && auth::verifyToken(m_token, helloToken)) {
                        m_pending.remove(socket);
                        m_clients.insert(socket);
                        log::info("Cliente WS autenticado ({} clientes)", static_cast<int>(m_clients.size()));
                        emit clientConnected();
                        emit messageReceived(raw, socket); // ProtocolHandler responde al hello
                    } else {
                        log::warn("Puente WS: token invalido, desconectando");
                        dropClient(socket);
                        closeSocket(socket, static_cast<int>(QWebSocketProtocol::CloseCodePolicyViolated),
                                    QStringLiteral("token invalido"));
                    }
                });

        if (authenticated) {
            m_clients.insert(socket);
            emit clientConnected();
        } else {
            m_pending.insert(socket);
            // Ventana de 5 s para autenticarse.
            QTimer::singleShot(kAuthWindowMs, socket, [this, socket]() {
                if (m_pending.remove(socket)) {
                    log::warn("Puente WS: cliente sin autenticar, desconectado");
                    closeSocket(socket, static_cast<int>(QWebSocketProtocol::CloseCodePolicyViolated),
                                QStringLiteral("autenticacion requerida"));
                }
            });
        }
    }
}

} // namespace sdm
