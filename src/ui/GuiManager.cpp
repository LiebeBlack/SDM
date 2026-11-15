// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/ui/GuiManager.cpp
// ============================================================================
#include "ui/GuiManager.h"

#include "bridge/WebSocketServer.h"
#include "core/Log.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QJsonArray>
#include <QQmlContext>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <dwmapi.h>
#  include <windows.h>
#  include <psapi.h>
#else
#  include <unistd.h>
#endif

namespace sdm {

// ===========================================================================
// TaskListModel
// ===========================================================================

TaskListModel::TaskListModel(QObject* parent) : QAbstractListModel(parent) {
    DownloadEngine& engine = DownloadEngine::instance();
    connect(&engine, &DownloadEngine::tasksReloaded, this, &TaskListModel::reload);
    connect(&engine, &DownloadEngine::taskAdded, this, &TaskListModel::onTaskAdded);
    connect(&engine, &DownloadEngine::taskUpdated, this, &TaskListModel::onTaskUpdated);
    connect(&engine, &DownloadEngine::taskRemoved, this, &TaskListModel::onTaskRemoved);
    reload();
}

int TaskListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_visible.size());
}

int TaskListModel::totalCount() const {
    return static_cast<int>(m_cache.size());
}

int TaskListModel::visibleCount() const {
    return static_cast<int>(m_visible.size());
}

QVariant TaskListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) {
        return {};
    }
    const DownloadTask& task = m_cache.at(m_visible.at(index.row()));
    switch (role) {
        case IdRole:
            return QVariant::fromValue<qulonglong>(task.id);
        case NameRole:
            return task.fileName;
        case StateRole:
            return static_cast<int>(task.state);
        case StateTextRole:
            return stateText(task.state);
        case StateColorRole:
            return stateColorHex(task.state);
        case StateIconRole:
            return stateIcon(task.state);
        case CategoryRole:
            return static_cast<int>(task.category);
        case CategoryTextRole:
            return categoryText(task.category);
        case CategoryIconRole:
            return categoryIcon(task.category);
        case SizeTextRole:
            return humanSize(task.totalBytes);
        case ReceivedTextRole:
            return humanSize(task.receivedBytes);
        case ProgressRole:
            return task.progress();
        case ProgressTextRole:
            if (task.totalBytes <= 0 && task.state == DownloadState::Downloading) {
                return QStringLiteral("--");
            }
            if (task.state == DownloadState::Completed) {
                return QStringLiteral("100%");
            }
            return humanPercent(task.progress());
        case SpeedRole:
            return task.bytesPerSec;
        case SpeedTextRole:
            if (task.state == DownloadState::Completed) {
                return QStringLiteral("Completado");
            }
            if (task.state == DownloadState::Downloading) {
                return humanSpeed(task.bytesPerSec);
            }
            return QStringLiteral("--");
        case EtaTextRole: {
            if (task.state != DownloadState::Downloading || task.totalBytes <= 0 || task.bytesPerSec <= 1.0) {
                return QStringLiteral("--");
            }
            const qint64 remaining = task.totalBytes - task.receivedBytes;
            return humanEta(remaining / static_cast<qint64>(task.bytesPerSec));
        }
        case UrlRole:
            return task.url;
        case ThreadsRole:
            return task.threadCount;
        case SegmentCountRole:
            return task.segments.size();
        case CreatedTextRole:
            if (!task.createdAt.isValid()) {
                return QStringLiteral("--");
            }
            return QLocale::system().toString(task.createdAt, QLocale::ShortFormat);
        case ErrorRole:
            return task.lastError;
        case DirRole:
            return task.destDir;
        case SavePathRole:
            return task.savePath();
        case IsActiveRole:
            return task.isActive();
        case IsFinishedRole:
            return task.state == DownloadState::Completed;
        case SourceRole:
            return task.source;
        case StreamRole:
            return task.streamJob;
        default:
            return {};
    }
}

