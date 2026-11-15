// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/bridge/WebSocketServer.h
//
//  Canal secundario de comunicacion (spec 8.2): servidor WebSocket local
//  escuchando exclusivamente en 127.0.0.1 con token de autenticacion de
//  256 bits generado en el primer arranque.  Cualquier cliente no
//  autenticado es desconectado de inmediato.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>

class QWebSocket;
class QWebSocketServer;

namespace sdm {

/// Informacion del puente local: puerto efectivo + token de acceso.
struct BridgeInfo {
    quint16 port = 9010;
    QString token;
    QString filePath;

    /// Clave compacta "puerto:token" para pegarla en la extension.
    QString connectionKey() const;
    static bool parseConnectionKey(const QString& key, quint16* port, QString* token);
};

namespace auth {

/// Carga o crea el fichero de autenticacion (JSON) en `dir`:
///   { "port": 9010, "token": "<64 hex>" }
BridgeInfo loadOrCreate(const QString& dir);

/// Actualiza el puerto tras hacer listen (el token no cambia).
bool writeBridgeInfo(const BridgeInfo& info);

/// Compara el token recibido con el almacenado (comparacion constante).
bool verifyToken(const BridgeInfo& info, const QString& token);

} // namespace auth

class WebSocketServer : public QObject {
    Q_OBJECT

public:
    explicit WebSocketServer(QObject* parent = nullptr);
    ~WebSocketServer() override;

    /// Levanta el servidor en 127.0.0.1 (puerto fijo o el siguiente libre).
    bool listen(quint16 preferredPort, const QString& token, QString* error = nullptr);
    void close();

    bool isListening() const;
    quint16 port() const;
    int clientCount() const;

    /// Envia un mensaje JSON a todos los clientes autenticados.
    void broadcast(const QByteArray& payload);

    /// Envia un mensaje JSON solo al cliente que lo origino.
    void sendTo(QObject* sender, const QByteArray& payload);

signals:
    /// Mensaje JSON ya autenticado (lo procesa ProtocolHandler).
    void messageReceived(const QByteArray& raw, QObject* sender);
    void clientConnected();
    void clientDisconnected();

private:
    void onNewConnection();
    void closeSocket(QWebSocket* socket, int code, const QString& reason);
    void rejectOversizedMessage(QWebSocket* socket, qint64 bytes);
    void dropClient(QWebSocket* socket);

    QWebSocketServer* m_server = nullptr;
    QString m_token;
    QSet<QWebSocket*> m_clients;
    QSet<QWebSocket*> m_pending; // conectados, sin autenticar
};

} // namespace sdm
