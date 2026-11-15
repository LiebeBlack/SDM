// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/database/DatabaseManager.cpp
// ============================================================================
#include "database/DatabaseManager.h"

#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <sqlite3.h>

namespace sdm {

namespace {

// ---------------------------------------------------------------------------
// Helpers de bindeo/lectura UTF-16 (QString <-> SQLite)
// ---------------------------------------------------------------------------

void bindText(sqlite3_stmt* stmt, int index, const QString& value) {
    // Todas las columnas TEXT del esquema son NOT NULL DEFAULT '': un QString
    // nulo se persiste como cadena vacia (no SQL NULL) para respetar el
    // esquema y la semantica "vacio = sin valor" (p. ej. updateUrl).
    const QString text = value.isNull() ? QStringLiteral("") : value;
    sqlite3_bind_text16(stmt, index, text.utf16(), -1, SQLITE_TRANSIENT);
}

void bindInt(sqlite3_stmt* stmt, int index, qint64 value) {
    sqlite3_bind_int64(stmt, index, value);
}

QString columnText(sqlite3_stmt* stmt, int index) {
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return QString();
    }
    const void* raw = sqlite3_column_text16(stmt, index);
    if (raw == nullptr) {
        return QString();
    }
    const int bytes = sqlite3_column_bytes16(stmt, index);
    if (bytes <= 0) {
        return QString();
    }
    return QString::fromUtf16(static_cast<const char16_t*>(raw), bytes / 2);
}

qint64 columnInt(sqlite3_stmt* stmt, int index) {
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return 0;
    }
    return sqlite3_column_int64(stmt, index);
}

/// RAII para transacciones (DELETE+INSERT atomicos de segmentos).
struct Transaction {
    explicit Transaction(sqlite3* db) : m_db(db) {
        char* err = nullptr;
        if (sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, &err) != SQLITE_OK) {
            if (err != nullptr) {
                sqlite3_free(err);
            }
            m_db = nullptr;
        }
    }
    ~Transaction() {
        if (m_db == nullptr) {
            return;
        }
        char* err = nullptr;
        if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, &err) != SQLITE_OK) {
            if (err != nullptr) {
                sqlite3_free(err);
            }
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    sqlite3* m_db;
};

/// Versión del esquema de base de datos (PRAGMA user_version).
/// Cualquier cambio de tablas/columnas debe añadir un paso en migrateSchema()
/// e incrementar este valor. Nunca se rebaja: una base de una versión más
/// reciente que el programa se rechaza con error claro.
constexpr int kSchemaVersion = 1;

const char* kSchemaSql = R"SQL(
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
PRAGMA foreign_keys=ON;

CREATE TABLE IF NOT EXISTS downloads (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    url             TEXT    NOT NULL,
    origin_url      TEXT    NOT NULL DEFAULT '',
    referer         TEXT    NOT NULL DEFAULT '',
    cookies         TEXT    NOT NULL DEFAULT '',
    user_agent      TEXT    NOT NULL DEFAULT '',
    dest_dir        TEXT    NOT NULL,
    file_name       TEXT    NOT NULL,
    mime_type       TEXT    NOT NULL DEFAULT '',
    category        INTEGER NOT NULL DEFAULT 0,
    state           INTEGER NOT NULL DEFAULT 0,
    total_bytes     INTEGER NOT NULL DEFAULT -1,
    received_bytes  INTEGER NOT NULL DEFAULT 0,
    thread_count    INTEGER NOT NULL DEFAULT 8,
    etag            TEXT    NOT NULL DEFAULT '',
    last_modified   TEXT    NOT NULL DEFAULT '',
    last_error      TEXT    NOT NULL DEFAULT '',
    retries         INTEGER NOT NULL DEFAULT 0,
    source          TEXT    NOT NULL DEFAULT 'manual',
    http_user       TEXT    NOT NULL DEFAULT '',
    http_pass       TEXT    NOT NULL DEFAULT '',
    extra_headers   TEXT    NOT NULL DEFAULT '',
    stream_job      INTEGER NOT NULL DEFAULT 0,
    created_at      INTEGER NOT NULL,
    updated_at      INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS segments (
    download_id     INTEGER NOT NULL,
    idx             INTEGER NOT NULL,
    start_byte      INTEGER NOT NULL,
    end_byte        INTEGER NOT NULL,
    received_bytes  INTEGER NOT NULL DEFAULT 0,
    state           INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (download_id, idx),
    FOREIGN KEY (download_id) REFERENCES downloads(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_downloads_state ON downloads(state);
CREATE INDEX IF NOT EXISTS idx_downloads_created ON downloads(created_at);
)SQL";

/// Serializa cabeceras extra como "clave=valor\n" (simple y legible).
QString serializeHeaders(const QMap<QString, QString>& headers) {
    QString out;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        out += it.key();
        out += QLatin1Char('=');
        out += it.value();
        out += QLatin1Char('\n');
    }
    return out;
}

QMap<QString, QString> deserializeHeaders(const QString& text) {
    QMap<QString, QString> headers;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.isEmpty()) {
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        headers.insert(line.left(eq), line.mid(eq + 1));
    }
    return headers;
}

} // namespace

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::fail(const QString& message, QString* error) {
    m_lastError = message;
    if (error != nullptr) {
        *error = message;
    }
    log::error("SQLite: {}", log::utf8(message));
    return false;
}