QHash<int, QByteArray> TaskListModel::roleNames() const {
    return {
        {IdRole, "taskId"},
        {NameRole, "name"},
        {StateRole, "state"},
        {StateTextRole, "stateText"},
        {StateColorRole, "stateColor"},
        {StateIconRole, "stateIcon"},
        {CategoryRole, "category"},
        {CategoryTextRole, "categoryText"},
        {CategoryIconRole, "categoryIcon"},
        {SizeTextRole, "sizeText"},
        {ReceivedTextRole, "receivedText"},
        {ProgressRole, "progress"},
        {ProgressTextRole, "progressText"},
        {SpeedRole, "speed"},
        {SpeedTextRole, "speedText"},
        {EtaTextRole, "etaText"},
        {UrlRole, "url"},
        {ThreadsRole, "threads"},
        {SegmentCountRole, "segmentCount"},
        {CreatedTextRole, "createdText"},
        {ErrorRole, "error"},
        {DirRole, "dirPath"},
        {SavePathRole, "savePath"},
        {IsActiveRole, "isActive"},
        {IsFinishedRole, "isFinished"},
        {SourceRole, "source"},
        {StreamRole, "isStream"},
    };
}

bool TaskListModel::matches(const DownloadTask& task) const {
    if (m_category >= 0 && static_cast<int>(task.category) != m_category) {
        return false;
    }
    if (m_state >= 0) {
        const int state = static_cast<int>(task.state);
        if (m_state == static_cast<int>(DownloadState::Downloading)) {
            // "En curso" incluye la fase de conexion.
            if (state != static_cast<int>(DownloadState::Downloading) &&
                state != static_cast<int>(DownloadState::Connecting)) {
                return false;
            }
        } else if (state != m_state) {
            return false;
        }
    }
    if (!m_search.isEmpty()) {
        const bool hit = task.fileName.contains(m_search, Qt::CaseInsensitive) ||
                         task.url.contains(m_search, Qt::CaseInsensitive);
        if (!hit) {
            return false;
        }
    }
    return true;
}

int TaskListModel::cacheIndexOf(quint64 id) const {
    const auto it = m_cacheIndex.constFind(id);
    return it == m_cacheIndex.constEnd() ? -1 : it.value();
}

void TaskListModel::refilter() {
    beginResetModel();
    m_visible.clear();
    m_visible.reserve(m_cache.size());
    for (int i = 0; i < m_cache.size(); ++i) {
        if (matches(m_cache.at(i))) {
            m_visible.append(i);
        }
    }
    endResetModel();
    emit countsChanged();
}

void TaskListModel::reload() {
    beginResetModel();
    m_cache = DownloadEngine::instance().allTasks();
    m_cacheIndex.clear();
    m_visible.clear();
    for (int i = 0; i < m_cache.size(); ++i) {
        m_cacheIndex.insert(m_cache.at(i).id, i);
        if (matches(m_cache.at(i))) {
            m_visible.append(i);
        }
    }
    endResetModel();
    emit countsChanged();
}

void TaskListModel::onTaskAdded(quint64 id) {
    const DownloadTask task = DownloadEngine::instance().task(id);
    if (task.id == 0 || m_cacheIndex.contains(id)) {
        reload();
        return;
    }
    m_cacheIndex.insert(id, m_cache.size());
    m_cache.append(task);
    if (matches(task)) {
        const int newRow = static_cast<int>(m_visible.size());
        beginInsertRows(QModelIndex(), newRow, newRow);
        m_visible.append(m_cache.size() - 1);
        endInsertRows();
    } else {
        refilter();
    }
    emit countsChanged();
}

void TaskListModel::onTaskUpdated(quint64 id) {
    const int cacheIndex = cacheIndexOf(id);
    if (cacheIndex < 0) {
        reload();
        return;
    }
    const DownloadTask updated = DownloadEngine::instance().task(id);
    if (updated.id == 0) {
        return;
    }
    const bool visibleBefore = m_visible.contains(cacheIndex);
    m_cache[cacheIndex] = updated;
    const bool visibleAfter = matches(updated);

    if (visibleBefore && visibleAfter) {
        const qsizetype row = m_visible.indexOf(cacheIndex);
        if (row >= 0) {
            const QModelIndex idx = index(static_cast<int>(row), 0);
            emit dataChanged(idx, idx);
        }
    } else {
        // Cambio de pertenencia (filtro de estado/categoria): reconstruccion.
        refilter();
    }
    emit countsChanged();
}

