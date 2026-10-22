// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/StreamDownloader.cpp
// ============================================================================
#include "core/StreamDownloader.h"

#include "core/BrowserEmulator.h"
#include "core/Log.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>
#include <QXmlStreamReader>

#include <curl/curl.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sdm {

namespace {

// ---------------------------------------------------------------------------
// Helpers de parseo
// ---------------------------------------------------------------------------

QHash<QString, QString> parseAttrList(const QString& text) {
    QHash<QString, QString> out;
    int i = 0;
    const int n = text.size();
    while (i < n) {
        const int eq = text.indexOf(QLatin1Char('='), i);
        if (eq < 0) {
            break;
        }
        const QString key = text.mid(i, eq - i).trimmed().toUpper();
        i = eq + 1;
        QString value;
        if (i < n && text.at(i) == QLatin1Char('"')) {
            const int end = text.indexOf(QLatin1Char('"'), i + 1);
            value = end < 0 ? text.mid(i + 1) : text.mid(i + 1, end - i - 1);
            i = end < 0 ? n : end + 1;
        } else {
            const int comma = text.indexOf(QLatin1Char(','), i);
            const int end = comma < 0 ? n : comma;
            value = text.mid(i, end - i).trimmed();
            i = end;
        }
        if (!key.isEmpty()) {
            out.insert(key, value);
        }
        while (i < n && (text.at(i) == QLatin1Char(',') || text.at(i) == QLatin1Char(' '))) {
            ++i;
        }
    }
    return out;
}

/// Duracion ISO-8601 ("PT1H2M30S") -> segundos.
double parseIsoDuration(const QString& text) {
    static const QRegularExpression re(QStringLiteral(R"((\d+(?:\.\d+)?)([hHmMsS])))"));
    double seconds = 0.0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const double value = match.captured(1).toDouble();
        const QChar unit = match.captured(2).at(0).toLower();
        if (unit == QLatin1Char('h')) {
            seconds += value * 3600.0;
        } else if (unit == QLatin1Char('m')) {
            seconds += value * 60.0;
        } else if (unit == QLatin1Char('s')) {
            seconds += value;
        }
    }
    return seconds;
}

/// Sustituye $RepresentationID$, $Bandwidth$, $Number$, $Time$ (y $$).
QString applyDashTemplate(QString text, const QString& repId, qint64 bandwidth, qint64 number, qint64 time) {
    text.replace(QStringLiteral("$$"), QStringLiteral("\x01"));
    static const QRegularExpression re(
        QStringLiteral(R"(\$(RepresentationID|Bandwidth|Number|Time)(%0(\d+)d)?\$)"));
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    QString result;
    qsizetype last = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += text.mid(last, match.capturedStart() - last);
        const QString name = match.captured(1);
        const QString widthText = match.captured(3);
        QString value;
        if (name == QLatin1String("RepresentationID")) {
            value = repId;
        } else if (name == QLatin1String("Bandwidth")) {
            value = QString::number(bandwidth);
        } else if (name == QLatin1String("Number")) {
            value = QString::number(number);
        } else {
            value = QString::number(time);
        }
        if (!widthText.isEmpty()) {
            bool okWidth = false;
            const int width = widthText.toInt(&okWidth);
            if (okWidth && width > value.size()) {
                value = value.rightJustified(width, QLatin1Char('0'));
            }
        }
        result += value;
        last = match.capturedEnd();
    }
    result += text.mid(last);
    result.replace(QStringLiteral("\x01"), QStringLiteral("$"));
    return result;
}

// ---------------------------------------------------------------------------
// Transferencia HTTP (libcurl)
// ---------------------------------------------------------------------------

struct MemorySink {
    QByteArray* data = nullptr;
    qint64 maxBytes = 0;
    std::atomic<bool>* abortFlag = nullptr;
    bool tooLarge = false;
};

size_t memoryWriteThunk(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* sink = static_cast<MemorySink*>(userdata);
    if (sink == nullptr || sink->data == nullptr) {
        return 0;
    }
    if (sink->abortFlag != nullptr && sink->abortFlag->load(std::memory_order_relaxed)) {
        return 0;
    }
    const size_t total = size * nmemb;
    if (sink->maxBytes > 0 && sink->data->size() + static_cast<qint64>(total) > sink->maxBytes) {
        sink->tooLarge = true;
        return 0; // corta: el recurso es demasiado grande para ser un manifiesto
    }
    sink->data->append(ptr, static_cast<qsizetype>(total));
    return total;
}

struct FileSink {
    QFile file;
    qint64 written = 0;
    std::atomic<bool>* abortFlag = nullptr;
    bool failed = false;
};

size_t fileWriteThunk(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* sink = static_cast<FileSink*>(userdata);
    if (sink == nullptr) {
        return 0;
    }
    if (sink->abortFlag != nullptr && sink->abortFlag->load(std::memory_order_relaxed)) {
        return 0;
    }
    const size_t total = size * nmemb;
    const qint64 written = sink->file.write(ptr, static_cast<qint64>(total));
    if (written < 0) {
        sink->failed = true;
        return 0;
    }
    sink->written += written;
    return static_cast<size_t>(written);
}

int abortXferThunk(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* flag = static_cast<std::atomic<bool>*>(userdata);
    if (flag != nullptr && flag->load(std::memory_order_relaxed)) {
        return 1;
    }
    return 0;
}

DownloadTask mediaTask(const QString& url, const QString& referer, const QString& cookies) {
    DownloadTask task;
    task.url = url;
    task.referer = referer;
    task.cookies = cookies;
    task.fileName = fileNameFromUrl(url);
    return task;
}

} // namespace

QVariantMap StreamVariant::toVariantMap() const {
    QVariantMap map;
    map.insert(QStringLiteral("label"), label);
    map.insert(QStringLiteral("resolution"), resolution);
    map.insert(QStringLiteral("bandwidth"), bandwidth);
    map.insert(QStringLiteral("bandwidthText"),
               bandwidth > 0 ? QStringLiteral("%1 kbps").arg(bandwidth / 1000) : QStringLiteral("--"));
    map.insert(QStringLiteral("width"), width);
    map.insert(QStringLiteral("height"), height);
    map.insert(QStringLiteral("codecs"), codecs);
    map.insert(QStringLiteral("mimeType"), mimeType);
    map.insert(QStringLiteral("url"), url);
    map.insert(QStringLiteral("hasSeparateAudio"), hasSeparateAudio());
    return map;
}

// ---------------------------------------------------------------------------
// Ciclo de vida
// ---------------------------------------------------------------------------

StreamDownloader::StreamDownloader(QObject* parent) : QObject(parent) {
}

StreamDownloader::~StreamDownloader() {
    abort();
    joinThread();
}