bool DatabaseManager::exec(const char* sql, QString* error) {
    if (m_db == nullptr) {
        return fail(QStringLiteral("Base de datos no abierta"), error);
    }
    char* errMsg = nullptr;
    const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        const QString message = errMsg != nullptr ? QString::fromUtf8(errMsg) : QStringLiteral("error %1").arg(rc);
        if (errMsg != nullptr) {
            sqlite3_free(errMsg);
        }
        return fail(message, error);
    }
    return true;
}

bool DatabaseManager::open(const QString& path, QString* error) {
    if (m_db != nullptr) {
        close();
    }
    if (path.isEmpty()) {
        return fail(QStringLiteral("Ruta de base de datos vacia"), error);
    }

    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    const QByteArray encoded = QFile::encodeName(path);
    const int rc = sqlite3_open_v2(encoded.constData(), &m_db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        const QString message = QStringLiteral("No se pudo abrir la base: %1").arg(QString::fromUtf8(sqlite3_errmsg(m_db)));
        sqlite3_close(m_db);
        m_db = nullptr;
        return fail(message, error);
    }
    m_path = path;

    if (!createSchema(error)) {
        close();
        return false;
    }
    log::info("Base de datos abierta: {}", log::utf8(path));
    return true;
}

bool DatabaseManager::createSchema(QString* error) {
    // Ejecuta el script sentencia a sentencia (PRAGMA no admite parametros).
    const QString script = QString::fromUtf8(kSchemaSql);
    const QStringList statements = script.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& raw : statements) {
        const QString statement = raw.trimmed();
        if (statement.isEmpty()) {
            continue;
        }
        if (!exec(statement.toUtf8().constData(), error)) {
            return false;
        }
    }
    return migrateSchema(error);
}

bool DatabaseManager::migrateSchema(QString* error) {
    // Lectura de la versión actual del esquema.
    int version = 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            version = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (version > kSchemaVersion) {
        return fail(QStringLiteral("La base de datos usa el esquema v%1 y este programa solo "
                                   "soporta hasta v%2. Actualice SDM o use una copia de la base.")
                        .arg(version)
                        .arg(kSchemaVersion),
                    error);
    }
    if (version == kSchemaVersion) {
        return true;
    }

    // --- Migraciones incrementales (versión -> versión + 1) ---------------
    // v0 -> v1: esquema inicial (CREATE TABLE IF NOT EXISTS ya ejecutado).
    // if (version < 2) { ... ALTER/CREATE adicionales ... }

    const QString pragma = QStringLiteral("PRAGMA user_version = %1;").arg(kSchemaVersion);
    if (!exec(pragma.toUtf8().constData(), error)) {
        return false;
    }
    log::info("Esquema de base de datos migrado a v{}", kSchemaVersion);
    return true;
}

void DatabaseManager::close() {
    if (m_db != nullptr) {
        sqlite3_exec(m_db, "PRAGMA wal_checkpoint(TRUNCATE);", nullptr, nullptr, nullptr);
        sqlite3_close(m_db);
        m_db = nullptr;
    }
    m_path.clear();
}

bool DatabaseManager::isOpen() const {
    return m_db != nullptr;
}

