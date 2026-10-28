// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/database/Models.h
//
//  Modelos de datos compartidos por el motor, la base de datos, el puente IPC
//  y la capa de presentacion (MVVM).  Header-only por diseno (spec: Models.h).
// ============================================================================
#pragma once

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <array>

namespace sdm {

// ---------------------------------------------------------------------------
// Enumeraciones de dominio
// ---------------------------------------------------------------------------

/// Estados posibles de una tarea de descarga (maquina de estados del motor).
enum class DownloadState : int {
    Queued = 0,          // En cola, esperando hueco en el limitador global
    Connecting = 1,      // Sonda HEAD/GET al servidor (capacidades + tamano)
    Downloading = 2,     // Segmentos activos transfiriendo
    Paused = 3,          // Pausada por el usuario (reanudable byte exacto)
    Completed = 4,       // Descargada, verificada y renombrada (.part -> final)
    Failed = 5,          // Error permanente (reintentos agotados / enlace caido)
    WaitingRefresh = 6   // HTTP 403/401: esperando refresco de enlace del navegador
};

/// Categorias de archivo para el panel lateral (clasificacion por extension).
enum class FileCategory : int {
    Other = 0,
    Video = 1,
    Audio = 2,
    Archive = 3,
    Document = 4,
    Program = 5,
    Image = 6
};

/// Estados visuales de un segmento/hilo (Segment Visualizer).
enum class SegmentState : int {
    Pending = 0,      // Gris  - asignado, sin actividad
    Downloading = 1,  // Verde - transfiriendo
    Rebalancing = 2,  // Amarillo - re-segmentando / cediendo rango
    Completed = 3,    // Azul  - terminado y unificado en disco
    Error = 4         // Rojo  - error / reintentando conexion
};

/// Tipos de origen de una captura de enlace.
enum class TaskSource : int {
    Manual = 0,     // Dialogo "Nueva tarea"
    Clipboard = 1,  // Captura del portapapeles
    Browser = 2,    // Extension Manifest V3
    Stream = 3,     // Captura HLS / DASH
    Restored = 4    // Cargada de la base de datos al arrancar
};

// ---------------------------------------------------------------------------
// Constantes de dominio
// ---------------------------------------------------------------------------

/// Numero de hilos soportadas por el balanceador (spec 5.3).
inline constexpr std::array<int, 4> kThreadOptions = {1, 6, 8, 16};

/// Valor por defecto: Modo Equilibrado Optimo.
inline constexpr int kDefaultThreads = 8;

/// Tamano minimo de un segmento para poder re-balancearlo (bytes).
inline constexpr qint64 kMinRebalanceChunk = 8 * 1024 * 1024; // 8 MiB

/// Tamanio minimo de particion inicial para splitear (evita hilos inutiles).
inline constexpr qint64 kMinSegmentBytes = 64 * 1024; // 64 KiB

/// Reintentos por defecto ante fallos transitorios de red.
inline constexpr int kDefaultMaxRetries = 5;

/// Tareas en descarga simultaneas por defecto.
inline constexpr int kDefaultMaxConcurrent = 3;

/// Intentos de refresco de enlace expirado (HTTP 403) antes de fallar.
inline constexpr int kTokenRefreshTimeoutMs = 30000;

// ---------------------------------------------------------------------------
// Estructuras
// ---------------------------------------------------------------------------

/// Un segmento = un rango [startByte..endByte] (inclusivo) del archivo.
/// endByte == -1 significa "hasta el fin del archivo" (descarga sin Range).
struct SegmentInfo {
    int index = 0;
    qint64 startByte = 0;
    qint64 endByte = -1;
    qint64 receivedBytes = 0;
    SegmentState state = SegmentState::Pending;
    double bytesPerSec = 0.0;

    /// Tamano total del rango (-1 si es abierto).
    qint64 totalBytes() const {
        if (endByte < 0) {
            return -1;
        }
        return endByte - startByte + 1;
    }

    /// Bytes que faltan por recibir de este rango (-1 si desconocido).
    qint64 remainingBytes() const {
        if (endByte < 0) {
            return -1;
        }
        return std::max<qint64>(0, totalBytes() - receivedBytes);
    }

    /// Posicion absoluta actual de escritura en el archivo.
    qint64 currentOffset() const { return startByte + receivedBytes; }

