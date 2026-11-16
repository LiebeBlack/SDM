// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/ui/GuiManager.h
//
//  Capa ViewModel del patron MVVM: expone el motor de descargas, los ajustes
//  y el estado del puente IPC a QML, y aporta los modelos virtualizados de la
//  tabla principal y del visualizador de segmentos.
// ============================================================================
#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "core/DownloadEngine.h"
#include "database/Models.h"

class QQmlApplicationEngine;
class QQuickWindow;
class QTimer;
class QNetworkAccessManager;

namespace sdm {

class WebSocketServer;

// ---------------------------------------------------------------------------
// Modelo de la tabla principal de descargas
// ---------------------------------------------------------------------------

class TaskListModel : public QAbstractListModel {
    Q_OBJECT
    /// Recuento para el panel lateral: [cat0..cat6, estado0..estado6].
    Q_PROPERTY(QVariantList counts READ counts NOTIFY countsChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        StateRole,
        StateTextRole,
        StateColorRole,
        StateIconRole,
        CategoryRole,
        CategoryTextRole,
        CategoryIconRole,
        SizeTextRole,
        ReceivedTextRole,
        ProgressRole,
        ProgressTextRole,
        SpeedRole,
        SpeedTextRole,
        EtaTextRole,
        UrlRole,
        ThreadsRole,
        SegmentCountRole,
        CreatedTextRole,
        ErrorRole,
        DirRole,
        SavePathRole,
        IsActiveRole,
        IsFinishedRole,
        SourceRole,
        StreamRole
    };

    explicit TaskListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // --- Filtros (panel lateral + busqueda) ---
    Q_INVOKABLE void setCategoryFilter(int category); // -1 = todas
    Q_INVOKABLE void setStateFilter(int state);       // -1 = todas, 2 = en curso
    Q_INVOKABLE void setSearchText(const QString& text);
    Q_INVOKABLE void clearFilters();
    Q_INVOKABLE int countForCategory(int category) const;
    Q_INVOKABLE int countForState(int state) const;
    Q_INVOKABLE int visibleCount() const;
    Q_INVOKABLE int totalCount() const;

    /// Recuentos cacheados por categoria (0..6) y por estado (7..13).
    QVariantList counts() const;

signals:
    void countsChanged();

public:
    // --- Acciones sobre la fila seleccionada ---
    Q_INVOKABLE quint64 idAt(int row) const;
    Q_INVOKABLE int rowForTask(quint64 id) const;
    Q_INVOKABLE void pauseAt(int row);
    Q_INVOKABLE void resumeAt(int row);
    Q_INVOKABLE void removeAt(int row, bool deleteFile);
    Q_INVOKABLE void retryAt(int row);

public slots:
    void reload();
    void onTaskAdded(quint64 id);
    void onTaskUpdated(quint64 id);
    void onTaskRemoved(quint64 id);

private:
    bool matches(const DownloadTask& task) const;
    void refilter();
    int cacheIndexOf(quint64 id) const;

    QList<DownloadTask> m_cache;
    QHash<quint64, int> m_cacheIndex;
    QList<int> m_visible; // indices de m_cache visibles
    int m_category = -1;
    int m_state = -1;
    QString m_search;
};

// ---------------------------------------------------------------------------
// Modelo del visualizador de segmentos/hilos
// ---------------------------------------------------------------------------

class SegmentModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role { IndexRole = Qt::UserRole + 1, ProgressRole, StateRole, StateTextRole, ColorRole, RangeTextRole };

    explicit SegmentModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setTask(quint64 id);
    Q_INVOKABLE void clear();
    /// Número de segmentos expuesto a QML (rowCount no es invocable).
    Q_INVOKABLE int count() const { return static_cast<int>(m_segments.size()); }
    quint64 currentTask() const { return m_taskId; }

public slots:
    void refresh(quint64 id);

private:
    quint64 m_taskId = 0;
    QList<SegmentInfo> m_segments;
};

// ---------------------------------------------------------------------------
// GuiManager: punto unico QML <-> Motor
// ---------------------------------------------------------------------------

class GuiManager : public QObject {
    Q_OBJECT

