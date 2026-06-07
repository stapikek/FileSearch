#include "filesystemindexer.h"
#include "utils/driveutils.h"
#include "utils/filesystemcheck.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>
#include <QCoreApplication>
#include <QTimer>
#include <QStandardPaths>
#include <QMetaObject>
#include <QApplication>
#include <QtConcurrent/QtConcurrent>
#include <QStorageInfo>
#include <memory>
#include <atomic>

// Глобальный флаг остановки для параллельных потоков
namespace {
std::atomic<bool> s_parallelStopRequested{false};
}

FileSystemIndexer::FileSystemIndexer(Database* database, QObject* parent)
    : QObject(parent)
    , m_database(database)
    , m_dbPath(database ? database->databasePath() : QString())
    , m_workerThread(nullptr)
    , m_indexing(false)
    , m_stopRequested(false)
    , m_totalFilesIndexed(0)
    , m_totalDirectoriesProcessed(0)
    , m_driveFilesIndexed(0)
    , m_indexingDriveTotal(0)
    , m_indexingDriveIndex(0)
{
    m_excludedPaths = QStringList()
        << "C:/Windows"
        << "C:/Program Files/WindowsApps"
        << "System Volume Information"
        << "Recycler";
}

FileSystemIndexer::~FileSystemIndexer() {
    stopIndexing();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(5000);
        delete m_workerThread;
    }
}

QStringList FileSystemIndexer::getAvailableDrives() {
    const QVector<DriveAuditResult> audit = FileSystemCheck::auditAllDrives();
    qDebug().noquote() << FileSystemCheck::formatAuditReport(audit);

    QStringList drives;
    drives.reserve(audit.size());

    for (const DriveAuditResult& result : audit) {
        qDebug() << " " << result.root
                 << "kind:" << result.driveKind
                 << "fs:" << result.fileSystem
                 << "status:" << result.status;

        if (result.status == QStringLiteral("OK")) {
            drives.append(result.root);
            continue;
        }

        if (result.accessible && result.readable && result.canEnumerate) {
            qWarning() << "Indexing drive with warnings:" << result.letter
                       << result.status << result.note;
            drives.append(result.root);
            continue;
        }

        qWarning() << "Skipping drive" << result.letter << "-" << result.status
                   << result.note;
    }

    if (drives.isEmpty()) {
        for (const QFileInfo& drive : QDir::drives()) {
            const QString letter = DriveUtils::normalizeDrive(drive.absolutePath());
            if (!letter.isEmpty() && DriveUtils::isDriveAccessible(letter)) {
                drives.append(letter + QChar(u'/'));
            }
        }
    }

    qDebug() << "Drives to index:" << drives;
    return drives;
}

void FileSystemIndexer::startIndexing() {
    if (m_indexing) {
        qDebug() << "Indexing already in progress";
        return;
    }

    m_indexing = true;
    m_stopRequested = false;
    s_parallelStopRequested.store(false);
    m_totalFilesIndexed = 0;
    m_totalDirectoriesProcessed = 0;

    // Очищаем всю базу перед переиндексацией (из главного потока)
    if (m_database) {
        m_database->clearAll();
    }

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(2000);
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    m_workerThread = new QThread(this);
    this->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, this, [this]() {
        QStringList drives = getAvailableDrives();
        qDebug() << "Starting indexing of all drives:" << drives;

        m_indexingDriveTotal = drives.size();

        // Параллельная индексация всех дисков через QtConcurrent
        emit indexingStarted(m_indexingDriveTotal);

        int completedDrives = 0;
        if (!drives.isEmpty()) {
            QFuture<qint64> future = QtConcurrent::mapped(drives, &FileSystemIndexer::indexDriveStatic);
            future.waitForFinished();

            const QList<qint64> results = future.results();
            qint64 totalIndexed = 0;
            for (qint64 count : results) {
                totalIndexed += count;
                completedDrives++;
                emit indexingDriveCompleted(completedDrives, m_indexingDriveTotal);
            }
            m_totalFilesIndexed = totalIndexed;
        }

        qDebug() << "Indexing finished. Total files:" << m_totalFilesIndexed;
        emit indexingFinished(m_totalFilesIndexed);
        m_indexing = false;
    }, Qt::DirectConnection);

    connect(m_workerThread, &QThread::finished, this, [this]() {
        this->moveToThread(QApplication::instance()->thread());
    }, Qt::DirectConnection);

    m_workerThread->start();
}

