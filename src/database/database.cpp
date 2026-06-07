#include "database.h"
#include "utils/driveutils.h"
#include "utils/portablepaths.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDebug>

Database::Database(const QString& dbPath)
    : m_dbPath(dbPath.isEmpty() ? getDefaultDbPath() : dbPath)
{
}

QString Database::getDefaultDbPath() {
    const QString dataPath = PortablePaths::dataDirectory();
    return dataPath + QStringLiteral("/FileSearch.db");
}

Database::~Database() {
    close();
}

bool Database::init() {
    QMutexLocker locker(&m_mutex);

    if (m_db.isOpen()) {
        return true;
    }

    QString connectionName = QString("FileSearch_%1").arg(reinterpret_cast<quintptr>(this));
    m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qWarning() << "Failed to open database:" << m_db.lastError().text();
        return false;
    }

    // Оптимизация SQLite для производительности
    QSqlQuery query(m_db);
    query.exec("PRAGMA journal_mode = WAL");
    query.exec("PRAGMA synchronous = NORMAL");
    query.exec("PRAGMA cache_size = -64000");
    query.exec("PRAGMA temp_store = MEMORY");
    query.exec("PRAGMA mmap_size = 268435456");
    query.exec("PRAGMA busy_timeout = 5000");

    if (!createTables()) {
        return false;
    }

    if (!createIndexes()) {
        return false;
    }

    m_initialized = true;
    qDebug() << "Database initialized at:" << m_dbPath;
    return true;
}

bool Database::createTables() {
    QSqlQuery query(m_db);

    // Таблица файлов
    QString createFilesTable = R"(
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            path TEXT NOT NULL UNIQUE,
            size INTEGER DEFAULT 0,
            modified INTEGER DEFAULT 0,
            drive TEXT NOT NULL,
            is_directory INTEGER DEFAULT 0,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )";

    if (!query.exec(createFilesTable)) {
        qWarning() << "Failed to create files table:" << query.lastError().text();
        return false;
    }

    // Таблица для метаданных индексации
    QString createMetaTable = R"(
        CREATE TABLE IF NOT EXISTS indexing_meta (
            key TEXT PRIMARY KEY,
            value TEXT,
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )";

    if (!query.exec(createMetaTable)) {
        qWarning() << "Failed to create meta table:" << query.lastError().text();
        return false;
    }

    return true;
}

bool Database::createIndexes() {
    QSqlQuery query(m_db);

    // Индекс на имя для быстрого поиска по имени
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_files_name ON files(name)")) {
        qWarning() << "Failed to create name index:" << query.lastError().text();
        return false;
    }

    // Индекс на путь для проверки дубликатов
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_files_path ON files(path)")) {
        qWarning() << "Failed to create path index:" << query.lastError().text();
        return false;
    }

    // Индекс на диск для фильтрации
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_files_drive ON files(drive)")) {
        qWarning() << "Failed to create drive index:" << query.lastError().text();
        return false;
    }

    // Составной индекс для поиска
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_files_name_path ON files(name, path)")) {
        qWarning() << "Failed to create combined index:" << query.lastError().text();
        return false;
    }

    return true;
}

void Database::close() {
    QMutexLocker locker(&m_mutex);

    if (m_db.isOpen()) {
        m_db.close();
    }

    QString connectionName = m_db.connectionName();
    m_db = QSqlDatabase(); // Очищаем дескриптор
    QSqlDatabase::removeDatabase(connectionName);

    m_initialized = false;
}

bool Database::addFile(const FileEntry& file) {
    QMutexLocker locker(&m_mutex);

    if (!m_db.isOpen()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO files (name, path, size, modified, drive, is_directory)
        VALUES (:name, :path, :size, :modified, :drive, :is_directory)
    )");

    query.bindValue(":name", file.name);
    query.bindValue(":path", file.path);
    query.bindValue(":size", file.size);
    query.bindValue(":modified", file.modified);
    query.bindValue(":drive", file.drive);
    query.bindValue(":is_directory", file.isDirectory ? 1 : 0);

    if (!query.exec()) {
        qWarning() << "Failed to add file:" << file.path << query.lastError().text();
        return false;
    }

    return true;
}

