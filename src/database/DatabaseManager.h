// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  src/database/DatabaseManager.h
//
//  Capa de persistencia sobre SQLite3 (spec 3.1): historial de descargas,
//  colas, metadatos y estados de segmentos.  Una sola conexion, usada
//  exclusivamente desde el hilo principal (GUI/cola).
// ============================================================================
#pragma once

#include <QString>

#include <memory>

#include "database/Models.h"

struct sqlite3;

namespace sdm {

class DatabaseManager {
public:
    DatabaseManager() = default;
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    /// Abre (creando si no existe) la base en `path` e inicializa el esquema.
    bool open(const QString& path, QString* error = nullptr);
    void close();
    bool isOpen() const;

    /// Ultimo error SQLite en texto legible.
    QString lastError() const { return m_lastError; }

    /// Ruta de la base abierta.
    QString path() const { return m_path; }

    // --- Descargas -------------------------------------------------------
    quint64 insertTask(const DownloadTask& task, QString* error = nullptr);
    bool updateTask(const DownloadTask& task, QString* error = nullptr);
    bool updateUrl(quint64 id, const QString& url, const QString& cookies, const QString& referer,
                   QString* error = nullptr);
    bool updateProgress(quint64 id, DownloadState state, qint64 receivedBytes, const QString& lastError,
                        QString* error = nullptr);
    bool removeTask(quint64 id, QString* error = nullptr);
    bool clearCompleted(QString* error = nullptr);

    /// Carga todas las tareas con sus segmentos (orden de creacion).
    QList<DownloadTask> loadAll(QString* error = nullptr);
    DownloadTask loadTask(quint64 id, QString* error = nullptr);

    // --- Segmentos -------------------------------------------------------
    bool saveSegments(quint64 id, const QList<SegmentInfo>& segments, QString* error = nullptr);
    QList<SegmentInfo> loadSegments(quint64 id, QString* error = nullptr);

    // --- Estadisticas ----------------------------------------------------
    int countInState(DownloadState state);
    int countAll();

private:
    bool exec(const char* sql, QString* error = nullptr);
    bool fail(const QString& message, QString* error = nullptr);
    bool createSchema(QString* error);

    sqlite3* m_db = nullptr;
    QString m_path;
    QString m_lastError;
};

} // namespace sdm