void FileSystemIndexer::stopIndexing() {
    m_stopRequested = true;
    s_parallelStopRequested.store(true);
}

void FileSystemIndexer::indexDrive(const QString& drivePath) {
    if (m_stopRequested) return;

    qDebug() << "Indexing drive:" << drivePath;

    m_driveFilesIndexed = 0;
    emitDriveProgress();

    QVector<FileEntry> batchFiles;
    batchFiles.reserve(BATCH_SIZE);

    indexDirectory(drivePath, batchFiles, 0);

    if (!batchFiles.isEmpty()) {
        flushFiles(batchFiles);
        batchFiles.clear();
    }

    if (m_driveFilesIndexed > 0) {
        emit indexingProgress(m_driveFilesIndexed, m_driveFilesIndexed);
    }

    qDebug() << "Finished indexing drive:" << drivePath << "Total files:" << m_totalFilesIndexed;
}

void FileSystemIndexer::emitDriveProgress() {
    const qint64 estimated = qMax<qint64>(
        1000,
        m_driveFilesIndexed + qMax<qint64>(5000, m_driveFilesIndexed / 5));
    emit indexingProgress(m_driveFilesIndexed, estimated);
}

void FileSystemIndexer::indexDirectory(const QString& dirPath, QVector<FileEntry>& files, int depth) {
    if (m_stopRequested) return;

    if (depth > MAX_DEPTH) {
        return;
    }

    if (isExcluded(dirPath)) {
        return;
    }

    QDir dir(dirPath);
    if (!dir.exists()) {
        return;
    }

    if (!dir.isReadable()) {
        qDebug() << "Skipping unreadable directory:" << dirPath;
        return;
    }

    QString dirName = QFileInfo(dirPath).fileName();
    if (isSystemDirectory(dirName)) {
        return;
    }

    m_totalDirectoriesProcessed++;

    QDirIterator it(dirPath, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

    if (!it.hasNext()) {
        return;
    }

    do {
        if (m_stopRequested) break;

        it.next();
        const QFileInfo info = it.fileInfo();

        if (isSystemDirectory(info.fileName())) {
            continue;
        }

        if (info.isSymLink() || info.isBundle()) {
            continue;
        }
#ifdef Q_OS_WIN
        if (info.isJunction()) {
            continue;
        }
#endif

        const bool isDir = info.isDir() && !info.isFile();

        FileEntry entry;
        entry.name = info.fileName();
        entry.path = info.absoluteFilePath();
        entry.size = isDir ? 0 : info.size();
        entry.modified = info.lastModified().toSecsSinceEpoch();
        entry.drive = DriveUtils::normalizeDrive(entry.path);
        entry.isDirectory = isDir;

        files.append(entry);
        m_totalFilesIndexed++;
        m_driveFilesIndexed++;

        if (m_driveFilesIndexed % 1000 == 0) {
            emitDriveProgress();
        }

        if (files.size() >= BATCH_SIZE) {
            emitDriveProgress();
            flushFiles(files);
            files.clear();
        }

        if (info.isDir() && !m_stopRequested && !isSystemDirectory(info.fileName())) {
            indexDirectory(info.absoluteFilePath(), files, depth + 1);
        }
    } while (it.hasNext());
}

bool FileSystemIndexer::isExcluded(const QString& path) const {
    QString normalizedPath = QDir::toNativeSeparators(path).toLower();

    for (const QString& excluded : m_excludedPaths) {
        if (normalizedPath.contains(excluded.toLower())) {
            return true;
        }
    }
    return false;
}

bool FileSystemIndexer::isSystemDirectory(const QString& dirName) const {
    static const QStringList systemDirs = {
        "System Volume Information",
        "Windows",
        "MSOCache",
        "Config.Msi",
        "$WinREAgent",
        "Recovery",
        "PerfLogs"
    };

    for (const QString& sysDir : systemDirs) {
        if (dirName.compare(sysDir, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return false;
}

void FileSystemIndexer::flushFiles(const QVector<FileEntry>& files) {
    if (files.isEmpty() || m_dbPath.isEmpty()) {
        return;
    }

    thread_local QString workerDbPath;
    thread_local std::unique_ptr<Database> workerDb;

    if (workerDbPath != m_dbPath || !workerDb) {
        workerDbPath = m_dbPath;
        workerDb = std::make_unique<Database>(m_dbPath);
        if (!workerDb->init()) {
            qWarning() << "Failed to init worker database connection";
            workerDb.reset();
            return;
        }
    }

    if (!workerDb->addFiles(files)) {
        qWarning() << "Failed to flush" << files.size() << "files to database";
    }
}

void FileSystemIndexer::processQueue() {
    qDebug() << "Worker thread started";
}

// ============================================================================
// Параллельная индексация (выполняется в потоках QtConcurrent)
// ============================================================================

namespace {
constexpr int PARALLEL_BATCH_SIZE = 10000;
constexpr int MAX_DEPTH = 128;

bool isSystemDirectoryStatic(const QString& dirName) {
    static const QStringList systemDirs = {
        "System Volume Information",
        "Windows",
        "MSOCache",
        "Config.Msi",
        "$WinREAgent",
        "Recovery",
        "PerfLogs"
    };
    for (const QString& sysDir : systemDirs) {
        if (dirName.compare(sysDir, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool isExcludedStatic(const QString& path) {
    static const QStringList excludedPaths = {
        "C:/Windows",
        "C:/Program Files/WindowsApps",
        "System Volume Information",
        "Recycler"
    };
    QString normalizedPath = QDir::toNativeSeparators(path).toLower();
    for (const QString& excluded : excludedPaths) {
        if (normalizedPath.contains(excluded.toLower())) {
            return true;
        }
    }
    return false;
}

void indexDirectoryParallel(const QString& dirPath,
                            QVector<FileEntry>& files,
                            int depth) {
    if (s_parallelStopRequested.load()) return;
    if (depth > MAX_DEPTH) return;
    if (isExcludedStatic(dirPath)) return;

    QDir dir(dirPath);
    if (!dir.exists() || !dir.isReadable()) return;

    const QString dirName = QFileInfo(dirPath).fileName();
    if (isSystemDirectoryStatic(dirName)) return;

    QDirIterator it(dirPath, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    if (!it.hasNext()) return;

    do {
        if (s_parallelStopRequested.load()) break;

        it.next();
        const QFileInfo info = it.fileInfo();

        if (isSystemDirectoryStatic(info.fileName())) continue;
        if (info.isSymLink() || info.isBundle()) continue;
#ifdef Q_OS_WIN
        if (info.isJunction()) continue;
#endif

        const bool isDir = info.isDir() && !info.isFile();

        FileEntry entry;
        entry.name = info.fileName();
        entry.path = info.absoluteFilePath();
        entry.size = isDir ? 0 : info.size();
        entry.modified = info.lastModified().toSecsSinceEpoch();
        entry.drive = DriveUtils::normalizeDrive(entry.path);
        entry.isDirectory = isDir;

        files.append(entry);

        if (info.isDir() && !s_parallelStopRequested.load()) {
            indexDirectoryParallel(info.absoluteFilePath(), files, depth + 1);
        }
    } while (it.hasNext());
}

} // namespace

qint64 FileSystemIndexer::indexDriveStatic(const QString& drivePath) {
    QVector<FileEntry> batchFiles;
    batchFiles.reserve(PARALLEL_BATCH_SIZE);

    indexDirectoryParallel(drivePath, batchFiles, 0);

    if (batchFiles.isEmpty()) {
        return 0;
    }

    // Записываем напрямую в БД
    Database db;
    if (!db.init()) {
        qWarning() << "Parallel indexer: failed to open database";
        return 0;
    }

    // Разбиваем на батчи
    qint64 totalWritten = 0;
    for (int i = 0; i < batchFiles.size(); i += PARALLEL_BATCH_SIZE) {
        const int end = qMin(i + PARALLEL_BATCH_SIZE, batchFiles.size());
        QVector<FileEntry> batch(batchFiles.mid(i, end - i));
        if (db.addFiles(batch)) {
            totalWritten += batch.size();
        } else {
            qWarning() << "Parallel indexer: failed to write batch at offset" << i;
        }
    }

    qDebug() << "Parallel indexed" << drivePath << ":" << totalWritten << "files";
    return totalWritten;
}