    // --- Ajustes generales ---
    Q_PROPERTY(QString downloadDir READ downloadDir WRITE setDownloadDir NOTIFY settingsChanged)
    Q_PROPERTY(int defaultThreads READ defaultThreads WRITE setDefaultThreads NOTIFY settingsChanged)
    Q_PROPERTY(int maxConcurrent READ maxConcurrent WRITE setMaxConcurrent NOTIFY settingsChanged)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY settingsChanged)
    Q_PROPERTY(bool rotateUserAgent READ rotateUserAgent WRITE setRotateUserAgent NOTIFY settingsChanged)
    Q_PROPERTY(bool preallocate READ preallocate WRITE setPreallocate NOTIFY settingsChanged)
    Q_PROPERTY(bool useYtDlp READ useYtDlp WRITE setUseYtDlp NOTIFY settingsChanged)
    Q_PROPERTY(QString proxy READ proxy WRITE setProxy NOTIFY settingsChanged)
    /// Limite de velocidad por tarea en KiB/s (0 = sin limite), estilo IDM.
    Q_PROPERTY(int speedLimitKbps READ speedLimitKbps WRITE setSpeedLimitKbps NOTIFY settingsChanged)
    /// Anade automaticamente los enlaces copiados al portapapeles (estilo IDM).
    Q_PROPERTY(bool clipboardMonitor READ clipboardMonitor WRITE setClipboardMonitor NOTIFY settingsChanged)
    /// Guarda cada descarga en una subcarpeta por tipo (Videos, Musica, ...).
    Q_PROPERTY(bool organizeByCategory READ organizeByCategory WRITE setOrganizeByCategory NOTIFY settingsChanged)
    /// Apaga el equipo cuando la cola termina (estilo IDM "shutdown when done").
    Q_PROPERTY(bool shutdownWhenDone READ shutdownWhenDone WRITE setShutdownWhenDone NOTIFY settingsChanged)
    /// URL del release mas reciente detectado (vacia si no hay novedad).
    Q_PROPERTY(QString pendingUpdateUrl READ pendingUpdateUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString ffmpegPath READ ffmpegPath WRITE setFfmpegPath NOTIFY settingsChanged)
    Q_PROPERTY(QString ytDlpPath READ ytDlpPath WRITE setYtDlpPath NOTIFY settingsChanged)

    // --- Puente con el navegador ---
    Q_PROPERTY(QString connectionKey READ connectionKey NOTIFY bridgeChanged)
    Q_PROPERTY(int bridgePort READ bridgePort NOTIFY bridgeChanged)
    Q_PROPERTY(int connectedClients READ connectedClients NOTIFY bridgeChanged)
    Q_PROPERTY(QString extensionId READ extensionId WRITE setExtensionId NOTIFY bridgeChanged)
    Q_PROPERTY(QString nativeManifestPath READ nativeManifestPath NOTIFY bridgeChanged)

    // --- Apariencia ---
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY chromeChanged)
    Q_PROPERTY(bool micaEnabled READ micaEnabled NOTIFY chromeChanged)

    // --- Estado global (barra inferior) ---
    Q_PROPERTY(double globalSpeed READ globalSpeed NOTIFY statsChanged)
    Q_PROPERTY(QString globalSpeedText READ globalSpeedText NOTIFY statsChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY statsChanged)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY statsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY statsChanged)
    Q_PROPERTY(double memoryMb READ memoryMb NOTIFY statsChanged)

