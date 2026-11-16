// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/bridge/ProtocolHandler.h
//
//  Enrutador de mensajes JSON entre la extension del navegador / Native Host
//  y el motor de descargas (spec cap. 8).  Mensajes documentados en
//  docs/PROTOCOL.md.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace sdm {

class ProtocolHandler : public QObject {
    Q_OBJECT

public:
    explicit ProtocolHandler(QObject* parent = nullptr);

    void setBridgeToken(const QString& token) { m_bridgeToken = token; }
    /// Puerto del WebSocket local; se publica en getBridgeInfo para que la
    /// extension pueda abrir el canal de respaldo sin configuracion manual.
    void setBridgePort(quint16 port) { m_bridgePort = port; }

    /// Procesa un mensaje JSON entrante y devuelve la respuesta ("" si la
    /// respuesta la emite el motor de forma asincrona).
    QByteArray process(const QByteArray& raw);

    /// Construye un mensaje de error normalizado.
    static QByteArray errorResponse(const QString& message, const QString& ref = QString());

    /// Construye un mensaje JSON con tipo + payload.
    static QByteArray makeMessage(const QString& type, const QVariantMap& payload);

signals:
    /// Mensaje saliente hacia todos los clientes conectados (broadcast).
    void push(const QByteArray& payload);

private slots:
    void onTaskUpdated(quint64 id);
    void onTaskAdded(quint64 id);
    void onTaskRemoved(quint64 id);
    void onTasksReloaded();
    void onToast(const QString& message, int level);
    void onTokenRefreshNeeded(quint64 id, const QString& originUrl, const QString& fileName);
    void onStreamVariantsReady(quint64 id, const QVariantList& variants, const QString& manifestUrl);
    void onStatusChanged();

private:
    void wireEngine();

    QElapsedTimer m_progressThrottle;
    QElapsedTimer m_statusThrottle;
    QString m_bridgeToken;
    quint16 m_bridgePort = 0;
};

} // namespace sdm