int DatabaseManager::schemaVersion() const {
    if (m_db == nullptr) {
        return 0;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "PRAGMA user_version;", -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

// ---------------------------------------------------------------------------
// Descargas
// ---------------------------------------------------------------------------

quint64 DatabaseManager::insertTask(const DownloadTask& task, QString* error) {
    if (m_db == nullptr) {
        fail(QStringLiteral("Base de datos no abierta"), error);
        return 0;
    }
    const char* sql =
        "INSERT INTO downloads (url, origin_url, referer, cookies, user_agent, dest_dir, file_name, mime_type,"
        " category, state, total_bytes, received_bytes, thread_count, etag, last_modified, last_error, retries,"
        " source, http_user, http_pass, extra_headers, stream_job, created_at, updated_at)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
        return 0;
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 created = task.createdAt.isValid() ? task.createdAt.toSecsSinceEpoch() : now;
    const qint64 updated = task.updatedAt.isValid() ? task.updatedAt.toSecsSinceEpoch() : now;

    int i = 1;
    bindText(stmt, i++, task.url);
    bindText(stmt, i++, task.originUrl);
    bindText(stmt, i++, task.referer);
    bindText(stmt, i++, task.cookies);
    bindText(stmt, i++, task.userAgent);
    bindText(stmt, i++, task.destDir);
    bindText(stmt, i++, task.fileName);
    bindText(stmt, i++, task.mimeType);
    bindInt(stmt, i++, static_cast<qint64>(task.category));
    bindInt(stmt, i++, static_cast<qint64>(task.state));
    bindInt(stmt, i++, task.totalBytes);
    bindInt(stmt, i++, task.receivedBytes);
    bindInt(stmt, i++, task.threadCount);
    bindText(stmt, i++, task.etag);
    bindText(stmt, i++, task.lastModified);
    bindText(stmt, i++, task.lastError);
    bindInt(stmt, i++, task.retries);
    bindText(stmt, i++, task.source);
    bindText(stmt, i++, task.httpUser);
    bindText(stmt, i++, task.httpPass);
    bindText(stmt, i++, serializeHeaders(task.extraHeaders));
    bindInt(stmt, i++, task.streamJob ? 1 : 0);
    bindInt(stmt, i++, created);
    bindInt(stmt, i++, updated);

    quint64 id = 0;
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    } else {
        id = static_cast<quint64>(sqlite3_last_insert_rowid(m_db));
    }
    sqlite3_finalize(stmt);
    return id;
}

bool DatabaseManager::updateTask(const DownloadTask& task, QString* error) {
    if (m_db == nullptr || task.id == 0) {
        return fail(QStringLiteral("updateTask invalido"), error);
    }
    const char* sql =
        "UPDATE downloads SET url=?, origin_url=?, referer=?, cookies=?, user_agent=?, dest_dir=?, file_name=?,"
        " mime_type=?, category=?, state=?, total_bytes=?, received_bytes=?, thread_count=?, etag=?,"
        " last_modified=?, last_error=?, retries=?, source=?, http_user=?, http_pass=?, extra_headers=?,"
        " stream_job=?, updated_at=? WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    }

    int i = 1;
    bindText(stmt, i++, task.url);
    bindText(stmt, i++, task.originUrl);
    bindText(stmt, i++, task.referer);
    bindText(stmt, i++, task.cookies);
    bindText(stmt, i++, task.userAgent);
    bindText(stmt, i++, task.destDir);
    bindText(stmt, i++, task.fileName);
    bindText(stmt, i++, task.mimeType);
    bindInt(stmt, i++, static_cast<qint64>(task.category));
    bindInt(stmt, i++, static_cast<qint64>(task.state));
    bindInt(stmt, i++, task.totalBytes);
    bindInt(stmt, i++, task.receivedBytes);
    bindInt(stmt, i++, task.threadCount);
    bindText(stmt, i++, task.etag);
    bindText(stmt, i++, task.lastModified);
    bindText(stmt, i++, task.lastError);
    bindInt(stmt, i++, task.retries);
    bindText(stmt, i++, task.source);
    bindText(stmt, i++, task.httpUser);
    bindText(stmt, i++, task.httpPass);
    bindText(stmt, i++, serializeHeaders(task.extraHeaders));
    bindInt(stmt, i++, task.streamJob ? 1 : 0);
    bindInt(stmt, i++, QDateTime::currentSecsSinceEpoch());
    bindInt(stmt, i++, static_cast<qint64>(task.id));

    bool ok = true;
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        ok = fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::updateUrl(quint64 id, const QString& url, const QString& cookies, const QString& referer,
                                QString* error) {
    if (m_db == nullptr) {
        return fail(QStringLiteral("Base de datos no abierta"), error);
    }
    const char* sql =
        "UPDATE downloads SET url=?, cookies=?, referer=CASE WHEN ?='' THEN referer ELSE ? END,"
        " updated_at=? WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    }
    int i = 1;
    bindText(stmt, i++, url);
    bindText(stmt, i++, cookies);
    bindText(stmt, i++, referer);   // CASE WHEN ?=''
    bindText(stmt, i++, referer);   // ELSE ?
    bindInt(stmt, i++, QDateTime::currentSecsSinceEpoch());  // updated_at
    bindInt(stmt, i++, static_cast<qint64>(id));            // WHERE id=?

    bool ok = true;
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        ok = fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::updateProgress(quint64 id, DownloadState state, qint64 receivedBytes,
                                     const QString& lastError, QString* error) {
    if (m_db == nullptr) {
        return fail(QStringLiteral("Base de datos no abierta"), error);
    }
    const char* sql =
        "UPDATE downloads SET state=?, received_bytes=?, last_error=?, updated_at=? WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    }
    int i = 1;
    bindInt(stmt, i++, static_cast<qint64>(state));
    bindInt(stmt, i++, receivedBytes);
    bindText(stmt, i++, lastError);
    bindInt(stmt, i++, QDateTime::currentSecsSinceEpoch());
    bindInt(stmt, i++, static_cast<qint64>(id));

    bool ok = true;
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        ok = fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::removeTask(quint64 id, QString* error) {
    if (m_db == nullptr) {
        return fail(QStringLiteral("Base de datos no abierta"), error);
    }
    const char* sql = "DELETE FROM downloads WHERE id=?;"; // segments cascade
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    }
    bindInt(stmt, 1, static_cast<qint64>(id));
    bool ok = true;
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        ok = fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::clearCompleted(QString* error) {
    if (m_db == nullptr) {
        return fail(QStringLiteral("Base de datos no abierta"), error);
    }
    return exec("DELETE FROM downloads WHERE state=4;", error); // 4 = Completed
}

QList<DownloadTask> DatabaseManager::loadAll(QString* error) {
    QList<DownloadTask> tasks;
    if (m_db == nullptr) {
        fail(QStringLiteral("Base de datos no abierta"), error);
        return tasks;
    }
    const char* sql =
        "SELECT id, url, origin_url, referer, cookies, user_agent, dest_dir, file_name, mime_type, category,"
        " state, total_bytes, received_bytes, thread_count, etag, last_modified, last_error, retries, source,"
        " http_user, http_pass, extra_headers, stream_job, created_at, updated_at"
        " FROM downloads ORDER BY created_at ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
        return tasks;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DownloadTask task;
        int i = 0;
        task.id = static_cast<quint64>(columnInt(stmt, i++));
        task.url = columnText(stmt, i++);
        task.originUrl = columnText(stmt, i++);
        task.referer = columnText(stmt, i++);
        task.cookies = columnText(stmt, i++);
        task.userAgent = columnText(stmt, i++);
        task.destDir = columnText(stmt, i++);
        task.fileName = columnText(stmt, i++);
        task.mimeType = columnText(stmt, i++);
        task.category = static_cast<FileCategory>(columnInt(stmt, i++));
        task.state = static_cast<DownloadState>(columnInt(stmt, i++));
        task.totalBytes = columnInt(stmt, i++);
        task.receivedBytes = columnInt(stmt, i++);
        task.threadCount = static_cast<int>(columnInt(stmt, i++));
        task.etag = columnText(stmt, i++);
        task.lastModified = columnText(stmt, i++);
        task.lastError = columnText(stmt, i++);
        task.retries = static_cast<int>(columnInt(stmt, i++));
        task.source = columnText(stmt, i++);
        task.httpUser = columnText(stmt, i++);
        task.httpPass = columnText(stmt, i++);
        task.extraHeaders = deserializeHeaders(columnText(stmt, i++));
        task.streamJob = columnInt(stmt, i++) != 0;
        task.createdAt = QDateTime::fromSecsSinceEpoch(columnInt(stmt, i++));
        task.updatedAt = QDateTime::fromSecsSinceEpoch(columnInt(stmt, i++));
        task.probed = task.totalBytes > 0;
        tasks.push_back(std::move(task));
    }
    sqlite3_finalize(stmt);

    // Carga masiva de segmentos en una sola consulta.
    for (DownloadTask& task : tasks) {
        task.segments = loadSegments(task.id, nullptr);
    }
    return tasks;
}

DownloadTask DatabaseManager::loadTask(quint64 id, QString* error) {
    const QList<DownloadTask> all = loadAll(error);
    for (const DownloadTask& task : all) {
        if (task.id == id) {
            return task;
        }
    }
    return DownloadTask{};
}

// ---------------------------------------------------------------------------
// Segmentos
// ---------------------------------------------------------------------------

bool DatabaseManager::saveSegments(quint64 id, const QList<SegmentInfo>& segments, QString* error) {
    if (m_db == nullptr) {
        return fail(QStringLiteral("Base de datos no abierta"), error);
    }

    const Transaction tx(m_db);

    const char* delSql = "DELETE FROM segments WHERE download_id=?;";
    sqlite3_stmt* delStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, delSql, -1, &delStmt, nullptr) != SQLITE_OK) {
        return fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    }
    bindInt(delStmt, 1, static_cast<qint64>(id));
    if (sqlite3_step(delStmt) != SQLITE_DONE) {
        sqlite3_finalize(delStmt);
        return fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    }
    sqlite3_finalize(delStmt);

    if (segments.isEmpty()) {
        return true;
    }

    const char* insSql =
        "INSERT INTO segments (download_id, idx, start_byte, end_byte, received_bytes, state)"
        " VALUES (?,?,?,?,?,?);";
    sqlite3_stmt* insStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, insSql, -1, &insStmt, nullptr) != SQLITE_OK) {
        return fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
    }
    for (const SegmentInfo& seg : segments) {
        int i = 1;
        bindInt(insStmt, i++, static_cast<qint64>(id));
        bindInt(insStmt, i++, seg.index);
        bindInt(insStmt, i++, seg.startByte);
        bindInt(insStmt, i++, seg.endByte);
        bindInt(insStmt, i++, seg.receivedBytes);
        bindInt(insStmt, i++, static_cast<qint64>(seg.state));
        if (sqlite3_step(insStmt) != SQLITE_DONE) {
            sqlite3_finalize(insStmt);
            return fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
        }
        sqlite3_reset(insStmt);
    }
    sqlite3_finalize(insStmt);
    return true;
}

