# Registro de cambios

Todos los cambios notables de este proyecto se documentan aquí.
El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el
proyecto usa [versionado semántico](https://semver.org/lang/es/).

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

### Corregido
- Error de compilación en `SegmentWorker::writeThunk`: el callback estático de
  libcurl accedía a miembros de instancia sin `self->` (protección por rango
  y sniffing anti-HTML).
- Todos los warnings de conversión `qsizetype` → `int` (`-Wconversion`) en
  `Models.h`, `BrowserEmulator.cpp`, `DownloadEngine.cpp`, `StreamDownloader.cpp`,
  `DatabaseManager.cpp`, `WebSocketServer.cpp` y `GuiManager.cpp`; constantes de
  User-Agent sin uso reutilizadas para construir el pool de rotación.
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
