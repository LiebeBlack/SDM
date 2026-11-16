// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/Log.cpp - Implementacion del logger con rotacion de archivos.
// ============================================================================
#include "core/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

namespace sdm::log {
namespace {

constexpr qint64 kMaxLogBytes = 5 * 1024 * 1024; // 5 MB por archivo

QMutex g_mutex;
QFile* g_file = nullptr;
Level g_level = Level::Info;
QString g_dir;
bool g_toStderr = true;

const char* levelTag(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO ";
        case Level::Warn: return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "INFO ";
}

void rotateIfNeededLocked() {
    if (g_file == nullptr || !g_file->isOpen()) {
        return;
    }
    if (g_file->size() < kMaxLogBytes) {
        return;
    }
    g_file->close();
    const QString base = g_file->fileName();
    const QString rotated = base + QStringLiteral(".1");
    QFile::remove(rotated);
    QFile::rename(base, rotated);
    g_file->setFileName(base);
    g_file->open(QIODevice::Append | QIODevice::Text | QIODevice::Unbuffered);
}

} // namespace

QString redactSecrets(const QString& text) {
    if (text.isEmpty()) {
        return text;
    }
    // Parametros de consulta (token=..., cookies=...): son los que llevan mas
    // riesgo porque el protocolo y los logs de curl los incluyen tal cual.
    static const QRegularExpression kParams(
        QStringLiteral("(?i)\b(token|access_token|api_key|password|cookies|httpPass)"
                       "=([^&\s\"']+)"));
    // Campos JSON del protocolo: {"token":"...", "cookies":"..."}
    static const QRegularExpression kJsonFields(QStringLiteral(
        "(?i)\"(token|cookies|httpPass|password|access_token)\"\s*:\s*\"[^\"]*\""));
    // Cabeceras HTTP sensibles.
    static const QRegularExpression kHeaders(QStringLiteral(
        "(?i)\b(authorization|proxy-authorization|cookie|set-cookie)\s*:\s*[^\r\n]+"));
    // Credenciales embebidas: http://usuario:clave@host/...
    static const QRegularExpression kUrlCredentials(QStringLiteral("(://[^/:@\s]+):([^@\s]+)@"));

    QString safe = text;
    safe.replace(kParams, QStringLiteral("\1=***"));
    safe.replace(kJsonFields, QStringLiteral("\"\1\":\"***\""));
    safe.replace(kHeaders, QStringLiteral("\1: ***"));
    safe.replace(kUrlCredentials, QStringLiteral("\1:***@"));
    return safe;
}

bool init(const QString& dir) {
    QMutexLocker lock(&g_mutex);
    if (g_file != nullptr) {
        return g_file->isOpen();
    }
    g_dir = dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : dir;
    QDir().mkpath(g_dir);

    g_file = new QFile(g_dir + QStringLiteral("/sdm.log"));
    const bool ok = g_file->open(QIODevice::Append | QIODevice::Text | QIODevice::Unbuffered);
    if (!ok) {
        // Sin archivo: seguimos imprimiendo en stderr (diagnostico del sistema).
        g_toStderr = true;
    }
    return ok;
}

void shutdown() {
    QMutexLocker lock(&g_mutex);
    if (g_file != nullptr) {
        if (g_file->isOpen()) {
            g_file->flush();
            g_file->close();
        }
        delete g_file;
        g_file = nullptr;
    }
}

void setLevel(Level level) {
    QMutexLocker lock(&g_mutex);
    g_level = level;
}

Level level() {
    QMutexLocker lock(&g_mutex);
    return g_level;
}

void write(Level level, const QString& message) {
    QMutexLocker lock(&g_mutex);
    if (static_cast<int>(level) < static_cast<int>(g_level)) {
        return;
    }
    const QString stamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    // Nunca se escriben credenciales en claro (tokens, cookies, Authorization).
    const QString line =
        QStringLiteral("%1 [%2] %3").arg(stamp, levelTag(level), redactSecrets(message));

    if (g_file != nullptr && g_file->isOpen()) {
        rotateIfNeededLocked();
        g_file->write(line.toUtf8());
        g_file->write("\n", 1);
        g_file->flush();
    }
    if (g_toStderr) {
        const QByteArray utf8Line = line.toUtf8();
        std::fprintf(stderr, "%s\n", utf8Line.constData());
    }
}

} // namespace sdm::log
