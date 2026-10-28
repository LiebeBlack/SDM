// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/SegmentWorker.cpp
// ============================================================================
#include "core/SegmentWorker.h"

#include "core/BrowserEmulator.h"
#include "core/FileAllocator.h"
#include "core/Log.h"

#include <QRandomGenerator>
#include <QThread>
#include <QUrl>

#include <curl/curl.h>

#include <algorithm>
#include <cstring>
#include <string_view>

namespace sdm {

SegmentWorker::SegmentWorker(SegmentRequest request, FileAllocator* io, QObject* parent)
    : QObject(parent), m_request(std::move(request)), m_io(io) {
    m_received.store(m_request.initialReceived, std::memory_order_relaxed);
}

SegmentWorker::~SegmentWorker() = default;

void SegmentWorker::requestAbort() {
    m_abort.store(true, std::memory_order_relaxed);
}

qint64 SegmentWorker::receivedBytes() const noexcept {
    return m_received.load(std::memory_order_relaxed);
}

QString SegmentWorker::rangeHeaderValue() const {
    const qint64 start = m_request.startByte + m_received.load(std::memory_order_relaxed);
    if (m_request.endByte < 0) {
        return QStringLiteral("bytes=%1-").arg(start);
    }
    return QStringLiteral("bytes=%1-%2").arg(start).arg(m_request.endByte);
}

bool SegmentWorker::isRangeComplete() const {
    const qint64 total = m_request.rangeSize();
    if (total < 0) {
        return false;
    }
    return m_received.load(std::memory_order_relaxed) >= total;
}

bool SegmentWorker::appendToDisk(const char* data, qint64 len) {
    if (m_io == nullptr || data == nullptr || len <= 0) {
        return len <= 0;
    }
    qint64 written = 0;
    while (written < len) {
        const qint64 offset = m_request.startByte + m_received.load(std::memory_order_relaxed) + written;
        const qint64 n = m_io->writeAt(offset, data + written, len - written);
        if (n <= 0) {
            log::error("SegmentWorker: fallo de escritura task={} seg={} offset={}",
                       static_cast<long long>(m_request.task.id), m_request.index,
                       static_cast<long long>(offset));
            return false;
        }
        written += n;
    }
    m_received.fetch_add(written, std::memory_order_relaxed);
    return true;
}

size_t SegmentWorker::writeThunk(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* self = static_cast<SegmentWorker*>(userdata);
    if (self == nullptr || ptr == nullptr) {
        return 0;
    }
    if (self->m_abort.load(std::memory_order_relaxed)) {
        return 0; // Aborta la transferencia (CURLE_WRITE_ERROR controlado)
    }
    if (self->m_rangeIgnored) {
        // El servidor nos mando el archivo completo en vez del rango: no
        // escribimos nada para no corromper los offsets.
        return 0;
    }
    const size_t total = size * nmemb;
    if (!self->appendToDisk(ptr, static_cast<qint64>(total))) {
        return 0;
    }
    return total;
}

size_t SegmentWorker::headerThunk(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* self = static_cast<SegmentWorker*>(userdata);
    const size_t length = size * nitems;
    if (self == nullptr || buffer == nullptr) {
        return length;
    }
    const std::string_view line(buffer, length);

    if (line.size() > 5 && line.compare(0, 5, "HTTP/") == 0) {
        // "HTTP/1.1 206 Partial Content" | "HTTP/2 200" | "HTTP/3 403 ..."
        const size_t space = line.find(' ');
        if (space != std::string_view::npos && line.size() > space + 3) {
            int code = 0;
            for (size_t i = space + 1; i < space + 4 && i < line.size(); ++i) {
                const char c = line[i];
                if (c < '0' || c > '9') {
                    code = 0;
                    break;
                }
                code = code * 10 + (c - '0');
            }
            if (code > 0) {
                self->m_lastStatus = code;
                // Respuesta 200 a una peticion de rango con offset > 0:
                // el servidor ignora "Range" (spec: evaluacion de soporte).
                if (code == 200 && self->m_request.sendRange &&
                    (self->m_request.startByte + self->m_received.load(std::memory_order_relaxed)) > 0) {
                    self->m_rangeIgnored = true;
                }
            }
        }
    }
    return length; // Debe devolver exactamente el tamano o curl aborta
}

int SegmentWorker::xferinfoThunk(void* userdata, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                                 curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* self = static_cast<SegmentWorker*>(userdata);
    if (self == nullptr) {
        return 1;
    }
    if (self->m_abort.load(std::memory_order_relaxed) || self->m_rangeIgnored) {
        return 1; // CURLE_ABORTED_BY_CALLBACK
    }
    return 0;
}

bool SegmentWorker::performOnce(int* httpStatus, bool* rangeIgnored, QString* error) {
    *httpStatus = 0;
    *rangeIgnored = false;
    m_lastStatus = 0;
    m_rangeIgnored = false;

    const QUrl url(m_request.task.url);
    if (!url.isValid() || url.scheme().isEmpty()) {
        *error = QStringLiteral("URL invalida: %1").arg(m_request.task.url);
        return false;
    }
    QByteArray urlBytes = url.toEncoded(QUrl::FullyEncoded | QUrl::RemoveFragment);

    CURL* easy = curl_easy_init();
    if (easy == nullptr) {
        *error = QStringLiteral("curl_easy_init devolvio nullptr");
        return false;
    }

    // --- Cabeceras: conjunto del emulador (spec 6.2) + Range (spec 5.1) ---
    QList<QPair<QString, QString>> headers =
        BrowserEmulator::buildHeaders(m_request.task, QStringLiteral("media"));
    if (m_request.sendRange) {
        headers.append({QStringLiteral("Range"), rangeHeaderValue()});
    }
    curl_slist* slist = BrowserEmulator::buildSlist(headers);

    BrowserEmulator::applyCommonOptions(easy, m_request.task, BrowserEmulator::currentProfile(), m_request.proxy);
    curl_easy_setopt(easy, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, slist);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &SegmentWorker::writeThunk);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &SegmentWorker::headerThunk);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, this);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, &SegmentWorker::xferinfoThunk);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);   // 4xx/5xx sin escribir el cuerpo de error
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, static_cast<long>(m_request.connectTimeoutSec));
    // Si el servidor no soporta TLS 1.2+ el handshake falla (anti downgrade).

    const CURLcode rc = curl_easy_perform(easy);

    long code = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
    if (code > 0) {
        m_lastStatus = static_cast<int>(code);
    }
    *httpStatus = m_lastStatus;
    *rangeIgnored = m_rangeIgnored;

    if (rc != CURLE_OK) {
        if (m_abort.load(std::memory_order_relaxed)) {
            *error = QStringLiteral("abortado");
        } else if (rc == CURLE_HTTP_RETURNED_ERROR) {
            *error = QStringLiteral("HTTP %1").arg(m_lastStatus);
        } else if (rc == CURLE_OPERATION_TIMEDOUT) {
            *error = QStringLiteral("Tiempo de espera agotado (conexion lenta)");
        } else if (rc == CURLE_WRITE_ERROR) {
            *error = m_rangeIgnored ? QStringLiteral("El servidor ignora la cabecera Range")
                                    : QStringLiteral("Fallo de escritura en disco");
        } else if (rc == CURLE_SSL_CACERT_BADFILE || rc == CURLE_PEER_FAILED_VERIFICATION) {
            *error = QStringLiteral("Fallo de verificacion TLS (certificado)");
        } else {
            *error = QString::fromUtf8(curl_easy_strerror(rc));
        }
    } else {
        error->clear();
    }

    curl_slist_free_all(slist);
    curl_easy_cleanup(easy);

    return rc == CURLE_OK;
}