void StreamDownloader::abort() {
    m_abort.store(true, std::memory_order_relaxed);
}

bool StreamDownloader::isBusy() const {
    return m_busy.load(std::memory_order_relaxed);
}

void StreamDownloader::joinThread() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

// ---------------------------------------------------------------------------
// URL resolution
// ---------------------------------------------------------------------------

QString StreamDownloader::resolveUrl(const QString& base, const QString& relative) {
    const QString trimmed = relative.trimmed();
    if (trimmed.isEmpty()) {
        return base;
    }
    const QUrl relativeUrl(trimmed);
    if (relativeUrl.isValid() && !relativeUrl.scheme().isEmpty()) {
        return relativeUrl.toString();
    }
    const QUrl baseUrl(base);
    return baseUrl.resolved(relativeUrl).toString();
}

// ---------------------------------------------------------------------------
// Parser HLS
// ---------------------------------------------------------------------------

QList<StreamVariant> StreamDownloader::parseMasterPlaylist(const QString& text, const QString& baseUrl) {
    QList<StreamVariant> variants;
    if (!text.startsWith(QLatin1String("#EXTM3U"))) {
        return variants;
    }

    const QStringList lines = text.split(QLatin1Char('\n'));
    StreamVariant pending;
    bool hasPending = false;

    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QLatin1String("#EXT-X-STREAM-INF:"))) {
            const QHash<QString, QString> attrs = parseAttrList(line.mid(18));
            pending = StreamVariant{};
            pending.bandwidth = attrs.value(QStringLiteral("BANDWIDTH")).toInt();
            pending.codecs = attrs.value(QStringLiteral("CODECS"));
            const QString resolution = attrs.value(QStringLiteral("RESOLUTION"));
            const QStringList parts = resolution.split(QLatin1Char('x'));
            if (parts.size() == 2) {
                pending.width = parts.at(0).toInt();
                pending.height = parts.at(1).toInt();
                pending.resolution = resolution;
                pending.label = QStringLiteral("%1p").arg(pending.height);
            } else if (pending.bandwidth > 0) {
                pending.label = QStringLiteral("%1 kbps").arg(pending.bandwidth / 1000);
            } else {
                pending.label = QStringLiteral("Variante");
            }
            hasPending = true;
        } else if (line.startsWith(QLatin1String("#EXT-X-I-FRAME-STREAM-INF:"))) {
            hasPending = false; // los I-FRAME no son descargables como video
        } else if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))) {
            if (hasPending && !pending.url.isEmpty()) {
                variants.append(pending);
                hasPending = false;
            } else if (hasPending) {
                pending.url = resolveUrl(baseUrl, line);
                pending.isDash = false;
                variants.append(pending);
                hasPending = false;
            }
        }
    }

    std::sort(variants.begin(), variants.end(), [](const StreamVariant& a, const StreamVariant& b) {
        if (a.height != b.height) {
            return a.height > b.height;
        }
        return a.bandwidth > b.bandwidth;
    });
    return variants;
}

