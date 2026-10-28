// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/DownloadEngine.cpp
// ============================================================================
#include "core/DownloadEngine.h"

#include "core/BrowserEmulator.h"
#include "core/FileAllocator.h"
#include "core/Log.h"
#include "core/SegmentWorker.h"
#include "core/StreamDownloader.h"
#include "database/DatabaseManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QRunnable>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>

#include <curl/curl.h>

#include <algorithm>
#include <cstdlib>

namespace sdm {

namespace {

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------

qint64 nowMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

SegmentInfo* findSegment(DownloadTask& task, int index) {
    for (SegmentInfo& segment : task.segments) {
        if (segment.index == index) {
            return &segment;
        }
    }
    return nullptr;
}

const SegmentInfo* findSegment(const DownloadTask& task, int index) {
    for (const SegmentInfo& segment : task.segments) {
        if (segment.index == index) {
            return &segment;
        }
    }
    return nullptr;
}

/// Los segmentos cubren exactamente [0, total-1] y conservan sus bytes.
bool segmentsAreValid(const DownloadTask& task) {
    if (task.segments.isEmpty() || task.totalBytes <= 0) {
        return false;
    }
    qint64 expectedStart = 0;
    for (const SegmentInfo& segment : task.segments) {
        if (segment.startByte != expectedStart) {
            return false;
        }
        if (segment.endByte < segment.startByte) {
            return false;
        }
        if (segment.receivedBytes < 0 || segment.receivedBytes > segment.totalBytes()) {
            return false;
        }
        expectedStart = segment.endByte + 1;
    }
    return expectedStart == task.totalBytes;
}

bool allSegmentsComplete(const DownloadTask& task) {
    if (task.segments.isEmpty()) {
        return task.totalBytes > 0 && task.receivedBytes >= task.totalBytes;
    }
    for (const SegmentInfo& segment : task.segments) {
        if (segment.endByte < 0) {
            return false; // rango abierto: solo el resultado del worker lo confirma
        }
        if (segment.receivedBytes < segment.totalBytes()) {
            return false;
        }
    }
    return true;
}

/// Ruta destino unica (evita pisar archivos existentes con "name (1).ext").
QString uniqueTargetPath(const QString& target) {
    if (!QFileInfo::exists(target)) {
        return target;
    }
    const QFileInfo info(target);
    const QString dir = info.absolutePath();
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();
    for (int i = 1; i < 1000; ++i) {
        QString candidate = dir + QLatin1Char('/') + base + QStringLiteral(" (") + QString::number(i) +
                            QLatin1Char(')');
        if (!suffix.isEmpty()) {
            candidate += QLatin1Char('.') + suffix;
        }
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return target + QLatin1Char('.') + QString::number(nowMs());
}

// ---------------------------------------------------------------------------
// Sonda de capacidades del servidor (spec 5.2.1)
// ---------------------------------------------------------------------------

struct ProbeSink {
    ServerCapabilities caps;
    int status = 0;
    bool acceptRanges = false;
    bool is206 = false;
    qint64 contentLength = -1;
    qint64 rangeTotal = -1;
};

QString parseDispositionFilename(const QString& raw) {
    const int star = raw.indexOf(QLatin1String("filename*="), 0, Qt::CaseInsensitive);
    if (star >= 0) {
        QString value = raw.mid(star + 10).trimmed();
        if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')) && value.size() > 1) {
            value = value.mid(1, value.size() - 2);
        }
        const int sep = value.indexOf(QLatin1String("''"));
        if (sep >= 0) {
            value = value.mid(sep + 2);
        }
        const QString decoded = QUrl::fromPercentEncoding(value.toUtf8());
        if (!decoded.isEmpty()) {
            return decoded;
        }
    }
    const int plain = raw.indexOf(QLatin1String("filename="), 0, Qt::CaseInsensitive);
    if (plain >= 0) {
        QString value = raw.mid(plain + 9).trimmed();
        if (value.startsWith(QLatin1Char('"'))) {
            const int end = value.indexOf(QLatin1Char('"'), 1);
            value = end > 0 ? value.mid(1, end - 1) : value.mid(1);
        } else {
            const int semi = value.indexOf(QLatin1Char(';'));
            if (semi >= 0) {
                value = value.left(semi);
            }
            value = value.trimmed();
        }
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

size_t probeHeaderThunk(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* sink = static_cast<ProbeSink*>(userdata);
    const size_t length = size * nitems;
    if (sink == nullptr || buffer == nullptr) {
        return length;
    }
    const QString line = QString::fromUtf8(buffer, static_cast<qsizetype>(length)).trimmed();

    if (line.startsWith(QLatin1String("HTTP/"))) {
        const int space = line.indexOf(QLatin1Char(' '));
        if (space > 0 && line.size() > space + 3) {
            bool okFlag = false;
            const int code = line.mid(space + 1, 3).toInt(&okFlag);
            if (okFlag) {
                sink->status = code;
            }
        }
        // Cabeceras del ultimo salto de redireccion.
        return length;
    }

    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon <= 0) {
        return length;
    }
    const QString key = line.left(colon).trimmed().toLower();
    const QString value = line.mid(colon + 1).trimmed();

    if (key == QLatin1String("content-length")) {
        bool okLen = false;
        const qint64 len = value.toLongLong(&okLen);
        if (okLen) {
            sink->contentLength = len;
        }
    } else if (key == QLatin1String("accept-ranges")) {
        sink->acceptRanges = value.contains(QLatin1String("bytes"), Qt::CaseInsensitive);
    } else if (key == QLatin1String("etag")) {
        sink->caps.etag = value;
    } else if (key == QLatin1String("last-modified")) {
        sink->caps.lastModified = value;
    } else if (key == QLatin1String("content-type")) {
        QString mime = value;
        const int semi = mime.indexOf(QLatin1Char(';'));
        if (semi >= 0) {
            mime = mime.left(semi);
        }
        sink->caps.mimeType = mime.trimmed();
    } else if (key == QLatin1String("content-disposition")) {
        sink->caps.contentDisposition = value;
        const QString name = parseDispositionFilename(value);
        if (!name.isEmpty()) {
            sink->caps.fileName = name;
        }
    } else if (key == QLatin1String("content-range")) {
        // "bytes 0-0/123456"
        const int slash = value.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0 && slash + 1 < value.size()) {
            const QString totalPart = value.mid(slash + 1).trimmed();
            bool okTotal = false;
            const qint64 total = totalPart.toLongLong(&okTotal);
            if (okTotal) {
                sink->rangeTotal = total;
                sink->is206 = true;
            }
        }
    }
    return length;
}

size_t probeDiscardThunk(char* /*ptr*/, size_t size, size_t nmemb, void* /*userdata*/) {
    return size * nmemb; // acepta (y descarta) el cuerpo: solo queremos cabeceras
}

/// Una peticion de sondeo (HEAD o GET con Range: bytes=0-0).
bool probeOnce(const DownloadTask& task, const QString& url, bool head, bool rangeZero, const QString& proxy,
               ServerCapabilities* caps, int* statusOut, QString* errOut) {
    *caps = ServerCapabilities{};
    *statusOut = 0;
    if (errOut != nullptr) {
        errOut->clear();
    }

    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme().isEmpty()) {
        if (errOut != nullptr) {
            *errOut = QStringLiteral("URL invalida: %1").arg(url);
        }
        return false;
    }
    const QByteArray urlBytes = parsed.toEncoded(QUrl::FullyEncoded | QUrl::RemoveFragment);

    CURL* easy = curl_easy_init();
    if (easy == nullptr) {
        if (errOut != nullptr) {
            *errOut = QStringLiteral("curl_easy_init falló");
        }
        return false;
    }

    ProbeSink sink;
    QList<QPair<QString, QString>> headers = BrowserEmulator::buildHeaders(task, QStringLiteral("document"));
    if (rangeZero) {
        headers.append({QStringLiteral("Range"), QStringLiteral("bytes=0-0")});
    }
    curl_slist* slist = BrowserEmulator::buildSlist(headers);

    BrowserEmulator::applyCommonOptions(easy, task, BrowserEmulator::currentProfile(), proxy);
    curl_easy_setopt(easy, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, slist);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &probeHeaderThunk);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, &sink);
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
    if (!head) {
        curl_easy_setopt(easy, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(4096));
    }

    if (head) {
        curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
    } else {
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &probeDiscardThunk);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, nullptr);
        curl_easy_setopt(easy, CURLOPT_RANGE, "0-0");
    }

    const CURLcode rc = curl_easy_perform(easy);

    long code = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
    if (code > 0) {
        sink.status = static_cast<int>(code);
    }
    char* effective = nullptr;
    if (curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK && effective != nullptr) {
        sink.caps.finalUrl = QString::fromUtf8(effective);
    }
    if (sink.caps.finalUrl.isEmpty()) {
        sink.caps.finalUrl = url;
    }

    // Construccion del resultado.
    sink.caps.httpStatus = sink.status;
    sink.caps.supportsRange = sink.acceptRanges || sink.is206;
    if (sink.is206 && sink.rangeTotal > 0) {
        sink.caps.totalSize = sink.rangeTotal;
    } else if (sink.contentLength >= 0) {
        sink.caps.totalSize = sink.contentLength;
    } else {
        sink.caps.totalSize = -1;
    }

    bool ok = (sink.status >= 200 && sink.status < 300);
    if (rc == CURLE_OK || rc == CURLE_FILESIZE_EXCEEDED) {
        ok = (sink.status >= 200 && sink.status < 300);
    } else {
        ok = false;
    }
    sink.caps.valid = ok;
    *caps = sink.caps;
    *statusOut = sink.status;

    if (!ok && errOut != nullptr) {
        if (rc == CURLE_HTTP_RETURNED_ERROR || (sink.status >= 400 && sink.status < 600)) {
            *errOut = QStringLiteral("HTTP %1").arg(sink.status);
        } else {
            *errOut = QString::fromUtf8(curl_easy_strerror(rc));
        }
    }

    curl_slist_free_all(slist);
    curl_easy_cleanup(easy);
    return ok;
}

