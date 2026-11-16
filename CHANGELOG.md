# Registro de cambios

Todos los cambios notables de este proyecto se documentan aquí.
El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el
proyecto usa [versionado semántico](https://semver.org/lang/es/).

## [No publicado]

### Añadido

**Conexión automática software ↔ extensión (cero configuración)**
- `src/bridge/NativeBridgeRegistrar`: SDM detecta el ID de la extensión leyendo
  los perfiles de los navegadores Chromium (`extensions.settings`) y registra el
  puente nativo `com.sdm.bridge` en Chrome, Chromium, Edge, Brave, Vivaldi y
  Opera (HKCU en Windows, `NativeMessagingHosts` en Linux). Reintenta cada
  minuto hasta encontrarla, así que también funciona si la extensión se carga
  después de arrancar SDM.
- Firefox queda registrado sin descubrimiento: el paquete declara el id fijo
  `sdm@sdm.local` (`browser_specific_settings.gecko.id`).
- El instalador ya no pregunta el ID de la extensión (se eliminó la página
  personalizada); deja `allowed_origins` vacío y SDM lo rellena solo.
- La extensión recibe `puerto:token` del WebSocket local a través de
  `getBridgeInfo`, de modo que el canal de respaldo también se configura solo.
- `SDMNativeHost` arranca SDM (`--minimized`) si el navegador lo lanza y el
  gestor no está en ejecución.

**Botón flotante junto al reproductor**
- El botón se ancla al `<video>` visible (esquina superior derecha del
  reproductor), se recoloca con scroll/redimensionado y cae a la esquina
  inferior derecha cuando la detección viene de la red y no hay reproductor.
- UI en Shadow DOM: el CSS de la web no puede romperla ni ocultarla.

**Empaquetado**
- `scripts/package_extension.py` genera también
  `dist/sdm-extension-firefox-<ver>.zip` (MV3 con event page) listo para AMO.

### Añadido (paridad con IDM)

- **Descargas por lote con patrón** (`Models.h::expandBatchPattern`):
  `archivo[01-10].zip`, `[1,3,7]`, `[a-z]`, `[0-100:10]` y varios grupos por URL,
  con tope de 2000 URLs por patrón. Se intercepta en `GuiManager::addTask`, así
  que funciona desde la UI, el puente y la CLI. Cubierto por 5 tests nuevos.
- **Límite de velocidad por tarea** (`EngineSettings::speedLimitKbps`, 0 = sin
  límite): se reparte entre las conexiones activas de cada tarea y se aplica con
  `CURLOPT_MAX_RECV_SPEED_LARGE` en cada segmento. Control en Ajustes → «Límite
  de velocidad».
- **Monitor de portapapeles** (Ajustes → «Añadir enlaces copiados al
  portapapeles»): al copiar una URL http/https/ftp se encola sola.
- **CLI `--url` / `-u`**: `SDM --url "<enlace>"` añade la descarga; si ya hay una
  instancia abierta se la reenvía por `QLocalServer` (también admite lotes).
- **Panel «Medios detectados» en el popup** (estilo media grabber de IDM): lista
  todas las transmisiones/vídeos de la página con botón de descarga individual.
- **Detección de duplicados** (`DownloadEngine::findDuplicateTask`): repetir una
  URL ya activa devuelve la tarea existente en vez de duplicarla (la comparación
  normaliza esquema, host y fragmento). Las tareas completadas o fallidas sí se
  pueden repetir.
- **Organización automática por tipo** (`organizeByCategory`): guarda en
  `Vídeos/`, `Música/`, `Comprimidos/`, `Documentos/`, `Programas/`, `Imágenes/`
  u `Otros/` cuando la tarea usa la carpeta por defecto.
- **Apagar el equipo al terminar la cola** (`shutdownWhenDone`): vigila el fin de
  la cola y programa `shutdown /s /t 60` (Windows) o `systemctl poweroff`
  (Linux), con margen de cancelación.
- **Comprobación de actualizaciones**: consulta
  `api.github.com/repos/LiebeBlack/SDM/releases/latest` 20 s después de arrancar,
  compara versiones (`compareVersions`) y avisa; botones «Buscar
  actualizaciones» y «Descargar novedad» en Ajustes → Acerca de.

### Corregido
- `service_worker.js` llamaba a `sendHello()`, que no existía: la excepción se
  tragaba el `try/catch` de `connectNative()` y el canal nativo se descartaba
  siempre (por eso hacía falta configurar el token a mano).
- El manifiesto nativo que escribía la app se llamaba `com.sdm.nativehost`
  (distinto del `com.sdm.bridge` que usa la extensión) y se guardaba en una ruta
  que ningún navegador lee.
- El canal nativo deshabilitado ya no deja el WebSocket sin intentar: el
  fallback se abre de inmediato y el nativo se reintenta cada 30 s.

## [1.0.0] - 2025-01-01

Primera versión de producción del gestor de descargas S-Download Manager (SDM).

### Añadido

**Motor de descarga**
- Descarga multihilo con segmentación dinámica, rebalanceo de segmentos y
  reanudación por cabeceras HTTP `Range` (ETag / Last-Modified).
- Cola de descargas con límite de descargas concurrentes y reintento
  configurable con backoff progresivo.
- **Anti-HTML Guard**: detección de respuestas web (DOCTYPE, `<html>`, BOM,
  prologo XML, comentarios) con umbral de 512 bytes; nunca se guarda una página
  web como archivo. Primer aviso pide un enlace nuevo (refresco automático),
  el segundo fallo marca la tarea como fallida (anti-bucle).
- Protección por rango en cada segmento: nunca se escribe más allá del fin
  asignado aunque el servidor envíe datos de más.
- `StreamDownloader` para descargas que no soportan `Range` (flujo único).

**Puente de navegador**
- Host de mensajería nativa (`com.sdm.bridge`) para Chrome/Edge/Firefox y
  fallback WebSocket local con token de conexión.
- Extensión MV3: captura de descargas del navegador, menú contextual
  (descargar enlace, vídeo/imagen, todos los enlaces de la página),
  recopilación de enlaces y captura de medios de la página.

**Interfaz**
- UI Qt Quick con diseño Fluent/Mica, tema claro/oscuro, tarjetas con
  sombras y gradientes, píldoras de estado, gráfico de velocidad en vivo,
  toasts animados y barra de segmentos con telemetría.
- Diálogo de ajustes con pestañas (General, Red, Multimedia, Conexión,
  Acerca de), subrayado de pestaña activa animado y validación de
  configuración.
- Diálogos (Nueva descarga, Ajustes, Eliminar, Calidades) con cabecera y
  pie propios alineados con los design tokens; selector de hora programada
  (HH:MM) con campos numéricos temáticos.
- Programación de inicio con cola múltiple: varias tareas programadas
  coexisten y arrancan a su hora sin pisarse.
- Atajos de teclado, búsqueda y filtros por estado/categoría.

**Calidad**
- Suite de pruebas unitarias CTest (`tests/`): particionado de rangos,
  sanitización de nombres, detección anti-HTML, formateadores y capa de
  persistencia SQLite (round-trip, cascadas, refresco de URL).
- Esquema de base de datos versionado (`PRAGMA user_version`) con
  migraciones incrementales y rechazo claro de bases de versiones futuras.
- Logger con rotación de archivos (5 MB) y empaquetado portátil + instalador
  NSIS + extensión en CI.

### Seguridad
- Tokens de conexión para el fallback WebSocket; el manifiesto del host nativo
  se genera con rutas absolutas del usuario.
- Sanitización de nombres de archivo (traversal, caracteres ilegales,
  truncado seguro conservando extensión).
- Puente WebSocket endurecido: frames limitados a 1 MiB
  (`setMaxAllowedIncomingMessageSize`), handshake con timeout de 5 s y tope de
  16 conexiones pendientes de autenticar para no agotar memoria ni el puerto.

### Corregido
- Error de compilación en `SegmentWorker::writeThunk`: el callback estático de
  libcurl accedía a miembros de instancia sin `self->` (protección por rango
  y sniffing anti-HTML).
- `QJsonDocument::toObject()` inexistente en el handshake WebSocket (la API es
  `object()`): ningún cliente podía autenticarse por el mensaje `hello`.
- Macros `min`/`max` de `windows.h` rompían `std::max(...)` en `Models.h`:
  llamadas parentizadas `(std::max)` y `NOMINMAX`/`WIN32_LEAN_AND_MEAN`
  definidos globalmente en MSVC.
- Desplazamientos de 32 bits en el cálculo de backoff (`1 << shift`) que MSVC
  estrechaban a 64 bits (C4334): ahora `qint64(1) << shift`.
- Todos los warnings de conversión `qsizetype` → `int` (`-Wconversion`) en
  `Models.h`, `BrowserEmulator.cpp`, `DownloadEngine.cpp`, `StreamDownloader.cpp`,
  `DatabaseManager.cpp`, `WebSocketServer.cpp` y `GuiManager.cpp`; constantes de
  User-Agent sin uso reutilizadas para construir el pool de rotación.
- Estándar C++ declarado a CMake (`CMAKE_CXX_STANDARD`): Qt6 imponía
  `cxx_std_17` y generaba un `-std` redundante (D9025 en MSVC).
- Diálogos que no se abrían por usar la propiedad inexistente `Dialog.titleItem`
  y el tipo inexistente `TimeEdit` de Qt Widgets en QML; sustituidos por
  cabeceras propias y un selector HH:MM sobre `SpinBox`.
- La carpeta elegida en el diálogo nativo nunca llegaba al formulario
  (`FolderDialog.selectedFolder` no existe; la API es `folder`).
- `segmentsModel.rowCount()` se invocaba desde QML sin ser invocable; expuesto
  `SegmentModel::count()`.
- Los pies de los diálogos usaban `Layout.margins` fuera de un Layout (se
  ignoraban); envueltos con padding real.
- El botón «Restaurar» de la carpeta de descargas no persistía el cambio;
  el borrado confirmado dependía de la fila visible (ahora es por identificador).
