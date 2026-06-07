#ifndef FILEINDEX_H
#define FILEINDEX_H

#include <QString>
#include <QDateTime>
#include <QDebug>

/**
 * @brief Структура, представляющая запись файла в индексе
 */
struct FileEntry {
    QString name;           // Имя файла
    QString path;           // Полный путь к файлу
    qint64 size;           // Размер файла в байтах
    qint64 modified;       // Время последнего изменения (timestamp)
    QString drive;         // Буква диска (C:, D:, etc.)
    bool isDirectory;     // Является ли элемент директорией

    FileEntry() : size(0), modified(0), isDirectory(false) {}

    FileEntry(const QString& name, const QString& path, qint64 size,
              qint64 modified, const QString& drive, bool isDir = false)
        : name(name), path(path), size(size),
          modified(modified), drive(drive), isDirectory(isDir) {}

    /**
     * @brief Проверяет, является ли файл системным (скрытым/системным в Windows)
     */
    bool isSystemFile() const {
        return name.startsWith('$') ||    // NTFS system files
               name.startsWith('.') ||     // Unix hidden files
               name == "System Volume Information" ||
               name == "$Recycle.Bin";
    }

    /**
     * @brief Возвращает расширение файла
     */
    QString extension() const {
        int dotIndex = name.lastIndexOf('.');
        if (dotIndex > 0 && dotIndex < name.length() - 1) {
            return name.mid(dotIndex + 1).toLower();
        }
        return QString();
    }

    /**
     * @brief Проверяет, нужно ли показывать элемент как папку
     */
    bool displaysAsDirectory() const {
        if (!isDirectory) {
            return false;
        }
        // Файлы с расширением или размером — не папки
        if (!extension().isEmpty() || size > 0) {
            return false;
        }
        return true;
    }

    /**
     * @brief Форматирует размер файла в читаемый вид
     */
    QString formattedSize() const {
        if (displaysAsDirectory()) {
            return QString();
        }
        const double KB = 1024.0;
        const double MB = KB * 1024.0;
        const double GB = MB * 1024.0;

        if (size < KB) {
            return QStringLiteral("%1 B").arg(size);
        } else if (size < MB) {
            return QStringLiteral("%1 KB").arg(size / KB, 0, 'f', 2);
        } else if (size < GB) {
            return QStringLiteral("%1 MB").arg(size / MB, 0, 'f', 2);
        } else {
            return QStringLiteral("%1 GB").arg(size / GB, 0, 'f', 2);
        }
    }

    /**
     * @brief Форматирует дату изменения
     */
    QString formattedDate() const {
        if (modified == 0) return QStringLiteral("-");
        QDateTime dt = QDateTime::fromSecsSinceEpoch(modified);
        return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }

    /**
     * @brief Для отладки
     */
    QString toString() const {
        return QStringLiteral("FileEntry{name=%1, path=%2, size=%3, modified=%4}")
            .arg(name, path).arg(size).arg(modified);
    }
};

/// Специализация для qDebug
inline QDebug operator<<(QDebug debug, const FileEntry& entry) {
    QDebugStateSaver saver(debug);
    debug.noquote() << entry.toString();
    return debug;
}

/// Хеширование для использования в QSet/QHash
inline size_t qHash(const FileEntry& key, size_t seed = 0) {
    return qHash(key.path, seed);
}

/// Сравнение для использования в QSet/QHash
inline bool operator==(const FileEntry& a, const FileEntry& b) {
    return a.path == b.path;
}

#endif // FILEINDEX_H