void TaskListModel::onTaskRemoved(quint64 id) {
    const int cacheIndex = cacheIndexOf(id);
    if (cacheIndex < 0) {
        return;
    }
    m_cache.removeAt(cacheIndex);
    m_cacheIndex.clear();
    for (int i = 0; i < m_cache.size(); ++i) {
        m_cacheIndex.insert(m_cache.at(i).id, i);
    }
    refilter();
    emit countsChanged();
}

void TaskListModel::setCategoryFilter(int category) {
    m_category = category;
    refilter();
}

void TaskListModel::setStateFilter(int state) {
    m_state = state;
    refilter();
}

void TaskListModel::setSearchText(const QString& text) {
    const QString normalized = text.trimmed();
    if (normalized == m_search) {
        return;
    }
    m_search = normalized;
    refilter();
}

void TaskListModel::clearFilters() {
    m_category = -1;
    m_state = -1;
    m_search.clear();
    refilter();
}

int TaskListModel::countForCategory(int category) const {
    int count = 0;
    for (const DownloadTask& task : m_cache) {
        if (category >= 0 && static_cast<int>(task.category) != category) {
            continue;
        }
        if (m_state >= 0) {
            const int state = static_cast<int>(task.state);
            const bool stateMatch = (m_state == static_cast<int>(DownloadState::Downloading))
                                        ? (state == static_cast<int>(DownloadState::Downloading) ||
                                           state == static_cast<int>(DownloadState::Connecting))
                                        : (state == m_state);
            if (!stateMatch) {
                continue;
            }
        }
        ++count;
    }
    return count;
}

int TaskListModel::countForState(int state) const {
    int count = 0;
    for (const DownloadTask& task : m_cache) {
        if (m_category >= 0 && static_cast<int>(task.category) != m_category) {
            continue;
        }
        const int taskState = static_cast<int>(task.state);
        bool match;
        if (state == static_cast<int>(DownloadState::Downloading)) {
            match = taskState == static_cast<int>(DownloadState::Downloading) ||
                    taskState == static_cast<int>(DownloadState::Connecting);
        } else if (state < 0) {
            match = true;
        } else {
            match = taskState == state;
        }
        if (match) {
            ++count;
        }
    }
    return count;
}

QVariantList TaskListModel::counts() const {
    // Indice 0..6: categorias (FileCategory).  7..13: estados (DownloadState).
    QVariantList out;
    out.reserve(14);
    int categoryCounts[7] = {0, 0, 0, 0, 0, 0, 0};
    int stateCounts[7] = {0, 0, 0, 0, 0, 0, 0};
    for (const DownloadTask& task : m_cache) {
        const int category = static_cast<int>(task.category);
        if (category >= 0 && category < 7) {
            ++categoryCounts[category];
        }
        int state = static_cast<int>(task.state);
        if (state >= 0 && state < 7) {
            ++stateCounts[state];
        }
        if (state == static_cast<int>(DownloadState::Connecting)) {
            ++stateCounts[static_cast<int>(DownloadState::Downloading)]; // "en curso"
        }
    }
    for (int i = 0; i < 7; ++i) {
        out.append(categoryCounts[i]);
    }
    for (int i = 0; i < 7; ++i) {
        out.append(stateCounts[i]);
    }
    return out;
}

quint64 TaskListModel::idAt(int row) const {
    if (row < 0 || row >= m_visible.size()) {
        return 0;
    }
    return static_cast<quint64>(m_cache.at(m_visible.at(row)).id);
}

int TaskListModel::rowForTask(quint64 id) const {
    const int cacheIndex = cacheIndexOf(id);
    if (cacheIndex < 0) {
        return -1;
    }
    return m_visible.indexOf(cacheIndex);
}

void TaskListModel::pauseAt(int row) {
    const quint64 id = idAt(row);
    if (id != 0) {
        DownloadEngine::instance().pauseTask(id);
    }
}

void TaskListModel::resumeAt(int row) {
    const quint64 id = idAt(row);
    if (id != 0) {
        DownloadEngine::instance().startTask(id);
    }
}

void TaskListModel::removeAt(int row, bool deleteFile) {
    const quint64 id = idAt(row);
    if (id != 0) {
        DownloadEngine::instance().removeTask(id, deleteFile);
    }
}