QList<SegmentInfo> DatabaseManager::loadSegments(quint64 id, QString* error) {
    QList<SegmentInfo> segments;
    if (m_db == nullptr) {
        fail(QStringLiteral("Base de datos no abierta"), error);
        return segments;
    }
    const char* sql =
        "SELECT idx, start_byte, end_byte, received_bytes, state FROM segments"
        " WHERE download_id=? ORDER BY idx ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fail(QString::fromUtf8(sqlite3_errmsg(m_db)), error);
        return segments;
    }
    bindInt(stmt, 1, static_cast<qint64>(id));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SegmentInfo seg;
        int i = 0;
        seg.index = static_cast<int>(columnInt(stmt, i++));
        seg.startByte = columnInt(stmt, i++);
        seg.endByte = columnInt(stmt, i++);
        seg.receivedBytes = columnInt(stmt, i++);
        seg.state = static_cast<SegmentState>(columnInt(stmt, i++));
        segments.push_back(seg);
    }
    sqlite3_finalize(stmt);
    return segments;
}

// ---------------------------------------------------------------------------
// Estadisticas
// ---------------------------------------------------------------------------

int DatabaseManager::countInState(DownloadState state) {
    if (m_db == nullptr) {
        return 0;
    }
    const char* sql = "SELECT COUNT(*) FROM downloads WHERE state=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    bindInt(stmt, 1, static_cast<qint64>(state));
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<int>(columnInt(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return count;
}

int DatabaseManager::countAll() {
    if (m_db == nullptr) {
        return 0;
    }
    const char* sql = "SELECT COUNT(*) FROM downloads;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<int>(columnInt(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return count;
}

} // namespace sdm
