// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/bridge/ProtocolHandler.cpp
// ============================================================================
#include "bridge/ProtocolHandler.h"

#include "bridge/WebSocketServer.h"
#include "core/DownloadEngine.h"
#include "core/Log.h"

#include <QDateTime>
#include <QUrl>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <limits>

namespace sdm {

using nlohmann::json;

namespace {

const char* kAppVersion = "1.0.0";

/// Convierte un QVariant (de QVariantMap/QVariantList) a JSON.
json variantToJson(const QVariant& value) {
    switch (value.userType()) {
        case QMetaType::Bool:
            return json(value.toBool());
        case QMetaType::Int:
        case QMetaType::Short:
        case QMetaType::Long:
        case QMetaType::LongLong:
            return json(static_cast<long long>(value.toLongLong()));
        case QMetaType::UInt:
        case QMetaType::ULong:
        case QMetaType::ULongLong:
        case QMetaType::UShort:
        case QMetaType::UChar:
            return json(static_cast<unsigned long long>(value.toULongLong()));
        case QMetaType::Double:
        case QMetaType::Float:
            return json(value.toDouble());
        case QMetaType::QString:
        case QMetaType::QByteArray:
            return json(value.toString().toStdString());
        case QMetaType::QVariantMap: {
            json out = json::object();
            const QVariantMap map = value.toMap();
            for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
                out[it.key().toStdString()] = variantToJson(it.value());
            }
            return out;
        }
        case QMetaType::QVariantList: {
            json out = json::array();
            const QVariantList list = value.toList();
            for (const QVariant& item : list) {
                out.push_back(variantToJson(item));
            }
            return out;
        }
        default:
            if (value.canConvert<QString>()) {
                return json(value.toString().toStdString());
            }
            return json();
    }
}

json taskJson(quint64 id) {
    const DownloadTask task = DownloadEngine::instance().task(id);
    if (task.id == 0) {
        return json();
    }
    return variantToJson(QVariant(DownloadEngine::snapshot(task)));
}

QString reqString(const json& object, const char* key) {
    if (!object.is_object()) {
        return {};
    }
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return {};
    }
    if (it->is_string()) {
        return QString::fromStdString(it->get<std::string>());
    }
    if (it->is_number_integer()) {
        return QString::number(it->get<long long>());
    }
    return {};
}

int reqInt(const json& object, const char* key, int fallback) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return fallback;
    }
    const qint64 value = it->get<qint64>();
    return value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()
               ? fallback
               : static_cast<int>(value);
}

qint64 reqInt64(const json& object, const char* key, qint64 fallback) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return fallback;
    }
    if (it->is_number_unsigned()) {
        const quint64 value = it->get<quint64>();
        return value > static_cast<quint64>(std::numeric_limits<qint64>::max())
                   ? fallback
                   : static_cast<qint64>(value);
    }
    return it->get<qint64>();
}

bool reqBool(const json& object, const char* key, bool fallback) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    if (it == object.end()) {
        return fallback;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    if (it->is_number_unsigned()) {
        return it->get<quint64>() != 0;
    }
    if (it->is_number_integer()) {
        return it->get<qint64>() != 0;
    }
    return fallback;
}

json makeObject(const QString& type, const json& payload = json::object(), const QString& ref = QString()) {
    json out;
    out["type"] = type.toStdString();
    if (!ref.isEmpty()) {
        out["ref"] = ref.toStdString();
    }
    if (payload.is_object()) {
        out["payload"] = payload;
    } else {
        out["payload"] = json::object();
    }
    return out;
}

QByteArray toBytes(const json& value) {
    const std::string dumped = value.dump();
    return QByteArray(dumped.data(), static_cast<qsizetype>(dumped.size()));
}

} // namespace

// ---------------------------------------------------------------------------
// Construccion de mensajes
// ---------------------------------------------------------------------------

QByteArray ProtocolHandler::makeMessage(const QString& type, const QVariantMap& payload) {
    json body = json::object();
    for (auto it = payload.constBegin(); it != payload.constEnd(); ++it) {
        body[it.key().toStdString()] = variantToJson(it.value());
    }
    return toBytes(makeObject(type, body));
}

