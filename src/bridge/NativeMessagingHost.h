// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/bridge/NativeMessagingHost.h
//
//  Canal primario (spec 8.1): puente STDIN/STDOUT <-> WebSocket local.
//  El navegador (Chrome/Edge/Firefox) lo lanza y encola los mensajes como
//  entero de 32 bits en orden nativo (little-endian) + carga util JSON.
//  Este binario solo reenvia: toda la logica vive en SDM.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QWebSocket;
class QTimer;

namespace sdm {

class NativeMessagingHost : public QObject {
    Q_OBJECT

public:
    explicit NativeMessagingHost(QObject* parent = nullptr);
    ~NativeMessagingHost() override;

    /// Lee bridge.json (puerto + token) y conecta el WebSocket local.
    bool start(const QString& bridgeInfoPath, QString* error = nullptr);

    /// Encola un mensaje JSON hacia STDOUT (lo escribe el bucle principal).
    void sendToBrowser(const QByteArray& jsonPayload);

signals:
    /// Mensaje recibido del navegador (ya leido de STDIN).
    void fromBrowser(const QByteArray& jsonPayload);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& message);
    void tryConnect();

private:
    void pumpStdout();

    QWebSocket* m_socket = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    quint16 m_port = 0;
    QString m_token;
    QByteArray m_outbox;       // mensajes pendientes de conexion WS
    bool m_wsConnected = false;
    bool m_exiting = false;
};

} // namespace sdm
