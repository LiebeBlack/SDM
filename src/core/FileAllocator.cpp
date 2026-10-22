// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/FileAllocator.cpp
// ============================================================================
#include "core/FileAllocator.h"

#include "core/Log.h"

#include <QByteArray>
#include <QFileInfo>

#include <cerrno>
#include <cstring>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace sdm {

namespace {

#ifdef Q_OS_WIN
QString winErrorMessage(DWORD code) {
    LPWSTR buffer = nullptr;
    const DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                         FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text = buffer != nullptr ? QString::fromWCharArray(buffer, static_cast<qsizetype>(len)).trimmed()
                                     : QStringLiteral("error %1").arg(code);
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return text;
}
#endif

} // namespace

FileAllocator::~FileAllocator() {
    close();
}

bool FileAllocator::setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
    log::error("FileAllocator: {}", log::utf8(message));
    return false;
}

bool FileAllocator::open(const QString& path, qint64 size, bool preallocate, QString* error, bool truncate) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (path.isEmpty()) {
        return setError(error, QStringLiteral("Ruta de archivo vacia"));
    }

    closeUnlocked(); // liberamos un descriptor previo si existia (el mutex ya esta tomado)

#ifdef Q_OS_WIN
    HANDLE handle = CreateFileW(reinterpret_cast<PCWSTR>(path.utf16()),
                                GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                truncate ? CREATE_ALWAYS : OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        return setError(error, QStringLiteral("CreateFile falló (%1): %2").arg(code).arg(winErrorMessage(code)));
    }
    m_handle = handle;
    m_path = path;
    m_size = 0;

    if (size > 0) {
        // Fija el tamano logico (EOF).  Con truncate=false solo se ajusta si el
        // fichero existente difiere del tamano anunciado por el servidor.
        LARGE_INTEGER li{};
        li.QuadPart = size;
        if (!SetFilePointerEx(handle, li, nullptr, FILE_BEGIN) || !SetEndOfFile(handle)) {
            const DWORD code = GetLastError();
            closeUnlocked();
            return setError(error, QStringLiteral("No se pudo fijar el tamano: %1").arg(winErrorMessage(code)));
        }
        if (preallocate) {
            // Reserva de espacio en disco sin fragmentacion (mejora escritura).
            FILE_ALLOCATION_INFO allocInfo{};
            allocInfo.EndOfFile.QuadPart = size;
            if (!SetFileInformationByHandle(handle, FileAllocationInfo, &allocInfo, sizeof(allocInfo))) {
                // FAT32/redes antiguas: el EOF ya esta fijado, seguimos igual.
                log::debug("FileAllocator: preallocate no soportado en {}", log::utf8(path));
            }
        }
    }
    LARGE_INTEGER current{};
    if (GetFileSizeEx(handle, &current)) {
        m_size = current.QuadPart;
    }
#else
    const QByteArray encoded = QFile::encodeName(path);
    int flags = O_RDWR | O_CREAT;
    if (truncate) {
        flags |= O_TRUNC;
    }
    int fd = ::open(encoded.constData(), flags, 0644);
    if (fd < 0) {
        return setError(error, QStringLiteral("open(%1) falló: %2").arg(path, QString::fromLocal8Bit(std::strerror(errno))));
    }
    m_fd = fd;
    m_path = path;
    m_size = 0;

    if (size > 0 && preallocate) {
        if (posix_fallocate(fd, 0, static_cast<off_t>(size)) != 0) {
            // Algunos FS (exFAT, tmpfs antiguo) no lo soportan: truncamos.
            if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
                const int err = errno;
                closeUnlocked();
                return setError(error, QStringLiteral("ftruncate falló: %1").arg(QString::fromLocal8Bit(std::strerror(err))));
            }
        }
    } else if (size > 0) {
        struct stat st{};
        if (::fstat(fd, &st) == 0 && static_cast<qint64>(st.st_size) != size) {
            if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
                const int err = errno;
                closeUnlocked();
                return setError(error, QStringLiteral("ftruncate falló: %1").arg(QString::fromLocal8Bit(std::strerror(err))));
            }
        }
    }
    if (size > 0) {
        m_size = size;
    } else {
        struct stat st{};
        m_size = (::fstat(fd, &st) == 0) ? static_cast<qint64>(st.st_size) : 0;
    }
#endif

    log::debug("FileAllocator abierto path={} size={} preallocate={} truncate={}", log::utf8(path),
               static_cast<long long>(size), preallocate ? 1 : 0, truncate ? 1 : 0);
    return true;
}

void FileAllocator::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    closeUnlocked();
}

void FileAllocator::closeUnlocked() {
#ifdef Q_OS_WIN
    if (m_handle != nullptr) {
        HANDLE handle = static_cast<HANDLE>(m_handle);
        FlushFileBuffers(handle);
        CloseHandle(handle);
        m_handle = nullptr;
    }
#else
    if (m_fd >= 0) {
        ::fsync(m_fd);
        ::close(m_fd);
        m_fd = -1;
    }
#endif
    m_path.clear();
    m_size = -1;
}

bool FileAllocator::isOpen() const {
    std::lock_guard<std::mutex> lock(m_mutex);
#ifdef Q_OS_WIN
    return m_handle != nullptr;
#else
    return m_fd >= 0;
#endif
}

