// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/DownloadEngine.h
//
//  Coordinador global de tareas y colas (spec cap. 3 y 5):
//   - Sonda de capacidades del servidor (HEAD / Range 0-0)
//   - Particionado dinámico 1/6/8/16 hilos con re-segmentacion continua
//   - Reanudacion byte-exacta con validacion ETag
//   - Limitador global de concurrencia + cola FIFO
//   - Deteccion HTTP 403/401 y refresco de enlace via navegador (spec cap. 10)
//   - Integracion del mulo HLS/DASH (StreamDownloader)
// ============================================================================
#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <memory>

#include "database/Models.h"

class QThread;

namespace sdm {

class DatabaseManager;
class FileAllocator;
class SegmentWorker;
class StreamDownloader;
struct ServerCapabilities;

/// Ajustes globales del motor (persistidos en QSettings por la capa UI).
struct EngineSettings {
    QString downloadDir;                        // Carpeta de destino por defecto
    int defaultThreads = kDefaultThreads;       // 1 | 6 | 8 | 16
    int maxConcurrentTasks = kDefaultMaxConcurrent;
    bool autoStart = true;                      // Arrancar tareas al añadirlas
    bool rotateUserAgent = false;               // Rotar perfil de navegador por tarea
    bool preallocateFiles = true;               // Reserva de espacio en disco
    bool useYtDlp = false;                      // Usar yt-dlp para streams complejos
    QString proxy;                              // Vacío = conexión directa
    QString ffmpegPath = QStringLiteral("ffmpeg");
    QString ytDlpPath = QStringLiteral("yt-dlp");
    int maxRetries = kDefaultMaxRetries;
    int streamChoiceTimeoutSec = 60;            // Auto-mejor variante si nadie elige
};

/// Resultado de la sonda (almacenado temporalmente, hilo de pool -> GUI).
struct ProbeResult {
    bool ok = false;
    ServerCapabilities capabilities;
    QString error;
    int httpStatus = 0;
};

class DownloadEngine : public QObject {
    Q_OBJECT

public:
    static DownloadEngine& instance();

    void initialize(DatabaseManager* db, const EngineSettings& settings);
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    const EngineSettings& settings() const { return m_settings; }
    void updateSettings(const EngineSettings& settings);

    // --- Gestion de tareas ------------------------------------------------
    quint64 addTask(const NewTaskRequest& request);
    quint64 addStreamTask(const QString& manifestUrl, const QString& destDir, const QString& referer,
                          const QString& cookies, const QString& fileName);
    void startTask(quint64 id);
    void pauseTask(quint64 id);
    void removeTask(quint64 id, bool deleteFile);
    void retryTask(quint64 id);
    void setThreadCount(quint64 id, int threads);
    void setDestination(quint64 id, const QString& dir);
    void updateTaskUrl(quint64 id, const QString& url, const QString& cookies, const QString& referer);
    void startAll();
    void pauseAll();
    void clearCompleted();
    void restoreFromDatabase();
    void selectStreamVariant(quint64 id, int variantIndex);

    // --- Consultas --------------------------------------------------------
    bool hasTask(quint64 id) const;
    DownloadTask task(quint64 id) const;
    QList<DownloadTask> allTasks() const;
    int taskCount() const { return static_cast<int>(m_tasks.size()); }
    QList<SegmentInfo> segments(quint64 id) const;
    int activeTaskCount() const;
    int queuedTaskCount() const;
    double globalSpeed() const;
    bool isTaskActive(quint64 id) const;

    /// Instantanea serializable de una tarea (para el protocolo IPC).
    static QVariantMap snapshot(const DownloadTask& task);

    /// Numero de hilos efectivas para una tarea (soportadas: 1/6/8/16).
    static int normalizeThreadCount(int threads);

    /// Guarda el resultado de una sonda ejecutada en el pool de hilos
    /// (lo invoca ProbeRunnable; hilo seguro mediante mutex).
    void storeProbeResult(quint64 id, const ProbeResult& result);