bool StreamDownloader::parseMediaPlaylist(const QString& text, const QString& baseUrl, QStringList* segments,
                                          QString* initSegment, QString* error) {
    if (segments != nullptr) {
        segments->clear();
    }
    if (initSegment != nullptr) {
        initSegment->clear();
    }
    auto fail = [&](const QString& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    if (!text.startsWith(QLatin1String("#EXTM3U"))) {
        return fail(QStringLiteral("No es un playlist HLS válido"));
    }

    bool endList = false;
    bool expectUri = false;
    const QStringList lines = text.split(QLatin1Char('\n'));

    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1String("#EXT-X-MAP:"))) {
            const QHash<QString, QString> attrs = parseAttrList(line.mid(11));
            const QString uri = attrs.value(QStringLiteral("URI"));
            if (!uri.isEmpty() && initSegment != nullptr) {
                *initSegment = resolveUrl(baseUrl, uri);
            }
        } else if (line.startsWith(QLatin1String("#EXT-X-BYTERANGE"))) {
            return fail(QStringLiteral("Segmentación por rangos (BYTERANGE) no soportada; usa el modo yt-dlp"));
        } else if (line.startsWith(QLatin1String("#EXT-X-KEY:"))) {
            const QHash<QString, QString> attrs = parseAttrList(line.mid(11));
            const QString method = attrs.value(QStringLiteral("METHOD"), QStringLiteral("NONE")).toUpper();
            if (method != QLatin1String("NONE")) {
                return fail(QStringLiteral("Contenido cifrado (%1) no soportado; usa el modo yt-dlp").arg(method));
            }
        } else if (line.startsWith(QLatin1String("#EXTINF:"))) {
            expectUri = true;
        } else if (line.startsWith(QLatin1String("#EXT-X-ENDLIST"))) {
            endList = true;
        } else if (line.startsWith(QLatin1Char('#'))) {
            continue;
        } else if (segments != nullptr) {
            segments->append(resolveUrl(baseUrl, line));
            expectUri = false;
        }
        Q_UNUSED(expectUri)
    }

    if (segments == nullptr || segments->isEmpty()) {
        return fail(QStringLiteral("El playlist HLS no contiene segmentos"));
    }
    if (!endList) {
        return fail(QStringLiteral("Transmisión en directo no soportada (falta #EXT-X-ENDLIST)"));
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Parser DASH (MPD)
// ---------------------------------------------------------------------------

QList<StreamVariant> StreamDownloader::parseDashManifest(const QString& xml, const QString& baseUrl,
                                                         QString* error) {
    QList<StreamVariant> out;
    auto fail = [&](const QString& message) {
        if (error != nullptr) {
            *error = message;
        }
        return out;
    };

    if (!xml.contains(QStringLiteral("<MPD"))) {
        return fail(QStringLiteral("No es un manifiesto DASH válido"));
    }

    struct Track {
        QString id;
        QString mime;
        QString codecs;
        int bandwidth = 0;
        int width = 0;
        int height = 0;
        // Fuentes de segmentos (solo una activa por representacion):
        QStringList segmentListUrls;
        QString segmentListInit;
        QString singleFile;
        QString tmplMedia;
        QString tmplInit;
        qint64 tmplDuration = 0;
        qint64 tmplTimescale = 1;
        qint64 tmplStartNumber = 1;
        bool tmplHasTimeline = false;
        QStringList segments; // resuelto
        QString init;         // resuelto
    };

    QXmlStreamReader reader(xml);
    double periodDuration = -1.0;
    QString mpdBase = baseUrl;
    QString periodBase = baseUrl;
    QString setBase = baseUrl;
    QString repBase = baseUrl;

    bool inRepresentation = false;
    bool inAdaptation = false;
    bool inPeriod = false;

    Track current;
    Track setTrack;         // fuentes de segmentos declaradas a nivel AdaptationSet
    QString setMime;        // mimeType del AdaptationSet (heredado)
    bool setHasBase = false;
    QList<Track> videoTracks;
    QList<Track> audioTracks;

    // Aplica las fuentes de segmentos de `track` y devuelve true si hay datos.
    auto finalizeTrack = [&](Track& track) {
        if (!track.segments.isEmpty()) {
            return true;
        }
        if (!track.segmentListUrls.isEmpty()) {
            track.segments = track.segmentListUrls;
            track.init = track.segmentListInit;
            return true;
        }
        if (!track.singleFile.isEmpty()) {
            track.segments = QStringList{track.singleFile};
            return true;
        }
        if (!track.tmplMedia.isEmpty()) {
            if (track.tmplHasTimeline) {
                return false; // SegmentTimeline: lo resuelve yt-dlp
            }
            if (track.tmplDuration <= 0 || track.tmplTimescale <= 0 || periodDuration <= 0.0) {
                return false;
            }
            const double segmentSeconds = static_cast<double>(track.tmplDuration) / static_cast<double>(track.tmplTimescale);
            if (segmentSeconds <= 0.0) {
                return false;
            }
            const int count = static_cast<int>(std::ceil(periodDuration / segmentSeconds));
            if (count <= 0 || count > 100000) {
                return false;
            }
            if (!track.tmplInit.isEmpty()) {
                track.init = resolveUrl(repBase, applyDashTemplate(track.tmplInit, track.id, track.bandwidth, 0, 0));
            }
            for (int i = 0; i < count; ++i) {
                const qint64 number = track.tmplStartNumber + i;
                const QString url = resolveUrl(
                    repBase, applyDashTemplate(track.tmplMedia, track.id, track.bandwidth, number, number));
                track.segments.append(url);
            }
            return !track.segments.isEmpty();
        }
        return false;
    };

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = reader.name().toString();
            if (name == QLatin1String("MPD")) {
                const QString durationAttr = reader.attributes().value(QLatin1String("mediaPresentationDuration")).toString();
                periodDuration = parseIsoDuration(durationAttr);
            } else if (name == QLatin1String("Period")) {
                inPeriod = true;
                periodBase = mpdBase;
                const QString durationAttr = reader.attributes().value(QLatin1String("duration")).toString();
                if (!durationAttr.isEmpty()) {
                    periodDuration = parseIsoDuration(durationAttr);
                }
            } else if (name == QLatin1String("AdaptationSet")) {
                inAdaptation = true;
                setBase = periodBase;
                setTrack = Track{};
                setHasBase = false;
                setMime = reader.attributes().value(QLatin1String("mimeType")).toString();
            } else if (name == QLatin1String("Representation")) {
                // Finaliza la representacion anterior si la habia.
                if (inRepresentation && !current.segments.isEmpty()) {
                    (current.mime.startsWith(QLatin1String("audio")) || current.width == 0 ? audioTracks : videoTracks)
                        .append(current);
                }
                inRepresentation = true;
                repBase = setBase;
                // Hereda las fuentes declaradas en el AdaptationSet padre.
                current = setTrack;
                if (setHasBase && current.singleFile.isEmpty() && current.segmentListUrls.isEmpty() &&
                    current.tmplMedia.isEmpty()) {
                    current.singleFile = setBase;
                }
                current.id = reader.attributes().value(QLatin1String("id")).toString();
                current.bandwidth = reader.attributes().value(QLatin1String("bandwidth")).toInt();
                current.width = reader.attributes().value(QLatin1String("width")).toInt();
                current.height = reader.attributes().value(QLatin1String("height")).toInt();
                const QString repMime = reader.attributes().value(QLatin1String("mimeType")).toString();
                if (!repMime.isEmpty()) {
                    current.mime = repMime;
                } else if (current.mime.isEmpty()) {
                    current.mime = setMime;
                }
                const QString repCodecs = reader.attributes().value(QLatin1String("codecs")).toString();
                if (!repCodecs.isEmpty()) {
                    current.codecs = repCodecs;
                }
            } else if (name == QLatin1String("BaseURL")) {
                const QString text = reader.readElementText();
                const QString resolved = resolveUrl(inRepresentation ? repBase
                                                    : (inAdaptation ? setBase
                                                                    : (inPeriod ? periodBase : mpdBase)),
                                                    text);
                if (inRepresentation) {
                    repBase = resolved;
                    if (current.singleFile.isEmpty() && current.segmentListUrls.isEmpty() &&
                        current.tmplMedia.isEmpty()) {
                        current.singleFile = resolved;
                    }
                } else if (inAdaptation) {
                    setBase = resolved;
                    setHasBase = !text.endsWith(QLatin1Char('/'));
                } else if (inPeriod) {
                    periodBase = resolved;
                } else {
                    mpdBase = resolved;
                }
            } else if (name == QLatin1String("SegmentList")) {
                Track& target = inRepresentation ? current : setTrack;
                const QString durationAttr = reader.attributes().value(QLatin1String("duration")).toString();
                const QString timescaleAttr = reader.attributes().value(QLatin1String("timescale")).toString();
                if (!durationAttr.isEmpty()) {
                    target.tmplDuration = durationAttr.toLongLong();
                }
                if (!timescaleAttr.isEmpty()) {
                    target.tmplTimescale = std::max<qint64>(1, timescaleAttr.toLongLong());
                }
                target.singleFile.clear();
            } else if (name == QLatin1String("Initialization")) {
                Track& target = inRepresentation ? current : setTrack;
                const QString source = reader.attributes().value(QLatin1String("sourceURL")).toString();
                if (!source.isEmpty()) {
                    const QString resolved = resolveUrl(repBase, source);
                    if (target.segmentListUrls.isEmpty() && target.tmplMedia.isEmpty() &&
                        target.singleFile.isEmpty()) {
                        target.segmentListInit = resolved;
                    } else if (target.tmplMedia.isEmpty()) {
                        target.init = resolved;
                    }
                }
            } else if (name == QLatin1String("SegmentURL")) {
                Track& target = inRepresentation ? current : setTrack;
                const QString media = reader.attributes().value(QLatin1String("media")).toString();
                if (!media.isEmpty()) {
                    target.segmentListUrls.append(resolveUrl(repBase, media));
                    target.singleFile.clear();
                }
            } else if (name == QLatin1String("SegmentTemplate")) {
                Track& target = inRepresentation ? current : setTrack;
                target.tmplMedia = reader.attributes().value(QLatin1String("media")).toString();
                target.tmplInit = reader.attributes().value(QLatin1String("initialization")).toString();
                const QString durationAttr = reader.attributes().value(QLatin1String("duration")).toString();
                const QString timescaleAttr = reader.attributes().value(QLatin1String("timescale")).toString();
                const QString startAttr = reader.attributes().value(QLatin1String("startNumber")).toString();
                if (!durationAttr.isEmpty()) {
                    target.tmplDuration = durationAttr.toLongLong();
                }
                if (!timescaleAttr.isEmpty()) {
                    target.tmplTimescale = std::max<qint64>(1, timescaleAttr.toLongLong());
                }
                if (!startAttr.isEmpty()) {
                    target.tmplStartNumber = std::max<qint64>(1, startAttr.toLongLong());
                }
                target.singleFile.clear();
                target.segmentListUrls.clear();
            } else if (name == QLatin1String("SegmentTimeline")) {
                Track& target = inRepresentation ? current : setTrack;
                target.tmplHasTimeline = true;
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QString name = reader.name().toString();
            if (name == QLatin1String("Representation")) {
                finalizeTrack(current);
                if (!current.segments.isEmpty()) {
                    const bool isAudio = current.mime.startsWith(QLatin1String("audio")) ||
                                        (current.height == 0 && current.width == 0 &&
                                         current.mime.isEmpty());
                    if (isAudio) {
                        audioTracks.append(current);
                    } else {
                        videoTracks.append(current);
                    }
                }
                inRepresentation = false;
                current = Track{};
            } else if (name == QLatin1String("AdaptationSet")) {
                inAdaptation = false;
                setBase = periodBase;
            } else if (name == QLatin1String("Period")) {
                inPeriod = false;
            }
        }
    }
    if (reader.hasError()) {
        // Un XML con error estructural severo no debe producir variantes.
        if (videoTracks.isEmpty() && audioTracks.isEmpty()) {
            return fail(QStringLiteral("XML DASH invalido: %1").arg(reader.errorString()));
        }
    }

    // Completa el mime de pistas de audio sin atributo (caso tipico: solo codec).
    for (Track& track : audioTracks) {
        if (track.mime.isEmpty()) {
            track.mime = QStringLiteral("audio/mp4");
        }
    }

    if (!videoTracks.isEmpty()) {
        // Mejor pista de audio (si la hay) compartida por las variantes.
        const Track* bestAudio = nullptr;
        for (const Track& track : audioTracks) {
            if (bestAudio == nullptr || track.bandwidth > bestAudio->bandwidth) {
                bestAudio = &track;
            }
        }
        for (const Track& video : videoTracks) {
            StreamVariant variant;
            variant.isDash = true;
            variant.bandwidth = video.bandwidth;
            variant.width = video.width;
            variant.height = video.height;
            variant.codecs = video.codecs;
            variant.mimeType = video.mime;
            variant.segments = video.segments;
            variant.initSegment = video.init;
            if (video.height > 0) {
                variant.resolution = QStringLiteral("%1x%2").arg(video.width).arg(video.height);
                variant.label = QStringLiteral("%1p").arg(video.height);
            } else {
                variant.label = video.bandwidth > 0 ? QStringLiteral("%1 kbps").arg(video.bandwidth / 1000)
                                                    : QStringLiteral("Video");
            }
            if (bestAudio != nullptr) {
                variant.audioSegments = bestAudio->segments;
                variant.audioInitSegment = bestAudio->init;
            }
            out.append(variant);
        }
    } else {
        for (const Track& audio : audioTracks) {
            StreamVariant variant;
            variant.isDash = true;
            variant.bandwidth = audio.bandwidth;
            variant.codecs = audio.codecs;
            variant.mimeType = audio.mime;
            variant.segments = audio.segments;
            variant.initSegment = audio.init;
            variant.label = audio.bandwidth > 0 ? QStringLiteral("Audio %1 kbps").arg(audio.bandwidth / 1000)
                                                : QStringLiteral("Audio");
            variant.resolution = QStringLiteral("audio");
            out.append(variant);
        }
    }

    if (out.isEmpty()) {
        return fail(QStringLiteral("El manifiesto DASH no contiene representaciones descargables"));
    }

    std::sort(out.begin(), out.end(), [](const StreamVariant& a, const StreamVariant& b) {
        if (a.height != b.height) {
            return a.height > b.height;
        }
        return a.bandwidth > b.bandwidth;
    });
    if (error != nullptr) {
        error->clear();
    }
    return out;
}

