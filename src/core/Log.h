// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/core/Log.h - Logger de alto rendimiento basado en fmt con rotacion.
// ============================================================================
#pragma once

#include <QString>

#include <fmt/format.h>

#include <utility>

namespace sdm::log {

enum class Level : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

/// Inicializa el subsistema de logging (archivo en `dir`, rotacion a 5 MB).
/// Devuelve false si no se pudo abrir el archivo (el log sigue en stderr).
bool init(const QString& dir);

/// Cierra y vuelca el archivo.
void shutdown();

/// Nivel minimo por debajo del cual no se escribe nada.
void setLevel(Level level);
Level level();

/// Escribe una linea ya construida (thread-safe).
void write(Level level, const QString& message);

/// Enmascara credenciales antes de escribir en el log: parametros `token`,
/// `password`, `cookies`, cabeceras `Authorization`/`Cookie`, credenciales
/// embebidas en URLs y campos JSON equivalentes. Exportada para pruebas.
QString redactSecrets(const QString& text);

/// Escribe una linea formateada con fmt (thread-safe).
template <typename... Args>
inline void writef(Level level, fmt::format_string<Args...> fmtStr, Args&&... args) {
    const std::string text = fmt::format(fmtStr, std::forward<Args>(args)...);
    write(level, QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())));
}

template <typename... Args>
inline void debug(fmt::format_string<Args...> fmtStr, Args&&... args) {
    writef(Level::Debug, fmtStr, std::forward<Args>(args)...);
}

template <typename... Args>
inline void info(fmt::format_string<Args...> fmtStr, Args&&... args) {
    writef(Level::Info, fmtStr, std::forward<Args>(args)...);
}

template <typename... Args>
inline void warn(fmt::format_string<Args...> fmtStr, Args&&... args) {
    writef(Level::Warn, fmtStr, std::forward<Args>(args)...);
}

template <typename... Args>
inline void error(fmt::format_string<Args...> fmtStr, Args&&... args) {
    writef(Level::Error, fmtStr, std::forward<Args>(args)...);
}

/// Convierte un QString a std::string UTF-8 para usarlo como argumento fmt.
inline std::string utf8(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

} // namespace sdm::log