QByteArray ProtocolHandler::errorResponse(const QString& message, const QString& ref) {
    json payload;
    payload["message"] = message.toStdString();
    return toBytes(makeObject(QStringLiteral("error"), payload, ref));
}

// ---------------------------------------------------------------------------
// Constructor + cableado con el motor
// ---------------------------------------------------------------------------

ProtocolHandler::ProtocolHandler(QObject* parent) : QObject(parent) {
    m_progressThrottle.start();
    m_statusThrottle.start();
    wireEngine();
}

void ProtocolHandler::wireEngine() {
    DownloadEngine& engine = DownloadEngine::instance();
    connect(&engine, &DownloadEngine::taskUpdated, this, &ProtocolHandler::onTaskUpdated);
    connect(&engine, &DownloadEngine::taskAdded, this, &ProtocolHandler::onTaskAdded);
    connect(&engine, &DownloadEngine::taskRemoved, this, &ProtocolHandler::onTaskRemoved);
    connect(&engine, &DownloadEngine::tasksReloaded, this, &ProtocolHandler::onTasksReloaded);
    connect(&engine, &DownloadEngine::toast, this, &ProtocolHandler::onToast);
    connect(&engine, &DownloadEngine::tokenRefreshNeeded, this, &ProtocolHandler::onTokenRefreshNeeded);
    connect(&engine, &DownloadEngine::streamVariantsReady, this, &ProtocolHandler::onStreamVariantsReady);
    connect(&engine, &DownloadEngine::statusChanged, this, &ProtocolHandler::onStatusChanged);
}

// ---------------------------------------------------------------------------
// Pushes asincronos
// ---------------------------------------------------------------------------

void ProtocolHandler::onTaskUpdated(quint64 id) {
    if (!m_progressThrottle.isValid() || m_progressThrottle.elapsed() >= 250) {
        m_progressThrottle.restart();
        json payload;
        payload["task"] = taskJson(id);
        if (!payload["task"].is_null()) {
            emit push(toBytes(makeObject(QStringLiteral("taskProgress"), payload)));
        }
    }
}

void ProtocolHandler::onTaskAdded(quint64 id) {
    json payload;
    payload["task"] = taskJson(id);
    emit push(toBytes(makeObject(QStringLiteral("taskAdded"), payload)));
}

void ProtocolHandler::onTaskRemoved(quint64 id) {
    json payload;
    payload["id"] = static_cast<unsigned long long>(id);
    emit push(toBytes(makeObject(QStringLiteral("taskRemoved"), payload)));
}

void ProtocolHandler::onTasksReloaded() {
    json tasks = json::array();
    for (const DownloadTask& task : DownloadEngine::instance().allTasks()) {
        tasks.push_back(variantToJson(QVariant(DownloadEngine::snapshot(task))));
    }
    json payload;
    payload["tasks"] = tasks;
    emit push(toBytes(makeObject(QStringLiteral("tasks"), payload)));
}

void ProtocolHandler::onToast(const QString& message, int level) {
    json payload;
    payload["message"] = message.toStdString();
    payload["level"] = level;
    emit push(toBytes(makeObject(QStringLiteral("toast"), payload)));
}

void ProtocolHandler::onTokenRefreshNeeded(quint64 id, const QString& originUrl, const QString& fileName) {
    // Spec cap. 10: la extension abre el origen y devuelve el enlace nuevo.
    json payload;
    payload["taskId"] = static_cast<unsigned long long>(id);
    payload["originUrl"] = originUrl.toStdString();
    payload["fileName"] = fileName.toStdString();
    emit push(toBytes(makeObject(QStringLiteral("refreshUrl"), payload)));
}

void ProtocolHandler::onStreamVariantsReady(quint64 id, const QVariantList& variants,
                                            const QString& manifestUrl) {
    json payload;
    payload["taskId"] = static_cast<unsigned long long>(id);
    payload["manifestUrl"] = manifestUrl.toStdString();
    payload["variants"] = variantToJson(QVariant(variants));
    emit push(toBytes(makeObject(QStringLiteral("streamVariants"), payload)));
}