public:
    explicit GuiManager(QQmlApplicationEngine& engine, QObject* parent = nullptr);

    /// Registra modelos y propiedades de contexto antes de cargar QML.
    void registerContext();

    /// Aplica el bridge tras levantar el WebSocket local.
    void applyBridgeInfo(quint16 port, const QString& token, const QString& bridgeFilePath,
                         WebSocketServer* server);

    /// Arranca el motor de descargas con los ajustes persistidos.
    void initializeEngine(DatabaseManager* db);

    /// Ventana principal: titulo oscuro + Mica (Win11) con degradacion segura.
    void applyWindowChrome(QQuickWindow* window);

    // --- Ajustes ---
    QString downloadDir() const;
    void setDownloadDir(const QString& dir);
    int defaultThreads() const;
    void setDefaultThreads(int threads);
    int maxConcurrent() const;
    void setMaxConcurrent(int value);
    bool autoStart() const;
    void setAutoStart(bool value);
    bool rotateUserAgent() const;
    void setRotateUserAgent(bool value);
    bool preallocate() const;
    void setPreallocate(bool value);
    bool useYtDlp() const;
    void setUseYtDlp(bool value);
    QString proxy() const;
    void setProxy(const QString& value);
    int speedLimitKbps() const;
    void setSpeedLimitKbps(int kbps);
    bool clipboardMonitor() const;
    void setClipboardMonitor(bool value);
    bool organizeByCategory() const;
    void setOrganizeByCategory(bool value);
    bool shutdownWhenDone() const;
    void setShutdownWhenDone(bool value);
    QString pendingUpdateUrl() const;
    QString ffmpegPath() const;
    void setFfmpegPath(const QString& value);
    QString ytDlpPath() const;
    void setYtDlpPath(const QString& value);

    // --- Puente ---
    QString connectionKey() const;
    int bridgePort() const;
    int connectedClients() const;
    QString extensionId() const;
    void setExtensionId(const QString& id);
    QString nativeManifestPath() const;

    // --- Apariencia ---
    bool darkMode() const;
    void setDarkMode(bool value);
    bool micaEnabled() const;

    // --- Estadisticas ---
    double globalSpeed() const;
    QString globalSpeedText() const;
    int activeCount() const;
    int queuedCount() const;
    int totalCount() const;
    double memoryMb() const;

    // --- Acciones invocables desde QML ---
    Q_INVOKABLE quint64 addTask(const QVariantMap& options);
    Q_INVOKABLE void pauseTask(quint64 id);
    Q_INVOKABLE void resumeTask(quint64 id);
    Q_INVOKABLE void stopTask(quint64 id);
    Q_INVOKABLE void removeTask(quint64 id, bool deleteFile);
    Q_INVOKABLE void retryTask(quint64 id);
    Q_INVOKABLE void setThreads(quint64 id, int threads);
    Q_INVOKABLE void pauseAll();
    Q_INVOKABLE void resumeAll();
    Q_INVOKABLE void clearCompleted();
    Q_INVOKABLE void selectTask(quint64 id);
    /// Elige la variante de calidad de un flujo HLS/DASH.
    Q_INVOKABLE void selectStreamVariant(quint64 id, int variantIndex);
    /// Cancela una tarea multimedia en espera de elección.
    Q_INVOKABLE void cancelStreamTask(quint64 id);
    Q_INVOKABLE void openFolderFor(quint64 id);
    Q_INVOKABLE void openFileFor(quint64 id);
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE QString suggestedFolder() const;
    Q_INVOKABLE void writeNativeHostManifest();
    Q_INVOKABLE QVariantList segmentsOf(quint64 id) const;
    /// Instantanea de una tarea para paneles de detalle (SegmentBar, etc.).
    Q_INVOKABLE QVariantMap taskInfo(quint64 id) const;
    Q_INVOKABLE void refreshStats();
    /// Comprueba si hay una version nueva en GitHub Releases (toast con resultado).
    Q_INVOKABLE void checkForUpdates();
    /// Abre en el navegador la pagina del release detectado.
    Q_INVOKABLE void openUpdatePage();

signals:
    void settingsChanged();
    void bridgeChanged();
    void chromeChanged();
    void statsChanged();
    void toast(const QString& message, int level);
    void streamVariantsReady(quint64 id, const QVariantList& variants, const QString& manifestUrl);
    void taskSelected(quint64 id);

private:
    void loadSettings();
    void persistSettings();
    void pushEngineSettings();
    EngineSettings buildEngineSettings() const;

    QQmlApplicationEngine& m_engine;
    TaskListModel* m_tasksModel = nullptr;
    SegmentModel* m_segmentsModel = nullptr;
    WebSocketServer* m_webSocket = nullptr;

    // Ajustes persistidos
    QString m_downloadDir;
    int m_defaultThreads = kDefaultThreads;
    int m_maxConcurrent = kDefaultMaxConcurrent;
    bool m_autoStart = true;
    bool m_rotateUserAgent = false;
    bool m_preallocate = true;
    bool m_useYtDlp = false;
    QString m_proxy;
    int m_speedLimitKbps = 0;
    bool m_clipboardMonitor = false;
    bool m_organizeByCategory = false;
    bool m_shutdownWhenDone = false;
    bool m_finishedActionDone = false;
    QString m_lastClipboardUrl;
    QString m_pendingUpdateUrl;
    QString m_ffmpegPath = QStringLiteral("ffmpeg");
    QString m_ytDlpPath = QStringLiteral("yt-dlp");
    bool m_darkMode = true;

    // Puente
    quint16 m_bridgePort = 0;
    QString m_bridgeToken;
    QString m_bridgeFilePath;
    /// Detecta un enlace nuevo en el portapapeles y lo encola.
    void handleClipboardText(const QString& text);
    /// Ejecuta la accion de fin de cola (apagar el equipo) si esta activada.
    void maybeRunFinishedAction();

    QString m_extensionId;
    QString m_nativeManifestPath;
    /// Reintenta el descubrimiento de la extension hasta encontrarla.
    QTimer* m_bridgeScanTimer = nullptr;
    /// Vigila el fin de la cola para la accion de apagado.
    QTimer* m_finishedTimer = nullptr;
    QNetworkAccessManager* m_updateCheck = nullptr;
    bool m_micaEnabled = false;
    QQuickWindow* m_window = nullptr;
};

} // namespace sdm
