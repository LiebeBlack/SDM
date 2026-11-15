// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/bridge/NativeMessagingHost.h
//
//  Canal primario (spec 8.1): puente STDIN/STDOUT <-> WebSocket local.
//  El navegador (Chrome/Edge/Firefox) lo lanza y encola los mensajes como
//  entero de 32 bits en orden nativo (little-endian) + carga util JSON.
//  Este binario solo reenvia: toda la logica vive en SDM.
//
//  Gestion de recursos:
//   - Cola de salida (hacia el navegador) acotada por bytes: contrapresion;
//     si el navegador no consume, la memoria no crece sin limite.
//   - Hilo escritor dedicado: las escrituras STDIO bloqueantes nunca frenan
//     el bucle de eventos, la red ni la interfaz del navegador.
//   - Cola de retencion (hacia SDM) acotada por mensajes y bytes: las
//     peticiones del usuario sobreviven a un reinicio corto del gestor.
//   - Reconexión al WebSocket local con backoff exponencial (1 s -> 30 s).
//   - Cierre ordenado: señal de parada, drenaje final y espera acotada con
//     desacoplamiento seguro (el hilo solo toca estado compartido).
// ============================================================================
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

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

    /// Encola un mensaje JSON hacia STDOUT (lo drena el hilo escritor).
    void sendToBrowser(const QByteArray& jsonPayload);

    /// Reenvia un mensaje del navegador a SDM; si el WebSocket aun no esta
    /// conectado, la peticion se retiene (con limite) hasta que lo este.
    void forwardToSdm(const QByteArray& jsonPayload);

signals:
    /// Mensaje recibido del navegador (ya leido de STDIN).
    void fromBrowser(const QByteArray& jsonPayload);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& message);
    void tryConnect();

private:
    /// Estado compartido entre este objeto y el hilo escritor. El hilo solo
    /// toca esta estructura y STDOUT: puede desacoplarse con seguridad si una
    /// escritura queda bloqueada al cerrar (el proceso recupera el descriptor).
    struct WriterState {
        std::mutex mutex;
        std::condition_variable cv;
        QList<QByteArray> queue; // frames JSON pendientes hacia el navegador
        qint64 bytes = 0;        // bytes retenidos en queue (contrapresion)
        int dropped = 0;         // frames descartados por saturacion
        bool stop = false;       // señal de parada cooperativa
        bool finished = false;   // el hilo ha terminado y drenado
    };

    /// Cuerpo del hilo escritor: drena la cola hacia STDOUT con escrituras
    /// bloqueantes fuera del bucle de eventos. Termina al señalizar stop y
    /// drenar lo pendiente, o al romperse el canal con el navegador.
    static void pumpStdout(const std::shared_ptr<WriterState>& state);

    /// Programa el siguiente reintento con backoff exponencial (1 s -> 30 s).
    /// Idempotente: un reintento ya programado no se reprograma.
    void scheduleReconnect();

    /// Entrega al WebSocket los mensajes del navegador retenidos.
    void flushPendingToSdm();

    QWebSocket* m_socket = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    quint16 m_port = 0;
    QString m_token;
    QList<QByteArray> m_pendingToSdm; // mensajes del navegador esperando WebSocket
    qint64 m_pendingToSdmBytes = 0;   // bytes retenidos en m_pendingToSdm
    int m_reconnectDelayMs = 0;       // backoff actual (se duplica hasta el tope)
    bool m_wsConnected = false;
    bool m_exiting = false;

    std::shared_ptr<WriterState> m_writer;
    std::thread m_writerThread;
};

} // namespace sdm
