// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  tests/test_models.cpp
//
//  Pruebas unitarias de la lógica pura de Models.h (sin red ni GUI):
//   - Particionado de rangos (contigüidad, cobertura exacta, límites)
//   - Sanitización de nombres de archivo (traversal, caracteres ilegales)
//   - Resolución de nombre desde URL
//   - Detección anti-HTML (looksLikeHtmlDocument / isHtmlLikeFileName)
//   - Formateadores legibles (tamaño, velocidad, ETA, porcentaje)
//   - Clasificación por categoría y cálculos de DownloadTask
// ============================================================================
#include <QtTest>

#include "database/Models.h"

using namespace sdm;

class TestModels : public QObject {
    Q_OBJECT

private slots:
    // --- Particionado -------------------------------------------------
    void split_coversExactRange();
    void split_singleThread();
    void split_tinyFileUsesFewerThreads();
    void split_invalidInputs();
    void split_contiguityAndOrder();

    // --- Nombres de archivo -------------------------------------------
    void sanitize_replacesIllegalChars();
    void sanitize_stripsLeadingDotsAndSpaces();
    void sanitize_emptyFallsBack();
    void sanitize_truncatesLongNamesKeepingExtension();

    void fileNameFromUrl_stripsQueryAndFragment();
    void fileNameFromUrl_decodesCommonEscapes();
    void fileNameFromUrl_emptyPathFallback();

    // --- Anti-HTML ----------------------------------------------------
    void htmlDetect_doctype();
    void htmlDetect_htmlTag();
    void htmlDetect_bomPrologAndComments();
    void htmlDetect_negativeBinary();
    void htmlDetect_negativeSvgAndJson();
    void htmlFileName_extensions();

    // --- Formateo -----------------------------------------------------
    void format_humanSize();
    void format_humanSpeed();
    void format_humanEta();
    void format_humanPercent();

    // --- Categorías y tarea -------------------------------------------
    void category_classification();
    void task_progressAndSums();
    void task_paths();

    // --- Lotes estilo IDM ---------------------------------------------
    void batch_numericRange();
    void batch_zeroPadded();
    void batch_alphaAndList();
    void batch_maxScaleAndStep();
    void batch_invalidAndPlainUrl();

    // --- Duplicados y versiones --------------------------------------
    void duplicate_normalizesUrl();
    void versions_compare();
};

// --- Particionado ---------------------------------------------------------

void TestModels::split_coversExactRange() {
    const auto segments = splitRangeIntoSegments(1000, 4);
    QVERIFY(!segments.isEmpty());
    QCOMPARE(segments.first().startByte, 0);
    QCOMPARE(segments.last().endByte, 999);
    qint64 total = 0;
    for (const auto& s : segments) {
        total += s.totalBytes();
    }
    QCOMPARE(total, 1000);
}

void TestModels::split_singleThread() {
    const auto segments = splitRangeIntoSegments(500, 1);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().startByte, 0);
    QCOMPARE(segments.first().endByte, 499);
}

void TestModels::split_tinyFileUsesFewerThreads() {
    // Menos bytes que kMinSegmentBytes * threads -> se reducen los hilos útiles.
    const auto segments = splitRangeIntoSegments(kMinSegmentBytes * 2, 16);
    QVERIFY(segments.size() < 16);
    QVERIFY(segments.size() >= 1);
    qint64 expectedStart = 0;
    for (const auto& s : segments) {
        QCOMPARE(s.startByte, expectedStart);
        expectedStart = s.endByte + 1;
    }
}

void TestModels::split_invalidInputs() {
    QVERIFY(splitRangeIntoSegments(0, 4).isEmpty());
    QVERIFY(splitRangeIntoSegments(-5, 4).isEmpty());
    QVERIFY(splitRangeIntoSegments(100, 0).isEmpty());
}

void TestModels::split_contiguityAndOrder() {
    const auto segments = splitRangeIntoSegments(10 * 1024 * 1024, 8);
    QCOMPARE(segments.size(), 8);
    qint64 expectedStart = 0;
    for (const auto& s : segments) {
        QCOMPARE(s.startByte, expectedStart);
        QVERIFY(s.endByte >= s.startByte);
        QVERIFY(s.totalBytes() >= kMinSegmentBytes);
        expectedStart = s.endByte + 1;
    }
    QCOMPARE(expectedStart, 10 * 1024 * 1024);
}