/// Runnable de sonda ejecutado en QThreadPool (nunca bloquea la GUI).
class ProbeRunnable : public QRunnable {
public:
    ProbeRunnable(DownloadEngine* engine, quint64 taskId, DownloadTask snapshot, QString proxy)
        : m_engine(engine), m_taskId(taskId), m_task(std::move(snapshot)), m_proxy(std::move(proxy)) {}

    void run() override {
        ProbeResult result = DownloadEngine::runProbe(m_task, m_proxy);
        m_engine->storeProbeResult(m_taskId, result);
        QMetaObject::invokeMethod(m_engine, "onProbeArrived", Qt::QueuedConnection, Q_ARG(quint64, m_taskId));
    }

private:
    DownloadEngine* m_engine;
    quint64 m_taskId;
    DownloadTask m_task;
    QString m_proxy;
};

} // namespace

// ---------------------------------------------------------------------------
// Ciclo de vida
// ---------------------------------------------------------------------------

DownloadEngine::DownloadEngine(QObject* parent) : QObject(parent) {
}

DownloadEngine::~DownloadEngine() {
    shutdown();
}

DownloadEngine& DownloadEngine::instance() {
    static DownloadEngine engine;
    return engine;
}

void DownloadEngine::initialize(DatabaseManager* db, const EngineSettings& settings) {
    if (m_initialized) {
        updateSettings(settings);
        return;
    }
    m_db = db;
    m_settings = settings;
    if (m_settings.downloadDir.isEmpty()) {
        m_settings.downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (m_settings.downloadDir.isEmpty()) {
            m_settings.downloadDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        }
    }
    QDir().mkpath(m_settings.downloadDir);

    m_stream = new StreamDownloader(this);
    connect(m_stream, &StreamDownloader::probeFinished, this, &DownloadEngine::onStreamProbeFinished);
    connect(m_stream, &StreamDownloader::progress, this, &DownloadEngine::onStreamProgress);
    connect(m_stream, &StreamDownloader::downloadFinished, this, &DownloadEngine::onStreamFinished);

    connect(&m_tickTimer, &QTimer::timeout, this, &DownloadEngine::tick);
    m_tickTimer.setInterval(250);

    m_initialized = true;
    restoreFromDatabase();
    m_tickTimer.start();
    log::info("DownloadEngine inicializado (dir={}, hilos={}, maxTareas={})", log::utf8(m_settings.downloadDir),
              m_settings.defaultThreads, m_settings.maxConcurrentTasks);
}

void DownloadEngine::shutdown() {
    if (!m_initialized) {
        return;
    }
    m_shuttingDown = true;
    m_tickTimer.stop();

    // Detiene todos los workers y espera a sus hilos.
    for (auto it = m_runtimes.begin(); it != m_runtimes.end(); ++it) {
        Runtime* runtime = it.value().get();
        runtime->stopRequested = true;
        runtime->refreshStopping = false;
        runtime->removePending = false;
        for (SegmentWorker* worker : runtime->workers) {
            if (worker != nullptr) {
                worker->requestAbort();
            }
        }
    }
    for (auto it = m_runtimes.begin(); it != m_runtimes.end(); ++it) {
        Runtime* runtime = it.value().get();
        for (QThread* thread : runtime->threads) {
            if (thread != nullptr) {
                thread->quit();
                thread->wait(4000);
            }
        }
        // Persiste el estado final.
        DownloadTask* task = mutableTask(it.key());
        if (task != nullptr && runtime->io != nullptr) {
            qint64 total = 0;
            for (const SegmentInfo& segment : task->segments) {
                total += segment.receivedBytes;
            }
            task->receivedBytes = total;
            if (task->state == DownloadState::Downloading || task->state == DownloadState::Connecting) {
                task->state = DownloadState::Paused;
            }
            if (m_db != nullptr && m_db->isOpen()) {
                m_db->updateProgress(task->id, task->state, task->receivedBytes, task->lastError, nullptr);
                m_db->saveSegments(task->id, task->segments, nullptr);
            }
        }
    }
    m_runtimes.clear(); // destructores cierran los FileAllocator

    if (m_stream != nullptr) {
        m_stream->abort();
    }
    m_initialized = false;
    log::info("DownloadEngine detenido correctamente");
}

void DownloadEngine::updateSettings(const EngineSettings& settings) {
    const bool concurrentChanged = settings.maxConcurrentTasks != m_settings.maxConcurrentTasks;
    m_settings = settings;
    if (concurrentChanged) {
        pumpQueue();
    }
}

// ---------------------------------------------------------------------------
// Recuperacion de base de datos
// ---------------------------------------------------------------------------

void DownloadEngine::restoreFromDatabase() {
    m_tasks.clear();
    m_index.clear();
    m_runtimes.clear();

    if (m_db != nullptr && m_db->isOpen()) {
        const QList<DownloadTask> loaded = m_db->loadAll(nullptr);
        for (DownloadTask task : loaded) {
            // Tareas que quedaron activas al cerrar: se reponen en cola/pausa.
            if (task.state == DownloadState::Connecting || task.state == DownloadState::Downloading ||
                task.state == DownloadState::WaitingRefresh) {
                task.state = m_settings.autoStart ? DownloadState::Queued : DownloadState::Paused;
                if (task.lastError.isEmpty()) {
                    task.lastError = QStringLiteral("Interrumpida al cerrar la aplicación");
                }
            }
            if (task.source.isEmpty()) {
                task.source = QStringLiteral("restored");
            }
            task.bytesPerSec = 0.0;
            task.probed = task.totalBytes > 0;
            appendRestored(task);
        }
    }
    emit tasksReloaded();
    emit statusChanged();
    if (m_settings.autoStart) {
        QTimer::singleShot(0, this, &DownloadEngine::pumpQueue);
    }
}

void DownloadEngine::appendRestored(const DownloadTask& task) {
    DownloadTask copy = task;
    if (copy.id == 0) {
        copy.id = m_nextId++;
    }
    m_index.insert(copy.id, m_tasks.size());
    m_tasks.push_back(copy);
    m_nextId = std::max(m_nextId, copy.id + 1);
}

// ---------------------------------------------------------------------------
// Consultas
// ---------------------------------------------------------------------------

qsizetype DownloadEngine::indexOf(quint64 id) const {
    const auto it = m_index.constFind(id);
    return it == m_index.constEnd() ? qsizetype(-1) : it.value();
}

DownloadTask* DownloadEngine::mutableTask(quint64 id) {
    const qsizetype index = indexOf(id);
    if (index < 0 || index >= m_tasks.size()) {
        return nullptr;
    }
    return &m_tasks[index];
}

DownloadEngine::Runtime* DownloadEngine::runtimeFor(quint64 id, bool create) {
    auto it = m_runtimes.find(id);
    if (it != m_runtimes.end()) {
        return it.value().get();
    }
    if (!create) {
        return nullptr;
    }
    auto runtime = std::make_unique<Runtime>();
    Runtime* raw = runtime.get();
    m_runtimes.insert(id, std::move(runtime));
    return raw;
}

bool DownloadEngine::hasTask(quint64 id) const {
    return indexOf(id) >= 0;
}

DownloadTask DownloadEngine::task(quint64 id) const {
    const qsizetype index = indexOf(id);
    return index >= 0 ? m_tasks.at(index) : DownloadTask{};
}

QList<DownloadTask> DownloadEngine::allTasks() const {
    return m_tasks;
}

QList<SegmentInfo> DownloadEngine::segments(quint64 id) const {
    const qsizetype index = indexOf(id);
    return index >= 0 ? m_tasks.at(index).segments : QList<SegmentInfo>{};
}

int DownloadEngine::activeTaskCount() const {
    int count = 0;
    for (const DownloadTask& task : m_tasks) {
        if (task.state == DownloadState::Downloading || task.state == DownloadState::Connecting) {
            ++count;
        }
    }
    return count;
}

int DownloadEngine::queuedTaskCount() const {
    int count = 0;
    for (const DownloadTask& task : m_tasks) {
        if (task.state == DownloadState::Queued) {
            ++count;
        }
    }
    return count;
}

double DownloadEngine::globalSpeed() const {
    double speed = 0.0;
    for (const DownloadTask& task : m_tasks) {
        if (task.state == DownloadState::Downloading) {
            speed += task.bytesPerSec;
        }
    }
    return speed;
}

bool DownloadEngine::isTaskActive(quint64 id) const {
    const qsizetype index = indexOf(id);
    if (index < 0) {
        return false;
    }
    const DownloadTask& task = m_tasks.at(index);
    return task.state == DownloadState::Downloading || task.state == DownloadState::Connecting;
}

int DownloadEngine::normalizeThreadCount(int threads) {
    int best = kThreadOptions[0];
    int bestDistance = std::abs(threads - best);
    for (const int option : kThreadOptions) {
        const int distance = std::abs(threads - option);
        if (distance < bestDistance) {
            best = option;
            bestDistance = distance;
        }
    }
    return best;
}