bool Database::addFiles(const QVector<FileEntry>& files) {
    QMutexLocker locker(&m_mutex);

    if (!m_db.isOpen() || files.isEmpty()) {
        return false;
    }

    m_db.transaction();

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO files (name, path, size, modified, drive, is_directory)
        VALUES (:name, :path, :size, :modified, :drive, :is_directory)
    )");

    int batchCount = 0;
    for (const FileEntry& file : files) {
        query.bindValue(":name", file.name);
        query.bindValue(":path", file.path);
        query.bindValue(":size", file.size);
        query.bindValue(":modified", file.modified);
        query.bindValue(":drive", file.drive);
        query.bindValue(":is_directory", file.isDirectory ? 1 : 0);

        if (!query.exec()) {
            qWarning() << "Failed to add file in batch:" << file.path;
            m_db.rollback();
            return false;
        }
        batchCount++;
    }

    m_db.commit();

    qDebug() << "Batch inserted" << batchCount << "files";
    return true;
}

QVector<FileEntry> Database::getAllFiles() const {
    QVector<FileEntry> files;

    QMutexLocker locker(&m_mutex);

    if (!m_db.isOpen()) {
        return files;
    }

    QSqlQuery countQuery(m_db);
    if (countQuery.exec("SELECT COUNT(*) FROM files") && countQuery.next()) {
        files.reserve(countQuery.value(0).toInt());
    }

    QSqlQuery query(m_db);
    query.setForwardOnly(true);

    if (!query.exec("SELECT name, path, size, modified, drive, is_directory FROM files")) {
        qWarning() << "Failed to get files:" << query.lastError().text();
        return files;
    }

    while (query.next()) {
        FileEntry entry;
        entry.name = query.value(0).toString();
        entry.path = query.value(1).toString();
        entry.size = query.value(2).toLongLong();
        entry.modified = query.value(3).toLongLong();
        entry.drive = query.value(4).toString();
        if (entry.drive.isEmpty()) {
            entry.drive = DriveUtils::normalizeDrive(entry.path);
        }
        entry.isDirectory = query.value(5).toInt() != 0;
        files.append(std::move(entry));
    }

    return files;
}

void Database::getAllFilesAsync(std::function<void(const FileEntry&)> callback) const {
    QMutexLocker locker(&m_mutex);

    if (!m_db.isOpen()) {
        return;
    }

    QSqlQuery query(m_db);
    query.setForwardOnly(true);

    if (!query.exec("SELECT name, path, size, modified, drive, is_directory FROM files")) {
        qWarning() << "Failed to get files async:" << query.lastError().text();
        return;
    }

    while (query.next()) {
        FileEntry entry;
        entry.name = query.value(0).toString();
        entry.path = query.value(1).toString();
        entry.size = query.value(2).toLongLong();
        entry.modified = query.value(3).toLongLong();
        entry.drive = query.value(4).toString();
        if (entry.drive.isEmpty()) {
            entry.drive = DriveUtils::normalizeDrive(entry.path);
        }
        entry.isDirectory = query.value(5).toInt() != 0;
        callback(entry);
    }
}