// --- Nombres de archivo ---------------------------------------------------

void TestModels::sanitize_replacesIllegalChars() {
    const QString out = sanitizeFileName(QStringLiteral("a<b>c:d\"e/f\\g|h?i*j.txt"));
    QCOMPARE(out, QStringLiteral("a_b_c_d_e_f_g_h_i_j.txt"));
}

void TestModels::sanitize_stripsLeadingDotsAndSpaces() {
    QCOMPARE(sanitizeFileName(QStringLiteral("  ..archivo.zip")), QStringLiteral("archivo.zip"));
}

void TestModels::sanitize_emptyFallsBack() {
    QCOMPARE(sanitizeFileName(QString()), QStringLiteral("descarga.bin"));
    QCOMPARE(sanitizeFileName(QStringLiteral("...")), QStringLiteral("descarga.bin"));
}

void TestModels::sanitize_truncatesLongNamesKeepingExtension() {
    const QString longName = QStringLiteral("a").repeated(300) + QStringLiteral(".zip");
    const QString out = sanitizeFileName(longName);
    QVERIFY(out.size() <= 200);
    QVERIFY(out.endsWith(QStringLiteral(".zip")));
}

void TestModels::fileNameFromUrl_stripsQueryAndFragment() {
    QCOMPARE(fileNameFromUrl(QStringLiteral("https://h.com/dir/archivo.zip?token=1#top")),
             QStringLiteral("archivo.zip"));
}

void TestModels::fileNameFromUrl_decodesCommonEscapes() {
    QCOMPARE(fileNameFromUrl(QStringLiteral("https://h.com/mi%20archivo%2B1.bin")),
             QStringLiteral("mi archivo+1.bin"));
}

void TestModels::fileNameFromUrl_emptyPathFallback() {
    QCOMPARE(fileNameFromUrl(QStringLiteral("https://h.com/")), QStringLiteral("descarga.bin"));
}

// --- Anti-HTML ------------------------------------------------------------

void TestModels::htmlDetect_doctype() {
    QVERIFY(looksLikeHtmlDocument("<!DOCTYPE html><html><body>x</body></html>"));
    QVERIFY(looksLikeHtmlDocument("<!doctype html PUBLIC \"-//W3C//DTD\">"));
}

void TestModels::htmlDetect_htmlTag() {
    QVERIFY(looksLikeHtmlDocument("<html lang=\"es\"><head></head></html>"));
    QVERIFY(looksLikeHtmlDocument("   \n <head><title>t</title></head>"));
}

void TestModels::htmlDetect_bomPrologAndComments() {
    // BOM UTF-8 + prologo XML + comentario + etiqueta raíz html.
    const QByteArray bom = QByteArray("\xEF\xBB\xBF", 3);
    QVERIFY(looksLikeHtmlDocument(bom + "<?xml version=\"1.0\"?><!-- comentario --><html></html>"));
    QVERIFY(looksLikeHtmlDocument("<!-- solo comentario --><!DOCTYPE html>"));
}

void TestModels::htmlDetect_negativeBinary() {
    QVERIFY(!looksLikeHtmlDocument(QByteArray("PK\x03\x04 datos de zip", 18)));
    QVERIFY(!looksLikeHtmlDocument(QByteArray()));
    QVERIFY(!looksLikeHtmlDocument("\x89PNG\r\n\x1a\n"));
}

void TestModels::htmlDetect_negativeSvgAndJson() {
    // SVG y XML genérico no son páginas HTML.
    QVERIFY(!looksLikeHtmlDocument("<?xml version=\"1.0\"?><svg xmlns=\"x\"></svg>"));
    QVERIFY(!looksLikeHtmlDocument("{\"clave\": \"valor\"}"));
}

void TestModels::htmlFileName_extensions() {
    QVERIFY(isHtmlLikeFileName(QStringLiteral("pagina.HTML")));
    QVERIFY(isHtmlLikeFileName(QStringLiteral("x.xhtml")));
    QVERIFY(isHtmlLikeFileName(QStringLiteral("archivo.mht")));
    QVERIFY(!isHtmlLikeFileName(QStringLiteral("archivo.zip")));
    QVERIFY(!isHtmlLikeFileName(QStringLiteral("sinextension")));
}

// --- Formateo -------------------------------------------------------------

