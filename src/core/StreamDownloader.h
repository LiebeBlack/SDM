// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/StreamDownloader.h
//
//  Mulo de captura de streaming (spec cap. 9):
//   - Parseo de manifiestos HLS (.m3u8) y DASH (.mpd)
//   - Extraccion de calidades (1080p/2K/4K/8K + bitrates)
//   - Descarga paralela de segmentos con reintentos
//   - Ensamblado/MUX transparente a MP4/MKV mediante FFmpeg
//   - Modo alternativo yt-dlp (sin interfaz) para sitios complejos
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <mutex>
#include <thread>

namespace sdm {

/// Una variante de calidad disponible en el manifiesto.
struct StreamVariant {
    QString url;              // HLS: URL del playlist de la variante
    QString label;            // "1080p", "1920x1080", "4500 kbps"
    QString resolution;       // "1920x1080"
    QString codecs;
    QString mimeType;
    int bandwidth = 0;        // bps
    int width = 0;
    int height = 0;

    // DASH: segmentos ya resueltos (la variante se auto-contiene).
    bool isDash = false;
    QStringList segments;
    QString initSegment;
    QStringList audioSegments;
    QString audioInitSegment;

    /// Serializa para el protocolo IPC / la UI QML.
    QVariantMap toVariantMap() const;
    bool hasSeparateAudio() const { return !audioSegments.isEmpty(); }
};

class StreamDownloader : public QObject {
    Q_OBJECT

public:
    explicit StreamDownloader(QObject* parent = nullptr);
    ~StreamDownloader() override;

    // --- Operaciones asincronas (devuelven senales) -----------------------

    /// Lee el manifiesto y extrae las variantes de calidad disponibles.
    void probe(const QString& manifestUrl, const QString& referer, const QString& cookies,
               const QString& proxy);

    /// Descarga la variante `variantIndex` y escribe el fichero final.
    /// `destPartPath` es la ruta temporal (.part) de la tarea.
    void download(quint64 jobId, const QString& manifestUrl, int variantIndex, const QString& destPartPath,
                  const QString& referer, const QString& cookies, const QString& proxy,
                  const QString& ffmpegPath, const QString& ytDlpPath, bool useYtDlp);

    /// Peticion de detencion (thread-safe).
    void abort();
    bool isBusy() const;

    // --- Parseo puro (sin red) - cubierto por tests -----------------------

    /// Parsea un playlist maestro HLS (#EXT-X-STREAM-INF).  Vacio si no lo es.
    static QList<StreamVariant> parseMasterPlaylist(const QString& text, const QString& baseUrl);

    /// Parsea un playlist media HLS (#EXTINF + URI).  false si es invalido
    /// o es una emision en directo (sin #EXT-X-ENDLIST).
    static bool parseMediaPlaylist(const QString& text, const QString& baseUrl, QStringList* segments,
                                   QString* initSegment, QString* error);

    /// Parsea un manifiesto DASH (MPD).  Devuelve variantes de video con su
    /// audio embebido o en pistas separadas.
    static QList<StreamVariant> parseDashManifest(const QString& xml, const QString& baseUrl, QString* error);

    /// Resuelve una referencia relativa contra la URL base del manifiesto.
    static QString resolveUrl(const QString& base, const QString& relative);

    /// Ejecuta un proceso externo (FFmpeg, etc.) con espera activa y soporte
    /// de cancelacion.  Devuelve false si termina con error o se aborta.
    static bool runProcess(const QString& program, const QStringList& args, int timeoutMs,
                           std::atomic<bool>* abortFlag, QString* output, QString* error);

signals:
    void probeFinished(bool ok, const QVariantList& variants, const QString& error);
    void progress(qint64 received, qint64 total);
    void downloadFinished(bool ok, const QString& outputPath, const QString& error);

private:
    struct JobPlan {
        QStringList videoSegments;
        QString videoInit;
        QStringList audioSegments;
        QString audioInit;
        qint64 knownTotal = -1;
        bool needsMux = false;
        QString baseName;       // nombre base sin extension
        QString targetFormat;   // "mp4" | "mkv" | "ts" | ""
    };

    void joinThread();
    void runProbe(const QString& manifestUrl, const QString& referer, const QString& cookies,
                  const QString& proxy);
    void runDownload(quint64 jobId, const QString& manifestUrl, int variantIndex, const QString& destPartPath,
                     const QString& referer, const QString& cookies, const QString& proxy,
                     const QString& ffmpegPath, const QString& ytDlpPath, bool useYtDlp);

    /// Descarga paralela de `urls` a ficheros temporales (devuelve fallos).
    bool fetchSegmentsParallel(const QStringList& urls, const QString& referer, const QString& cookies,
                               const QString& proxy, const QString& tempDir, const QString& prefix,
                               QStringList* outFilePaths, QString* error);

    static bool fetchToMemory(const QString& url, const QString& referer, const QString& cookies,
                              const QString& proxy, QByteArray* out, int* status, QString* error,
                              std::atomic<bool>* abortFlag, qint64 maxBytes = 8 * 1024 * 1024);

    static bool fetchToFile(const QString& url, const QString& referer, const QString& cookies,
                            const QString& proxy, const QString& filePath, qint64* outBytes, QString* error,
                            std::atomic<bool>* abortFlag);

    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_busy{false};
    std::thread m_thread;
    QList<StreamVariant> m_cachedVariants;
    QString m_cachedManifestUrl;
    mutable std::mutex m_cacheMutex;
};

} // namespace sdm