void TaskListModel::retryAt(int row) {
    const quint64 id = idAt(row);
    if (id != 0) {
        DownloadEngine::instance().retryTask(id);
    }
}

// ===========================================================================
// SegmentModel
// ===========================================================================

SegmentModel::SegmentModel(QObject* parent) : QAbstractListModel(parent) {
    connect(&DownloadEngine::instance(), &DownloadEngine::taskUpdated, this, &SegmentModel::refresh);
}

int SegmentModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_segments.size());
}

QVariant SegmentModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_segments.size()) {
        return {};
    }
    const SegmentInfo& segment = m_segments.at(index.row());
    switch (role) {
        case IndexRole:
            return segment.index;
        case ProgressRole:
            return segment.progress();
        case StateRole:
            return static_cast<int>(segment.state);
        case StateTextRole:
            return segmentStateText(segment.state);
        case ColorRole:
            return segmentStateHex(segment.state);
        case RangeTextRole:
            if (segment.endByte < 0) {
                return QStringLiteral("%1 → …").arg(humanSize(segment.startByte));
            }
            return QStringLiteral("%1 – %2").arg(humanSize(segment.startByte), humanSize(segment.endByte + 1));
        default:
            return {};
    }
}

QHash<int, QByteArray> SegmentModel::roleNames() const {
    return {
        {IndexRole, "segmentIndex"},
        {ProgressRole, "progress"},
        {StateRole, "state"},
        {StateTextRole, "stateText"},
        {ColorRole, "color"},
        {RangeTextRole, "rangeText"},
    };
}

void SegmentModel::setTask(quint64 id) {
    if (m_taskId == id) {
        refresh(id);
        return;
    }
    beginResetModel();
    m_taskId = id;
    m_segments = DownloadEngine::instance().segments(id);
    endResetModel();
}

void SegmentModel::clear() {
    beginResetModel();
    m_taskId = 0;
    m_segments.clear();
    endResetModel();
}

void SegmentModel::refresh(quint64 id) {
    if (id == 0 || id != m_taskId) {
        return;
    }
    const QList<SegmentInfo> updated = DownloadEngine::instance().segments(id);
    if (updated.size() != m_segments.size()) {
        beginResetModel();
        m_segments = updated;
        endResetModel();
        return;
    }
    m_segments = updated;
    if (!m_segments.isEmpty()) {
        const QModelIndex first = index(0, 0);
        const QModelIndex last = index(static_cast<int>(m_segments.size()) - 1, 0);
        emit dataChanged(first, last);
    }
}

// ===========================================================================
// GuiManager
// ===========================================================================

GuiManager::GuiManager(QQmlApplicationEngine& engine, QObject* parent)
    : QObject(parent), m_engine(engine) {
    loadSettings();
    m_tasksModel = new TaskListModel(this);
    m_segmentsModel = new SegmentModel(this);

    DownloadEngine& downloadEngine = DownloadEngine::instance();
    connect(&downloadEngine, &DownloadEngine::toast, this, &GuiManager::toast);
    connect(&downloadEngine, &DownloadEngine::streamVariantsReady, this, &GuiManager::streamVariantsReady);
    connect(&downloadEngine, &DownloadEngine::statusChanged, this, &GuiManager::statsChanged);
}

void GuiManager::registerContext() {
    QQmlContext* context = m_engine.rootContext();
    context->setContextProperty(QStringLiteral("bridge"), this);
    context->setContextProperty(QStringLiteral("tasksModel"), m_tasksModel);
    context->setContextProperty(QStringLiteral("segmentsModel"), m_segmentsModel);
    context->setContextProperty(QStringLiteral("appVersion"), QStringLiteral("1.0.0"));
}

void GuiManager::initializeEngine(DatabaseManager* db) {
    DownloadEngine::instance().initialize(db, buildEngineSettings());
}

void GuiManager::applyBridgeInfo(quint16 port, const QString& token, const QString& bridgeFilePath,
                                 WebSocketServer* server) {
    m_bridgePort = port;
    m_bridgeToken = token;
    m_bridgeFilePath = bridgeFilePath;
    m_webSocket = server;
    if (server != nullptr) {
        connect(server, &WebSocketServer::clientConnected, this, &GuiManager::bridgeChanged);
        connect(server, &WebSocketServer::clientDisconnected, this, &GuiManager::bridgeChanged);
    }
    emit bridgeChanged();
}

