// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/SegmentWorker.h
//
//  Manejador de hilos de descarga con libcurl (spec cap. 5): cada worker
//  descarga un rango [start..end] mediante peticiones HTTP "Range: bytes="
//  con reintentos exponenciales, deteccion de 403 (enlace caducado) y
//  deteccion de servidores que ignoran el rango.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>

#include <atomic>

#include <curl/curl.h>

#include "database/Models.h"

namespace sdm {

class FileAllocator;

/// Resultado de la ejecucion de un segmento (se envia como int en senales
/// para garantizar compatibilidad total con conexiones en cola).
enum class SegmentResult : int {
    Completed = 0,        // Rango completo y escrito en disco
    Aborted = 1,          // Detenido por el usuario / re-segmentacion
    Failed = 2,           // Error de red tras agotar reintentos
    Expired = 3,          // HTTP 403/401 -> refresco de enlace requerido
    RangeUnsupported = 4, // El servidor ignora "Range" (fallback a 1 hilo)
    WebPage = 5           // El servidor devolvio HTML en vez del archivo
};

/// Peticion inmutable de descarga de un segmento.
struct SegmentRequest {
    DownloadTask task;         // Copia con url/referer/cookies/UA/auth de la tarea
    int index = 0;             // Indice del segmento dentro de la tarea
    qint64 startByte = 0;      // Inicio absoluto del rango
    qint64 endByte = -1;       // Fin absoluto inclusivo (-1 = hasta EOF)
    qint64 initialReceived = 0;// Bytes ya presentes en disco de este rango (reanudacion)
    bool sendRange = true;     // false -> servidor sin Accept-Ranges (descarga lineal)
    QString proxy;             // Proxy HTTP(S) opcional (vacio = directo)
    int maxRetries = 3;        // Reintentos por fallo transitorio
    int connectTimeoutSec = 20;

    /// Tamano total del rango (-1 si es abierto).
    qint64 rangeSize() const {
        return endByte < 0 ? -1 : (endByte - startByte + 1);
    }
};

/// Un hilo de descarga = un QObject movido a su propio QThread.
class SegmentWorker : public QObject {
    Q_OBJECT

public:
    explicit SegmentWorker(SegmentRequest request, FileAllocator* io, QObject* parent = nullptr);
    ~SegmentWorker() override;

    /// Detiene la transferencia lo antes posible (thread-safe, atomico).
    void requestAbort();

    /// Bytes acumulados del rango (incluye los preexistentes de reanudacion).
    qint64 receivedBytes() const noexcept;

    /// Bytes escritos durante esta ejecucion (para diagnostico).
    int index() const noexcept { return m_request.index; }
    const SegmentRequest& request() const noexcept { return m_request; }

public slots:
    /// Punto de entrada en el hilo del worker (senal QThread::started).
    void start();

signals:
    /// Emitido exactamente una vez al terminar.
    /// result: static_cast<int>(SegmentResult)
    void finished(quint64 taskId, int index, qint64 received, int result, int httpStatus,
                  const QString& error);

private:
    // Ejecuta una transferencia completa con los parametros actuales.
    bool performOnce(int* httpStatus, bool* rangeIgnored, QString* error);

    // Callbacks estaticos de libcurl.
    static size_t writeThunk(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t headerThunk(char* buffer, size_t size, size_t nitems, void* userdata);
    static int xferinfoThunk(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                             curl_off_t ulnow);

    // Escritura efectiva en disco (offset = startByte + received).
    bool appendToDisk(const char* data, qint64 len);

    // Anti "descargar una web": decide sobre los primeros bytes del cuerpo.
    // 0 = contenido legitimo (retenido escrito), 1 = pagina web, 2 = error de disco.
    int finishSniff();

    bool isRangeComplete() const;
    QString rangeHeaderValue() const;

    SegmentRequest m_request;
    FileAllocator* m_io = nullptr;

    std::atomic<bool> m_abort{false};
    std::atomic<qint64> m_received{0}; // total de bytes del rango ya en disco

    // Estado solo-hilo (no necesita proteccion):
    int m_lastStatus = 0;       // Ultimo codigo HTTP observado
    bool m_rangeIgnored = false;// El servidor respondio 200 a un rango con offset > 0
    bool m_sniffPending = false;// Olfateo anti-HTML pendiente (solo offset 0)
    bool m_webPage = false;     // El cuerpo resulto ser una pagina web
    QByteArray m_sniffBuf;      // Primeros bytes retenidos hasta decidir
};

} // namespace sdm
