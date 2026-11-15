// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  tests/test_database.cpp
//
//  Pruebas unitarias de persistencia (SQLite en fichero temporal):
//   - Alta/baja/recuperación de tareas (round-trip de campos)
//   - Persistencia y carga de segmentos
//   - Actualización de progreso y de URL (refresco de enlace)
//   - Contadores por estado y limpieza de completadas
//   - Versionado del esquema (PRAGMA user_version)
// ============================================================================
#include <QtTest>

#include <QTemporaryDir>

#include "database/DatabaseManager.h"

using namespace sdm;

class TestDatabase : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void schema_hasExpectedVersion();
    void task_roundTrip();
    void task_updateProgress();
    void task_updateUrlKeepsRefererWhenEmpty();
    void segments_roundTrip();
    void task_removeCascadesSegments();
    void counters_andClearCompleted();
    void reopen_preservesData();

private:
    DownloadTask makeTask(const QString& name) const;

    QTemporaryDir* m_dir = nullptr;
    DatabaseManager* m_db = nullptr;
};

void TestDatabase::init() {
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
    m_db = new DatabaseManager();
    QString error;
    QVERIFY2(m_db->open(m_dir->filePath(QStringLiteral("test.sqlite3")), &error),
             qPrintable(error));
}

void TestDatabase::cleanup() {
    delete m_db;
    m_db = nullptr;
    delete m_dir;
    m_dir = nullptr;
}

DownloadTask TestDatabase::makeTask(const QString& name) const {
    DownloadTask task;
    task.url = QStringLiteral("https://example.com/") + name;
    task.referer = QStringLiteral("https://example.com/page");
    task.destDir = QStringLiteral("/tmp/sdm-test");
    task.fileName = name;
    task.category = categoryForFileName(name);
    task.state = DownloadState::Queued;
    task.totalBytes = 1024;
    task.receivedBytes = 0;
    task.threadCount = 8;
    task.source = QStringLiteral("manual");
    task.createdAt = QDateTime::currentDateTime();
    task.updatedAt = task.createdAt;
    return task;
}

// --- Tests ----------------------------------------------------------------

void TestDatabase::schema_hasExpectedVersion() {
    // Tras abrir, el esquema queda marcado con la versión actual (user_version)
    // y reabrir no lo degrada ni rompe la lectura.
    QCOMPARE(m_db->schemaVersion(), 1);
    QCOMPARE(m_db->countAll(), 0);
    QString error;
    m_db->close();
    QVERIFY2(m_db->open(m_dir->filePath(QStringLiteral("test.sqlite3")), &error), qPrintable(error));
    QCOMPARE(m_db->schemaVersion(), 1);
    QCOMPARE(m_db->countAll(), 0);
}

void TestDatabase::task_roundTrip() {
    DownloadTask task = makeTask(QStringLiteral("archivo.zip"));
    task.cookies = QStringLiteral("session=abc");
    task.referer = QStringLiteral("https://example.com/page");
    task.mimeType = QStringLiteral("application/zip");
    task.etag = QStringLiteral("\"abc123\"");

    const quint64 id = m_db->insertTask(task, nullptr);
    QVERIFY(id > 0);

    const DownloadTask loaded = m_db->loadTask(id, nullptr);
    QCOMPARE(loaded.id, id);
    QCOMPARE(loaded.url, task.url);
    QCOMPARE(loaded.fileName, QStringLiteral("archivo.zip"));
    QCOMPARE(loaded.cookies, task.cookies);
    QCOMPARE(loaded.referer, task.referer);
    QCOMPARE(loaded.etag, task.etag);
    QCOMPARE(loaded.category, FileCategory::Archive);
    QCOMPARE(loaded.totalBytes, 1024);
    QCOMPARE(loaded.threadCount, 8);
}

void TestDatabase::task_updateProgress() {
    const quint64 id = m_db->insertTask(makeTask(QStringLiteral("video.mp4")), nullptr);
    QVERIFY(id > 0);

    QVERIFY(m_db->updateProgress(id, DownloadState::Downloading, 512, QString(), nullptr));
    const DownloadTask loaded = m_db->loadTask(id, nullptr);
    QCOMPARE(loaded.state, DownloadState::Downloading);
    QCOMPARE(loaded.receivedBytes, 512);

    QVERIFY(m_db->updateProgress(id, DownloadState::Failed, 512, QStringLiteral("HTTP 404"), nullptr));
    const DownloadTask failed = m_db->loadTask(id, nullptr);
    QCOMPARE(failed.lastError, QStringLiteral("HTTP 404"));
}