// ---------------------------------------------------------------------------
// Transferencias
// ---------------------------------------------------------------------------

bool StreamDownloader::fetchToMemory(const QString& url, const QString& referer, const QString& cookies,
                                     const QString& proxy, QByteArray* out, int* status, QString* error,
                                     std::atomic<bool>* abortFlag, qint64 maxBytes) {
    if (out != nullptr) {
        out->clear();
    }
    if (status != nullptr) {
        *status = 0;
    }
    const QUrl parsed(url);
    if (!parsed.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("URL invalida: %1").arg(url);
        }
        return false;
    }
    const QByteArray urlBytes = parsed.toEncoded(QUrl::FullyEncoded | QUrl::RemoveFragment);

    CURL* easy = curl_easy_init();
    if (easy == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("curl_easy_init falló");
        }
        return false;
    }

    MemorySink sink;
    sink.data = out;
    sink.maxBytes = maxBytes;
    sink.abortFlag = abortFlag;

    const DownloadTask task = mediaTask(url, referer, cookies);
    const QList<QPair<QString, QString>> headers =
        BrowserEmulator::buildHeaders(task, QStringLiteral("media"));
    curl_slist* slist = BrowserEmulator::buildSlist(headers);

    BrowserEmulator::applyCommonOptions(easy, task, BrowserEmulator::currentProfile(), proxy);
    curl_easy_setopt(easy, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, slist);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &memoryWriteThunk);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, &abortXferThunk);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, abortFlag);

    const CURLcode rc = curl_easy_perform(easy);
    long code = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
    if (status != nullptr) {
        *status = static_cast<int>(code);
    }

    bool ok = (rc == CURLE_OK);
    if (error != nullptr) {
        error->clear();
        if (!ok) {
            if (sink.tooLarge) {
                *error = QStringLiteral("__TOO_LARGE__");
            } else if (rc == CURLE_HTTP_RETURNED_ERROR) {
                *error = QStringLiteral("HTTP %1").arg(static_cast<int>(code));
            } else if (abortFlag != nullptr && abortFlag->load()) {
                *error = QStringLiteral("abortado");
            } else {
                *error = QString::fromUtf8(curl_easy_strerror(rc));
            }
        }
    }

    curl_slist_free_all(slist);
    curl_easy_cleanup(easy);
    return ok;
}