void GuiManager::applyWindowChrome(QQuickWindow* window) {
    m_window = window;
    if (window == nullptr) {
        return;
    }
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd != nullptr) {
        // Barra de titulo oscura (Windows 10 20H1+ / Windows 11).
        const BOOL useDark = m_darkMode ? TRUE : FALSE;
        if (FAILED(DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &useDark,
                                         sizeof(useDark)))) {
            DwmSetWindowAttribute(hwnd, 19 /*legacy*/, &useDark, sizeof(useDark));
        }
        // Mica (Windows 11): solo se activa si el sistema lo confirma.
        const int backdrop = 2; // DWMSBT_MAINWINDOW
        m_micaEnabled =
            SUCCEEDED(DwmSetWindowAttribute(hwnd, 38 /*DWMWA_SYSTEMBACKDROP_TYPE*/, &backdrop, sizeof(backdrop)));
        if (m_micaEnabled) {
            window->setColor(Qt::transparent);
        }
    }
#endif
    emit chromeChanged();
}

// ---------------------------------------------------------------------------
// Ajustes
// ---------------------------------------------------------------------------

void GuiManager::loadSettings() {
    QSettings settings(QStringLiteral("SDM"), QStringLiteral("S-Download Manager"));
    QString fallbackDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (fallbackDir.isEmpty()) {
        fallbackDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    m_downloadDir = settings.value(QStringLiteral("general/downloadDir"), fallbackDir).toString();
    m_defaultThreads =
        DownloadEngine::normalizeThreadCount(settings.value(QStringLiteral("general/threads"), kDefaultThreads).toInt());
    m_maxConcurrent = settings.value(QStringLiteral("general/maxConcurrent"), kDefaultMaxConcurrent).toInt();
    m_autoStart = settings.value(QStringLiteral("general/autoStart"), true).toBool();
    m_rotateUserAgent = settings.value(QStringLiteral("network/rotateUa"), false).toBool();
    m_preallocate = settings.value(QStringLiteral("general/preallocate"), true).toBool();
    m_useYtDlp = settings.value(QStringLiteral("media/useYtDlp"), false).toBool();
    m_proxy = settings.value(QStringLiteral("network/proxy")).toString();
    m_ffmpegPath = settings.value(QStringLiteral("media/ffmpegPath"), QStringLiteral("ffmpeg")).toString();
    m_ytDlpPath = settings.value(QStringLiteral("media/ytDlpPath"), QStringLiteral("yt-dlp")).toString();
    m_darkMode = settings.value(QStringLiteral("ui/darkMode"), true).toBool();
    m_extensionId = settings.value(QStringLiteral("bridge/extensionId")).toString();
    m_nativeManifestPath =
        settings.value(QStringLiteral("bridge/nativeManifestPath"),
                       QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                           QStringLiteral("/com.sdm.nativehost.json"))
            .toString();
    if (!QFileInfo::exists(m_downloadDir)) {
        QDir().mkpath(m_downloadDir);
    }
}

void GuiManager::persistSettings() {
    QSettings settings(QStringLiteral("SDM"), QStringLiteral("S-Download Manager"));
    settings.setValue(QStringLiteral("general/downloadDir"), m_downloadDir);
    settings.setValue(QStringLiteral("general/threads"), m_defaultThreads);
    settings.setValue(QStringLiteral("general/maxConcurrent"), m_maxConcurrent);
    settings.setValue(QStringLiteral("general/autoStart"), m_autoStart);
    settings.setValue(QStringLiteral("network/rotateUa"), m_rotateUserAgent);
    settings.setValue(QStringLiteral("general/preallocate"), m_preallocate);
    settings.setValue(QStringLiteral("media/useYtDlp"), m_useYtDlp);
    settings.setValue(QStringLiteral("network/proxy"), m_proxy);
    settings.setValue(QStringLiteral("media/ffmpegPath"), m_ffmpegPath);
    settings.setValue(QStringLiteral("media/ytDlpPath"), m_ytDlpPath);
    settings.setValue(QStringLiteral("ui/darkMode"), m_darkMode);
    settings.setValue(QStringLiteral("bridge/extensionId"), m_extensionId);
    settings.setValue(QStringLiteral("bridge/nativeManifestPath"), m_nativeManifestPath);
}

EngineSettings GuiManager::buildEngineSettings() const {
    EngineSettings engineSettings;
    engineSettings.downloadDir = m_downloadDir;
    engineSettings.defaultThreads = m_defaultThreads;
    engineSettings.maxConcurrentTasks = qBound(1, m_maxConcurrent, 20);
    engineSettings.autoStart = m_autoStart;
    engineSettings.rotateUserAgent = m_rotateUserAgent;
    engineSettings.preallocateFiles = m_preallocate;
    engineSettings.useYtDlp = m_useYtDlp;
    engineSettings.proxy = m_proxy;
    engineSettings.ffmpegPath = m_ffmpegPath;
    engineSettings.ytDlpPath = m_ytDlpPath;
    return engineSettings;
}

void GuiManager::pushEngineSettings() {
    DownloadEngine::instance().updateSettings(buildEngineSettings());
}

QString GuiManager::downloadDir() const { return m_downloadDir; }
void GuiManager::setDownloadDir(const QString& dir) {
    if (dir.isEmpty() || dir == m_downloadDir) {
        return;
    }
    QDir().mkpath(dir);
    m_downloadDir = dir;
    persistSettings();
    pushEngineSettings();
    emit settingsChanged();
}

int GuiManager::defaultThreads() const { return m_defaultThreads; }
void GuiManager::setDefaultThreads(int threads) {
    m_defaultThreads = DownloadEngine::normalizeThreadCount(threads);
    persistSettings();
    pushEngineSettings();
    emit settingsChanged();
}

int GuiManager::maxConcurrent() const { return m_maxConcurrent; }
void GuiManager::setMaxConcurrent(int value) {
    m_maxConcurrent = qBound(1, value, 20);
    persistSettings();
    pushEngineSettings();
    emit settingsChanged();
}

bool GuiManager::autoStart() const { return m_autoStart; }
void GuiManager::setAutoStart(bool value) {
    m_autoStart = value;
    persistSettings();
    pushEngineSettings();
    emit settingsChanged();
}

bool GuiManager::rotateUserAgent() const { return m_rotateUserAgent; }
void GuiManager::setRotateUserAgent(bool value) {
    m_rotateUserAgent = value;
    persistSettings();
    pushEngineSettings();
    emit settingsChanged();
}

bool GuiManager::preallocate() const { return m_preallocate; }
void GuiManager::setPreallocate(bool value) {
    m_preallocate = value;
    persistSettings();
    pushEngineSettings();
    emit settingsChanged();
}

bool GuiManager::useYtDlp() const { return m_useYtDlp; }
void GuiManager::setUseYtDlp(bool value) {
    m_useYtDlp = value;
    persistSettings();
    pushEngineSettings();
    emit settingsChanged();
}

QString GuiManager::proxy() const { return m_proxy; }
void GuiManager::setProxy(const QString& value) {
    m_proxy = value.trimmed();
    persistSettings();
    pushEngineSettings();
    emit settingsChanged();
}

QString GuiManager::ffmpegPath() const { return m_ffmpegPath; }
void GuiManager::setFfmpegPath(const QString& value) {
    m_ffmpegPath = value.trimmed().isEmpty() ? QStringLiteral("ffmpeg") : value.trimmed();
    persistSettings();
    pushEngineSettings();
    emit settingsChanged();
}

QString GuiManager::ytDlpPath() const { return m_ytDlpPath; }
void GuiManager::setYtDlpPath(const QString& value) {
    m_ytDlpPath = value.trimmed().isEmpty() ? QStringLiteral("yt-dlp") : value.trimmed();
    persistSettings();
    pushEngineSettings();
    emit settingsChanged();
}

// ---------------------------------------------------------------------------
// Puente
// ---------------------------------------------------------------------------

QString GuiManager::connectionKey() const {
    if (m_bridgeToken.isEmpty()) {
        return {};
    }
    BridgeInfo info;
    info.port = m_bridgePort;
    info.token = m_bridgeToken;
    return info.connectionKey();
}

int GuiManager::bridgePort() const { return static_cast<int>(m_bridgePort); }

int GuiManager::connectedClients() const {
    return m_webSocket != nullptr ? m_webSocket->clientCount() : 0;
}

QString GuiManager::extensionId() const { return m_extensionId; }

void GuiManager::setExtensionId(const QString& id) {
    m_extensionId = id.trimmed();
    persistSettings();
    writeNativeHostManifest();
    emit bridgeChanged();
}

QString GuiManager::nativeManifestPath() const { return m_nativeManifestPath; }

void GuiManager::writeNativeHostManifest() {
#ifdef Q_OS_WIN
    const QString hostPath = QCoreApplication::applicationDirPath() + QStringLiteral("/SDMNativeHost.exe");
#else
    const QString hostPath = QCoreApplication::applicationDirPath() + QStringLiteral("/SDMNativeHost");
#endif

    QJsonObject root;
    root.insert(QStringLiteral("name"), QStringLiteral("com.sdm.nativehost"));
    root.insert(QStringLiteral("description"), QStringLiteral("S-Download Manager native bridge"));
    root.insert(QStringLiteral("path"), hostPath);
    root.insert(QStringLiteral("type"), QStringLiteral("stdio"));

    QJsonArray origins;
    const QString originId = m_extensionId.isEmpty() ? QStringLiteral("PEGA_EL_ID_DE_LA_EXTENSION")
                                                     : m_extensionId;
    origins.append(QStringLiteral("chrome-extension://%1/").arg(originId));
    root.insert(QStringLiteral("allowed_origins"), origins);

    QDir().mkpath(QFileInfo(m_nativeManifestPath).absolutePath());
    QFile file(m_nativeManifestPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        log::info("Manifiesto nativo escrito en {}", log::utf8(m_nativeManifestPath));
    } else {
        log::error("No se pudo escribir {}", log::utf8(m_nativeManifestPath));
        emit toast(QStringLiteral("No se pudo escribir el manifiesto nativo"), 2);
    }
}

// ---------------------------------------------------------------------------
// Apariencia
// ---------------------------------------------------------------------------

bool GuiManager::darkMode() const { return m_darkMode; }

void GuiManager::setDarkMode(bool value) {
    if (m_darkMode == value) {
        return;
    }
    m_darkMode = value;
    persistSettings();
    if (m_window != nullptr) {
        applyWindowChrome(m_window);
    }
    emit chromeChanged();
}

bool GuiManager::micaEnabled() const { return m_micaEnabled; }

// ---------------------------------------------------------------------------
// Estadisticas
// ---------------------------------------------------------------------------

double GuiManager::globalSpeed() const {
    return DownloadEngine::instance().globalSpeed();
}

QString GuiManager::globalSpeedText() const {
    return humanSpeed(DownloadEngine::instance().globalSpeed());
}

int GuiManager::activeCount() const {
    return DownloadEngine::instance().activeTaskCount();
}

int GuiManager::queuedCount() const {
    return DownloadEngine::instance().queuedTaskCount();
}

int GuiManager::totalCount() const {
    return DownloadEngine::instance().taskCount();
}

double GuiManager::memoryMb() const {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return static_cast<double>(counters.WorkingSetSize) / 1048576.0;
    }
    return 0.0;
#else
    QFile file(QStringLiteral("/proc/self/statm"));
    if (file.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> parts = file.readAll().simplified().split(' ');
        if (parts.size() >= 2) {
            const qint64 pages = parts.at(1).toLongLong();
            const long pageSize = sysconf(_SC_PAGESIZE);
            if (pageSize > 0) {
                return (static_cast<double>(pages) * static_cast<double>(pageSize)) / 1048576.0;
            }
        }
    }
    return 0.0;
#endif
}

