// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/BrowserEmulator.cpp
// ============================================================================
#include "core/BrowserEmulator.h"

#include "core/Log.h"

#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>

namespace sdm {

namespace {

// ---------------------------------------------------------------------------
// Perfiles de emulacion (spec 6.2 - simulacion Chrome en Windows 11)
// ---------------------------------------------------------------------------

const char* kChromeUaPrefix =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/";
const char* kChromeUaSuffix = "0.0.0.0 Safari/537.36";
const char* kEdgeUaPrefix =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/";
const char* kEdgeUaMid = ".0.0.0 Safari/537.36 Edg/";
const char* kFirefoxUaPrefix = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:";
const char* kFirefoxUaSuffix = ".0) Gecko/20100101 Firefox/";

const char* kDefaultAcceptDocument =
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8";
const char* kDefaultAcceptMedia = "*/*";

// Ciphersuites alineados con Chrome/Windows (Order de Chrome + TLS1.3).
const char* kChromeTls12Ciphers =
    "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:"
    "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
    "ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA:AES256-SHA";
const char* kChromeTls13Ciphers = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";

QMutex g_indexMutex;
int g_profileIndex = 0; // Chrome/Win11 por defecto (spec 6.2)

} // namespace

const QList<BrowserProfile>& BrowserEmulator::profiles() {
    static const QList<BrowserProfile> kProfiles = [] {
        QList<BrowserProfile> list;

        BrowserProfile chrome;
        chrome.id = QStringLiteral("chrome-win11");
        chrome.name = QStringLiteral("Google Chrome 122 (Windows 11)");
        chrome.userAgent = QStringLiteral(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
            "Chrome/122.0.0.0 Safari/537.36");
        chrome.accept = QString::fromUtf8(kDefaultAcceptDocument);
        chrome.acceptLanguage = QStringLiteral("es-ES,es;q=0.9,en;q=0.8");
        chrome.secChUa = QStringLiteral("\"Chromium\";v=\"122\", \"Not(A:Brand\";v=\"24\", \"Google Chrome\";v=\"122\"");
        chrome.secChUaMobile = QStringLiteral("?0");
        chrome.secChUaPlatform = QStringLiteral("\"Windows\"");
        chrome.httpVersion = CURL_HTTP_VERSION_2_0;
        chrome.tlsCiphers = QString::fromUtf8(kChromeTls12Ciphers);
        chrome.tls13Ciphers = QString::fromUtf8(kChromeTls13Ciphers);
        list.push_back(chrome);

        BrowserProfile edge;
        edge.id = QStringLiteral("edge-win11");
        edge.name = QStringLiteral("Microsoft Edge 122 (Windows 11)");
        edge.userAgent = QStringLiteral(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
            "Chrome/122.0.0.0 Safari/537.36 Edg/122.0.0.0");
        edge.accept = QString::fromUtf8(kDefaultAcceptDocument);
        edge.acceptLanguage = QStringLiteral("es-ES,es;q=0.9,en;q=0.8");
        edge.secChUa = QStringLiteral("\"Chromium\";v=\"122\", \"Not(A:Brand\";v=\"24\", \"Microsoft Edge\";v=\"122\"");
        edge.secChUaMobile = QStringLiteral("?0");
        edge.secChUaPlatform = QStringLiteral("\"Windows\"");
        edge.httpVersion = CURL_HTTP_VERSION_2_0;
        edge.tlsCiphers = QString::fromUtf8(kChromeTls12Ciphers);
        edge.tls13Ciphers = QString::fromUtf8(kChromeTls13Ciphers);
        list.push_back(edge);

        BrowserProfile firefox;
        firefox.id = QStringLiteral("firefox-win11");
        firefox.name = QStringLiteral("Mozilla Firefox 123 (Windows 11)");
        firefox.userAgent = QStringLiteral(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:123.0) Gecko/20100101 Firefox/123.0");
        firefox.accept = QString::fromUtf8(kDefaultAcceptDocument);
        firefox.acceptLanguage = QStringLiteral("es-ES,es;q=0.9,en;q=0.8");
        firefox.secChUa.clear(); // Firefox no envia Client Hints
        firefox.secChUaMobile.clear();
        firefox.secChUaPlatform.clear();
        firefox.httpVersion = CURL_HTTP_VERSION_2;
        firefox.tlsCiphers = QString::fromUtf8(kChromeTls12Ciphers);
        firefox.tls13Ciphers = QString::fromUtf8(kChromeTls13Ciphers);
        list.push_back(firefox);

        return list;
    }();
    return kProfiles;
}

const QList<QString>& BrowserEmulator::userAgentPool() {
    static const QList<QString> kPool = [] {
        QList<QString> list;
        for (const BrowserProfile& profile : profiles()) {
            list.push_back(profile.userAgent);
        }
        // Versiones ligeramente anteriores para variabilidad (rotacion).
        list.push_back(QStringLiteral(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
            "Chrome/121.0.0.0 Safari/537.36"));
        list.push_back(QStringLiteral(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
            "Chrome/120.0.0.0 Safari/537.36"));
        list.push_back(QStringLiteral(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:122.0) Gecko/20100101 Firefox/122.0"));
        list.push_back(QStringLiteral(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
            "Chrome/122.0.0.0 Safari/537.36 Edg/122.0.21.2"));
        return list;
    }();
    return kPool;
}

int BrowserEmulator::currentIndex() {
    QMutexLocker lock(&g_indexMutex);
    return g_profileIndex;
}

const BrowserProfile& BrowserEmulator::currentProfile() {
    const QList<BrowserProfile>& all = profiles();
    int index = currentIndex();
    if (index < 0 || index >= all.size()) {
        index = 0;
    }
    return all.at(index);
}

void BrowserEmulator::setCurrentIndex(int index) {
    QMutexLocker lock(&g_indexMutex);
    const int count = static_cast<int>(profiles().size());
    g_profileIndex = (index >= 0 && index < count) ? index : 0;
}

bool BrowserEmulator::selectProfileById(const QString& id) {
    const QList<BrowserProfile>& all = profiles();
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).id == id) {
            setCurrentIndex(i);
            return true;
        }
    }
    return false;
}

