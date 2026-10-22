// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/BrowserEmulator.h
//
//  Motor de suplantacion de navegador (spec cap. 6): rotacion de User-Agents,
//  replicacion de cabeceras HTTP/2 (Sec-CH-UA, Sec-Fetch-*), configuracion de
//  ciphersuites TLS y gestion de cookies/referer.
// ============================================================================
#pragma once

#include <QList>
#include <QPair>
#include <QString>

#include <curl/curl.h>

#include "database/Models.h"

namespace sdm {

/// Perfil de emulacion de cliente (Chrome/Edge/Firefox en Windows 11).
struct BrowserProfile {
    QString id;             // "chrome-win11", "edge-win11", ...
    QString name;           // Nombre legible para la UI
    QString userAgent;      // Cabecera User-Agent completa
    QString accept;         // Cabecera Accept
    QString acceptLanguage; // Cabecera Accept-Language
    QString secChUa;        // Sec-CH-UA ("Chromium";v="...", ...)
    QString secChUaMobile;  // "?0"
    QString secChUaPlatform; // "\"Windows\""
    long httpVersion = CURL_HTTP_VERSION_2_0;
    QString tlsCiphers;     // Ciphersuites TLS 1.2 (OpenSSL)
    QString tls13Ciphers;   // Ciphersuites TLS 1.3
};

/// Coleccion de User-Agents para rotacion aleatoria (anti-bloqueo).
class BrowserEmulator {
public:
    /// Perfiles oficiales (Chrome, Edge, Firefox).
    static const QList<BrowserProfile>& profiles();

    /// Perfil actualmente activo (por defecto: Chrome/Win11 de la spec 6.2).
    static const BrowserProfile& currentProfile();
    static int currentIndex();
    static void setCurrentIndex(int index);
    static bool selectProfileById(const QString& id);

    /// Rota al siguiente perfil (si autoRotate esta activo en Ajustes).
    static void rotateProfile();

    /// Devuelve un UA aleatorio del pool amplio (rotacion dinamica).
    static QString randomUserAgent();

    /// Cabeceras por defecto inyectadas (spec 6.2) + Referer/Cookie de la tarea.
    /// `purpose` permite afinar: "document" (HTML), "media" (segmentos/manifest).
    static QList<QPair<QString, QString>> buildHeaders(const DownloadTask& task,
                                                       const QString& purpose = QStringLiteral("document"));

    /// Cabeceras para peticiones internas (sondas HEAD / Range 0-0).
    static QList<QPair<QString, QString>> probeHeaders(const DownloadTask& task);

    /// Aplica la configuracion comun de un cliente real a un handle curl:
    /// UA, HTTP/2, TLS, verificacion SSL, proxy, autenticacion y referer.
    /// Los encabezados se pasan aparte via CURLOPT_HTTPHEADER (slist).
    static void applyCommonOptions(CURL* easy, const DownloadTask& task, const BrowserProfile& profile,
                                   const QString& proxy);

    /// Construye un curl_slist a partir de pares (devuelve nullptr si vacio).
    /// El caller debe liberarlo con curl_slist_free_all.
    static struct curl_slist* buildSlist(const QList<QPair<QString, QString>>& headers);

    /// Version de Chrome usada por el perfil (ej. "122.0.0.0").
    static QString chromeVersionString();

    /// User-Agent aleatorio simple (para rotacion rapida por peticion).
    static const QList<QString>& userAgentPool();
};

} // namespace sdm