void SegmentWorker::start() {
    const QString tag = QStringLiteral("task=%1 seg=%2")
                            .arg(static_cast<qint64>(m_request.task.id))
                            .arg(m_request.index);
    log::info("SegmentWorker inicio {} range={}..{} recv={}", log::utf8(tag),
              static_cast<long long>(m_request.startByte), static_cast<long long>(m_request.endByte),
              static_cast<long long>(m_received.load()));

    int attempt = 0;
    QString lastError;

    while (true) {
        if (m_abort.load(std::memory_order_relaxed)) {
            emit finished(m_request.task.id, m_request.index, m_received.load(), 
                          static_cast<int>(SegmentResult::Aborted), m_lastStatus, QString());
            return;
        }

        int status = 0;
        bool ignored = false;
        QString error;
        const bool ok = performOnce(&status, &ignored, &error);

        // 1) Abort solicitado (pausa / re-segmentacion / cierre).
        if (m_abort.load(std::memory_order_relaxed)) {
            emit finished(m_request.task.id, m_request.index, m_received.load(),
                          static_cast<int>(SegmentResult::Aborted), status, QString());
            return;
        }

        // 2) El servidor ignora Range -> el motor degrada a 1 hilo.
        if (ignored) {
            log::warn("SegmentWorker {} el servidor ignora Range (HTTP {})", log::utf8(tag), status);
            emit finished(m_request.task.id, m_request.index, m_received.load(),
                          static_cast<int>(SegmentResult::RangeUnsupported), status,
                          QStringLiteral("El servidor no soporta rangos"));
            return;
        }

        // 3) Token caducado / autorizacion requerida (spec cap. 10).
        if (status == 403 || status == 401) {
            log::warn("SegmentWorker {} HTTP {} -> refresco de enlace", log::utf8(tag), status);
            emit finished(m_request.task.id, m_request.index, m_received.load(),
                          static_cast<int>(SegmentResult::Expired), status,
                          QStringLiteral("HTTP %1: enlace caducado").arg(status));
            return;
        }

        // 4) Rango completado (o descarga lineal terminada).
        if (isRangeComplete() || (m_request.endByte < 0 && ok)) {
            emit finished(m_request.task.id, m_request.index, m_received.load(),
                          static_cast<int>(SegmentResult::Completed), status, QString());
            return;
        }

        // 5) HTTP 416: rango invalido -> ¿estaba ya completo?
        if (status == 416) {
            if (isRangeComplete()) {
                emit finished(m_request.task.id, m_request.index, m_received.load(),
                              static_cast<int>(SegmentResult::Completed), status, QString());
            } else {
                emit finished(m_request.task.id, m_request.index, m_received.load(),
                              static_cast<int>(SegmentResult::Failed), status,
                              QStringLiteral("HTTP 416: rango no satisfacible"));
            }
            return;
        }

        // 6) Fallo transitorio -> reintento con backoff exponencial + jitter.
        lastError = error.isEmpty() ? QStringLiteral("conexion interrumpida") : error;
        ++attempt;
        if (attempt > m_request.maxRetries) {
            log::error("SegmentWorker {} agoto reintentos: {}", log::utf8(tag), log::utf8(lastError));
            emit finished(m_request.task.id, m_request.index, m_received.load(),
                          static_cast<int>(SegmentResult::Failed), status, lastError);
            return;
        }
        const int shift = std::min(attempt, 4);
        const int backoffMs = std::min(8000, 500 * (1 << shift));
        const int jitter = QRandomGenerator::global()->bounded(250);
        log::debug("SegmentWorker {} reintento {}/{} en {} ms ({})", log::utf8(tag), attempt,
                   m_request.maxRetries, backoffMs + jitter, log::utf8(lastError));
        QThread::msleep(static_cast<unsigned long>(backoffMs + jitter));
    }
}

} // namespace sdm