qint64 FileAllocator::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_size;
}

QString FileAllocator::path() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_path;
}

bool FileAllocator::resize(qint64 newSize, QString* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (newSize < 0) {
        return setError(error, QStringLiteral("Tamano negativo"));
    }
#ifdef Q_OS_WIN
    if (m_handle == nullptr) {
        return setError(error, QStringLiteral("Archivo no abierto"));
    }
    HANDLE handle = static_cast<HANDLE>(m_handle);
    LARGE_INTEGER li{};
    li.QuadPart = newSize;
    if (!SetFilePointerEx(handle, li, nullptr, FILE_BEGIN) || !SetEndOfFile(handle)) {
        const DWORD code = GetLastError();
        return setError(error, QStringLiteral("SetEndOfFile falló: %1").arg(winErrorMessage(code)));
    }
#else
    if (m_fd < 0) {
        return setError(error, QStringLiteral("Archivo no abierto"));
    }
    if (ftruncate(m_fd, static_cast<off_t>(newSize)) != 0) {
        return setError(error, QStringLiteral("ftruncate falló: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
    }
#endif
    m_size = newSize;
    return true;
}

qint64 FileAllocator::writeAt(qint64 offset, const void* data, qint64 len) {
    if (data == nullptr || len <= 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_mutex);

#ifdef Q_OS_WIN
    if (m_handle == nullptr) {
        return -1;
    }
    HANDLE handle = static_cast<HANDLE>(m_handle);
    qint64 written = 0;
    const char* cursor = static_cast<const char*>(data);
    while (written < len) {
        const qint64 position = offset + written;
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(position & 0xFFFFFFFFLL);
        overlapped.OffsetHigh = static_cast<DWORD>((position >> 32) & 0xFFFFFFFFLL);
        DWORD chunk = 0;
        const DWORD toWrite = static_cast<DWORD>(std::min<qint64>(len - written, 64 * 1024 * 1024));
        const BOOL ok = WriteFile(handle, cursor + written, toWrite, &chunk, &overlapped);
        if (!ok) {
            const DWORD code = GetLastError();
            if (code == ERROR_HANDLE_EOF) {
                break;
            }
            log::error("WriteFile falló offset={} : {}", static_cast<long long>(position), log::utf8(winErrorMessage(code)));
            return written > 0 ? written : -1;
        }
        if (chunk == 0) {
            break;
        }
        written += static_cast<qint64>(chunk);
    }
#else
    if (m_fd < 0) {
        return -1;
    }
    qint64 written = 0;
    const char* cursor = static_cast<const char*>(data);
    while (written < len) {
        const ssize_t n = ::pwrite(m_fd, cursor + written, static_cast<size_t>(len - written),
                                   static_cast<off_t>(offset + written));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            log::error("pwrite falló offset={} : {}", static_cast<long long>(offset + written),
                       std::strerror(errno));
            return written > 0 ? written : -1;
        }
        if (n == 0) {
            break;
        }
        written += static_cast<qint64>(n);
    }
#endif

    if (offset + written > m_size) {
        m_size = offset + written;
    }
    return written;
}

qint64 FileAllocator::readAt(qint64 offset, void* buffer, qint64 len) const {
    if (buffer == nullptr || len <= 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_mutex);

#ifdef Q_OS_WIN
    if (m_handle == nullptr) {
        return -1;
    }
    HANDLE handle = static_cast<HANDLE>(m_handle);
    qint64 read = 0;
    char* cursor = static_cast<char*>(buffer);
    while (read < len) {
        const qint64 position = offset + read;
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(position & 0xFFFFFFFFLL);
        overlapped.OffsetHigh = static_cast<DWORD>((position >> 32) & 0xFFFFFFFFLL);
        DWORD chunk = 0;
        if (!ReadFile(handle, cursor + read, static_cast<DWORD>(len - read), &chunk, &overlapped)) {
            return read > 0 ? read : -1;
        }
        if (chunk == 0) {
            break;
        }
        read += static_cast<qint64>(chunk);
    }
    return read;
#else
    if (m_fd < 0) {
        return -1;
    }
    qint64 read = 0;
    char* cursor = static_cast<char*>(buffer);
    while (read < len) {
        const ssize_t n = ::pread(m_fd, cursor + read, static_cast<size_t>(len - read),
                                  static_cast<off_t>(offset + read));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return read > 0 ? read : -1;
        }
        if (n == 0) {
            break;
        }
        read += static_cast<qint64>(n);
    }
    return read;
#endif
}

bool FileAllocator::zeroRange(qint64 offset, qint64 len, QString* error) {
    if (len <= 0) {
        return true;
    }
    static constexpr qint64 kBlock = 256 * 1024;
    const QByteArray zeros(static_cast<qsizetype>(kBlock), '\0');
    qint64 written = 0;
    while (written < len) {
        const qint64 chunk = std::min(kBlock, len - written);
        const qint64 n = writeAt(offset + written, zeros.constData(), chunk);
        if (n < 0) {
            return setError(error, QStringLiteral("Fallo al rellenar con ceros en %1").arg(offset + written));
        }
        written += n;
    }
    return true;
}

} // namespace sdm