QVariantMap DownloadEngine::snapshot(const DownloadTask& task) {
    QVariantMap map;
    map.insert(QStringLiteral("id"), static_cast<qulonglong>(task.id));
    map.insert(QStringLiteral("url"), task.url);
    map.insert(QStringLiteral("name"), task.fileName);
    map.insert(QStringLiteral("dir"), task.destDir);
    map.insert(QStringLiteral("path"), task.savePath());
    map.insert(QStringLiteral("state"), static_cast<int>(task.state));
    map.insert(QStringLiteral("stateText"), stateText(task.state));
    map.insert(QStringLiteral("stateColor"), stateColorHex(task.state));
    map.insert(QStringLiteral("stateIcon"), stateIcon(task.state));
    map.insert(QStringLiteral("category"), static_cast<int>(task.category));
    map.insert(QStringLiteral("categoryText"), categoryText(task.category));
    map.insert(QStringLiteral("categoryIcon"), categoryIcon(task.category));
    map.insert(QStringLiteral("size"), static_cast<qlonglong>(task.totalBytes));
    map.insert(QStringLiteral("sizeText"), humanSize(task.totalBytes));
    map.insert(QStringLiteral("received"), static_cast<qlonglong>(task.receivedBytes));
    map.insert(QStringLiteral("progress"), task.progress());
    map.insert(QStringLiteral("progressText"), humanPercent(task.progress()));
    map.insert(QStringLiteral("speed"), task.bytesPerSec);
    map.insert(QStringLiteral("speedText"), humanSpeed(task.bytesPerSec));
    const qint64 remaining =
        (task.totalBytes > 0 && task.bytesPerSec > 1.0) ? (task.totalBytes - task.receivedBytes) / static_cast<qint64>(task.bytesPerSec) : -1;
    map.insert(QStringLiteral("eta"), remaining);
    map.insert(QStringLiteral("etaText"), humanEta(remaining));
    map.insert(QStringLiteral("threads"), task.threadCount);
    map.insert(QStringLiteral("segments"), task.segments.size());
    map.insert(QStringLiteral("error"), task.lastError);
    map.insert(QStringLiteral("source"), task.source);
    map.insert(QStringLiteral("stream"), task.streamJob);
    map.insert(QStringLiteral("createdAt"), task.createdAt.toMSecsSinceEpoch());
    return map;
}

// ---------------------------------------------------------------------------
// Alta de tareas
// ---------------------------------------------------------------------------

quint64 DownloadEngine::addTask(const NewTaskRequest& request) {
    if (!m_initialized) {
        return 0;
    }
    const QString url = request.url.trimmed();
    if (url.isEmpty()) {
        emit toast(QStringLiteral("La URL está vacía"), 2);
        return 0;
    }
    const QUrl parsed(url);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https") &&
                              scheme != QLatin1String("ftp"))) {
        emit toast(QStringLiteral("URL no soportada: %1").arg(url), 2);
        return 0;
    }

    DownloadTask task;
    task.url = url;
    task.originUrl = request.originUrl.isEmpty() ? request.referer : request.originUrl;
    task.referer = request.referer;
    task.cookies = request.cookies;
    task.userAgent = request.userAgent;
    task.destDir = request.destDir.isEmpty() ? m_settings.downloadDir : request.destDir;
    task.fileName = request.fileName.isEmpty() ? sanitizeFileName(fileNameFromUrl(url))
                                                : sanitizeFileName(request.fileName);
    task.httpUser = request.httpUser;
    task.httpPass = request.httpPass;
    task.extraHeaders = request.extraHeaders;
    task.category = categoryForFileName(task.fileName);
    task.state = DownloadState::Queued;
    task.threadCount = normalizeThreadCount(request.threadCount > 0 ? request.threadCount
                                                                    : m_settings.defaultThreads);
    task.source = request.source;
    task.createdAt = QDateTime::currentDateTime();
    task.updatedAt = task.createdAt;
    task.totalBytes = -1;
    task.receivedBytes = 0;

    QDir().mkpath(task.destDir);

    if (m_db != nullptr && m_db->isOpen()) {
        task.id = m_db->insertTask(task, nullptr);
    }
    if (task.id == 0) {
        task.id = m_nextId++; // modo memoria (sin base de datos)
    } else {
        m_nextId = std::max(m_nextId, task.id + 1);
    }

    appendRestored(task);
    emit taskAdded(task.id);
    emit statusChanged();
    log::info("Tarea {} creada: {}", static_cast<unsigned long long>(task.id), log::utf8(task.url));

    if (m_settings.autoStart && request.autoStart) {
        startTask(task.id);
    }
    return task.id;
}

quint64 DownloadEngine::addStreamTask(const QString& manifestUrl, const QString& destDir, const QString& referer,
                                      const QString& cookies, const QString& fileName) {
    NewTaskRequest request;
    request.url = manifestUrl;
    request.referer = referer;
    request.cookies = cookies;
    request.destDir = destDir;
    request.fileName = fileName;
    request.source = QStringLiteral("stream");
    request.threadCount = m_settings.defaultThreads;
    const quint64 id = addTask(request);
    if (id != 0) {
        DownloadTask* task = mutableTask(id);
        if (task != nullptr) {
            task->streamJob = true;
            task->originUrl = referer.isEmpty() ? manifestUrl : referer;
            if (m_db != nullptr && m_db->isOpen()) {
                m_db->updateTask(*task, nullptr);
            }
            emit taskUpdated(id);
            if (m_settings.autoStart) {
                startTask(id);
            }
        }
    }
    return id;
}

// ---------------------------------------------------------------------------
// Arranque / pausa / borrado
// ---------------------------------------------------------------------------

void DownloadEngine::startTask(quint64 id) {
    if (!m_initialized || m_shuttingDown) {
        return;
    }
    DownloadTask* task = mutableTask(id);
    if (task == nullptr) {
        return;
    }
    if (task->state == DownloadState::Completed) {
        return;
    }

    // Runtime previo inactivo (reintento / reanudacion) -> se recicla.
    Runtime* existing = runtimeFor(id, false);
    if (existing != nullptr) {
        if (existing->probeInFlight || !existing->workers.isEmpty() || existing->streamActive) {
            return; // ya en marcha
        }
        destroyRuntime(id);
    }

    if (activeTaskCount() >= m_settings.maxConcurrentTasks) {
        if (task->state != DownloadState::Queued) {
            task->state = DownloadState::Queued;
            persistProgress(id);
            emit taskUpdated(id);
            emit toast(QStringLiteral("En cola (límite de %1 descargas simultáneas)")
                           .arg(m_settings.maxConcurrentTasks),
                       1);
        }
        return;
    }

    if (m_settings.rotateUserAgent) {
        BrowserEmulator::rotateProfile();
    }

    task->state = DownloadState::Connecting;
    task->lastError.clear();
    task->bytesPerSec = 0.0;
    task->updatedAt = QDateTime::currentDateTime();
    persistProgress(id);
    emit taskUpdated(id);
    emit statusChanged();

    Runtime* runtime = runtimeFor(id, true);
    if (runtime == nullptr) {
        return;
    }

    if (task->streamJob) {
        startStreamJob(id);
        return;
    }

    if (!task->probed) {
        launchProbe(id);
    } else {
        beginSegments(id);
    }
}

void DownloadEngine::pauseTask(quint64 id) {
    DownloadTask* task = mutableTask(id);
    if (task == nullptr) {
        return;
    }
    if (task->state == DownloadState::Completed) {
        return;
    }

    if (task->streamJob && m_streamJobId == id) {
        m_stream->abort();
    }

    Runtime* runtime = runtimeFor(id, false);
    if (runtime == nullptr || (runtime->workers.isEmpty() && !runtime->probeInFlight && !runtime->streamActive)) {
        // Sin actividad: solo cambia de estado.
        if (task->state != DownloadState::Queued || runtime == nullptr) {
            task->state = DownloadState::Paused;
            persistProgress(id);
            emit taskUpdated(id);
            emit statusChanged();
            destroyRuntime(id);
            return;
        }
        task->state = DownloadState::Paused;
        persistProgress(id);
        emit taskUpdated(id);
        return;
    }

    task->state = DownloadState::Paused;
    task->bytesPerSec = 0.0;
    runtime->stopRequested = true;
    runtime->refreshStopping = false;
    runtime->removePending = false;
    if (runtime->refreshTimer != nullptr) {
        runtime->refreshTimer->stop();
    }
    for (SegmentWorker* worker : runtime->workers) {
        if (worker != nullptr) {
            worker->requestAbort();
        }
    }
    emit taskUpdated(id);
    emit statusChanged();
    if (runtime->workers.isEmpty() && !runtime->probeInFlight) {
        finalizeStop(id);
    }
}

void DownloadEngine::removeTask(quint64 id, bool deleteFile) {
    if (!hasTask(id)) {
        return;
    }
    DownloadTask* task = mutableTask(id);
    if (task == nullptr) {
        return;
    }
    Runtime* runtime = runtimeFor(id, false);
    if (runtime != nullptr && (!runtime->workers.isEmpty() || runtime->probeInFlight || runtime->streamActive)) {
        // Se detiene primero; la eliminacion se completa al quedarse sin workers.
        if (task->streamJob && m_streamJobId == id && m_stream != nullptr) {
            m_stream->abort();
        }
        task->state = DownloadState::Paused;
        runtime->stopRequested = true;
        runtime->removePending = true;
        runtime->removeDeleteFile = deleteFile;
        for (SegmentWorker* worker : runtime->workers) {
            if (worker != nullptr) {
                worker->requestAbort();
            }
        }
        if (runtime->workers.isEmpty() && !runtime->probeInFlight) {
            removeInternal(id, deleteFile);
        }
        return;
    }
    removeInternal(id, deleteFile);
}

void DownloadEngine::removeInternal(quint64 id, bool deleteFile) {
    const qsizetype index = indexOf(id);
    if (index < 0) {
        return;
    }
    const DownloadTask copy = m_tasks.at(index);

    destroyRuntime(id);

    if (deleteFile) {
        QFile::remove(copy.partPath());
        if (copy.savePath() != copy.partPath()) {
            QFile::remove(copy.savePath());
        }
    }
    if (m_db != nullptr && m_db->isOpen()) {
        m_db->removeTask(id, nullptr);
    }

    m_tasks.removeAt(index);
    m_index.remove(id);
    // Reindexa la cola (los indices posteriores se desplazan).
    m_index.clear();
    for (qsizetype i = 0; i < m_tasks.size(); ++i) {
        m_index.insert(m_tasks.at(i).id, i);
    }

    emit taskRemoved(id);
    emit statusChanged();
    log::info("Tarea {} eliminada (archivo: {})", static_cast<unsigned long long>(id), deleteFile ? 1 : 0);
    pumpGuarded();
}