void BrowserEmulator::rotateProfile() {
    QMutexLocker lock(&g_indexMutex);
    const int count = static_cast<int>(profiles().size());
    g_profileIndex = (g_profileIndex + 1) % count;
}

QString BrowserEmulator::randomUserAgent() {
    const QList<QString>& pool = userAgentPool();
    const int index = QRandomGenerator::global()->bounded(pool.size());
    return pool.at(index);
}

QString BrowserEmulator::chromeVersionString() {
    const BrowserProfile& profile = currentProfile();
    const int idx = profile.userAgent.indexOf(QLatin1String("Chrome/"));
    if (idx < 0) {
        return QStringLiteral("122.0.0.0");
    }
    int end = profile.userAgent.indexOf(QLatin1Char(' '), idx);
    if (end < 0) {
        end = profile.userAgent.size();
    }
    QString version = profile.userAgent.mid(idx + 7, end - idx - 7);
    if (version.endsWith(QLatin1String(".0.0.0"))) {
        version.chop(4);
    }
    return version;
}

QList<QPair<QString, QString>> BrowserEmulator::buildHeaders(const DownloadTask& task, const QString& purpose) {
    const BrowserProfile& profile = currentProfile();
    QList<QPair<QString, QString>> headers;

    // --- Spec 6.2: conjunto por defecto de simulacion Chrome en Win11 ---
    headers.append({QStringLiteral("User-Agent"),
                    task.userAgent.isEmpty() ? profile.userAgent : task.userAgent});
    headers.append({QStringLiteral("Accept"),
                    purpose == QLatin1String("media") ? QString::fromUtf8(kDefaultAcceptMedia) : profile.accept});
    headers.append({QStringLiteral("Accept-Language"), profile.acceptLanguage});
    // Identity: imprescindible para que los bytes del rango coincidan con los
    // offsets de escritura (una respuesta comprimida corromperia el archivo).
    headers.append({QStringLiteral("Accept-Encoding"), QStringLiteral("identity")});

    if (!profile.secChUa.isEmpty()) {
        headers.append({QStringLiteral("Sec-CH-UA"), profile.secChUa});
        headers.append({QStringLiteral("Sec-CH-UA-Mobile"), profile.secChUaMobile});
        headers.append({QStringLiteral("Sec-CH-UA-Platform"), profile.secChUaPlatform});
    }
    if (purpose == QLatin1String("document")) {
        headers.append({QStringLiteral("Sec-Fetch-Dest"), QStringLiteral("document")});
        headers.append({QStringLiteral("Sec-Fetch-Mode"), QStringLiteral("navigate")});
        headers.append({QStringLiteral("Sec-Fetch-Site"), QStringLiteral("none")});
        headers.append({QStringLiteral("Sec-Fetch-User"), QStringLiteral("?1")});
        headers.append({QStringLiteral("Upgrade-Insecure-Requests"), QStringLiteral("1")});
    } else {
        headers.append({QStringLiteral("Sec-Fetch-Dest"), QStringLiteral("empty")});
        headers.append({QStringLiteral("Sec-Fetch-Mode"), QStringLiteral("cors")});
        headers.append({QStringLiteral("Sec-Fetch-Site"), QStringLiteral("cross-site")});
    }

    if (!task.referer.isEmpty()) {
        headers.append({QStringLiteral("Referer"), task.referer});
    }
    if (!task.cookies.isEmpty()) {
        headers.append({QStringLiteral("Cookie"), task.cookies});
    }

    // Cabeceras adicionales definidas por el usuario (Ajustes / Nueva tarea).
    for (auto it = task.extraHeaders.constBegin(); it != task.extraHeaders.constEnd(); ++it) {
        if (it.value().isEmpty() && it.key().isEmpty()) {
            continue;
        }
        headers.append({it.key(), it.value()});
    }

    return headers;
}