void TestModels::format_humanSize() {
    QCOMPARE(humanSize(-1), QStringLiteral("--"));
    QCOMPARE(humanSize(512), QStringLiteral("512 B"));
    QCOMPARE(humanSize(2048), QStringLiteral("2.0 KB"));
    QCOMPARE(humanSize(5 * 1048576LL), QStringLiteral("5.0 MB"));
    QCOMPARE(humanSize(1073741824LL), QStringLiteral("1.00 GB"));
}

void TestModels::format_humanSpeed() {
    QCOMPARE(humanSpeed(0.0), QStringLiteral("--"));
    QCOMPARE(humanSpeed(900.0), QStringLiteral("900 B/s"));
    QCOMPARE(humanSpeed(2048.0), QStringLiteral("2.0 KB/s"));
    QCOMPARE(humanSpeed(1048576.0 * 3), QStringLiteral("3.0 MB/s"));
}

void TestModels::format_humanEta() {
    QCOMPARE(humanEta(-1), QStringLiteral("--"));
    QCOMPARE(humanEta(45), QStringLiteral("45s"));
    QCOMPARE(humanEta(72), QStringLiteral("01m 12s"));
    QCOMPARE(humanEta(3900), QStringLiteral("1h 05m"));
}

void TestModels::format_humanPercent() {
    QCOMPARE(humanPercent(0.0), QStringLiteral("0%"));
    QCOMPARE(humanPercent(0.684), QStringLiteral("68%"));
    QCOMPARE(humanPercent(1.0), QStringLiteral("100%"));
    QCOMPARE(humanPercent(1.5), QStringLiteral("100%"));
}

// --- Categorías y tarea ---------------------------------------------------

void TestModels::category_classification() {
    QCOMPARE(categoryForFileName(QStringLiteral("peli.mp4")), FileCategory::Video);
    QCOMPARE(categoryForFileName(QStringLiteral("album.flac")), FileCategory::Audio);
    QCOMPARE(categoryForFileName(QStringLiteral("backup.zip")), FileCategory::Archive);
    QCOMPARE(categoryForFileName(QStringLiteral("doc.pdf")), FileCategory::Document);
    QCOMPARE(categoryForFileName(QStringLiteral("app.exe")), FileCategory::Program);
    QCOMPARE(categoryForFileName(QStringLiteral("img.png")), FileCategory::Image);
    QCOMPARE(categoryForFileName(QStringLiteral("desconocido.xyz")), FileCategory::Other);
}

void TestModels::task_progressAndSums() {
    DownloadTask task;
    task.totalBytes = 1000;
    task.receivedBytes = 250;
    QCOMPARE(task.progress(), 0.25);

    SegmentInfo a;
    a.index = 0;
    a.startByte = 0;
    a.endByte = 499;
    a.receivedBytes = 100;
    SegmentInfo b;
    b.index = 1;
    b.startByte = 500;
    b.endByte = 999;
    b.receivedBytes = 200;
    task.segments = {a, b};
    QCOMPARE(task.sumSegmentBytes(), 300);

    DownloadTask done;
    done.state = DownloadState::Completed;
    done.totalBytes = 500;
    done.receivedBytes = 10; // completada => progreso 1.0
    QCOMPARE(done.progress(), 1.0);
}

void TestModels::task_paths() {
    DownloadTask task;
    task.destDir = QStringLiteral("/downloads");
    task.fileName = QStringLiteral("a.zip");
    QCOMPARE(task.savePath(), QStringLiteral("/downloads/a.zip"));
    QCOMPARE(task.partPath(), QStringLiteral("/downloads/a.zip.part"));

    task.destDir = QStringLiteral("/downloads/");
    QCOMPARE(task.savePath(), QStringLiteral("/downloads/a.zip"));
}

void TestModels::batch_numericRange() {
    QVERIFY(containsBatchPattern(QStringLiteral("http://x/a[1-3].zip")));
    const QStringList urls = expandBatchPattern(QStringLiteral("http://x/a[1-3].zip"));
    QCOMPARE(urls.size(), 3);
    QCOMPARE(urls.at(0), QStringLiteral("http://x/a1.zip"));
    QCOMPARE(urls.at(2), QStringLiteral("http://x/a3.zip"));
}