bool Database::clearDrive(const QString& drive) {
    QMutexLocker locker(&m_mutex);

    if (!m_db.isOpen()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM files WHERE drive = :drive");
    query.bindValue(":drive", drive);

    if (!query.exec()) {
        qWarning() << "Failed to clear drive:" << drive << query.lastError().text();
        return false;
    }

    qDebug() << "Cleared files for drive:" << drive << "deleted:" << query.numRowsAffected();
    return true;
}

bool Database::clearAll() {
    QMutexLocker locker(&m_mutex);

    if (!m_db.isOpen()) {
        return false;
    }

    QSqlQuery query(m_db);

    if (!query.exec("DELETE FROM files")) {
        qWarning() << "Failed to clear all:" << query.lastError().text();
        return false;
    }

    qDebug() << "Cleared all files from index";
    return true;
}

qint64 Database::getFileCount() const {
    QMutexLocker locker(&m_mutex);

    if (!m_db.isOpen()) {
        return 0;
    }

    QSqlQuery query(m_db);
    if (query.exec("SELECT COUNT(*) FROM files") && query.next()) {
        return query.value(0).toLongLong();
    }

    return 0;
}

qint64 Database::getFileCountForDrive(const QString& drive) const {
    QMutexLocker locker(&m_mutex);

    if (!m_db.isOpen()) {
        return 0;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM files WHERE drive = :drive");
    query.bindValue(":drive", drive);

    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }

    return 0;
}

bool Database::fileExists(const QString& path) const {
    QMutexLocker locker(&m_mutex);

    if (!m_db.isOpen()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM files WHERE path = :path LIMIT 1");
    query.bindValue(":path", path);

    return query.exec() && query.next();
}

void Database::beginTransaction() {
    QMutexLocker locker(&m_mutex);
    if (m_transactionRef == 0 && m_db.isOpen()) {
        m_db.transaction();
    }
    m_transactionRef++;
}

void Database::commitTransaction() {
    QMutexLocker locker(&m_mutex);
    m_transactionRef--;
    if (m_transactionRef == 0 && m_db.isOpen()) {
        m_db.commit();
    }
}

void Database::rollbackTransaction() {
    QMutexLocker locker(&m_mutex);
    if (m_transactionRef > 0 && m_db.isOpen()) {
        m_db.rollback();
    }
    m_transactionRef = 0;
}

void Database::analyze() {
    if (!m_db.isOpen()) {
        return;
    }

    QSqlQuery query(m_db);
    query.exec("ANALYZE");
}

QStringList Database::getIndexedDrives() const {
    QStringList drives;

    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) {
        return drives;
    }

    QSqlQuery query(m_db);
    if (!query.exec("SELECT DISTINCT drive FROM files ORDER BY drive")) {
        qWarning() << "Failed to get indexed drives:" << query.lastError().text();
        return drives;
    }

    while (query.next()) {
        drives.append(query.value(0).toString());
    }

    return drives;
}

int Database::removeUnavailableDrives(const QSet<QString>& availableDrives) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) {
        return 0;
    }

    const QStringList indexedDrives = [this]() {
        QStringList drives;
        QSqlQuery query(m_db);
        if (query.exec("SELECT DISTINCT drive FROM files")) {
            while (query.next()) {
                drives.append(query.value(0).toString());
            }
        }
        return drives;
    }();

    int totalRemoved = 0;
    m_db.transaction();

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM files WHERE drive = :drive");

    for (const QString& drive : indexedDrives) {
        const QString normalized = DriveUtils::normalizeDrive(drive);
        if (normalized.isEmpty()) {
            continue;
        }

        // Фиксированные диски (C:, D:) никогда не удаляем — только съёмные недоступные
        if (!DriveUtils::isRemovableDrive(normalized)) {
            continue;
        }

        if (availableDrives.contains(normalized)) {
            continue;
        }

        if (DriveUtils::isDriveAccessible(normalized)) {
            continue;
        }

        query.bindValue(":drive", drive);
        if (!query.exec()) {
            qWarning() << "Failed to remove drive from index:" << drive << query.lastError().text();
            m_db.rollback();
            return totalRemoved;
        }

        totalRemoved += query.numRowsAffected();
        qDebug() << "Removed unavailable drive from index:" << drive << "files:" << query.numRowsAffected();
    }

    m_db.commit();
    return totalRemoved;
}