void GuiManager::refreshStats() {
    emit statsChanged();
}

// ---------------------------------------------------------------------------
// Acciones
// ---------------------------------------------------------------------------

quint64 GuiManager::addTask(const QVariantMap& options) {
    DownloadEngine& engine = DownloadEngine::instance();
    const QString url = options.value(QStringLiteral("url")).toString().trimmed();

    NewTaskRequest request;
    request.url = url;
    request.destDir = options.value(QStringLiteral("dir")).toString();
    request.referer = options.value(QStringLiteral("referer")).toString();
    request.cookies = options.value(QStringLiteral("cookies")).toString();
    request.userAgent = options.value(QStringLiteral("userAgent")).toString();
    request.fileName = options.value(QStringLiteral("fileName")).toString();
    request.httpUser = options.value(QStringLiteral("user")).toString();
    request.httpPass = options.value(QStringLiteral("pass")).toString();
    request.originUrl = options.value(QStringLiteral("pageUrl")).toString();
    const int threads = options.value(QStringLiteral("threads")).toInt();
    request.threadCount = threads > 0 ? threads : m_defaultThreads;
    request.autoStart = options.value(QStringLiteral("autoStart"), true).toBool();
    request.source = options.value(QStringLiteral("source"), QStringLiteral("manual")).toString();

    // Deteccion automatica de manifiestos multimedia (spec cap. 9).
    const QString lower = url.toLower();
    if (lower.contains(QLatin1String(".m3u8")) || lower.contains(QLatin1String(".mpd")) ||
        lower.contains(QLatin1String("format=m3u8"))) {
        return engine.addStreamTask(url, request.destDir, request.referer, request.cookies, request.fileName);
    }
    return engine.addTask(request);
}

