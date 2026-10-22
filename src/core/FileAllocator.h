// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/FileAllocator.h
//
//  Reservador de espacio en disco y escritor asincrono por desplazamientos
//  (spec 5.2.5): cada hilo escribe en su rango mediante offsets directos,
//  sin fusion de ficheros temporales al finalizar.
// ============================================================================
#pragma once

#include <QString>

#include <mutex>

namespace sdm {

/// Allocador de fichero con escritura aleatoria por offsets.
/// Thread-safe: cada SegmentWorker llama a writeAt() desde su propio hilo.
class FileAllocator {
public:
    FileAllocator() = default;
    ~FileAllocator();

    FileAllocator(const FileAllocator&) = delete;
    FileAllocator& operator=(const FileAllocator&) = delete;

    /// Crea/abre `path` y reserva `size` bytes en disco.
    /// Si preallocate es false solo trunca (fallocate puede fallar en FAT32/RED).
    /// truncate=false preserva el contenido existente (reanudacion byte-exacta).
    /// Devuelve false y rellena *error en caso de fallo.
    bool open(const QString& path, qint64 size, bool preallocate, QString* error = nullptr,
              bool truncate = true);

    /// Cierra el descriptor (flushea pendientes). Idempotente.
    void close();

    bool isOpen() const;

    /// Tamano actual del fichero (-1 si no esta abierto).
    qint64 size() const;

    /// Cambia el tamano logico del fichero (truncado/extesion). Thread-safe.
    bool resize(qint64 newSize, QString* error = nullptr);

    /// Escribe `len` bytes de `data` en `offset` (bucle ante escrituras parciales).
    /// Devuelve bytes escritos o -1 en error.
    qint64 writeAt(qint64 offset, const void* data, qint64 len);

    /// Lectura en `offset` (uso de verificacion y pruebas). -1 en error.
    qint64 readAt(qint64 offset, void* buffer, qint64 len) const;

    /// Rellena con ceros [offset, offset+len) sin leer el contenido.
    bool zeroRange(qint64 offset, qint64 len, QString* error = nullptr);

    /// Ruta absoluta del fichero abierto.
    QString path() const;

private:
    /// Cierra sin tomar el mutex (el llamador ya lo posee).
    void closeUnlocked();

    bool setError(QString* error, const QString& message);

    mutable std::mutex m_mutex;
#ifdef Q_OS_WIN
    void* m_handle = nullptr; // HANDLE (evitamos incluir windows.h en el header)
#else
    int m_fd = -1;
#endif
    qint64 m_size = -1;
    QString m_path;
};

} // namespace sdm
