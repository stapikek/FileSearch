#ifndef DATABASE_H
#define DATABASE_H

#include <QString>
#include <QVariant>
#include <QSqlDatabase>
#include <QVector>
#include <QMutex>
#include <functional>
#include <QSet>
#include "models/fileindex.h"

/**
 * @brief Класс для работы с SQLite базой данных индекса файлов
 */
class Database {
public:
    /**
     * @brief Конструктор
     * @param dbPath Путь к файлу базы данных
     */
    explicit Database(const QString& dbPath = QString());

    /**
     * @brief Деструктор - закрывает соединение
     */
    ~Database();

    /**
     * @brief Инициализирует базу данных и создаёт таблицы
     * @return true при успехе
     */
    bool init();

    /**
     * @brief Закрывает соединение с базой данных
     */
    void close();

    /**
     * @brief Добавляет файл в индекс (batch-версия)
     * @param files Вектор файлов для добавления
     * @return true при успехе
     */
    bool addFiles(const QVector<FileEntry>& files);

    /**
     * @brief Добавляет один файл в индекс
     * @param file Запись файла
     * @return true при успехе
     */
    bool addFile(const FileEntry& file);

    /**
     * @brief Получает все файлы из индекса
     * @return Вектор всех файлов
     */
    QVector<FileEntry> getAllFiles() const;

    /**
     * @brief Получает все файлы из индекса асинхронно
     * @param callback Функция обратного вызова для каждого файла
     */
    void getAllFilesAsync(std::function<void(const FileEntry&)> callback) const;

    /**
     * @brief Удаляет все файлы указанного диска
     * @param drive Буква диска (C:, D:, etc.)
     * @return true при успехе
     */
    bool clearDrive(const QString& drive);

    /**
     * @brief Удаляет все файлы из индекса
     * @return true при успехе
     */
    bool clearAll();

    /**
     * @brief Получает количество файлов в индексе
     * @return Количество файлов
     */
    qint64 getFileCount() const;

    /**
     * @brief Получает количество файлов на диске
     * @param drive Буква диска
     * @return Количество файлов
     */
    qint64 getFileCountForDrive(const QString& drive) const;

    /**
     * @brief Проверяет, существует ли файл с указанным путём
     * @param path Путь к файлу
     * @return true если файл существует
     */
    bool fileExists(const QString& path) const;

    /**
     * @brief Начинает транзакцию для batch-операций
     */
    void beginTransaction();

    /**
     * @brief Фиксирует транзакцию
     */
    void commitTransaction();

    /**
     * @brief Откатывает транзакцию
     */
    void rollbackTransaction();

    /**
     * @brief Получает путь к файлу базы данных
     */
    QString databasePath() const { return m_dbPath; }

    /**
     * @brief Проверяет, инициализирована ли база
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Обновляет статистику SQLite для оптимизатора запросов
     */
    void analyze();

    /**
     * @brief Возвращает список дисков, которые есть в индексе
     */
    QStringList getIndexedDrives() const;

    /**
     * @brief Удаляет из индекса файлы с недоступных дисков
     * @param availableDrives Текущие доступные диски (C:, D:, ...)
     * @return Количество удалённых записей
     */
    int removeUnavailableDrives(const QSet<QString>& availableDrives);

private:
    /**
     * @brief Возвращает путь к БД по умолчанию
     */
    static QString getDefaultDbPath();

    /**
     * @brief Создаёт таблицы в базе данных
     */
    bool createTables();

    /**
     * @brief Создаёт индексы для быстрого поиска
     */
    bool createIndexes();

    QString m_dbPath;              // Путь к файлу БД
    mutable QSqlDatabase m_db;     // Соединение с БД
    mutable QMutex m_mutex;        // Мьютекс для потокобезопасности
    bool m_initialized = false;    // Флаг инициализации
    int m_transactionRef = 0;     // Счётчик транзакций
};

#endif // DATABASE_H