void GuiManager::pauseTask(quint64 id) { DownloadEngine::instance().pauseTask(id); }
void GuiManager::resumeTask(quint64 id) { DownloadEngine::instance().startTask(id); }
void GuiManager::stopTask(quint64 id) { DownloadEngine::instance().pauseTask(id); }
void GuiManager::removeTask(quint64 id, bool deleteFile) { DownloadEngine::instance().removeTask(id, deleteFile); }
void GuiManager::retryTask(quint64 id) { DownloadEngine::instance().retryTask(id); }
void GuiManager::setThreads(quint64 id, int threads) { DownloadEngine::instance().setThreadCount(id, threads); }
void GuiManager::pauseAll() { DownloadEngine::instance().pauseAll(); }
void GuiManager::resumeAll() { DownloadEngine::instance().startAll(); }
void GuiManager::clearCompleted() { DownloadEngine::instance().clearCompleted(); }

void GuiManager::selectTask(quint64 id) {
    m_segmentsModel->setTask(id);
    emit taskSelected(id);
}

void GuiManager::selectStreamVariant(quint64 id, int variantIndex) {
    DownloadEngine::instance().selectStreamVariant(id, variantIndex);
}

void GuiManager::cancelStreamTask(quint64 id) {
    DownloadEngine::instance().removeTask(id, true);
}