bool StreamDownloader::fetchToFile(const QString& url, const QString& referer, const QString& cookies,
                                   const QString& proxy, const QString& filePath, qint64* outBytes,
                                   QString* error, std::atomic<bool>* abortFlag) {
    if (outBytes != nullptr) {
        *outBytes = 0;
    }
    const QUrl parsed(url);
    if (!parsed.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("URL invalida: %1").arg(url);
        }
        return false;
    }
    const QByteArray urlBytes = parsed.toEncoded(QUrl::FullyEncoded | QUrl::RemoveFragment);

    CURL* easy = curl_easy_init();
    if (easy == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("curl_easy_init falló");
        }
        return false;
    }

    FileSink sink;
    sink.file.setFileName(filePath);
    if (!sink.file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        curl_easy_cleanup(easy);
        if (error != nullptr) {
            *error = QStringLiteral("No se pudo crear %1").arg(filePath);
        }
        return false;
    }
    sink.abortFlag = abortFlag;

    const DownloadTask task = mediaTask(url, referer, cookies);
    const QList<QPair<QString, QString>> headers =
        BrowserEmulator::buildHeaders(task, QStringLiteral("media"));
    curl_slist* slist = BrowserEmulator::buildSlist(headers);

    BrowserEmulator::applyCommonOptions(easy, task, BrowserEmulator::currentProfile(), proxy);
    curl_easy_setopt(easy, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, slist);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &fileWriteThunk);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, &abortXferThunk);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, abortFlag);

    const CURLcode rc = curl_easy_perform(easy);
    long code = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);

    sink.file.close();
    if (outBytes != nullptr) {
        *outBytes = sink.written;
    }

    bool ok = (rc == CURLE_OK) && !sink.failed;
    if (!ok && sink.file.exists() && sink.written == 0) {
        sink.file.remove();
    }
    if (error != nullptr) {
        error->clear();
        if (!ok) {
            if (rc == CURLE_HTTP_RETURNED_ERROR) {
                *error = QStringLiteral("HTTP %1").arg(static_cast<int>(code));
            } else if (abortFlag != nullptr && abortFlag->load()) {
                *error = QStringLiteral("abortado");
            } else if (sink.failed) {
                *error = QStringLiteral("Fallo de escritura en disco");
            } else {
                *error = QString::fromUtf8(curl_easy_strerror(rc));
            }
        }
    }

    curl_slist_free_all(slist);
    curl_easy_cleanup(easy);
    return ok;
}

