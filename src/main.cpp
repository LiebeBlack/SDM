// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/main.cpp - Punto de entrada de la aplicacion de escritorio.
//
//  Orden de arranque:
//    1. Log + curl global + instancia unica
//    2. Base de datos SQLite (historial, colas y metadatos)
//    3. Motor de descargas (MVVM: GuiManager inyecta los ajustes)
//    4. Puente IPC: WebSocket local 127.0.0.1 + token + ProtocolHandler
//    5. Interfaz Qt Quick (qrc:/qml/main.qml) + cromo Fluent/Mica
// ============================================================================
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <curl/curl.h>

#include <cstdio>

#include "bridge/ProtocolHandler.h"
#include "bridge/WebSocketServer.h"
#include "core/DownloadEngine.h"
#include "core/Log.h"
#include "database/DatabaseManager.h"
#include "ui/GuiManager.h"

using sdm::DatabaseManager;
using sdm::DownloadEngine;
using sdm::GuiManager;
using sdm::ProtocolHandler;
using sdm::WebSocketServer;

namespace {

/// Nombre de instancia unica por usuario (evita dobles arranques).
QString singleInstanceName() {
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) {
        user = qEnvironmentVariable("USER");
    }
    if (user.isEmpty()) {
        user = QStringLiteral("default");
    }
    return QStringLiteral("sdownload-manager-") + user;
}

} // namespace

