// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/bridge/NativeBridgeRegistrar.h
//
//  Auto-configuracion del puente Native Messaging (cero configuracion manual):
//   1. Descubre el ID de la extension de SDM leyendo los perfiles de los
//      navegadores Chromium instalados (Preferences -> extensions.settings).
//   2. Escribe/actualiza los manifiestos com.sdm.bridge para TODOS los
//      navegadores detectados (Chrome, Chromium, Edge, Brave, Vivaldi, Opera
//      y Firefox) sin pedir administrador.
//   3. Registra las claves HKCU necesarias en Windows (o los directorios de
//      usuario en Linux). Idempotente: se puede ejecutar en cada arranque.
//
//  Firefox no necesita descubrimiento: la extension declara un id fijo
//  (browser_specific_settings.gecko.id = sdm@sdm.local).
// ============================================================================
#pragma once

#include <QString>
#include <QStringList>

namespace sdm::bridge {

/// Nombre del host de Native Messaging (debe coincidir con la extension).
inline constexpr char kNativeHostName[] = "com.sdm.bridge";
/// Id fijo de la extension en Firefox.
inline constexpr char kFirefoxExtensionId[] = "sdm@sdm.local";

/// Resultado de una pasada de auto-configuracion.
struct NativeBridgeRegistration {
    bool ok = false;                 ///< Se escribio al menos el manifiesto Chromium.
    QString manifestPath;            ///< Manifiesto compartido por la familia Chromium.
    QString firefoxManifestPath;     ///< Manifiesto de Firefox.
    QStringList extensionIds;        ///< IDs descubiertos ("chrome-extension://<id>/").
    QStringList browsers;            ///< Navegadores registrados (etiquetas legibles).
    QStringList warnings;            ///< Incidencias no fatales (para el log/UI).
};

/// Navegador detectado en el sistema.
struct BrowserTarget {
    QString label;      ///< Nombre legible ("Chrome", "Edge", ...).
    QString userDataDir;///< Directorio de datos de usuario (puede no existir).
    QString registryKey;///< Clave HKCU de NativeMessagingHosts (solo Windows).
    bool firefox = false;
};

/// Lista de navegadores candidatos con sus rutas reales en este sistema.
QList<BrowserTarget> browserTargets();

/// Busca el ID de la extension SDM en los perfiles de los navegadores Chromium
/// instalados. Devuelve el origen completo ("chrome-extension://<id>/").
QStringList discoverChromiumExtensionIds();

/// Registra el puente nativo para todos los navegadores detectados.
/// @param hostExecutablePath Ruta absoluta de SDMNativeHost(.exe).
/// @param manifestDir       Directorio donde se escriben los manifiestos.
/// @param extraChromiumIds  IDs adicionales (ajuste manual del usuario).
NativeBridgeRegistration registerNativeBridge(const QString& hostExecutablePath,
                                              const QString& manifestDir,
                                              const QStringList& extraChromiumIds = {});

} // namespace sdm::bridge