    /// Progreso [0..1] del segmento (0 si el tamano es desconocido).
    double progress() const {
        const qint64 total = totalBytes();
        if (total <= 0) {
            return 0.0;
        }
        return std::clamp<double>(static_cast<double>(receivedBytes) / static_cast<double>(total), 0.0, 1.0);
    }

    bool isComplete() const { return endByte >= 0 && receivedBytes >= totalBytes(); }
};

/// Resultado de la sonda de capacidades del servidor (HEAD / Range 0-0).
struct ServerCapabilities {
    bool valid = false;
    bool supportsRange = false;
    qint64 totalSize = -1;
    QString etag;
    QString lastModified;
    QString fileName;        // Content-Disposition o ultimo segmento de la URL
    QString mimeType;        // Content-Type (sin parametros)
    QString contentDisposition;
    int httpStatus = 0;
    bool redirected = false;
    QString finalUrl;        // URL despues de redirecciones (302/303)
};

/// Una tarea de descarga completa (fila de la tabla `downloads`).
struct DownloadTask {
    quint64 id = 0;
    QString url;
    QString originUrl;       // URL de pagina de origen (para refresco de token)
    QString referer;
    QString cookies;         // Cabecera "Cookie:" completa
    QString userAgent;       // Vacio -> se usa el perfil activo del emulador
    QString destDir;
    QString fileName;
    QString mimeType;
    QString lastError;
    QString source;          // "manual" | "clipboard" | "browser" | "stream" | "restored"
    QString etag;            // Validacion de reanudacion (si cambia -> reinicio)
    QString lastModified;
    QString httpUser;        // Autenticacion HTTP basica (opcional)
    QString httpPass;
    QMap<QString, QString> extraHeaders; // Cabeceras personalizadas del usuario

    FileCategory category = FileCategory::Other;
    DownloadState state = DownloadState::Queued;

    qint64 totalBytes = -1;
    qint64 receivedBytes = 0;

    int threadCount = kDefaultThreads;
    int retries = 0;
    bool probed = false;       // true -> capacidades ya conocidas
    bool streamJob = false;    // true -> descarga HLS/DASH gestionada por StreamDownloader

    QDateTime createdAt;
    QDateTime updatedAt;
    double bytesPerSec = 0.0;

    QList<SegmentInfo> segments;

    /// Ruta final del archivo (sin sufijo .part).
    QString savePath() const {
        if (destDir.isEmpty()) {
            return fileName;
        }
        const QChar last = destDir.at(destDir.size() - 1);
        const QString sep = (last == QLatin1Char('/') || last == QLatin1Char('\\')) ? QString() : QStringLiteral("/");
        return destDir + sep + fileName;
    }

    /// Ruta temporal de escritura (mientras descarga).
    QString partPath() const { return savePath() + QStringLiteral(".part"); }

    /// Progreso global [0..1].
    double progress() const {
        if (state == DownloadState::Completed) {
            return 1.0;
        }
        if (totalBytes <= 0) {
            return 0.0;
        }
        return std::clamp<double>(static_cast<double>(receivedBytes) / static_cast<double>(totalBytes), 0.0, 1.0);
    }

    /// Suma los bytes recibidos de todos los segmentos (fuente de verdad).
    qint64 sumSegmentBytes() const {
        qint64 total = 0;
        for (const SegmentInfo& seg : segments) {
            total += seg.receivedBytes;
        }
        return total;
    }

    bool isActive() const {
        return state == DownloadState::Downloading || state == DownloadState::Connecting ||
               state == DownloadState::WaitingRefresh;
    }