void DownloadEngine::retryTask(quint64 id) {
    DownloadTask* task = mutableTask(id);
    if (task == nullptr || task->state == DownloadState::Completed) {
        return;
    }
    task->retries = 0;
    task->lastError.clear();
    task->state = DownloadState::Queued;
    if (m_db != nullptr && m_db->isOpen()) {
        m_db->updateTask(*task, nullptr);
    }
    emit taskUpdated(id);
    pumpGuarded();
}

void DownloadEngine::setThreadCount(quint64 id, int threads) {
    DownloadTask* task = mutableTask(id);
    if (task == nullptr) {
        return;
    }
    const int normalized = normalizeThreadCount(threads);
    task->threadCount = normalized;

    if (!isTaskActive(id) && task->receivedBytes == 0 && task->totalBytes > 0 && task->probed &&
        !task->streamJob) {
        task->segments = splitRangeIntoSegments(task->totalBytes, normalized);
        if (m_db != nullptr && m_db->isOpen()) {
            m_db->saveSegments(id, task->segments, nullptr);
        }
    } else if (isTaskActive(id)) {
        emit toast(QStringLiteral("El número de hilos se aplicará en la próxima re-segmentación"), 1);
    }

    if (m_db != nullptr && m_db->isOpen()) {
        m_db->updateTask(*task, nullptr);
    }
    emit taskUpdated(id);
}

void DownloadEngine::setDestination(quint64 id, const QString& dir) {
    DownloadTask* task = mutableTask(id);
    if (task == nullptr || dir.isEmpty()) {
        return;
    }
    if (isTaskActive(id)) {
        emit toast(QStringLiteral("Detén la descarga antes de cambiar la carpeta"), 1);
        return;
    }
    task->destDir = dir;
    QDir().mkpath(dir);
    if (m_db != nullptr && m_db->isOpen()) {
        m_db->updateTask(*task, nullptr);
    }
    emit taskUpdated(id);
}

void DownloadEngine::updateTaskUrl(quint64 id, const QString& url, const QString& cookies,
                                   const QString& referer) {
    DownloadTask* task = mutableTask(id);
    if (task == nullptr || url.isEmpty()) {
        return;
    }
    task->url = url;
    if (!cookies.isEmpty()) {
        task->cookies = cookies;
    }
    if (!referer.isEmpty()) {
        task->referer = referer;
    }
    task->probed = false; // la nueva URL debe volver a sondearse
    task->retries = 0;
    task->lastError.clear();
    task->state = DownloadState::Queued;

    if (m_db != nullptr && m_db->isOpen()) {
        m_db->updateUrl(id, url, cookies, referer, nullptr);
        m_db->updateProgress(id, task->state, task->receivedBytes, QString(), nullptr);
    }
    destroyRuntime(id);
    emit taskUpdated(id);
    emit statusChanged();
    log::info("Tarea {} refrescada con nuevo enlace", static_cast<unsigned long long>(id));
    QTimer::singleShot(0, this, &DownloadEngine::pumpQueue);
}

void DownloadEngine::startAll() {
    for (const DownloadTask& task : m_tasks) {
        if (task.state == DownloadState::Paused || task.state == DownloadState::Failed ||
            task.state == DownloadState::WaitingRefresh) {
            DownloadTask* mutableRef = mutableTask(task.id);
            if (mutableRef != nullptr) {
                mutableRef->retries = 0;
                mutableRef->lastError.clear();
                mutableRef->state = DownloadState::Queued;
                if (mutableRef->probed == false && mutableRef->totalBytes > 0) {
                    mutableRef->probed = true;
                }
            }
        }
    }
    pumpGuarded();
    emit statusChanged();
}

void DownloadEngine::pauseAll() {
    const QList<DownloadTask> snapshot = m_tasks;
    for (const DownloadTask& task : snapshot) {
        if (task.state == DownloadState::Downloading || task.state == DownloadState::Connecting ||
            task.state == DownloadState::Queued) {
            pauseTask(task.id);
        }
    }
}

void DownloadEngine::clearCompleted() {
    const QList<DownloadTask> snapshot = m_tasks;
    for (const DownloadTask& task : snapshot) {
        if (task.state == DownloadState::Completed) {
            removeInternal(task.id, false);
        }
    }
}

// ---------------------------------------------------------------------------
// Sonda de servidor
// ---------------------------------------------------------------------------

ProbeResult DownloadEngine::runProbe(const DownloadTask& task, const QString& proxy) {
    ProbeResult result;

    ServerCapabilities caps;
    int status = 0;
    QString error;
    bool ok = probeOnce(task, task.url, true, false, proxy, &caps, &status, &error);

    // Metodos no soportados o sin Content-Length -> GET con Range: bytes=0-0.
    if ((!ok && (status == 405 || status == 501 || status == 0)) || (ok && caps.totalSize < 0)) {
        ServerCapabilities caps2;
        int status2 = 0;
        QString error2;
        if (probeOnce(task, task.url, false, true, proxy, &caps2, &status2, &error2)) {
            caps = caps2;
            status = status2;
            ok = true;
            error.clear();
        } else if (status2 == 403 || status2 == 401) {
            status = status2;
            error = error2;
            ok = false;
        } else if (status == 0) {
            status = status2;
            error = error2;
        }
    }

    // Confirmacion de soporte de rangos cuando no anuncia Accept-Ranges.
    if (ok && !caps.supportsRange && caps.totalSize > 0) {
        ServerCapabilities caps3;
        int status3 = 0;
        QString error3;
        if (probeOnce(task, task.url, false, true, proxy, &caps3, &status3, &error3) && status3 == 206) {
            caps.supportsRange = true;
            if (caps3.totalSize > 0) {
                caps.totalSize = caps3.totalSize;
            }
        }
    }

    result.ok = ok;
    result.capabilities = caps;
    result.capabilities.httpStatus = status;
    result.httpStatus = status;
    result.error = error;
    if (!ok && error.isEmpty()) {
        result.error = QStringLiteral("HTTP %1").arg(status);
    }
    return result;
}

void DownloadEngine::storeProbeResult(quint64 id, const ProbeResult& result) {
    QMutexLocker lock(&m_probeMutex);
    m_probeResults.insert(id, result);
}

void DownloadEngine::launchProbe(quint64 id) {
    Runtime* runtime = runtimeFor(id, true);
    DownloadTask* task = mutableTask(id);
    if (runtime == nullptr || task == nullptr || runtime->probeInFlight) {
        return;
    }
    runtime->probeInFlight = true;
    auto* runnable = new ProbeRunnable(this, id, *task, m_settings.proxy);
    runnable->setAutoDelete(true);
    QThreadPool::globalInstance()->start(runnable);
}

void DownloadEngine::onProbeArrived(quint64 taskId) {
    ProbeResult result;
    {
        QMutexLocker lock(&m_probeMutex);
        result = m_probeResults.take(taskId);
    }
    onProbeResult(taskId, result);
}

void DownloadEngine::onProbeResult(quint64 taskId, const ProbeResult& result) {
    Runtime* runtime = runtimeFor(taskId, false);
    DownloadTask* task = mutableTask(taskId);
    if (task == nullptr) {
        destroyRuntime(taskId);
        return;
    }
    if (runtime != nullptr) {
        runtime->probeInFlight = false;
    }

    if (task->state != DownloadState::Connecting) {
        // El usuario pauso/elimino mientras se sondeaba.
        if (runtime != nullptr && runtime->workers.isEmpty()) {
            destroyRuntime(taskId);
            persistProgress(taskId);
        }
        return;
    }
    if (runtime == nullptr) {
        return;
    }

    if (!result.ok) {
        if (result.httpStatus == 403 || result.httpStatus == 401) {
            enterWaitingRefresh(taskId, result.httpStatus);
            return;
        }
        ++task->retries;
        task->lastError = result.error;
        if (task->retries > m_settings.maxRetries) {
            failTask(taskId, QStringLiteral("No se pudo conectar: %1").arg(result.error));
            return;
        }
        const int shift = std::min(task->retries, 5);
        const qint64 backoff = std::min<qint64>(30000, qint64(1000) * (1 << shift));
        runtime->retryAtMs = nowMs() + backoff;
        task->state = DownloadState::Queued;
        persistProgress(taskId);
        emit taskUpdated(taskId);
        emit toast(QStringLiteral("Reintento de conexión en %1 s (%2)")
                       .arg(backoff / 1000)
                       .arg(result.error),
                   1);
        QTimer::singleShot(backoff, this, [this, taskId]() {
            DownloadTask* current = mutableTask(taskId);
            Runtime* rt = runtimeFor(taskId, false);
            if (current == nullptr || rt == nullptr) {
                return;
            }
            if (current->state == DownloadState::Queued && rt->retryAtMs <= nowMs() &&
                !rt->probeInFlight && rt->workers.isEmpty()) {
                destroyRuntime(taskId);
                startTask(taskId);
            }
        });
        return;
    }

    const ServerCapabilities& caps = result.capabilities;

    // Validacion de reanudacion: ETag/Last-Modified o tamano distintos -> reinicio.
    if (task->receivedBytes > 0) {
        bool mustReset = false;
        if (!task->etag.isEmpty() && !caps.etag.isEmpty() && task->etag != caps.etag) {
            mustReset = true;
        } else if (task->totalBytes > 0 && caps.totalSize > 0 && task->totalBytes != caps.totalSize) {
            mustReset = true;
        } else if (!QFileInfo::exists(task->partPath()) && task->totalBytes > 0) {
            mustReset = true;
        }
        if (mustReset) {
            log::warn("Tarea {}: el archivo cambio en el servidor, reinicio limpio",
                      static_cast<unsigned long long>(taskId));
            resetTaskData(taskId);
            emit toast(QStringLiteral("El archivo cambió en el servidor; se reinicia la descarga"), 1);
            task = mutableTask(taskId);
            if (task == nullptr) {
                destroyRuntime(taskId);
                return;
            }
        }
    }

    task->totalBytes = caps.totalSize;
    task->etag = caps.etag;
    task->lastModified = caps.lastModified;
    task->mimeType = caps.mimeType;
    if (!caps.finalUrl.isEmpty() && caps.finalUrl != task->url) {
        task->url = caps.finalUrl;
    }
    const QString previousAutoName = fileNameFromUrl(task->url);
    if (!caps.fileName.isEmpty() &&
        (task->fileName.isEmpty() || task->fileName == previousAutoName)) {
        task->fileName = sanitizeFileName(caps.fileName);
        task->category = categoryForFileName(task->fileName);
    } else {
        task->category = categoryForFileName(task->fileName);
    }
    task->probed = true;
    task->retries = 0;
    task->lastError.clear();

    runtime = runtimeFor(taskId, true);
    if (runtime == nullptr) {
        return;
    }
    runtime->rangeSupported = caps.supportsRange && caps.totalSize > 0;
    if (!runtime->rangeSupported) {
        task->threadCount = 1;
    }

    // Reutiliza los segmentos existentes si siguen siendo coherentes.
    if (!segmentsAreValid(*task)) {
        task->segments.clear();
        if (task->totalBytes > 0 && runtime->rangeSupported) {
            task->segments = splitRangeIntoSegments(task->totalBytes, task->threadCount);
        }
    }

    if (m_db != nullptr && m_db->isOpen()) {
        m_db->updateTask(*task, nullptr);
        m_db->saveSegments(taskId, task->segments, nullptr);
    }
    emit taskUpdated(taskId);
    beginSegments(taskId);
}

