// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/bridge/NativeBridgeRegistrar.cpp
// ============================================================================
#include "bridge/NativeBridgeRegistrar.h"

#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>

namespace sdm::bridge {
namespace {

constexpr char kExtensionNameHint[] = "s-download manager";
constexpr int kExtensionIdLength = 32; // a-p, 32 caracteres

QString fromNative(const QString& path) { return QDir::fromNativeSeparators(path).toLower(); }

/// Un ID de extension Chromium valido son 32 caracteres en el rango a-p.
bool isValidExtensionId(const QString& id)
{
    if (id.size() != kExtensionIdLength) {
        return false;
    }
    for (const QChar ch : id) {
        if (ch < QLatin1Char('a') || ch > QLatin1Char('p')) {
            return false;
        }
    }
    return true;
}

/// Normaliza lo que escriba el usuario: acepta el ID pelado o el origen entero.
QString toOrigin(const QString& input)
{
    QString value = input.trimmed();
    if (value.isEmpty()) {
        return QString();
    }
    value.remove(QStringLiteral("chrome-extension://"));
    value.remove(QLatin1Char('/'));
    if (!isValidExtensionId(value)) {
        return QString();
    }
    return QStringLiteral("chrome-extension://%1/").arg(value);
}

/// Comprueba si una entrada de extensions.settings es la extension de SDM.
bool looksLikeSdmExtension(const QJsonObject& entry)
{
    const QJsonObject manifest = entry.value(QStringLiteral("manifest")).toObject();
    const QString name = manifest.value(QStringLiteral("name")).toString().toLower();
    if (name.contains(QString::fromLatin1(kExtensionNameHint))) {
        return true;
    }
    // Extension cargada como desempaquetada: la ruta termina en la carpeta
    // `extension` del proyecto.
    const QString path = fromNative(entry.value(QStringLiteral("path")).toString());
    if (path.isEmpty()) {
        return false;
    }
    return path.endsWith(QStringLiteral("/extension"))
           || path.contains(QStringLiteral("/extension/"));
}

/// Ficheros Preferences de todos los perfiles de un navegador Chromium.
QStringList profilePreferenceFiles(const QString& userDataDir)
{
    QStringList files;
    const QDir root(userDataDir);
    if (!root.exists()) {
        return files;
    }
    const QStringList entries =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
    for (const QString& name : entries) {
        const QString candidate = root.filePath(name + QStringLiteral("/Preferences"));
        if (QFileInfo::exists(candidate)) {
            files.append(candidate);
        }
    }
    return files;
}

bool writeJsonFile(const QString& path, const QJsonObject& object, QStringList* warnings)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (warnings != nullptr) {
            *warnings << QStringLiteral("No se pudo crear %1").arg(info.absolutePath());
        }
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (warnings != nullptr) {
            *warnings << QStringLiteral("No se pudo escribir %1").arg(path);
        }
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QJsonObject chromiumManifest(const QString& hostPath, const QStringList& origins)
{
    QJsonObject root;
    root.insert(QStringLiteral("name"), QString::fromLatin1(kNativeHostName));
    root.insert(QStringLiteral("description"),
                QStringLiteral("Puente nativo de S-Download Manager (SDM)"));
    root.insert(QStringLiteral("path"), hostPath);
    root.insert(QStringLiteral("type"), QStringLiteral("stdio"));
    QJsonArray allowed;
    for (const QString& origin : origins) {
        allowed.append(origin);
    }
    root.insert(QStringLiteral("allowed_origins"), allowed);
    return root;
}

QJsonObject firefoxManifest(const QString& hostPath)
{
    QJsonObject root;
    root.insert(QStringLiteral("name"), QString::fromLatin1(kNativeHostName));
    root.insert(QStringLiteral("description"),
                QStringLiteral("Puente nativo de S-Download Manager (SDM)"));
    root.insert(QStringLiteral("path"), hostPath);
    root.insert(QStringLiteral("type"), QStringLiteral("stdio"));
    QJsonArray allowed;
    allowed.append(QString::fromLatin1(kFirefoxExtensionId));
    root.insert(QStringLiteral("allowed_extensions"), allowed);
    return root;
}

#if defined(Q_OS_WIN)
/// Registra el host en HKCU (no requiere administrador).
bool registerInRegistry(const QString& vendorKey, const QString& manifestPath,
                        QStringList* warnings)
{
    if (vendorKey.isEmpty()) {
        return false;
    }
    QSettings registry(vendorKey + QStringLiteral("/NativeMessagingHosts"),
                       QSettings::NativeFormat);
    registry.setValue(QString::fromLatin1(kNativeHostName), QDir::toNativeSeparators(manifestPath));
    registry.sync();
    if (registry.status() != QSettings::NoError) {
        if (warnings != nullptr) {
            *warnings << QStringLiteral("No se pudo registrar HKCU\\%1").arg(vendorKey);
        }
        return false;
    }
    return true;
}
#endif

} // namespace

QList<BrowserTarget> browserTargets()
{
    QList<BrowserTarget> targets;

#if defined(Q_OS_WIN)
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    const QString roaming = qEnvironmentVariable("APPDATA");
    if (!local.isEmpty()) {
        targets.append({QStringLiteral("Chrome"), local + QStringLiteral("/Google/Chrome/User Data"),
                        QStringLiteral("Software\\Google\\Chrome"), false});
        targets.append({QStringLiteral("Chromium"), local + QStringLiteral("/Chromium/User Data"),
                        QStringLiteral("Software\\Chromium"), false});
        targets.append({QStringLiteral("Edge"),
                        local + QStringLiteral("/Microsoft/Edge/User Data"),
                        QStringLiteral("Software\\Microsoft\\Edge"), false});
        targets.append({QStringLiteral("Brave"),
                        local + QStringLiteral("/BraveSoftware/Brave-Browser/User Data"),
                        QStringLiteral("Software\\BraveSoftware\\Brave-Browser"), false});
        targets.append({QStringLiteral("Vivaldi"), local + QStringLiteral("/Vivaldi/User Data"),
                        QStringLiteral("Software\\Vivaldi"), false});
    }
    if (!roaming.isEmpty()) {
        targets.append({QStringLiteral("Opera"),
                        roaming + QStringLiteral("/Opera Software/Opera Stable"),
                        QStringLiteral("Software\\Opera Software"), false});
        targets.append({QStringLiteral("Firefox"),
                        roaming + QStringLiteral("/Mozilla/NativeMessagingHosts"), QString(),
                        true});
    }
#elif defined(Q_OS_LINUX)
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString configRoot = config.isEmpty() ? home + QStringLiteral("/.config") : config;
    targets.append({QStringLiteral("Chrome"), configRoot + QStringLiteral("/google-chrome"),
                    QString(), false});
    targets.append({QStringLiteral("Chromium"), configRoot + QStringLiteral("/chromium"),
                    QString(), false});
    targets.append({QStringLiteral("Edge"), configRoot + QStringLiteral("/microsoft-edge"),
                    QString(), false});
    targets.append({QStringLiteral("Brave"),
                    configRoot + QStringLiteral("/BraveSoftware/Brave-Browser"), QString(), false});
    targets.append({QStringLiteral("Vivaldi"), configRoot + QStringLiteral("/vivaldi"), QString(),
                    false});
    targets.append({QStringLiteral("Opera"), configRoot + QStringLiteral("/opera"), QString(),
                    false});
    targets.append({QStringLiteral("Firefox"),
                    home + QStringLiteral("/.mozilla/native-messaging-hosts"), QString(), true});
#endif
    return targets;
}

QStringList discoverChromiumExtensionIds()
{
    QStringList origins;
    for (const BrowserTarget& target : browserTargets()) {
        if (target.firefox) {
            continue;
        }
        const QStringList files = profilePreferenceFiles(target.userDataDir);
        for (const QString& prefPath : files) {
            QFile file(prefPath);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QByteArray raw = file.readAll();
            file.close();
            const QJsonDocument document = QJsonDocument::fromJson(raw);
            if (!document.isObject()) {
                continue;
            }
            const QJsonObject settings = document.object()
                                             .value(QStringLiteral("extensions"))
                                             .toObject()
                                             .value(QStringLiteral("settings"))
                                             .toObject();
            for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
                const QString id = it.key();
                if (!isValidExtensionId(id)) {
                    continue;
                }
                if (!looksLikeSdmExtension(it.value().toObject())) {
                    continue;
                }
                const QString origin = QStringLiteral("chrome-extension://%1/").arg(id);
                if (!origins.contains(origin)) {
                    origins.append(origin);
                    log::info("Extension SDM detectada en {}: {}", log::utf8(target.label),
                              log::utf8(origin));
                }
            }
        }
    }
    return origins;
}