int main(int argc, char* argv[]) {
    // Formato de superficie: MSAA 4x + stencil para render Fluent suave
    // (bordes redondeados y sombras por capas sin aliasing).
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSamples(4);
    format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);

    // Nombres de organizacion/aplicacion: determinan rutas de QSettings y
    // QStandardPaths (tambien los usa SDMNativeHost).
    QCoreApplication::setOrganizationName(QStringLiteral("SDM"));
    QCoreApplication::setApplicationName(QStringLiteral("S-Download Manager"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("S-Download Manager"));
    QGuiApplication::setDesktopFileName(QStringLiteral("sdm"));
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/icon.svg")));

    // --- CLI -------------------------------------------------------------
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("S-Download Manager (SDM)"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption portOption(
        {QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("Puerto del puente WebSocket local (por defecto 9010)."), QStringLiteral("port"));
    const QCommandLineOption dirOption({QStringLiteral("d"), QStringLiteral("dir")},
                                       QStringLiteral("Carpeta de destino por defecto."),
                                       QStringLiteral("dir"));
    const QCommandLineOption minimizedOption(QStringLiteral("minimized"),
                                             QStringLiteral("Arranca minimizado a la bandeja."));
    // Integracion externa (estilo IDM): `SDM --url <enlace>` desde cualquier
    // otro programa; si SDM ya esta abierto se le reenvia la descarga.
    const QCommandLineOption urlOption({QStringLiteral("u"), QStringLiteral("url")},
                                       QStringLiteral("Anade una descarga (admite lotes tipo [01-10])."),
                                       QStringLiteral("url"));
    parser.addOption(portOption);
    parser.addOption(dirOption);
    parser.addOption(minimizedOption);
    parser.addOption(urlOption);
    parser.process(app);

    // --- Instancia unica --------------------------------------------------
    const QString instanceName = singleInstanceName();
    QLocalServer instanceServer;
    const bool isPrimaryInstance = instanceServer.listen(instanceName);
    if (!isPrimaryInstance) {
        // Ya hay una instancia en ejecucion: se le pide que se levante.
        QLocalSocket existing;
        existing.connectToServer(instanceName);
        if (existing.waitForConnected(400)) {
            QByteArray payload("raise\n");
            if (parser.isSet(urlOption)) {
                payload += "url " + parser.value(urlOption).toUtf8() + '\n';
            }
            existing.write(payload);
            existing.flush();
            existing.waitForBytesWritten(400);
        }
        return 0;
    }

    // --- Log --------------------------------------------------------------
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    sdm::log::init(dataDir);
    sdm::log::info("SDM {}. Arranque.", QCoreApplication::applicationVersion().toStdString());

    // --- libcurl (global, una sola vez) ------------------------------------
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        std::fprintf(stderr, "SDM: fallo al inicializar libcurl\n");
        return 1;
    }

    // --- Base de datos -----------------------------------------------------
    DatabaseManager database;
    QString dbError;
    if (!database.open(dataDir + QStringLiteral("/sdm.sqlite3"), &dbError)) {
        sdm::log::error("Base de datos no disponible: {}", sdm::log::utf8(dbError));
        // Se continua en modo memoria: la aplicacion sigue siendo util.
    }

    // --- Interfaz + motor --------------------------------------------------
    // El estilo debe fijarse antes de cargar QML.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine qmlEngine;
    GuiManager gui(qmlEngine);
    gui.registerContext();
    gui.initializeEngine(database.isOpen() ? &database : nullptr);

    if (parser.isSet(dirOption)) {
        gui.setDownloadDir(parser.value(dirOption));
    }

    // --- Puente IPC local (spec cap. 8) ------------------------------------
    sdm::BridgeInfo bridgeInfo = sdm::auth::loadOrCreate(dataDir);
    if (parser.isSet(portOption)) {
        bool okPort = false;
        const int cliPort = parser.value(portOption).toInt(&okPort);
        if (okPort && cliPort > 0 && cliPort <= 65535) {
            bridgeInfo.port = static_cast<quint16>(cliPort);
        }
    }

    WebSocketServer webSocket;
    QString wsError;
    if (webSocket.listen(bridgeInfo.port, bridgeInfo.token, &wsError)) {
        bridgeInfo.port = webSocket.port();
        sdm::auth::writeBridgeInfo(bridgeInfo); // el puerto puede variar si 9010 estaba ocupado
    } else {
        sdm::log::error("Puente IPC desactivado: {}", sdm::log::utf8(wsError));
    }

    ProtocolHandler protocol;
    protocol.setBridgeToken(bridgeInfo.token);
    protocol.setBridgePort(bridgeInfo.port);
    QObject::connect(&webSocket, &WebSocketServer::messageReceived, &protocol,
                     [&webSocket, &protocol](const QByteArray& raw, QObject* sender) {
                         const QByteArray response = protocol.process(raw);
                         if (!response.isEmpty()) {
                             webSocket.sendTo(sender, response);
                         }
                     });
    QObject::connect(&protocol, &ProtocolHandler::push, &webSocket, &WebSocketServer::broadcast);

    gui.applyBridgeInfo(bridgeInfo.port, bridgeInfo.token, bridgeInfo.filePath, &webSocket);
    // applyBridgeInfo ya registra el puente nativo y arranca el reintento
    // periodico hasta descubrir la extension del navegador.

    // Descarga pedida por linea de comandos (primer arranque).
    if (parser.isSet(urlOption)) {
        QVariantMap cliTask;
        cliTask.insert(QStringLiteral("url"), parser.value(urlOption));
        cliTask.insert(QStringLiteral("source"), QStringLiteral("cli"));
        gui.addTask(cliTask);
    }

    // --- Carga del QML ------------------------------------------------------
    QObject::connect(&qmlEngine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { std::fprintf(stderr, "SDM: no se pudo cargar la interfaz QML\n"); },
                     Qt::QueuedConnection);
    qmlEngine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (qmlEngine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "SDM: la interfaz QML esta vacia\n");
        curl_global_cleanup();
        return 1;
    }

    QQuickWindow* window = qobject_cast<QQuickWindow*>(qmlEngine.rootObjects().constFirst());
    if (window != nullptr) {
        gui.applyWindowChrome(window);
        if (parser.isSet(minimizedOption)) {
            window->showMinimized();
        }
    }

    // --- Elevacion desde otra instancia ------------------------------------
    QObject::connect(&instanceServer, &QLocalServer::newConnection, &app, [&instanceServer, window, &gui]() {
        while (instanceServer.hasPendingConnections()) {
            QLocalSocket* socket = instanceServer.nextPendingConnection();
            if (socket == nullptr) {
                break;
            }
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, window, &gui]() {
                const QByteArray data = socket->readAll();
                if (data.contains("raise") && window != nullptr) {
                    window->showNormal();
                    window->raise();
                    window->requestActivate();
                }
                // Enlaces reenviados por otra instancia o por un programa externo.
                const QList<QByteArray> lines = data.split('\n');
                for (const QByteArray& rawLine : lines) {
                    const QByteArray line = rawLine.trimmed();
                    if (!line.startsWith("url ")) {
                        continue;
                    }
                    const QString url = QString::fromUtf8(line.mid(4)).trimmed();
                    if (url.isEmpty()) {
                        continue;
                    }
                    QVariantMap options;
                    options.insert(QStringLiteral("url"), url);
                    options.insert(QStringLiteral("source"), QStringLiteral("cli"));
                    gui.addTask(options);
                }
            });
        }
    });

    // --- Cierre ordenado ----------------------------------------------------
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        DownloadEngine::instance().shutdown();
        webSocket.close();
        database.close();
        sdm::log::shutdown();
        curl_global_cleanup();
    });

    sdm::log::info("UI lista ({} clientes puente)", webSocket.clientCount());
    return app.exec();
}