// ---------------------------------------------------------------------------
// Arranque de segmentos
// ---------------------------------------------------------------------------

void DownloadEngine::beginSegments(quint64 id) {
    Runtime* runtime = runtimeFor(id, true);
    DownloadTask* task = mutableTask(id);
    if (runtime == nullptr || task == nullptr) {
        return;
    }
    if (task->state == DownloadState::Paused) {
        return;
    }

    const bool resume = task->receivedBytes > 0 && QFileInfo::exists(task->partPath());
    if (resume == false && task->receivedBytes > 0) {
        // El .part desaparecio: reinicia contadores.
        task->receivedBytes = 0;
        for (SegmentInfo& segment : task->segments) {
            segment.receivedBytes = 0;
            segment.state = SegmentState::Pending;
        }
    }

    // 1) Archivo de destino (.part) con reserva de espacio.
    if (runtime->io == nullptr) {
        runtime->io = new FileAllocator();
    }
    if (!runtime->io->isOpen()) {
        QString ioError;
        const qint64 size = task->totalBytes > 0 ? task->totalBytes : 0;
        if (!runtime->io->open(task->partPath(), size, m_settings.preallocateFiles, &ioError,
                               /*truncate=*/!resume)) {
            failTask(id, ioError);
            return;
        }
    }

    // 2) Segmentacion inicial (spec 5.2.2).
    if (task->segments.isEmpty()) {
        if (task->totalBytes > 0 && runtime->rangeSupported) {
            task->segments = splitRangeIntoSegments(task->totalBytes, task->threadCount);
        } else {
            SegmentInfo linear;
            linear.index = 0;
            linear.startByte = 0;
            linear.endByte = -1;
            linear.receivedBytes = 0;
            linear.state = SegmentState::Pending;
            task->segments.append(linear);
        }
        if (m_db != nullptr && m_db->isOpen()) {
            m_db->saveSegments(id, task->segments, nullptr);
        }
    }

    // Sin soporte de rangos no existe reanudacion: el rango abierto empieza
    // siempre en 0 para no dejar huecos en el fichero.
    for (SegmentInfo& segment : task->segments) {
        if (segment.endByte < 0 && segment.receivedBytes > 0) {
            segment.receivedBytes = 0;
            segment.state = SegmentState::Pending;
        }
    }
    task->receivedBytes = task->sumSegmentBytes();

    // 3) Estado y arranque.
    task->state = DownloadState::Downloading;
    task->updatedAt = QDateTime::currentDateTime();
    persistProgress(id);

    int spawned = 0;
    for (const SegmentInfo& segment : task->segments) {
        if (segment.isComplete()) {
            if (SegmentInfo* mutableSeg = findSegment(*task, segment.index)) {
                mutableSeg->state = SegmentState::Completed;
            }
            continue;
        }
        spawnSegment(id, segment);
        ++spawned;
    }

    if (spawned == 0) {
        // No hay nada pendiente: o esta completa, o solo quedaba el rango abierto.
        if (allSegmentsComplete(*task)) {
            finalizeDownload(id);
            return;
        }
        // Rango abierto sin workers -> lo relanza.
        if (!task->segments.isEmpty() && task->segments.first().endByte < 0) {
            task->segments.first().receivedBytes = 0;
            spawnSegment(id, task->segments.first());
            spawned = 1;
        }
    }

    emit taskUpdated(id);
    emit statusChanged();
    log::info("Tarea {} descargando con {} segmento(s), total={}", static_cast<unsigned long long>(id), spawned,
              static_cast<long long>(task->totalBytes));
}

void DownloadEngine::spawnSegment(quint64 id, const SegmentInfo& segment) {
    Runtime* runtime = runtimeFor(id, false);
    DownloadTask* task = mutableTask(id);
    if (runtime == nullptr || task == nullptr || runtime->io == nullptr) {
        return;
    }
    if (runtime->workers.contains(segment.index)) {
        return;
    }

    SegmentRequest request;
    request.task = *task;
    request.index = segment.index;
    request.startByte = segment.startByte;
    request.endByte = segment.endByte;
    request.initialReceived = segment.receivedBytes;
    request.sendRange = runtime->rangeSupported && segment.endByte >= 0;
    request.proxy = m_settings.proxy;
    request.maxRetries = m_settings.maxRetries;

    auto* worker = new SegmentWorker(request, runtime->io);
    auto* thread = new QThread();
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &SegmentWorker::start);
    connect(worker, &SegmentWorker::finished, this, &DownloadEngine::onSegmentFinished);
    connect(worker, &SegmentWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, id, segmentIndex = segment.index, thread]() {
        if (Runtime* rt = runtimeFor(id, false)) {
            rt->threads.remove(segmentIndex);
        }
        thread->deleteLater();
    });

    runtime->workers.insert(segment.index, worker);
    runtime->threads.insert(segment.index, thread);

    if (SegmentInfo* mutableSeg = findSegment(*task, segment.index)) {
        mutableSeg->state = SegmentState::Downloading;
    }
    thread->start();
}

// ---------------------------------------------------------------------------
// Finalizacion de segmentos
// ---------------------------------------------------------------------------