    bool isFinishedTerminal() const {
        return state == DownloadState::Completed || state == DownloadState::Failed;
    }
};

/// Solicitud de creacion de tarea usada por UI e IPC.
struct NewTaskRequest {
    QString url;
    QString originUrl;
    QString referer;
    QString cookies;
    QString userAgent;
    QString destDir;
    QString fileName;      // Vacio -> resolucion automatica (Content-Disposition/URL)
    QString httpUser;
    QString httpPass;
    QMap<QString, QString> extraHeaders;
    QString source = QStringLiteral("manual");
    int threadCount = kDefaultThreads;
    bool autoStart = true;
};

// ---------------------------------------------------------------------------
// Segmentacion (spec 5.2: particionado inicial equilibrado)
// ---------------------------------------------------------------------------

/// Divide [0..total-1] en `threads` rangos contiguos repartiendo el resto
/// sobrante entre los primeros segmentos.  Devuelve la lista ordenada.
inline QList<SegmentInfo> splitRangeIntoSegments(qint64 total, int threads) {
    QList<SegmentInfo> out;
    if (total <= 0 || threads <= 0) {
        return out;
    }
    // Si el archivo es mas peque;o que threadCount * minimo, usa 1 hilo util.
    int usable = threads;
    if (total < kMinSegmentBytes * threads) {
        usable = static_cast<int>(std::max<qint64>(1, total / kMinSegmentBytes));
        usable = std::max(usable, 1);
        if (total < kMinSegmentBytes) {
            usable = 1;
        }
    }

    const qint64 base = total / usable;
    const qint64 remainder = total % usable;
    qint64 offset = 0;
    out.reserve(usable);
    for (int i = 0; i < usable; ++i) {
        const qint64 len = base + (i < remainder ? 1 : 0);
        SegmentInfo seg;
        seg.index = i;
        seg.startByte = offset;
        seg.endByte = offset + len - 1;
        seg.receivedBytes = 0;
        seg.state = SegmentState::Pending;
        out.push_back(seg);
        offset += len;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Helpers de clasificacion / presentacion
// ---------------------------------------------------------------------------

/// Extension minuscula sin punto ("" si no hay extension).
inline QString lowerExtension(const QString& fileName) {
    const int slash = static_cast<int>(std::max(fileName.lastIndexOf(QLatin1Char('/')), fileName.lastIndexOf(QLatin1Char('\\'))));
    const QString base = fileName.mid(slash + 1);
    const int dot = static_cast<int>(base.lastIndexOf(QLatin1Char('.')));
    if (dot <= 0 || dot == base.size() - 1) {
        return QString();
    }
    return base.mid(dot + 1).toLower();
}

/// Clasificacion automatica por extension de archivo (spec 4.2.1).
inline FileCategory categoryForFileName(const QString& fileName) {
    const QString ext = lowerExtension(fileName);
    if (ext.isEmpty()) {
        return FileCategory::Other;
    }
    static const QMap<QString, FileCategory> kMap = {
        // Video
        {QStringLiteral("mp4"), FileCategory::Video},   {QStringLiteral("mkv"), FileCategory::Video},
        {QStringLiteral("webm"), FileCategory::Video},  {QStringLiteral("avi"), FileCategory::Video},
        {QStringLiteral("mov"), FileCategory::Video},   {QStringLiteral("wmv"), FileCategory::Video},
        {QStringLiteral("flv"), FileCategory::Video},   {QStringLiteral("m4v"), FileCategory::Video},
        {QStringLiteral("mpg"), FileCategory::Video},   {QStringLiteral("mpeg"), FileCategory::Video},
        {QStringLiteral("ts"), FileCategory::Video},    {QStringLiteral("3gp"), FileCategory::Video},
        {QStringLiteral("vob"), FileCategory::Video},   {QStringLiteral("iso"), FileCategory::Archive},
        // Audio
        {QStringLiteral("mp3"), FileCategory::Audio},   {QStringLiteral("flac"), FileCategory::Audio},
        {QStringLiteral("wav"), FileCategory::Audio},   {QStringLiteral("aac"), FileCategory::Audio},
        {QStringLiteral("m4a"), FileCategory::Audio},   {QStringLiteral("ogg"), FileCategory::Audio},
        {QStringLiteral("opus"), FileCategory::Audio},  {QStringLiteral("wma"), FileCategory::Audio},
        {QStringLiteral("ape"), FileCategory::Audio},   {QStringLiteral("alac"), FileCategory::Audio},
        {QStringLiteral("mid"), FileCategory::Audio},   {QStringLiteral("midi"), FileCategory::Audio},
        // Comprimidos
        {QStringLiteral("zip"), FileCategory::Archive}, {QStringLiteral("rar"), FileCategory::Archive},
        {QStringLiteral("7z"), FileCategory::Archive},  {QStringLiteral("tar"), FileCategory::Archive},
        {QStringLiteral("gz"), FileCategory::Archive},  {QStringLiteral("bz2"), FileCategory::Archive},
        {QStringLiteral("xz"), FileCategory::Archive},  {QStringLiteral("tgz"), FileCategory::Archive},
        {QStringLiteral("lzma"), FileCategory::Archive},{QStringLiteral("cab"), FileCategory::Archive},
        {QStringLiteral("z"), FileCategory::Archive},   {QStringLiteral("lz4"), FileCategory::Archive},
        {QStringLiteral("dmg"), FileCategory::Archive}, {QStringLiteral("pkg"), FileCategory::Archive},
        // Documentos
        {QStringLiteral("pdf"), FileCategory::Document},{QStringLiteral("doc"), FileCategory::Document},
        {QStringLiteral("docx"), FileCategory::Document},{QStringLiteral("xls"), FileCategory::Document},
        {QStringLiteral("xlsx"), FileCategory::Document},{QStringLiteral("ppt"), FileCategory::Document},
        {QStringLiteral("pptx"), FileCategory::Document},{QStringLiteral("odt"), FileCategory::Document},
        {QStringLiteral("ods"), FileCategory::Document},{QStringLiteral("txt"), FileCategory::Document},
        {QStringLiteral("epub"), FileCategory::Document},{QStringLiteral("rtf"), FileCategory::Document},
        {QStringLiteral("csv"), FileCategory::Document},{QStringLiteral("md"), FileCategory::Document},
        // Programas
        {QStringLiteral("exe"), FileCategory::Program}, {QStringLiteral("msi"), FileCategory::Program},
        {QStringLiteral("msix"), FileCategory::Program},{QStringLiteral("appx"), FileCategory::Program},
        {QStringLiteral("deb"), FileCategory::Program}, {QStringLiteral("rpm"), FileCategory::Program},
        {QStringLiteral("appimage"), FileCategory::Program}, {QStringLiteral("bin"), FileCategory::Program},
        {QStringLiteral("apk"), FileCategory::Program}, {QStringLiteral("run"), FileCategory::Program},
        {QStringLiteral("dmgx"), FileCategory::Program},
        // Imagenes
        {QStringLiteral("png"), FileCategory::Image},   {QStringLiteral("jpg"), FileCategory::Image},
        {QStringLiteral("jpeg"), FileCategory::Image},  {QStringLiteral("webp"), FileCategory::Image},
        {QStringLiteral("gif"), FileCategory::Image},   {QStringLiteral("svg"), FileCategory::Image},
        {QStringLiteral("bmp"), FileCategory::Image},   {QStringLiteral("tif"), FileCategory::Image},
        {QStringLiteral("tiff"), FileCategory::Image},  {QStringLiteral("heic"), FileCategory::Image},
        // Descargas tipicas de gestores
        {QStringLiteral("torrent"), FileCategory::Archive},
    };
    const auto it = kMap.constFind(ext);
    return it == kMap.constEnd() ? FileCategory::Other : it.value();
}

/// Extrae un nombre de archivo razonable a partir de una URL.
inline QString fileNameFromUrl(const QString& url) {
    // Cortamos query y fragment.
    QString clean = url;
    const int q = static_cast<int>(clean.indexOf(QLatin1Char('?')));
    if (q >= 0) {
        clean = clean.left(q);
    }
    const int h = static_cast<int>(clean.indexOf(QLatin1Char('#')));
    if (h >= 0) {
        clean = clean.left(h);
    }
    const int slash = static_cast<int>(clean.lastIndexOf(QLatin1Char('/')));
    QString name = clean.mid(slash + 1);
    // Descodificacion minima de %20 y cia.
    name.replace(QStringLiteral("%20"), QStringLiteral(" "));
    name.replace(QStringLiteral("%21"), QStringLiteral("!"));
    name.replace(QStringLiteral("%23"), QStringLiteral("#"));
    name.replace(QStringLiteral("%25"), QStringLiteral("%"));
    name.replace(QStringLiteral("%28"), QStringLiteral("("));
    name.replace(QStringLiteral("%29"), QStringLiteral(")"));
    name.replace(QStringLiteral("%2B"), QStringLiteral("+"));
    if (name.isEmpty()) {
        name = QStringLiteral("descarga.bin");
    }
    return name;
}

/// Elimina caracteres prohibidos en nombres de archivo (Windows/POSIX).
inline QString sanitizeFileName(const QString& name) {
    static const QString kIllegal = QStringLiteral("<>:\"/\\|?*");
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        if (c.unicode() < 0x20 || kIllegal.contains(c)) {
            out.append(QLatin1Char('_'));
        } else {
            out.append(c);
        }
    }
    while (out.startsWith(QLatin1Char(' ')) || out.startsWith(QLatin1Char('.'))) {
        out.remove(0, 1);
    }
    if (out.isEmpty()) {
        out = QStringLiteral("descarga.bin");
    }
    // Longitud maxima practica (NTFS: 255 caracteres utiles).
    if (out.size() > 200) {
        const QString ext = lowerExtension(out);
        out = out.left(ext.isEmpty() ? 200 : 196 - ext.size());
        if (!ext.isEmpty()) {
            out += QLatin1Char('.') + ext;
        }
    }
    return out;
}

/// Texto de estado en espanol (etiquetas de la UI).
inline QString stateText(DownloadState s) {
    switch (s) {
        case DownloadState::Queued: return QStringLiteral("En cola");
        case DownloadState::Connecting: return QStringLiteral("Conectando");
        case DownloadState::Downloading: return QStringLiteral("Descargando");
        case DownloadState::Paused: return QStringLiteral("Pausado");
        case DownloadState::Completed: return QStringLiteral("Completado");
        case DownloadState::Failed: return QStringLiteral("Fallido");
        case DownloadState::WaitingRefresh: return QStringLiteral("Enlace expirado");
    }
    return QStringLiteral("Desconocido");
}

/// Color de acento por estado (paleta Fluent oscura/claro por defecto).
inline QString stateColorHex(DownloadState s) {
    switch (s) {
        case DownloadState::Queued: return QStringLiteral("#9aa0a6");       // gris
        case DownloadState::Connecting: return QStringLiteral("#e8a33d");    // ambar
        case DownloadState::Downloading: return QStringLiteral("#2ecc71");   // verde
        case DownloadState::Paused: return QStringLiteral("#e0b13a");        // amarillo
        case DownloadState::Completed: return QStringLiteral("#3b9dff");     // azul
        case DownloadState::Failed: return QStringLiteral("#e74c3c");        // rojo
        case DownloadState::WaitingRefresh: return QStringLiteral("#ff7b3d"); // naranja
    }
    return QStringLiteral("#9aa0a6");
}

/// Iconos emoji por estado (mockup de la spec 4.1).
inline QString stateIcon(DownloadState s) {
    switch (s) {
        case DownloadState::Queued: return QStringLiteral("⏳");
        case DownloadState::Connecting: return QStringLiteral("🔌");
        case DownloadState::Downloading: return QStringLiteral("⏬");
        case DownloadState::Paused: return QStringLiteral("⏸");
        case DownloadState::Completed: return QStringLiteral("✔");
        case DownloadState::Failed: return QStringLiteral("✖");
        case DownloadState::WaitingRefresh: return QStringLiteral("🔑");
    }
    return QStringLiteral("•");
}

inline QString stateToDbKey(DownloadState s) { return QString::number(static_cast<int>(s)); }
inline DownloadState stateFromInt(int v) { return static_cast<DownloadState>(v); }

inline QString categoryText(FileCategory c) {
    switch (c) {
        case FileCategory::Video: return QStringLiteral("Videos");
        case FileCategory::Audio: return QStringLiteral("Música");
        case FileCategory::Archive: return QStringLiteral("Comprimidos");
        case FileCategory::Document: return QStringLiteral("Documentos");
        case FileCategory::Program: return QStringLiteral("Programas");
        case FileCategory::Image: return QStringLiteral("Imágenes");
        case FileCategory::Other: return QStringLiteral("Otros");
    }
    return QStringLiteral("Otros");
}

/// Iconos emoji por categoria (panel lateral).
inline QString categoryIcon(FileCategory c) {
    switch (c) {
        case FileCategory::Video: return QStringLiteral("🎥");
        case FileCategory::Audio: return QStringLiteral("🎵");
        case FileCategory::Archive: return QStringLiteral("📦");
        case FileCategory::Document: return QStringLiteral("📄");
        case FileCategory::Program: return QStringLiteral("⚙️");
        case FileCategory::Image: return QStringLiteral("🖼️");
        case FileCategory::Other: return QStringLiteral("📥");
    }
    return QStringLiteral("📥");
}

inline QString segmentStateText(SegmentState s) {
    switch (s) {
        case SegmentState::Pending: return QStringLiteral("Pendiente");
        case SegmentState::Downloading: return QStringLiteral("Descargando");
        case SegmentState::Rebalancing: return QStringLiteral("Re-segmentando");
        case SegmentState::Completed: return QStringLiteral("Completado");
        case SegmentState::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("?");
}

inline QString segmentStateHex(SegmentState s) {
    switch (s) {
        case SegmentState::Pending: return QStringLiteral("#60646c");  // gris
        case SegmentState::Downloading: return QStringLiteral("#2ecc71"); // verde
        case SegmentState::Rebalancing: return QStringLiteral("#f1c40f"); // amarillo
        case SegmentState::Completed: return QStringLiteral("#3b9dff");   // azul
        case SegmentState::Error: return QStringLiteral("#e74c3c");       // rojo
    }
    return QStringLiteral("#60646c");
}

inline QString sourceToDbKey(TaskSource s) {
    switch (s) {
        case TaskSource::Manual: return QStringLiteral("manual");
        case TaskSource::Clipboard: return QStringLiteral("clipboard");
        case TaskSource::Browser: return QStringLiteral("browser");
        case TaskSource::Stream: return QStringLiteral("stream");
        case TaskSource::Restored: return QStringLiteral("restored");
    }
    return QStringLiteral("manual");
}

inline TaskSource sourceFromDbKey(const QString& key) {
    if (key == QLatin1String("clipboard")) return TaskSource::Clipboard;
    if (key == QLatin1String("browser")) return TaskSource::Browser;
    if (key == QLatin1String("stream")) return TaskSource::Stream;
    if (key == QLatin1String("restored")) return TaskSource::Restored;
    return TaskSource::Manual;
}

// ---------------------------------------------------------------------------
// Formateo legible (es-ES)
// ---------------------------------------------------------------------------

/// Tamano legible: 5.4 GB, 450 MB, 12 KB...
inline QString humanSize(qint64 bytes) {
    if (bytes < 0) {
        return QStringLiteral("--");
    }
    const double b = static_cast<double>(bytes);
    if (bytes >= 1073741824LL) {
        return QString::asprintf("%.2f GB", b / 1073741824.0);
    }
    if (bytes >= 1048576LL) {
        return QString::asprintf("%.1f MB", b / 1048576.0);
    }
    if (bytes >= 1024LL) {
        return QString::asprintf("%.1f KB", b / 1024.0);
    }
    return QString::asprintf("%lld B", static_cast<long long>(bytes));
}

/// Velocidad legible: 45.2 MB/s, 1.2 GB/s...
inline QString humanSpeed(double bytesPerSec) {
    if (bytesPerSec <= 0.0) {
        return QStringLiteral("--");
    }
    if (bytesPerSec >= 1073741824.0) {
        return QString::asprintf("%.2f GB/s", bytesPerSec / 1073741824.0);
    }
    if (bytesPerSec >= 1048576.0) {
        return QString::asprintf("%.1f MB/s", bytesPerSec / 1048576.0);
    }
    if (bytesPerSec >= 1024.0) {
        return QString::asprintf("%.1f KB/s", bytesPerSec / 1024.0);
    }
    return QString::asprintf("%.0f B/s", bytesPerSec);
}

/// Tiempo restante legible: 01m 12s / 1h 05m / --.
inline QString humanEta(qint64 seconds) {
    if (seconds < 0) {
        return QStringLiteral("--");
    }
    if (seconds >= 3600) {
        return QString::asprintf("%lldh %02lldm", static_cast<long long>(seconds / 3600),
                                 static_cast<long long>((seconds % 3600) / 60));
    }
    if (seconds >= 60) {
        return QString::asprintf("%02lldm %02llds", static_cast<long long>(seconds / 60),
                                 static_cast<long long>(seconds % 60));
    }
    return QString::asprintf("%02llds", static_cast<long long>(seconds));
}

/// Porcentaje entero legible ("68%").
inline QString humanPercent(double fraction) {
    const int pct = static_cast<int>(std::clamp(fraction, 0.0, 1.0) * 100.0 + 0.5);
    return QString::number(pct) + QLatin1Char('%');
}

/// Rango "bytes=a-b" para la cabecera HTTP Range (spec 5.1).
inline QString rangeHeaderValue(const SegmentInfo& seg) {
    if (seg.endByte < 0) {
        return QStringLiteral("bytes=%1-").arg(seg.startByte);
    }
    return QStringLiteral("bytes=%1-%2").arg(seg.startByte).arg(seg.endByte);
}

} // namespace sdm