    /// Ejecuta la sonda de capacidades (HEAD / Range 0-0).  Hilo de pool.
    static ProbeResult runProbe(const DownloadTask& task, const QString& proxy);

signals:
    void taskAdded(quint64 id);
    void taskUpdated(quint64 id);
    void taskRemoved(quint64 id);
    void tasksReloaded();
    void statusChanged();
    void tokenRefreshNeeded(quint64 id, const QString& originUrl, const QString& fileName);
    void toast(const QString& message, int level); // 0 info, 1 aviso, 2 error
    void streamVariantsReady(quint64 id, const QVariantList& variants, const QString& manifestUrl);

private slots:
    void tick();
    void pumpQueue();
    void onSegmentFinished(quint64 taskId, int index, qint64 received, int result, int httpStatus,
                           const QString& error);
    void onProbeArrived(quint64 taskId);
    void onStreamProbeFinished(bool ok, const QVariantList& variants, const QString& error);
    void onStreamProgress(qint64 received, qint64 total);
    void onStreamFinished(bool ok, const QString& outputPath, const QString& error);

private:
    explicit DownloadEngine(QObject* parent = nullptr);
    ~DownloadEngine() override;

    // Runtime por tarea (recursos vivos mientras descarga).
    struct Runtime {
        FileAllocator* io = nullptr;
        QHash<int, SegmentWorker*> workers;
        QHash<int, QThread*> threads;
        bool probeInFlight = false;
        bool stopRequested = false;     // pausa / borrado en curso
        bool refreshStopping = false;   // parada asociada a un 403
        bool removePending = false;     // borrar la tarea al detenerse
        bool removeDeleteFile = false;  // borrar tambien los ficheros
        bool degradePending = false;    // degradar a 1 hilo al detenerse
        bool rangeSupported = true;     // false -> el servidor ignora Range
        qint64 retryAtMs = 0;           // backoff antes de reintentar la sonda
        qint64 lastSavedBytes = -1;
        int ticksSinceSave = 0;
        qint64 lastTickMs = 0;
        qint64 lastBytes = 0;
        double speedEma = 0.0;
        int rebalanceIndex = -1;        // segmento abortado para re-partir (-1 = nada)
        QTimer* refreshTimer = nullptr;
        QTimer* variantTimer = nullptr;
        // Stream (HLS/DASH)
        bool streamActive = false;
        int pendingVariant = -1;
        qint64 streamReceived = 0;
        qint64 streamTotal = -1;
    };

    struct ProbeHolder; // definido en .cpp

    qsizetype indexOf(quint64 id) const;
    DownloadTask* mutableTask(quint64 id);
    Runtime* runtimeFor(quint64 id, bool create);

    void launchProbe(quint64 id);
    void onProbeResult(quint64 id, const ProbeResult& result);
    void beginSegments(quint64 id);
    void spawnSegment(quint64 id, const SegmentInfo& segment);
    void maybeRebalance(quint64 id);
    void performRebalance(quint64 id, int index, qint64 received);
    void stopAllSegments(quint64 id, bool forRefresh, bool forStop);
    void finalizeStop(quint64 id);
    void finalizeDownload(quint64 id);
    void failTask(quint64 id, const QString& message);
    void enterWaitingRefresh(quint64 id, int httpStatus);
    void resetTaskData(quint64 id);
    void persistProgress(quint64 id);
    void destroyRuntime(quint64 id);
    void removeInternal(quint64 id, bool deleteFile);
    void startStreamJob(quint64 id);
    void pumpGuarded();
    void appendRestored(const DownloadTask& task);
    void applyDegrade(quint64 id);

    DatabaseManager* m_db = nullptr;
    EngineSettings m_settings;
    bool m_initialized = false;
    bool m_pumping = false;
    bool m_shuttingDown = false;

    QList<DownloadTask> m_tasks;
    QHash<quint64, qsizetype> m_index;
    QHash<quint64, Runtime*> m_runtimes;
    QHash<quint64, ProbeResult> m_probeResults;
    mutable QMutex m_probeMutex;

    QTimer m_tickTimer;
    StreamDownloader* m_stream = nullptr;
    quint64 m_streamJobId = 0;
    quint64 m_nextId = 1;
};

} // namespace sdm