void DownloadEngine::onSegmentFinished(quint64 taskId, int index, qint64 received, int result, int httpStatus,
                                       const QString& error) {
    DownloadTask* task = mutableTask(taskId);
    Runtime* runtime = runtimeFor(taskId, false);
    if (task == nullptr) {
        destroyRuntime(taskId);
        return;
    }

    const auto outcome = static_cast<SegmentResult>(result);
    if (runtime != nullptr) {
        runtime->workers.remove(index);
    }
    if (SegmentInfo* segment = findSegment(*task, index)) {
        segment->receivedBytes = received;
    }

    // --- Re-segmentacion (spec 5.2.3-5.2.4) -------------------------------
    if (runtime != nullptr && runtime->rebalanceIndex == index) {
        runtime->rebalanceIndex = -1;
        if (outcome == SegmentResult::Aborted && task->state == DownloadState::Downloading &&
            !runtime->stopRequested && !runtime->refreshStopping) {
            performRebalance(taskId, index, received);
            emit taskUpdated(taskId);
            return;
        }
        // Gano la carrera (termino antes del abort): se continua normalmente.
    }

    const bool stopping = runtime != nullptr && runtime->stopRequested;
    const bool refreshing = runtime != nullptr && runtime->refreshStopping;
    const bool removing = runtime != nullptr && runtime->removePending;
    const bool degrading = runtime != nullptr && runtime->degradePending;

    // --- Enlace caducado (spec cap. 10) -----------------------------------
    if (outcome == SegmentResult::Expired && !refreshing && !stopping && !removing) {
        enterWaitingRefresh(taskId, httpStatus);
        return;
    }

    // --- Servidor sin soporte de Range: degradacion a 1 hilo ---------------
    if (outcome == SegmentResult::RangeUnsupported && !stopping && !refreshing && !removing && !degrading) {
        if (runtime != nullptr) {
            runtime->degradePending = true;
            runtime->rangeSupported = false;
            task->threadCount = 1;
            for (SegmentWorker* worker : runtime->workers) {
                if (worker != nullptr) {
                    worker->requestAbort();
                }
            }
            return;
        } else {
            // Runtime ya no existe: la tarea se degrada directamente,
            // sin depender de un ciclo de workers/abort.
            applyDegrade(taskId);
            return;
        }
    }
    if (stopping || refreshing || removing || degrading) {
        if (runtime == nullptr || runtime->workers.isEmpty()) {
            if (removing) {
                const bool deleteFiles = runtime != nullptr ? runtime->removeDeleteFile : false;
                removeInternal(taskId, deleteFiles);
                return;
            }
            if (degrading) {
                applyDegrade(taskId);
                return;
            }
            if (refreshing) {
                // Estado ya WaitingRefresh: se conserva el runtime porque su
                // temporizador de refresco sigue activo (spec cap. 10).
                persistProgress(taskId);
                emit taskUpdated(taskId);
                return;
            }
            finalizeStop(taskId);
            return;
        }
        emit taskUpdated(taskId);
        return;
    }

    // --- Resultados normales ------------------------------------------------
    switch (outcome) {
        case SegmentResult::Completed: {
            if (SegmentInfo* segment = findSegment(*task, index)) {
                segment->state = SegmentState::Completed;
            }
            task->receivedBytes = task->sumSegmentBytes();

            const bool linearDone = task->segments.size() == 1 && task->segments.first().endByte < 0;
            if (linearDone || allSegmentsComplete(*task)) {
                finalizeDownload(taskId);
                return;
            }
            if (runtime != nullptr && runtime->lastSavedBytes != task->receivedBytes) {
                persistProgress(taskId);
                runtime->lastSavedBytes = task->receivedBytes;
            }
            if (m_db != nullptr && m_db->isOpen()) {
                m_db->saveSegments(taskId, task->segments, nullptr);
            }
            if (task->state == DownloadState::Downloading) {
                maybeRebalance(taskId);
            }
            emit taskUpdated(taskId);
            return;
        }

        case SegmentResult::Aborted: {
            // Abort no planado (p.ej. cambio de estado) -> se relanza.
            if (task->state == DownloadState::Downloading && runtime != nullptr && !runtime->workers.isEmpty()) {
                if (SegmentInfo* segment = findSegment(*task, index)) {
                    if (!segment->isComplete()) {
                        segment->state = SegmentState::Downloading;
                        spawnSegment(taskId, *segment);
                    }
                }
            }
            emit taskUpdated(taskId);
            return;
        }

        case SegmentResult::Failed: {
            if (SegmentInfo* segment = findSegment(*task, index)) {
                segment->state = SegmentState::Error;
            }
            ++task->retries;
            task->lastError = error.isEmpty() ? QStringLiteral("Error de red") : error;
            if (runtime == nullptr) {
                failTask(taskId, task->lastError);
                return;
            }
            if (task->retries <= m_settings.maxRetries) {
                const int shift = std::min(task->retries, 5);
                const qint64 backoff = std::min<qint64>(30000, qint64(1000) * (1 << shift));
                runtime->retryAtMs = nowMs() + backoff;
                persistProgress(taskId);
                emit taskUpdated(taskId);
                QTimer::singleShot(backoff, this, [this, taskId, index, expected = task->retries]() {
                    DownloadTask* current = mutableTask(taskId);
                    Runtime* rt = runtimeFor(taskId, false);
                    if (current == nullptr || rt == nullptr || current->state != DownloadState::Downloading) {
                        return;
                    }
                    if (current->retries != expected) {
                        return;
                    }
                    rt->retryAtMs = 0;
                    if (SegmentInfo* segment = findSegment(*current, index)) {
                        if (!segment->isComplete()) {
                            spawnSegment(taskId, *segment);
                        }
                    }
                });
            } else {
                failTask(taskId, QStringLiteral("Descarga fallida: %1").arg(task->lastError));
            }
            return;
        }

        case SegmentResult::Expired:
        case SegmentResult::RangeUnsupported:
        default: {
            // Ya tratados arriba; si llegan aqui se dejan como error.
            if (SegmentInfo* segment = findSegment(*task, index)) {
                segment->state = SegmentState::Error;
            }
            emit taskUpdated(taskId);
            return;
        }
    }
}

void DownloadEngine::applyDegrade(quint64 id) {
    Runtime* runtime = runtimeFor(id, false);
    DownloadTask* task = mutableTask(id);
    if (runtime == nullptr || task == nullptr) {
        return;
    }
    runtime->degradePending = false;
    runtime->rangeSupported = false;
    task->threadCount = 1;

    const qint64 receivedNow = task->sumSegmentBytes();

    SegmentInfo linear;
    linear.index = 0;
    linear.startByte = 0;
    linear.endByte = task->totalBytes > 0 ? task->totalBytes - 1 : -1;
    linear.receivedBytes = task->totalBytes > 0 ? std::min(receivedNow, task->totalBytes) : 0;
    linear.state = SegmentState::Pending;
    task->segments = {linear};
    task->receivedBytes = linear.receivedBytes;

    if (m_db != nullptr && m_db->isOpen()) {
        m_db->saveSegments(id, task->segments, nullptr);
    }
    emit toast(QStringLiteral("El servidor no permite múltiples hilos; se usará 1 hilo"), 1);

    if (linear.endByte >= 0 && linear.receivedBytes >= linear.totalBytes()) {
        finalizeDownload(id);
        return;
    }
    if (task->state == DownloadState::Downloading || task->state == DownloadState::Connecting) {
        task->state = DownloadState::Downloading;
        spawnSegment(id, linear);
    }
    emit taskUpdated(id);
}

// ---------------------------------------------------------------------------
// Re-segmentacion dinamica (spec 5.2.3 - 5.2.4)
// ---------------------------------------------------------------------------

void DownloadEngine::maybeRebalance(quint64 id) {
    Runtime* runtime = runtimeFor(id, false);
    DownloadTask* task = mutableTask(id);
    if (runtime == nullptr || task == nullptr) {
        return;
    }
    if (runtime->rebalanceIndex >= 0) {
        return; // ya hay una re-segmentacion en curso
    }
    if (runtime->workers.size() <= 1) {
        return; // sin compañeros que repartan carga
    }

    int slowestIndex = -1;
    qint64 slowestRemaining = 0;
    for (const SegmentInfo& segment : task->segments) {
        if (!runtime->workers.contains(segment.index)) {
            continue;
        }
        const qint64 remaining = segment.remainingBytes();
        if (remaining > slowestRemaining) {
            slowestRemaining = remaining;
            slowestIndex = segment.index;
        }
    }
    if (slowestIndex < 0) {
        return;
    }
    // Solo conviene partir rangos grandes (evita micro-segmentos).
    if (slowestRemaining < kMinRebalanceChunk * 2) {
        return;
    }

    SegmentInfo* segment = findSegment(*task, slowestIndex);
    if (segment == nullptr) {
        return;
    }
    segment->state = SegmentState::Rebalancing;
    runtime->rebalanceIndex = slowestIndex;
    if (SegmentWorker* worker = runtime->workers.value(slowestIndex, nullptr)) {
        worker->requestAbort();
    }
    log::info("Re-segmentacion tarea {}: seg {} (quedan {} bytes)",
              static_cast<unsigned long long>(id), slowestIndex, static_cast<long long>(slowestRemaining));
    emit taskUpdated(id);
}

void DownloadEngine::performRebalance(quint64 id, int index, qint64 received) {
    Runtime* runtime = runtimeFor(id, false);
    DownloadTask* task = mutableTask(id);
    if (runtime == nullptr || task == nullptr) {
        return;
    }
    SegmentInfo* segment = findSegment(*task, index);
    if (segment == nullptr) {
        return;
    }
    segment->receivedBytes = received;
    segment->state = SegmentState::Pending;

    const qint64 position = segment->startByte + received;
    const qint64 remaining = segment->endByte - position + 1;

    if (remaining < kMinSegmentBytes * 2) {
        // Demasiado pequeño para partir: solo se relanza.
        if (runtime->rangeSupported) {
            spawnSegment(id, *segment);
        }
        return;
    }

    const qint64 midpoint = position + remaining / 2;

    // La primera mitad continua en el segmento original...
    const qint64 originalEnd = segment->endByte;
    segment->endByte = midpoint;

    // ...y la segunda mitad se asigna a un nuevo hilo (hilo B de la spec).
    SegmentInfo second;
    second.index = 0;
    for (const SegmentInfo& existing : task->segments) {
        second.index = std::max(second.index, existing.index + 1);
    }
    second.startByte = midpoint + 1;
    second.endByte = originalEnd;
    second.receivedBytes = 0;
    second.state = SegmentState::Pending;
    task->segments.append(second);

    if (m_db != nullptr && m_db->isOpen()) {
        m_db->saveSegments(id, task->segments, nullptr);
    }
    log::info("Tarea {} seg {} repartido: [{}..{}] + [{}..{}]", static_cast<unsigned long long>(id), index,
              static_cast<long long>(position), static_cast<long long>(midpoint),
              static_cast<long long>(second.startByte), static_cast<long long>(second.endByte));

    if (!segment->isComplete()) {
        spawnSegment(id, *segment);
    }
    spawnSegment(id, second);
}

// ---------------------------------------------------------------------------
// Pausa / fallo / refresco de enlace
// ---------------------------------------------------------------------------

void DownloadEngine::stopAllSegments(quint64 id, bool forRefresh, bool forStop) {
    Runtime* runtime = runtimeFor(id, false);
    if (runtime == nullptr) {
        return;
    }
    runtime->refreshStopping = forRefresh;
    runtime->stopRequested = forStop;
    for (SegmentWorker* worker : runtime->workers) {
        if (worker != nullptr) {
            worker->requestAbort();
        }
    }
    if (runtime->workers.isEmpty()) {
        if (forRefresh) {
            persistProgress(id);
            destroyRuntime(id);
        } else if (forStop) {
            finalizeStop(id);
        }
    }
}