void ProtocolHandler::onStatusChanged() {
    if (!m_statusThrottle.isValid() || m_statusThrottle.elapsed() >= 2000) {
        m_statusThrottle.restart();
        DownloadEngine& engine = DownloadEngine::instance();
        json payload;
        payload["active"] = engine.activeTaskCount();
        payload["queued"] = engine.queuedTaskCount();
        payload["total"] = engine.taskCount();
        payload["speed"] = engine.globalSpeed();
        emit push(toBytes(makeObject(QStringLiteral("status"), payload)));
    }
}

// ---------------------------------------------------------------------------
// Despacho de mensajes entrantes
// ---------------------------------------------------------------------------

QByteArray ProtocolHandler::process(const QByteArray& raw) {
    constexpr qsizetype kMaxProtocolPayloadBytes = 1024 * 1024;
    if (raw.isEmpty() || raw.size() > kMaxProtocolPayloadBytes) {
        return errorResponse(QStringLiteral("Mensaje vacío o demasiado grande"));
    }
    const json parsed = json::parse(raw.constData(), raw.constData() + raw.size(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return errorResponse(QStringLiteral("Mensaje JSON invalido"));
    }

    const json emptyPayload = json::object();
    const json& payload = parsed.contains("payload") && parsed["payload"].is_object() ? parsed["payload"]
                                                                                      : emptyPayload;
    const QString type = reqString(parsed, "type");
    const QString ref = reqString(parsed, "ref");
    if (type.isEmpty() || type.size() > 64 || ref.size() > 256) {
        return errorResponse(QStringLiteral("Tipo o referencia inválidos"));
    }

    DownloadEngine& engine = DownloadEngine::instance();
    auto ack = [&](const char* replyType, json extra = json::object()) {
        json body = extra.is_object() ? extra : json::object();
        body["ok"] = true;
        return toBytes(makeObject(QString::fromLatin1(replyType), body, ref));
    };

    if (type == QLatin1String("hello")) {
        const QString token = reqString(payload, "token");
        if (token.size() < 32 || token.size() > 256 || token != m_bridgeToken) {
            return errorResponse(QStringLiteral("Token de saludo inválido"), ref);
        }
        json body;
        body["ok"] = true;
        body["version"] = kAppVersion;
        body["app"] = "S-Download Manager";
        return toBytes(makeObject(QStringLiteral("helloAck"), body, ref));
    }

    if (type == QLatin1String("ping")) {
        json body;
        body["ts"] = static_cast<long long>(QDateTime::currentMSecsSinceEpoch());
        return toBytes(makeObject(QStringLiteral("pong"), body, ref));
    }

    if (type == QLatin1String("getBridgeInfo")) {
        json body;
        body["version"] = kAppVersion;
        body["total"] = engine.taskCount();
        return toBytes(makeObject(QStringLiteral("bridgeInfo"), body, ref));
    }

    if (type == QLatin1String("getTasks")) {
        json tasks = json::array();
        for (const DownloadTask& task : engine.allTasks()) {
            tasks.push_back(variantToJson(QVariant(DownloadEngine::snapshot(task))));
        }
        json body;
        body["tasks"] = tasks;
        return toBytes(makeObject(QStringLiteral("tasks"), body, ref));
    }

    if (type == QLatin1String("addDownload")) {
        NewTaskRequest request;
        request.url = reqString(payload, "url");
        request.referer = reqString(payload, "referer");
        request.cookies = reqString(payload, "cookies");
        request.userAgent = reqString(payload, "userAgent");
        request.fileName = reqString(payload, "fileName");
        request.destDir = reqString(payload, "destDir");
        request.originUrl = reqString(payload, "pageUrl");
        request.httpUser = reqString(payload, "httpUser");
        request.httpPass = reqString(payload, "httpPass");
        request.source = QStringLiteral("browser");
        const int threads = reqInt(payload, "threads", 0);
        if (threads > 0) {
            request.threadCount = threads;
        }
        request.autoStart = reqBool(payload, "autoStart", true);

        const quint64 id = engine.addTask(request);
        if (id == 0) {
            return errorResponse(QStringLiteral("No se pudo crear la tarea (URL invalida)"), ref);
        }
        json body;
        body["id"] = static_cast<unsigned long long>(id);
        body["task"] = taskJson(id);
        return toBytes(makeObject(QStringLiteral("taskAdded"), body, ref));
    }

    if (type == QLatin1String("startStream")) {
        const QString manifestUrl = reqString(payload, "url");
        const QUrl parsedUrl(manifestUrl);
        const QString scheme = parsedUrl.scheme().toLower();
        if (!parsedUrl.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))) {
            return errorResponse(QStringLiteral("URL HTTP/HTTPS del manifiesto no válida"), ref);
        }
        const quint64 id = engine.addStreamTask(manifestUrl, reqString(payload, "destDir"),
                                                reqString(payload, "referer"), reqString(payload, "cookies"),
                                                reqString(payload, "fileName"));
        if (id == 0) {
            return errorResponse(QStringLiteral("No se pudo crear la tarea multimedia"), ref);
        }
        json body;
        body["id"] = static_cast<unsigned long long>(id);
        return toBytes(makeObject(QStringLiteral("streamStarted"), body, ref));
    }

    if (type == QLatin1String("selectStreamVariant")) {
        const quint64 id = static_cast<quint64>(reqInt64(payload, "id", 0));
        if (!engine.hasTask(id)) {
            return errorResponse(QStringLiteral("Tarea inexistente"), ref);
        }
        engine.selectStreamVariant(id, reqInt(payload, "variant", 0));
        return ack("ack");
    }

    if (type == QLatin1String("pause") || type == QLatin1String("stop")) {
        const quint64 id = static_cast<quint64>(reqInt64(payload, "id", 0));
        if (!engine.hasTask(id)) {
            return errorResponse(QStringLiteral("Tarea inexistente"), ref);
        }
        engine.pauseTask(id);
        return ack("ack");
    }

    if (type == QLatin1String("resume") || type == QLatin1String("start")) {
        const quint64 id = static_cast<quint64>(reqInt64(payload, "id", 0));
        if (!engine.hasTask(id)) {
            return errorResponse(QStringLiteral("Tarea inexistente"), ref);
        }
        engine.startTask(id);
        return ack("ack");
    }

    if (type == QLatin1String("retry")) {
        const quint64 id = static_cast<quint64>(reqInt64(payload, "id", 0));
        if (!engine.hasTask(id)) {
            return errorResponse(QStringLiteral("Tarea inexistente"), ref);
        }
        engine.retryTask(id);
        return ack("ack");
    }

    if (type == QLatin1String("cancel")) {
        const quint64 id = static_cast<quint64>(reqInt64(payload, "id", 0));
        if (!engine.hasTask(id)) {
            return errorResponse(QStringLiteral("Tarea inexistente"), ref);
        }
        engine.removeTask(id, reqBool(payload, "deleteFile", false));
        return ack("ack");
    }

    if (type == QLatin1String("setThreads")) {
        const quint64 id = static_cast<quint64>(reqInt64(payload, "id", 0));
        if (!engine.hasTask(id)) {
            return errorResponse(QStringLiteral("Tarea inexistente"), ref);
        }
        engine.setThreadCount(id, reqInt(payload, "threads", kDefaultThreads));
        return ack("ack");
    }

    if (type == QLatin1String("urlUpdated")) {
        // Respuesta al refresco de enlace (spec cap. 10).
        const quint64 id = static_cast<quint64>(reqInt64(payload, "taskId", 0));
        const QString url = reqString(payload, "url");
        if (!engine.hasTask(id) || url.isEmpty()) {
            return errorResponse(QStringLiteral("Refresco de enlace invalido"), ref);
        }
        engine.updateTaskUrl(id, url, reqString(payload, "cookies"), reqString(payload, "referer"));
        return ack("ack");
    }

    if (type == QLatin1String("clearCompleted")) {
        engine.clearCompleted();
        return ack("ack");
    }

    return errorResponse(QStringLiteral("Tipo de mensaje desconocido: %1").arg(type), ref);
}

} // namespace sdm