void TestModels::batch_zeroPadded() {
    const QStringList urls = expandBatchPattern(QStringLiteral("http://x/p[08-10].bin"));
    QCOMPARE(urls.size(), 3);
    QCOMPARE(urls.at(0), QStringLiteral("http://x/p08.bin"));
    QCOMPARE(urls.at(1), QStringLiteral("http://x/p09.bin"));
    QCOMPARE(urls.at(2), QStringLiteral("http://x/p10.bin"));
}

void TestModels::batch_alphaAndList() {
    const QStringList letters = expandBatchPattern(QStringLiteral("http://x/[a-c]/f.bin"));
    QCOMPARE(letters.size(), 3);
    QCOMPARE(letters.at(0), QStringLiteral("http://x/a/f.bin"));
    QCOMPARE(letters.at(2), QStringLiteral("http://x/c/f.bin"));

    const QStringList list = expandBatchPattern(QStringLiteral("http://x/f[1,3,7].zip"));
    QCOMPARE(list.size(), 3);
    QCOMPARE(list.at(0), QStringLiteral("http://x/f1.zip"));
    QCOMPARE(list.at(1), QStringLiteral("http://x/f3.zip"));
    QCOMPARE(list.at(2), QStringLiteral("http://x/f7.zip"));

    // Varios grupos por URL.
    const QStringList groups = expandBatchPattern(QStringLiteral("http://x/[1-2]/v[01-02].ts"));
    QCOMPARE(groups.size(), 4);
    QCOMPARE(groups.at(0), QStringLiteral("http://x/1/v01.ts"));
    QCOMPARE(groups.at(3), QStringLiteral("http://x/2/v02.ts"));
}

void TestModels::batch_maxScaleAndStep() {
    const QStringList stepped = expandBatchPattern(QStringLiteral("http://x/f[0-10:5].bin"));
    QCOMPARE(stepped.size(), 3);
    QCOMPARE(stepped.at(1), QStringLiteral("http://x/f5.bin"));

    // `limit` acota patrones enormes.
    const QStringList capped = expandBatchPattern(QStringLiteral("http://x/f[1-100000].bin"), 50);
    QCOMPARE(capped.size(), 50);
}

void TestModels::batch_invalidAndPlainUrl() {
    QVERIFY(!containsBatchPattern(QStringLiteral("http://x/a.zip")));
    QCOMPARE(expandBatchPattern(QStringLiteral("http://x/a.zip")).size(), 1);
    // Corchetes sin rango utilizable: se interpretan como texto literal.
    const QStringList literal = expandBatchPattern(QStringLiteral("http://x/a[zz].zip"));
    QCOMPARE(literal.size(), 1);
    QCOMPARE(literal.at(0), QStringLiteral("http://x/azz.zip"));
    // Limite 0 no genera nada.
    QCOMPARE(expandBatchPattern(QStringLiteral("http://x/f[1-3].bin"), 0).size(), 0);
}

void TestModels::duplicate_normalizesUrl() {
    QCOMPARE(normalizeUrlForCompare(QStringLiteral("HTTP://Example.COM/a.zip#x")),
             normalizeUrlForCompare(QStringLiteral("http://example.com/a.zip")));
    QVERIFY(normalizeUrlForCompare(QStringLiteral("http://a/b.zip?x=1")) !=
            normalizeUrlForCompare(QStringLiteral("http://a/b.zip?x=2")));
    QCOMPARE(normalizeUrlForCompare(QStringLiteral("  http://a/b.zip  ")),
             normalizeUrlForCompare(QStringLiteral("http://a/b.zip")));
}

void TestModels::versions_compare() {
    QVERIFY(compareVersions(QStringLiteral("1.0.1"), QStringLiteral("1.0.0")) > 0);
    QVERIFY(compareVersions(QStringLiteral("1.0.0"), QStringLiteral("1.0.1")) < 0);
    QCOMPARE(compareVersions(QStringLiteral("v1.0.0"), QStringLiteral("1.0.0")), 0);
    QCOMPARE(compareVersions(QStringLiteral("1.0"), QStringLiteral("1.0.0")), 0);
    QVERIFY(compareVersions(QStringLiteral("1.10.0"), QStringLiteral("1.9.9")) > 0);
    QCOMPARE(compareVersions(QStringLiteral("2.0.0-beta.1"), QStringLiteral("2.0.0")), 0);
}

QTEST_GUILESS_MAIN(TestModels)
#include "test_models.moc"