void TestDatabase::task_updateUrlKeepsRefererWhenEmpty() {
    const quint64 id = m_db->insertTask(makeTask(QStringLiteral("f.bin")), nullptr);
    QVERIFY(id > 0);

    // Refresco de enlace: URL nueva, referer vacío conserva el anterior.
    QVERIFY(m_db->updateUrl(id, QStringLiteral("https://cdn.example.com/new.bin"),
                            QStringLiteral("session=xyz"), QString(), nullptr));
    const DownloadTask loaded = m_db->loadTask(id, nullptr);
    QCOMPARE(loaded.url, QStringLiteral("https://cdn.example.com/new.bin"));
    QCOMPARE(loaded.cookies, QStringLiteral("session=xyz"));
    // Referer vacío conserva el anterior (regla de refresco de enlace).
    QCOMPARE(loaded.referer, QStringLiteral("https://example.com/page"));

    // Referer explícito sí se sobrescribe.
    QVERIFY(m_db->updateUrl(id, loaded.url, QString(), QStringLiteral("https://otra.pagina/"), nullptr));
    QCOMPARE(m_db->loadTask(id, nullptr).referer, QStringLiteral("https://otra.pagina/"));
}

void TestDatabase::segments_roundTrip() {
    const quint64 id = m_db->insertTask(makeTask(QStringLiteral("big.iso")), nullptr);
    QVERIFY(id > 0);

    QList<SegmentInfo> segments;
    for (int i = 0; i < 4; ++i) {
        SegmentInfo s;
        s.index = i;
        s.startByte = i * 256;
        s.endByte = i * 256 + 255;
        s.receivedBytes = i * 10;
        s.state = i == 0 ? SegmentState::Completed : SegmentState::Pending;
        segments.append(s);
    }
    QVERIFY(m_db->saveSegments(id, segments, nullptr));

    const QList<SegmentInfo> loaded = m_db->loadSegments(id, nullptr);
    QCOMPARE(loaded.size(), 4);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(loaded[i].index, i);
        QCOMPARE(loaded[i].startByte, i * 256);
        QCOMPARE(loaded[i].endByte, i * 256 + 255);
        QCOMPARE(loaded[i].receivedBytes, i * 10);
    }
    QCOMPARE(loaded[0].state, SegmentState::Completed);

    // Re-guardar reemplaza (no duplica).
    QVERIFY(m_db->saveSegments(id, segments.mid(0, 2), nullptr));
    QCOMPARE(m_db->loadSegments(id, nullptr).size(), 2);
}

void TestDatabase::task_removeCascadesSegments() {
    const quint64 id = m_db->insertTask(makeTask(QStringLiteral("del.bin")), nullptr);
    QList<SegmentInfo> segments;
    SegmentInfo s;
    s.index = 0;
    s.startByte = 0;
    s.endByte = 99;
    segments.append(s);
    QVERIFY(m_db->saveSegments(id, segments, nullptr));

    QVERIFY(m_db->removeTask(id, nullptr));
    QCOMPARE(m_db->loadSegments(id, nullptr).size(), 0);
    QVERIFY(m_db->loadTask(id, nullptr).id == 0);
}

void TestDatabase::counters_andClearCompleted() {
    m_db->insertTask(makeTask(QStringLiteral("a.zip")), nullptr);
    const quint64 id2 = m_db->insertTask(makeTask(QStringLiteral("b.zip")), nullptr);
    m_db->insertTask(makeTask(QStringLiteral("c.zip")), nullptr);
    QVERIFY(m_db->updateProgress(id2, DownloadState::Completed, 1024, QString(), nullptr));

    QCOMPARE(m_db->countAll(), 3);
    QCOMPARE(m_db->countInState(DownloadState::Completed), 1);
    QCOMPARE(m_db->countInState(DownloadState::Queued), 2);

    QVERIFY(m_db->clearCompleted(nullptr));
    QCOMPARE(m_db->countAll(), 2);
    QCOMPARE(m_db->countInState(DownloadState::Completed), 0);
}

void TestDatabase::reopen_preservesData() {
    const quint64 id = m_db->insertTask(makeTask(QStringLiteral("persiste.bin")), nullptr);
    QList<SegmentInfo> segments;
    SegmentInfo s;
    s.index = 0;
    s.startByte = 0;
    s.endByte = 9;
    s.receivedBytes = 5;
    segments.append(s);
    QVERIFY(m_db->saveSegments(id, segments, nullptr));

    m_db->close();
    QString error;
    QVERIFY2(m_db->open(m_dir->filePath(QStringLiteral("test.sqlite3")), &error), qPrintable(error));

    const DownloadTask loaded = m_db->loadTask(id, nullptr);
    QCOMPARE(loaded.fileName, QStringLiteral("persiste.bin"));
    const QList<SegmentInfo> segs = m_db->loadSegments(id, nullptr);
    QCOMPARE(segs.size(), 1);
    QCOMPARE(segs.first().receivedBytes, 5);
}

QTEST_GUILESS_MAIN(TestDatabase)
#include "test_database.moc"