void DownloadEngine::finalizeStop(quint64 id) {
    DownloadTask* task = mutableTask(id);
    Runtime* runtime = runtimeFor(id, false);
    if (task == nullptr) {
        destroyRuntime(id);
        return;
    }
    task->receivedBytes = task->sumSegmentBytes();
    task->bytesPerSec = 0.0;
    task->updatedAt = QDateTime::currentDateTime();
    persistProgress(id);
    if (m_db != nullptr && m_db->isOpen()) {
        m_db->saveSegments(id, task->segments, nullptr);
    }
    destroyRuntime(id);
    emit taskUpdated(id);
    emit statusChanged();
    pumpGuarded();
}

void DownloadEngine::failTask(quint64 id, const QString& message) {
    DownloadTask* task = mutableTask(id);
    if (task == nullptr) {
        destroyRuntime(id);
        return;
    }
    task->state = DownloadState::Failed;
    task->lastError = message;
    task->bytesPerSec = 0.0;
    task->receivedBytes = task->sumSegmentBytes();
    persistProgress(id);
    emit taskUpdated(id);
    emit statusChanged();
    emit toast(message, 2);
    log::error("Tarea {} fallida: {}", static_cast<unsigned long long>(id), log::utf8(message));

    Runtime* runtime = runtimeFor(id, false);
    if (runtime == nullptr || runtime->workers.isEmpty()) {
        if (m_db != nullptr && m_db->isOpen()) {
            m_db->saveSegments(id, task->segments, nullptr);
        }
        destroyRuntime(id);
        pumpGuarded();
        return;
    }
    runtime->stopRequested = true;
    runtime->refreshStopping = false;
    runtime->degradePending = false;
    for (SegmentWorker* worker : runtime->workers) {
        if (worker != nullptr) {
            worker->requestAbort();
        }
    }
}

void DownloadEngine::enterWaitingRefresh(quint64 id, int httpStatus) {
    DownloadTask* task = mutableTask(id);
    if (task == nullptr) {
        return;
    }
    Runtime* runtime = runtimeFor(id, false);
    if (runtime == nullptr) {
        runtime = runtimeFor(id, true);
        if (runtime == nullptr) {
            return;
        }
    }

    task->state = DownloadState::WaitingRefresh;
    task->lastError = QStringLiteral("HTTP %1: el enlace caducó").arg(httpStatus);
    task->bytesPerSec = 0.0;
    task->receivedBytes = task->sumSegmentBytes();
    runtime->stopRequested = false;
    runtime->refreshStopping = true;
    runtime->degradePending = false;

    for (SegmentWorker* worker : runtime->workers) {
        if (worker != nullptr) {
            worker->requestAbort();
        }
    }

    // Si nadie refresca el enlace en el plazo, la tarea pasa a fallida.
    if (runtime->refreshTimer == nullptr) {
        runtime->refreshTimer = new QTimer(this);
        runtime->refreshTimer->setSingleShot(true);
        runtime->refreshTimer->setInterval(kTokenRefreshTimeoutMs);
        connect(runtime->refreshTimer, &QTimer::timeout, this, [this, id]() {
            DownloadTask* current = mutableTask(id);
            if (current != nullptr && current->state == DownloadState::WaitingRefresh) {
                failTask(id, QStringLiteral("No se pudo refrescar el enlace (403). Pulsa Reanudar para reintentar."));
            }
        });
    }
    runtime->refreshTimer->start();

    persistProgress(id);
    if (m_db != nullptr && m_db->isOpen()) {
        m_db->saveSegments(id, task->segments, nullptr);
    }

    emit taskUpdated(id);
    emit statusChanged();
    emit toast(QStringLiteral("Enlace caducado: pidiendo uno nuevo al navegador…"), 1);
    emit tokenRefreshNeeded(id, task->originUrl.isEmpty() ? task->url : task->originUrl, task->fileName);
    log::warn("Tarea {} HTTP {} -> esperando refresco de enlace", static_cast<unsigned long long>(id), httpStatus);
}

void DownloadEngine::resetTaskData(quint64 id) {
    DownloadTask* task = mutableTask(id);
    Runtime* runtime = runtimeFor(id, false);
    if (task == nullptr) {
        return;
    }
    if (runtime != nullptr && runtime->io != nullptr) {
        delete runtime->io;
        runtime->io = nullptr;
    }
    QFile::remove(task->partPath());
    task->segments.clear();
    task->receivedBytes = 0;
    task->retries = 0;
    task->bytesPerSec = 0.0;
    if (m_db != nullptr && m_db->isOpen()) {
        m_db->saveSegments(id, task->segments, nullptr);
        m_db->updateProgress(id, task->state, 0, QString(), nullptr);
    }
}

void DownloadEngine::persistProgress(quint64 id) {
    DownloadTask* task = mutableTask(id);
    if (task == nullptr) {
        return;
    }
    if (m_db != nullptr && m_db->isOpen()) {
        m_db->updateProgress(id, task->state, task->receivedBytes, task->lastError, nullptr);
    }
}

void DownloadEngine::destroyRuntime(quint64 id) {
    auto it = m_runtimes.find(id);
    if (it == m_runtimes.end()) {
        return;
    }
    Runtime* runtime = it.value().get();
    if (runtime->refreshTimer != nullptr) {
        runtime->refreshTimer->stop();
        runtime->refreshTimer->deleteLater();
        runtime->refreshTimer = nullptr;
    }
    if (runtime->variantTimer != nullptr) {
        runtime->variantTimer->stop();
        runtime->variantTimer->deleteLater();
        runtime->variantTimer = nullptr;
    }
    if (runtime->io != nullptr) {
        delete runtime->io;
        runtime->io = nullptr;
    }
    m_runtimes.erase(it);
}

void DownloadEngine::finalizeDownload(quint64 id) {
    DownloadTask* task = mutableTask(id);
    Runtime* runtime = runtimeFor(id, false);
    if (task == nullptr) {
        destroyRuntime(id);
        return;
    }
    if (runtime != nullptr && !runtime->workers.isEmpty()) {
        return; // quedan hilos activos
    }

    task->receivedBytes = task->sumSegmentBytes();
    if (task->totalBytes > 0 && task->receivedBytes < task->totalBytes && !task->segments.isEmpty() &&
        task->segments.first().endByte >= 0) {
        failTask(id, QStringLiteral("Descarga incompleta (%1 de %2)")
                         .arg(humanSize(task->receivedBytes), humanSize(task->totalBytes)));
        return;
    }

    // 1) Cierra el descriptor antes de renombrar (obligatorio en Windows).
    destroyRuntime(id);

    const QString partPath = task->partPath();
    if (!QFileInfo::exists(partPath)) {
        task->state = DownloadState::Failed;
        task->lastError = QStringLiteral("No se encontró el archivo temporal");
        persistProgress(id);
        emit taskUpdated(id);
        emit toast(task->lastError, 2);
        pumpGuarded();
        return;
    }

    // 2) Verificacion de integridad (tamano exacto anunciado por el servidor).
    const qint64 finalSize = QFileInfo(partPath).size();
    if (task->totalBytes > 0 && finalSize != task->totalBytes) {
        task->state = DownloadState::Failed;
        task->lastError = QStringLiteral("Verificación fallida: %1 != %2")
                              .arg(humanSize(finalSize), humanSize(task->totalBytes));
        persistProgress(id);
        emit taskUpdated(id);
        emit toast(task->lastError, 2);
        pumpGuarded();
        return;
    }

    // 3) Renombra .part -> archivo final (evita pisar existentes).
    const QString target = uniqueTargetPath(task->savePath());
    QDir().mkpath(QFileInfo(target).absolutePath());
    bool renamed = QFile::rename(partPath, target);
    if (!renamed) {
        renamed = QFile::copy(partPath, target);
        if (renamed) {
            QFile::remove(partPath);
        }
    }
    if (!renamed) {
        task->state = DownloadState::Failed;
        task->lastError = QStringLiteral("No se pudo guardar el archivo en %1").arg(target);
        persistProgress(id);
        emit taskUpdated(id);
        emit toast(task->lastError, 2);
        pumpGuarded();
        return;
    }

    task->fileName = QFileInfo(target).fileName();
    task->state = DownloadState::Completed;
    task->lastError.clear();
    task->bytesPerSec = 0.0;
    task->receivedBytes = task->totalBytes > 0 ? task->totalBytes : finalSize;
    task->updatedAt = QDateTime::currentDateTime();
    for (SegmentInfo& segment : task->segments) {
        segment.state = SegmentState::Completed;
    }

    if (m_db != nullptr && m_db->isOpen()) {
        m_db->updateTask(*task, nullptr);
        m_db->saveSegments(id, task->segments, nullptr);
        m_db->updateProgress(id, task->state, task->receivedBytes, QString(), nullptr);
    }

    emit taskUpdated(id);
    emit statusChanged();
    emit toast(QStringLiteral("Descarga completada: %1").arg(task->fileName), 0);
    log::info("Tarea {} completada -> {}", static_cast<unsigned long long>(id), log::utf8(target));
    pumpGuarded();
}

// ---------------------------------------------------------------------------
// Cola global
// ---------------------------------------------------------------------------

void DownloadEngine::pumpGuarded() {
    QTimer::singleShot(0, this, &DownloadEngine::pumpQueue);
}