void GuiManager::openFolderFor(quint64 id) {
    const DownloadTask task = DownloadEngine::instance().task(id);
    const QString dir = task.destDir.isEmpty() ? m_downloadDir : task.destDir;
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void GuiManager::openFileFor(quint64 id) {
    const DownloadTask task = DownloadEngine::instance().task(id);
    if (task.state == DownloadState::Completed && QFileInfo::exists(task.savePath())) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(task.savePath()));
        return;
    }
    const QString dir = task.destDir.isEmpty() ? m_downloadDir : task.destDir;
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void GuiManager::copyText(const QString& text) {
    QGuiApplication::clipboard()->setText(text);
    emit toast(QStringLiteral("Copiado al portapapeles"), 0);
}

QString GuiManager::clipboardText() const {
    return QGuiApplication::clipboard()->text();
}

QString GuiManager::suggestedFolder() const {
    return m_downloadDir;
}

QVariantList GuiManager::segmentsOf(quint64 id) const {
    QVariantList out;
    const QList<SegmentInfo> segments = DownloadEngine::instance().segments(id);
    for (const SegmentInfo& segment : segments) {
        QVariantMap map;
        map.insert(QStringLiteral("index"), segment.index);
        map.insert(QStringLiteral("progress"), segment.progress());
        map.insert(QStringLiteral("state"), static_cast<int>(segment.state));
        map.insert(QStringLiteral("stateText"), segmentStateText(segment.state));
        map.insert(QStringLiteral("color"), segmentStateHex(segment.state));
        out.append(map);
    }
    return out;
}

QVariantMap GuiManager::taskInfo(quint64 id) const {
    const DownloadTask task = DownloadEngine::instance().task(id);
    if (task.id == 0) {
        return {};
    }
    return DownloadEngine::snapshot(task);
}

} // namespace sdm