bool StreamDownloader::runProcess(const QString& program, const QStringList& args, int timeoutMs,
                                  std::atomic<bool>* abortFlag, QString* output, QString* error) {
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5000)) {
        if (error != nullptr) {
            *error = QStringLiteral("No se pudo ejecutar %1").arg(program);
        }
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning) {
        if (abortFlag != nullptr && abortFlag->load()) {
            process.kill();
            process.waitForFinished(3000);
            if (error != nullptr) {
                *error = QStringLiteral("abortado");
            }
            return false;
        }
        if (timeoutMs > 0 && timer.elapsed() > timeoutMs) {
            process.kill();
            process.waitForFinished(3000);
            if (error != nullptr) {
                *error = QStringLiteral("Tiempo de espera agotado: %1").arg(program);
            }
            return false;
        }
        process.waitForFinished(150);
    }
    if (output != nullptr) {
        *output = QString::fromUtf8(process.readAllStandardOutput());
    }
    if (process.exitCode() != 0) {
        if (error != nullptr) {
            const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
            *error = stderrText.left(400);
            if (error->isEmpty()) {
                *error = QStringLiteral("%1 terminó con código %2").arg(program).arg(process.exitCode());
            }
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Descarga paralela de segmentos
// ---------------------------------------------------------------------------

bool StreamDownloader::fetchSegmentsParallel(const QStringList& urls, const QString& referer,
                                             const QString& cookies, const QString& proxy,
                                             const QString& tempDir, const QString& prefix,
                                             QStringList* outFilePaths, QString* error) {
    const int count = urls.size();
    if (outFilePaths != nullptr) {
        outFilePaths->clear();
        outFilePaths->resize(count);
    }
    if (count == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("No hay segmentos que descargar");
        }
        return false;
    }

    std::atomic<int> nextIndex{0};
    std::atomic<int> doneCount{0};
    std::atomic<qint64> received{0};
    std::atomic<bool> failed{false};
    std::mutex errorMutex;
    QString firstError;
    QStringList paths(count);

    const int workerCount = std::max(1, std::min(count, 4));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (int w = 0; w < workerCount; ++w) {
        workers.emplace_back([&, w]() {
            Q_UNUSED(w)
            while (!m_abort.load(std::memory_order_relaxed) && !failed.load(std::memory_order_relaxed)) {
                const int index = nextIndex.fetch_add(1);
                if (index >= count) {
                    break;
                }
                const QString path =
                    tempDir + QLatin1Char('/') + prefix + QStringLiteral("_") +
                    QString::number(index).rightJustified(5, QLatin1Char('0')) + QStringLiteral(".bin");

                bool okSegment = false;
                QString segmentError;
                qint64 bytes = 0;
                for (int attempt = 0; attempt < 3 && !okSegment; ++attempt) {
                    if (m_abort.load(std::memory_order_relaxed)) {
                        break;
                    }
                    if (attempt > 0) {
                        QThread::msleep(static_cast<unsigned long>(300 * attempt));
                    }
                    okSegment = fetchToFile(urls.at(index), referer, cookies, proxy, path, &bytes,
                                            &segmentError, &m_abort);
                }

                if (!okSegment) {
                    failed.store(true);
                    std::lock_guard<std::mutex> lock(errorMutex);
                    if (firstError.isEmpty()) {
                        firstError = QStringLiteral("Segmento %1: %2").arg(index).arg(segmentError);
                    }
                    break;
                }

                paths[index] = path;
                received.fetch_add(bytes);
                const int done = doneCount.fetch_add(1) + 1;
                const qint64 totalSoFar = received.load();
                const qint64 estimate = done > 0 ? (totalSoFar * count) / done : -1;
                QMetaObject::invokeMethod(
                    this,
                    [this, totalSoFar, estimate]() { emit progress(totalSoFar, estimate); },
                    Qt::QueuedConnection);
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }

    if (m_abort.load(std::memory_order_relaxed)) {
        if (error != nullptr) {
            *error = QStringLiteral("abortado");
        }
        return false;
    }
    if (failed.load(std::memory_order_relaxed)) {
        if (error != nullptr) {
            *error = firstError.isEmpty() ? QStringLiteral("Fallo descargando segmentos") : firstError;
        }
        return false;
    }
    if (outFilePaths != nullptr) {
        *outFilePaths = paths;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Sonda de manifiesto
// ---------------------------------------------------------------------------

void StreamDownloader::probe(const QString& manifestUrl, const QString& referer, const QString& cookies,
                             const QString& proxy) {
    joinThread();
    m_abort.store(false, std::memory_order_relaxed);
    m_busy.store(true, std::memory_order_relaxed);
    m_thread = std::thread([this, manifestUrl, referer, cookies, proxy]() {
        runProbe(manifestUrl, referer, cookies, proxy);
        m_busy.store(false, std::memory_order_relaxed);
    });
}

void StreamDownloader::runProbe(const QString& manifestUrl, const QString& referer, const QString& cookies,
                                const QString& proxy) {
    QList<StreamVariant> variants;
    QString errorMessage;

    QByteArray payload;
    int status = 0;
    QString fetchError;
    const bool fetched = fetchToMemory(manifestUrl, referer, cookies, proxy, &payload, &status, &fetchError,
                                       &m_abort, 8 * 1024 * 1024);

    if (fetched) {
        const QString text = QString::fromUtf8(payload);
        if (text.startsWith(QLatin1String("#EXTM3U"))) {
            variants = parseMasterPlaylist(text, manifestUrl);
            if (variants.isEmpty()) {
                // Playlist media directo (unica calidad).
                const bool isLive = !text.contains(QStringLiteral("#EXT-X-ENDLIST"));
                if (isLive) {
                    errorMessage = QStringLiteral("Transmisión en directo no soportada");
                } else {
                    StreamVariant direct;
                    direct.url = manifestUrl;
                    direct.label = QStringLiteral("Calidad original");
                    direct.resolution = QStringLiteral("original");
                    variants.append(direct);
                }
            }
        } else if (text.contains(QStringLiteral("<MPD"))) {
            variants = parseDashManifest(text, manifestUrl, &errorMessage);
        } else {
            errorMessage = QStringLiteral("Formato de manifiesto no reconocido");
        }
    } else if (fetchError == QLatin1String("__TOO_LARGE__")) {
        // La URL apunta directamente a un archivo multimedia grande.
        StreamVariant direct;
        direct.url = manifestUrl;
        direct.label = QStringLiteral("Archivo directo");
        direct.resolution = QStringLiteral("original");
        variants.append(direct);
        errorMessage.clear();
    } else if (status == 403 || status == 401) {
        errorMessage = QStringLiteral("HTTP %1: el enlace caducó").arg(status);
    } else {
        errorMessage = fetchError;
    }

    // Fallback: el usuario ha habilitado yt-dlp.
    if (variants.isEmpty() && errorMessage.isEmpty()) {
        errorMessage = QStringLiteral("No se detectaron calidades disponibles");
    }

    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cachedVariants = variants;
        m_cachedManifestUrl = manifestUrl;
    }

    QVariantList serializable;
    for (const StreamVariant& variant : variants) {
        serializable.append(variant.toVariantMap());
    }
    log::info("Stream probe: {} variante(s), error='{}'", static_cast<int>(serializable.size()),
              log::utf8(errorMessage));

    QMetaObject::invokeMethod(
        this,
        [this, ok = !variants.isEmpty(), serializable, errorMessage]() {
            emit probeFinished(ok, serializable, ok ? QString() : errorMessage);
        },
        Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// Descarga del flujo
// ---------------------------------------------------------------------------

void StreamDownloader::download(quint64 jobId, const QString& manifestUrl, int variantIndex,
                                const QString& destPartPath, const QString& referer, const QString& cookies,
                                const QString& proxy, const QString& ffmpegPath, const QString& ytDlpPath,
                                bool useYtDlp) {
    joinThread();
    m_abort.store(false, std::memory_order_relaxed);
    m_busy.store(true, std::memory_order_relaxed);
    m_thread = std::thread([this, jobId, manifestUrl, variantIndex, destPartPath, referer, cookies, proxy,
                            ffmpegPath, ytDlpPath, useYtDlp]() {
        runDownload(jobId, manifestUrl, variantIndex, destPartPath, referer, cookies, proxy, ffmpegPath,
                    ytDlpPath, useYtDlp);
        m_busy.store(false, std::memory_order_relaxed);
    });
}

namespace {

/// Convierte la ruta .part en la ruta final con `extension` deseada.
QString finalPathFor(const QString& partPath, const QString& extension) {
    const QFileInfo info(partPath);
    QString base = info.completeBaseName(); // "video.mp4" de "video.mp4.part"
    static const QStringList kMediaExt = {QStringLiteral("mp4"), QStringLiteral("mkv"),  QStringLiteral("ts"),
                                          QStringLiteral("webm"), QStringLiteral("mp3"), QStringLiteral("m4a"),
                                          QStringLiteral("mov"), QStringLiteral("avi")};
    for (const QString& ext : kMediaExt) {
        if (base.endsWith(QLatin1Char('.') + ext, Qt::CaseInsensitive)) {
            base.chop(ext.size() + 1);
            break;
        }
    }
    return info.absolutePath() + QLatin1Char('/') + base + QLatin1Char('.') + extension;
}

bool hasFfmpeg(const QString& ffmpegPath) {
    const QString program = ffmpegPath.isEmpty() ? QStringLiteral("ffmpeg") : ffmpegPath;
    QString output;
    QString error;
    // Comprobacion barata: -version termina inmediatamente.
    QStringList args{QStringLiteral("-version")};
    std::atomic<bool> noAbort{false};
    return StreamDownloader::runProcess(program, args, 5000, &noAbort, &output, &error);
}

} // namespace

void StreamDownloader::runDownload(quint64 jobId, const QString& manifestUrl, int variantIndex,
                                   const QString& destPartPath, const QString& referer, const QString& cookies,
                                   const QString& proxy, const QString& ffmpegPath, const QString& ytDlpPath,
                                   bool useYtDlp) {
    Q_UNUSED(jobId)
    auto finish = [&](bool ok, const QString& path, const QString& error) {
        QMetaObject::invokeMethod(
            this, [this, ok, path, error]() { emit downloadFinished(ok, path, error); },
            Qt::QueuedConnection);
    };

    if (m_abort.load(std::memory_order_relaxed)) {
        finish(false, QString(), QStringLiteral("abortado"));
        return;
    }

    // --- 1) Variantes (cache de la sonda o nueva lectura) ------------------
    QList<StreamVariant> variants;
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_cachedManifestUrl == manifestUrl) {
            variants = m_cachedVariants;
        }
    }

    StreamVariant chosen;
    if (variantIndex >= 0 && variantIndex < variants.size()) {
        chosen = variants.at(variantIndex);
    } else if (!variants.isEmpty()) {
        chosen = variants.first();
    }

    // --- 2) Modo yt-dlp (si esta habilitado) -------------------------------
    if (useYtDlp) {
        const QString program = ytDlpPath.isEmpty() ? QStringLiteral("yt-dlp") : ytDlpPath;
        const QString baseFinal = finalPathFor(destPartPath, QStringLiteral("mp4"));
        QStringList args;
        args << QStringLiteral("--no-part") << QStringLiteral("--no-playlist") << QStringLiteral("--newline")
             << QStringLiteral("--progress-template")
             << QStringLiteral("download:%(progress.downloaded_bytes)s/%(progress.total_bytes)s");
        if (!referer.isEmpty()) {
            args << QStringLiteral("--add-header") << (QStringLiteral("Referer:") + referer);
        }
        if (!cookies.isEmpty()) {
            args << QStringLiteral("--add-header") << (QStringLiteral("Cookie:") + cookies);
        }
        args << QStringLiteral("-f") << QStringLiteral("best[ext=mp4]/best") << QStringLiteral("-o") << baseFinal
             << QStringLiteral("--") << manifestUrl;

        QProcess process;
        process.setProgram(program);
        process.setArguments(args);
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start();
        if (process.waitForStarted(5000)) {
            QString stderrBuffer;
            while (process.state() != QProcess::NotRunning) {
                if (m_abort.load(std::memory_order_relaxed)) {
                    process.kill();
                    process.waitForFinished(3000);
                    finish(false, QString(), QStringLiteral("abortado"));
                    return;
                }
                if (process.waitForReadyRead(200)) {
                    const QString chunk = QString::fromUtf8(process.readAllStandardOutput());
                    const QStringList lines = chunk.split(QLatin1Char('\n'));
                    for (const QString& line : lines) {
                        if (line.startsWith(QLatin1String("download:"))) {
                            const QStringList parts = line.mid(9).split(QLatin1Char('/'));
                            if (parts.size() == 2) {
                                bool okDown = false;
                                bool okTotal = false;
                                const qint64 done = parts.at(0).toDouble(&okDown) > 0
                                                        ? static_cast<qint64>(parts.at(0).toDouble())
                                                        : 0;
                                const qint64 total = parts.at(1).toDouble(&okTotal)
                                                         ? static_cast<qint64>(parts.at(1).toDouble())
                                                         : -1;
                                if (okDown) {
                                    emit progress(done, total);
                                }
                            }
                        }
                    }
                    stderrBuffer += QString::fromUtf8(process.readAllStandardError()).left(4000);
                }
            }
            if (process.exitCode() == 0 && QFileInfo::exists(baseFinal)) {
                QFile::remove(destPartPath);
                finish(true, baseFinal, QString());
                return;
            }
            log::warn("yt-dlp fallo (codigo {}), usando el motor nativo: {}", process.exitCode(),
                      log::utf8(stderrBuffer.left(200)));
        } else {
            log::warn("yt-dlp no disponible ({}), usando el motor nativo", log::utf8(program));
        }
    }

    // --- 3) Resolucion del manifiesto --------------------------------------
    QString mediaUrl = manifestUrl;
    if (!chosen.url.isEmpty()) {
        mediaUrl = chosen.url;
    }

    QStringList videoSegments;
    QString videoInit;
    QStringList audioSegments;
    QString audioInit;
    bool isDash = chosen.isDash && !chosen.segments.isEmpty();

    if (isDash) {
        videoSegments = chosen.segments;
        videoInit = chosen.initSegment;
        audioSegments = chosen.audioSegments;
        audioInit = chosen.audioInitSegment;
    } else {
        QByteArray payload;
        int status = 0;
        QString fetchError;
        if (!fetchToMemory(mediaUrl, referer, cookies, proxy, &payload, &status, &fetchError, &m_abort,
                           8 * 1024 * 1024)) {
            finish(false, QString(),
                   status == 403 || status == 401 ? QStringLiteral("HTTP %1: el enlace caducó").arg(status)
                                                  : fetchError);
            return;
        }
        const QString text = QString::fromUtf8(payload);
        QString parseError;
        if (text.startsWith(QLatin1String("#EXTM3U"))) {
            // ¿Otro nivel de maestro? (en raras CDN la variante es maestra)
            const QList<StreamVariant> nested = parseMasterPlaylist(text, mediaUrl);
            if (!nested.isEmpty()) {
                mediaUrl = nested.first().url;
                QByteArray nestedPayload;
                if (!fetchToMemory(mediaUrl, referer, cookies, proxy, &nestedPayload, &status, &fetchError,
                                   &m_abort, 8 * 1024 * 1024)) {
                    finish(false, QString(), fetchError);
                    return;
                }
                const QString nestedText = QString::fromUtf8(nestedPayload);
                if (!parseMediaPlaylist(nestedText, mediaUrl, &videoSegments, &videoInit, &parseError)) {
                    finish(false, QString(), parseError);
                    return;
                }
            } else if (!parseMediaPlaylist(text, mediaUrl, &videoSegments, &videoInit, &parseError)) {
                finish(false, QString(), parseError);
                return;
            }
        } else if (text.contains(QStringLiteral("<MPD"))) {
            const QList<StreamVariant> dash = parseDashManifest(text, mediaUrl, &parseError);
            if (dash.isEmpty()) {
                finish(false, QString(), parseError);
                return;
            }
            const StreamVariant& variant = (variantIndex >= 0 && variantIndex < dash.size())
                                               ? dash.at(variantIndex)
                                               : dash.first();
            videoSegments = variant.segments;
            videoInit = variant.initSegment;
            audioSegments = variant.audioSegments;
            audioInit = variant.audioInitSegment;
            isDash = true;
        } else {
            finish(false, QString(), QStringLiteral("Formato de manifiesto no reconocido"));
            return;
        }
    }

    if (videoSegments.isEmpty()) {
        finish(false, QString(), QStringLiteral("El manifiesto no contiene segmentos"));
        return;
    }

    // --- 4) Espacio temporal y descarga paralela ---------------------------
    const QFileInfo destInfo(destPartPath);
    const QString destDir = destInfo.absolutePath();
    QDir().mkpath(destDir);
    const QString tempDir = destPartPath + QStringLiteral(".segments");
    QDir(tempDir).removeRecursively();
    QDir().mkpath(tempDir);

    QStringList videoFiles;
    QString errorText;
    // El segmento de inicializacion (EXT-X-MAP / DASH Initialization) se
    // descarga junto con el resto para que el ensamblado conserve el orden.
    QStringList videoPlan = videoSegments;
    if (!videoInit.isEmpty()) {
        videoPlan.prepend(videoInit);
    }
    QStringList audioPlan = audioSegments;
    if (!audioInit.isEmpty()) {
        audioPlan.prepend(audioInit);
    }

    if (!fetchSegmentsParallel(videoPlan, referer, cookies, proxy, tempDir, QStringLiteral("v"),
                               &videoFiles, &errorText)) {
        QDir(tempDir).removeRecursively();
        QFile::remove(destPartPath);
        finish(false, QString(), errorText);
        return;
    }

    QStringList audioFiles;
    if (!audioPlan.isEmpty()) {
        if (!fetchSegmentsParallel(audioPlan, referer, cookies, proxy, tempDir, QStringLiteral("a"),
                                   &audioFiles, &errorText)) {
            QDir(tempDir).removeRecursively();
            QFile::remove(destPartPath);
            finish(false, QString(), errorText);
            return;
        }
    }

    if (m_abort.load(std::memory_order_relaxed)) {
        QDir(tempDir).removeRecursively();
        QFile::remove(destPartPath);
        finish(false, QString(), QStringLiteral("abortado"));
        return;
    }

    // --- 5) Ensamblado en orden (init + segmentos) -------------------------
    auto concatenate = [&](const QString& outputPath, const QStringList& files) -> bool {
        QFile out(outputPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            errorText = QStringLiteral("No se pudo escribir %1").arg(outputPath);
            return false;
        }
        for (const QString& file : files) {
            QFile in(file);
            if (!in.open(QIODevice::ReadOnly)) {
                errorText = QStringLiteral("No se pudo leer %1").arg(file);
                out.close();
                return false;
            }
            char buffer[256 * 1024];
            qint64 readBytes = 0;
            while ((readBytes = in.read(buffer, sizeof(buffer))) > 0) {
                if (out.write(buffer, readBytes) != readBytes) {
                    errorText = QStringLiteral("Fallo de escritura en disco");
                    in.close();
                    out.close();
                    return false;
                }
            }
            in.close();
        }
        out.close();
        return true;
    };

    if (!concatenate(destPartPath, videoFiles)) {
        QDir(tempDir).removeRecursively();
        QFile::remove(destPartPath);
        finish(false, QString(), errorText);
        return;
    }

    QString audioTempPath;
    if (!audioFiles.isEmpty()) {
        audioTempPath = destPartPath + QStringLiteral(".audio");
        if (!concatenate(audioTempPath, audioFiles)) {
            QDir(tempDir).removeRecursively();
            QFile::remove(destPartPath);
            finish(false, QString(), errorText);
            return;
        }
    }

    QDir(tempDir).removeRecursively();

    // --- 6) MUX / renombrado final -----------------------------------------
    if (!audioTempPath.isEmpty()) {
        // Audio y video por separado (DASH): se unen con FFmpeg.
        if (!hasFfmpeg(ffmpegPath)) {
            QFile::remove(destPartPath);
            QFile::remove(audioTempPath);
            finish(false, QString(),
                   QStringLiteral("Se requiere FFmpeg para unir audio y video (DASH). Instálalo o indica su ruta en Ajustes."));
            return;
        }
        const QString baseFinal = finalPathFor(destPartPath, QStringLiteral("mp4"));
        const QStringList ffmpegArgs{QStringLiteral("-y"),  QStringLiteral("-i"), destPartPath,
                                     QStringLiteral("-i"), audioTempPath,        QStringLiteral("-c"),
                                     QStringLiteral("copy"), QStringLiteral("-map"), QStringLiteral("0:v:0"),
                                     QStringLiteral("-map"), QStringLiteral("1:a:0"), baseFinal};
        QString ffmpegOut;
        QString ffmpegError;
        const bool muxed = runProcess(ffmpegPath.isEmpty() ? QStringLiteral("ffmpeg") : ffmpegPath, ffmpegArgs,
                                      0, &m_abort, &ffmpegOut, &ffmpegError);
        QFile::remove(destPartPath);
        QFile::remove(audioTempPath);
        if (!muxed || !QFileInfo::exists(baseFinal)) {
            QFile::remove(baseFinal);
            finish(false, QString(), QStringLiteral("Fallo al unir audio/video: %1").arg(ffmpegError));
            return;
        }
        finish(true, baseFinal, QString());
        return;
    }

    // Sin audio separado: se decide el contenedor final.
    const bool isFragmentedMp4 = !videoInit.isEmpty();
    QString targetPath;

    if (isDash) {
        // DASH con un unico fichero fuente: se conserva su extension real.
        if (videoSegments.size() == 1 && videoInit.isEmpty() && audioSegments.isEmpty()) {
            QString ext = lowerExtension(QUrl(videoSegments.first()).path());
            if (ext.isEmpty() || ext == QLatin1String("m4s") || ext == QLatin1String("mp4")) {
                ext = QStringLiteral("mp4");
            }
            targetPath = finalPathFor(destPartPath, ext);
        } else {
            targetPath = finalPathFor(destPartPath, QStringLiteral("mp4"));
        }
    } else if (isFragmentedMp4) {
        // HLS con fMP4: init + segmentos ya forman un MP4 valido.
        targetPath = finalPathFor(destPartPath, QStringLiteral("mp4"));
    } else {
        // HLS (.ts): preferimos MP4 (remux) y caemos a TS si no hay FFmpeg.
        if (hasFfmpeg(ffmpegPath)) {
            const QString baseFinal = finalPathFor(destPartPath, QStringLiteral("mp4"));
            const QStringList ffmpegArgs{QStringLiteral("-y"),
                                         QStringLiteral("-i"),
                                         destPartPath,
                                         QStringLiteral("-bsf:a"),
                                         QStringLiteral("aac_adtstoasc"),
                                         QStringLiteral("-c"),
                                         QStringLiteral("copy"),
                                         baseFinal};
            QString ffmpegOut;
            QString ffmpegError;
            const bool remuxed = runProcess(ffmpegPath.isEmpty() ? QStringLiteral("ffmpeg") : ffmpegPath,
                                            ffmpegArgs, 0, &m_abort, &ffmpegOut, &ffmpegError);
            if (remuxed && QFileInfo::exists(baseFinal)) {
                QFile::remove(destPartPath);
                finish(true, baseFinal, QString());
                return;
            }
            log::warn("Remux MP4 falló ({}), se entrega .ts", log::utf8(ffmpegError));
        }
        targetPath = finalPathFor(destPartPath, QStringLiteral("ts"));
    }

    QFile::remove(targetPath);
    if (!QFile::rename(destPartPath, targetPath)) {
        if (!QFile::copy(destPartPath, targetPath)) {
            finish(false, QString(), QStringLiteral("No se pudo guardar el archivo final"));
            return;
        }
        QFile::remove(destPartPath);
    }
    finish(true, targetPath, QString());
}

} // namespace sdm