QList<QPair<QString, QString>> BrowserEmulator::probeHeaders(const DownloadTask& task) {
    DownloadTask copy = task;
    copy.extraHeaders = task.extraHeaders;
    return buildHeaders(copy, QStringLiteral("document"));
}

void BrowserEmulator::applyCommonOptions(CURL* easy, const DownloadTask& task, const BrowserProfile& profile,
                                         const QString& proxy) {
    if (easy == nullptr) {
        return;
    }

    const QByteArray userAgent =
        (task.userAgent.isEmpty() ? profile.userAgent : task.userAgent).toUtf8();
    curl_easy_setopt(easy, CURLOPT_USERAGENT, userAgent.constData());

    // Seguir redirecciones (CDN / enlaces con token) hasta 10 saltos.
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(easy, CURLOPT_AUTOREFERER, 1L);

    // HTTP/2 por defecto (fallback automatico a HTTP/1.1 si el servidor no lo soporta).
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, profile.httpVersion);

    // --- Anti-fingerprinting TLS (spec 6.2.3) ---
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(easy, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2); // minimo TLS 1.2
    if (!profile.tlsCiphers.isEmpty()) {
        const QByteArray ciphers = profile.tlsCiphers.toUtf8();
        curl_easy_setopt(easy, CURLOPT_SSL_CIPHER_LIST, ciphers.constData());
    }
    if (!profile.tls13Ciphers.isEmpty()) {
        const QByteArray tls13 = profile.tls13Ciphers.toUtf8();
        curl_easy_setopt(easy, CURLOPT_TLS13_CIPHERS, tls13.constData());
    }

    // Compresion: identidad para preservar offsets byte a byte.
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "identity");
    curl_easy_setopt(easy, CURLOPT_HTTP_CONTENT_DECODING, 0L);

    // Sensibilidad: sin senales en hilos (curl lo exige con NOSIGNAL=1).
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);

    // Timeouts de conexion y de velocidad minima (cortan enlaces muertos).
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 60L);

    // Compresione(s) aceptadas como peticion explicita (algunos servidores
    // responden 415 si no llega la cabecera): pedimos identity.
    // (Aplicada via slist en SegmentWorker; aqui queda el fallback.)

    // Proxy opcional (Ajustes -> Red).
    if (!proxy.isEmpty()) {
        const QByteArray proxy8 = proxy.toUtf8();
        curl_easy_setopt(easy, CURLOPT_PROXY, proxy8.constData());
    }

    // Autenticacion HTTP basica opcional de la tarea.
    if (!task.httpUser.isEmpty()) {
        const QByteArray userpwd = (task.httpUser + QLatin1Char(':') + task.httpPass).toUtf8();
        curl_easy_setopt(easy, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
        curl_easy_setopt(easy, CURLOPT_USERPWD, userpwd.constData());
    }

    // Referer explicito si existe.
    if (!task.referer.isEmpty()) {
        const QByteArray referer = task.referer.toUtf8();
        curl_easy_setopt(easy, CURLOPT_REFERER, referer.constData());
    }
}

curl_slist* BrowserEmulator::buildSlist(const QList<QPair<QString, QString>>& headers) {
    curl_slist* list = nullptr;
    for (const auto& header : headers) {
        if (header.first.isEmpty()) {
            continue;
        }
        const QByteArray line = (header.first + QStringLiteral(": ") + header.second).toUtf8();
        list = curl_slist_append(list, line.constData());
    }
    // curl pide la cabecera "Expect:" sin valor para evitar el 100-continue
    // en peticiones con cuerpo; en GET no aplica, pero la neutralizamos.
    list = curl_slist_append(list, "Expect:");
    return list;
}

} // namespace sdm