void DownloadEngine::pumpQueue() {
    if (m_pumping || m_shuttingDown || !m_initialized) {
        return;
    }
    m_pumping = true;

    const qint64 now = nowMs();
    int guard = 0;
    while (activeTaskCount() < m_settings.maxConcurrentTasks && guard++ < 64) {
        qsizetype candidate = -1;
        QDateTime oldest;
        for (qsizetype i = 0; i < m_tasks.size(); ++i) {
            const DownloadTask& task = m_tasks.at(i);
            if (task.state != DownloadState::Queued || m_runtimes.contains(task.id)) {
                continue;
            }
            if (candidate < 0 || task.createdAt < oldest) {
                candidate = i;
                oldest = task.createdAt;
            }
        }
        if (candidate < 0) {
            break;
        }
        const quint64 id = m_tasks.at(candidate).id;

        // Backoff de reintento pendiente: no se adelanta.
        Runtime* runtime = runtimeFor(id, false);
        if (runtime != nullptr && runtime->retryAtMs > now) {
            // Pone la tarea en espera hasta el siguiente intento.
            DownloadTask* task = mutableTask(id);
            if (task != nullptr && task->state == DownloadState::Queued) {
                const qint64 delay = runtime->retryAtMs - now;
                QTimer::singleShot(delay + 50, this, [this, id]() { pumpQueue(); });
            }
            break;
        }

        startTask(id);
        if (!m_runtimes.contains(id)) {
            // No arranco (limite, error o guardia): evita bucle infinito.
            DownloadTask* task = mutableTask(id);
            if (task == nullptr || task->state != DownloadState::Queued) {
                continue;
            }
            break;
        }
    }

    m_pumping = false;
}

// ---------------------------------------------------------------------------
// Bucle de telemetria (250 ms)
// ---------------------------------------------------------------------------

void DownloadEngine::tick() {
    if (!m_initialized || m_shuttingDown) {
        return;
    }
    const qint64 now = nowMs();
    bool anyUpdate = false;

    for (auto it = m_runtimes.begin(); it != m_runtimes.end(); ++it) {
        const quint64 id = it.key();
        Runtime* runtime = it.value().get();
        DownloadTask* task = mutableTask(id);
        if (task == nullptr || runtime == nullptr) {
            continue;
        }
        if (runtime->streamActive || runtime->workers.isEmpty()) {
            continue;
        }
        if (task->state != DownloadState::Downloading) {
            continue;
        }

        // Bytes actuales (los workers vistos actualizan su contador atomico).
        qint64 total = 0;
        for (SegmentInfo& segment : task->segments) {
            SegmentWorker* worker = runtime->workers.value(segment.index, nullptr);
            if (worker != nullptr) {
                segment.receivedBytes = worker->receivedBytes();
                if (segment.state != SegmentState::Rebalancing && segment.state != SegmentState::Error &&
                    segment.state != SegmentState::Completed) {
                    segment.state = SegmentState::Downloading;
                }
            }
            total += segment.receivedBytes;
        }

        const qint64 previousTotal = task->receivedBytes;
        Q_UNUSED(previousTotal)
        task->receivedBytes = total;

        const qint64 dtMs = runtime->lastTickMs > 0 ? (now - runtime->lastTickMs) : 250;
        if (dtMs > 0 && runtime->lastTickMs > 0) {
            const double instant = static_cast<double>(total - runtime->lastBytes) * 1000.0 / static_cast<double>(dtMs);
            runtime->speedEma = (runtime->speedEma <= 0.0) ? instant : (runtime->speedEma * 0.6 + instant * 0.4);
        }
        runtime->lastBytes = total;
        runtime->lastTickMs = now;
        task->bytesPerSec = runtime->speedEma < 0.0 ? 0.0 : runtime->speedEma;

        // Guardado periodico (1 s) del progreso.
        if (++runtime->ticksSinceSave >= 4) {
            runtime->ticksSinceSave = 0;
            if (runtime->lastSavedBytes != total) {
                runtime->lastSavedBytes = total;
                persistProgress(id);
                if (m_db != nullptr && m_db->isOpen()) {
                    m_db->saveSegments(id, task->segments, nullptr);
                }
            }
        }

        Q_UNUSED(previousTotal)
        emit taskUpdated(id);
        anyUpdate = true;
    }

    static qint64 lastStatusEmit = 0;
    if (anyUpdate && now - lastStatusEmit >= 2000) {
        lastStatusEmit = now;
        emit statusChanged();
    }
}

// ---------------------------------------------------------------------------
// Mulo HLS / DASH
// ---------------------------------------------------------------------------

void DownloadEngine::startStreamJob(quint64 id) {
    DownloadTask* task = mutableTask(id);
    Runtime* runtime = runtimeFor(id, true);
    if (task == nullptr || runtime == nullptr || m_stream == nullptr) {
        return;
    }
    if (runtime->streamActive) {
        return;
    }
    // Un solo flujo multimedia a la vez (el resto espera en cola).
    for (auto it = m_runtimes.constBegin(); it != m_runtimes.constEnd(); ++it) {
        if (it.key() != id && it.value()->streamActive) {
            task->state = DownloadState::Queued;
            persistProgress(id);
            emit taskUpdated(id);
            QTimer::singleShot(500, this, [this, id]() { pumpQueue(); });
            return;
        }
    }

    runtime->streamActive = true;
    runtime->pendingVariant = -1;
    m_streamJobId = id;
    task->state = DownloadState::Connecting;
    emit taskUpdated(id);
    emit statusChanged();
    m_stream->probe(task->url, task->referer, task->cookies, m_settings.proxy);
}

void DownloadEngine::onStreamProbeFinished(bool ok, const QVariantList& variants, const QString& error) {
    const quint64 id = m_streamJobId;
    DownloadTask* task = mutableTask(id);
    Runtime* runtime = runtimeFor(id, false);
    if (task == nullptr) {
        if (runtime != nullptr) {
            destroyRuntime(id);
        }
        return;
    }
    if (runtime == nullptr) {
        return;
    }

    if (!ok || variants.isEmpty()) {
        runtime->streamActive = false;
        m_streamJobId = 0;
        if (task->state == DownloadState::Paused) {
            // Detencion manual durante la sonda: no es un error.
            finalizeStop(id);
            return;
        }
        failTask(id, error.isEmpty() ? QStringLiteral("No se pudo leer el manifiesto multimedia") : error);
        return;
    }

    emit streamVariantsReady(id, variants, task->url);

    // Si nadie elige variante, se toma la de mejor calidad automaticamente.
    if (runtime->variantTimer == nullptr) {
        runtime->variantTimer = new QTimer(this);
        runtime->variantTimer->setSingleShot(true);
        runtime->variantTimer->setInterval(std::max(5, m_settings.streamChoiceTimeoutSec) * 1000);
        connect(runtime->variantTimer, &QTimer::timeout, this, [this, id]() {
            Runtime* rt = runtimeFor(id, false);
            if (rt != nullptr && rt->pendingVariant < 0 && rt->streamActive) {
                selectStreamVariant(id, 0); // la lista llega ordenada de mejor a peor
            }
        });
    }
    runtime->variantTimer->start();
    emit taskUpdated(id);
}

void DownloadEngine::selectStreamVariant(quint64 id, int variantIndex) {
    DownloadTask* task = mutableTask(id);
    Runtime* runtime = runtimeFor(id, false);
    if (task == nullptr || runtime == nullptr || m_stream == nullptr) {
        return;
    }
    if (!task->streamJob || runtime->pendingVariant >= 0) {
        return;
    }
    if (runtime->variantTimer != nullptr) {
        runtime->variantTimer->stop();
    }
    runtime->pendingVariant = variantIndex;
    runtime->streamActive = true;
    m_streamJobId = id;

    task->state = DownloadState::Downloading;
    task->updatedAt = QDateTime::currentDateTime();
    persistProgress(id);
    emit taskUpdated(id);
    emit statusChanged();

    QDir().mkpath(task->destDir);
    m_stream->download(id, task->url, variantIndex, task->partPath(), task->referer, task->cookies,
                       m_settings.proxy, m_settings.ffmpegPath, m_settings.ytDlpPath, m_settings.useYtDlp);
}

void DownloadEngine::onStreamProgress(qint64 received, qint64 total) {
    DownloadTask* task = mutableTask(m_streamJobId);
    if (task == nullptr) {
        return;
    }
    task->receivedBytes = received;
    if (total > 0) {
        task->totalBytes = total;
    }
    emit taskUpdated(task->id);
}

void DownloadEngine::onStreamFinished(bool ok, const QString& outputPath, const QString& error) {
    const quint64 id = m_streamJobId;
    DownloadTask* task = mutableTask(id);
    Runtime* runtime = runtimeFor(id, false);
    m_streamJobId = 0;

    if (task == nullptr) {
        if (runtime != nullptr) {
            destroyRuntime(id);
        }
        return;
    }
    if (runtime != nullptr) {
        runtime->streamActive = false;
        runtime->pendingVariant = -1;
    }

    const bool userStopped = task->state == DownloadState::Paused;

    if (!ok) {
        if (userStopped) {
            finalizeStop(id);
            emit toast(QStringLiteral("Descarga multimedia detenida"), 1);
            return;
        }
        failTask(id, error.isEmpty() ? QStringLiteral("Fallo en la descarga multimedia") : error);
        return;
    }

    if (task->state == DownloadState::Paused) {
        finalizeStop(id);
        return;
    }

    const QFileInfo info(outputPath);
    if (!info.exists()) {
        failTask(id, QStringLiteral("El archivo final no existe tras el ensamblado"));
        return;
    }

    task->fileName = info.fileName();
    task->category = categoryForFileName(task->fileName);
    task->state = DownloadState::Completed;
    task->lastError.clear();
    task->bytesPerSec = 0.0;
    if (task->totalBytes > 0) {
        task->receivedBytes = task->totalBytes;
    } else {
        task->receivedBytes = info.size();
        task->totalBytes = info.size();
    }
    task->updatedAt = QDateTime::currentDateTime();

    if (m_db != nullptr && m_db->isOpen()) {
        m_db->updateTask(*task, nullptr);
        m_db->updateProgress(id, task->state, task->receivedBytes, QString(), nullptr);
    }

    destroyRuntime(id);
    emit taskUpdated(id);
    emit statusChanged();
    emit toast(QStringLiteral("Descarga multimedia completada: %1").arg(task->fileName), 0);
    log::info("Flujo {} completado -> {}", static_cast<unsigned long long>(id), log::utf8(outputPath));
    pumpGuarded();
}

} // namespace sdm