NativeBridgeRegistration registerNativeBridge(const QString& hostExecutablePath,
                                              const QString& manifestDir,
                                              const QStringList& extraChromiumIds)
{
    NativeBridgeRegistration result;
    const QFileInfo hostInfo(hostExecutablePath);
    if (!hostInfo.exists()) {
        result.warnings << QStringLiteral("No existe el host nativo: %1").arg(hostExecutablePath);
        return result;
    }
    const QString hostPath = QDir::toNativeSeparators(hostInfo.absoluteFilePath());

    QStringList origins = discoverChromiumExtensionIds();
    for (const QString& raw : extraChromiumIds) {
        const QString origin = toOrigin(raw);
        if (origin.isEmpty()) {
            result.warnings << QStringLiteral("ID de extension no valido: %1").arg(raw);
            continue;
        }
        if (!origins.contains(origin)) {
            origins.append(origin);
        }
    }
    result.extensionIds = origins;

    const QDir dir(manifestDir);
    if (!dir.exists() && !QDir().mkpath(manifestDir)) {
        result.warnings << QStringLiteral("No se pudo crear %1").arg(manifestDir);
        return result;
    }

    // --- Manifiestos ------------------------------------------------------
    const QString chromiumPath = dir.filePath(QStringLiteral("com.sdm.bridge.json"));
    if (writeJsonFile(chromiumPath, chromiumManifest(hostPath, origins), &result.warnings)) {
        result.manifestPath = chromiumPath;
        result.ok = true;
    }
    const QString firefoxPath = dir.filePath(QStringLiteral("com.sdm.bridge.firefox.json"));
    if (writeJsonFile(firefoxPath, firefoxManifest(hostPath), &result.warnings)) {
        result.firefoxManifestPath = firefoxPath;
    }

    if (origins.isEmpty()) {
        result.warnings << QStringLiteral(
            "Aun no se ha detectado la extension en ningun navegador: cargue 'extension' como "
            "desempaquetada y SDM la registrara sola en unos segundos.");
    }

    // --- Registro por navegador instalado --------------------------------
    for (const BrowserTarget& target : browserTargets()) {
        if (!QFileInfo::exists(target.userDataDir)) {
            continue; // navegador no instalado / nunca ejecutado
        }
        if (target.firefox) {
            if (writeJsonFile(target.userDataDir + QStringLiteral("/com.sdm.bridge.json"),
                              firefoxManifest(hostPath), &result.warnings)) {
                result.browsers.append(target.label);
            }
            continue;
        }
        bool registered = false;
#if defined(Q_OS_WIN)
        registered = registerInRegistry(target.registryKey, chromiumPath, &result.warnings);
        // Copia en el directorio de datos: refuerza el registro si el
        // navegador ignora HKCU o si la clave se borro.
        writeJsonFile(target.userDataDir + QStringLiteral("/NativeMessagingHosts/com.sdm.bridge.json"),
                      chromiumManifest(hostPath, origins), &result.warnings);
#else
        registered = writeJsonFile(
            target.userDataDir + QStringLiteral("/NativeMessagingHosts/com.sdm.bridge.json"),
            chromiumManifest(hostPath, origins), &result.warnings);
#endif
        if (registered) {
            result.browsers.append(target.label);
        }
    }

    if (result.browsers.isEmpty()) {
        result.warnings << QStringLiteral("No se detecto ningun navegador instalado.");
    } else {
        log::info("Puente nativo registrado en: {}", log::utf8(result.browsers.join(", ")));
    }
    return result;
}

} // namespace sdm::bridge
