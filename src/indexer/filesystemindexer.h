#ifndef FILESYSTEMINDEXER_H
#define FILESYSTEMINDEXER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QStringList>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QSet>
#include <QFutureWatcher>
#include "models/fileindex.h"
#include "database/database.h"

/**
 * @brief Класс для индексации файловой системы
 *
 * Проводит рекурсивный обход директорий и индексирует все файлы.
 * Поддерживает параллельную индексацию нескольких дисков.
 */
class FileSystemIndexer : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Конструктор
     * @param database Указатель на базу данных для сохранения индексов
     */
    explicit FileSystemIndexer(Database* database, QObject* parent = nullptr);

    /**
     * @brief Деструктор
     */
    ~FileSystemIndexer();

    /**
     * @brief Начинает индексацию всех доступных дисков
     */
    void startIndexing();

    /**
     * @brief Останавливает текущую индексацию
     */
    void stopIndexing();

    /**
     * @brief Проверяет, идёт ли сейчас индексация
     */
    bool isIndexing() const { return m_indexing; }

    /**
     * @brief Получает список доступных дисков
     */
    static QStringList getAvailableDrives();

    /**
     * @brief Устанавливает пути для исключения из индексации
     */
    void setExcludedPaths(const QStringList& paths) { m_excludedPaths = paths; }

    /**
     * @brief Добавляет путь к исключённым
     */
    void addExcludedPath(const QString& path) { m_excludedPaths.append(path); }

signals:
    /**
     * @brief Начало индексации (число дисков для расчёта прогресса)
     */
    void indexingStarted(int totalDrives);

    /**
     * @brief Сигнал о прогрессе индексации текущего диска
     * @param processed Файлов на текущем диске
     * @param estimated Оценка файлов на диске (>= processed)
     */
    void indexingProgress(qint64 processed, qint64 estimated);

    /**
     * @brief Завершена индексация одного диска
     */
    void indexingDriveCompleted(int completedDrives, int totalDrives);

    /**
     * @brief Сигнал о завершении индексации
     * @param totalIndexed Общее количество проиндексированных файлов
     */
    void indexingFinished(qint64 totalIndexed);

    /**
     * @brief Сигнал об ошибке индексации
     * @param error Сообщение об ошибке
     */
    void indexingError(const QString& error);

    /**
     * @brief Сигнал о начале индексации нового диска
     * @param drive Буква диска
     */
    void indexingDriveStarted(const QString& drive);

    /**
     * @brief Сигнал с найденными файлами для обработки
     * @param files Вектор найденных файлов
     */
    void filesFound(const QVector<FileEntry>& files);

private slots:
    /**
     * @brief Слот для обработки очереди директорий
     */
    void processQueue();

private:
    /**
     * @brief Индексирует один диск
     * @param drivePath Путь к корню диска (например, "C:/")
     */
    void indexDrive(const QString& drivePath);

    /**
     * @brief Статический метод для параллельной индексации диска
     * Используется из QtConcurrent::mapped.
     */
    static qint64 indexDriveStatic(const QString& drivePath);

    /**
     * @brief Рекурсивно индексирует директорию
     * @param dirPath Путь к директории
     * @param files Вектор для накопления найденных файлов
     * @param depth Текущая глубина рекурсии
     */
    void indexDirectory(const QString& dirPath, QVector<FileEntry>& files, int depth = 0);

    /**
     * @brief Проверяет, находится ли путь в исключённых
     */
    bool isExcluded(const QString& path) const;

    /**
     * @brief Проверяет, является ли директория системной
     */
    bool isSystemDirectory(const QString& dirName) const;

    /**
     * @brief Обрабатывает найденные файлы - сохраняет в базу
     */
    void flushFiles(const QVector<FileEntry>& files);

    void emitDriveProgress();

    Database* m_database;           // Указатель на базу данных (главный поток)
    QString m_dbPath;               // Путь к БД для потока индексации
    QThread* m_workerThread;        // Рабочий поток
    bool m_indexing;                // Флаг индексации
    bool m_stopRequested;           // Флаг запроса остановки

    QMutex m_mutex;                 // Мьютекс для синхронизации
    QWaitCondition m_queueCondition; // Условие ожидания очереди

    QVector<QString> m_directoryQueue; // Очередь директорий для обработки
    QStringList m_excludedPaths;     // Пути для исключения

    // Счётчики для прогресса
    qint64 m_totalFilesIndexed;
    qint64 m_totalDirectoriesProcessed;
    qint64 m_driveFilesIndexed;
    int m_indexingDriveTotal;
    int m_indexingDriveIndex;

    // Максимальная глубина рекурсии
    static constexpr int MAX_DEPTH = 128;

    // Размер батча для записи в БД
    static constexpr int BATCH_SIZE = 10000;
};

#endif // FILESYSTEMINDEXER_H