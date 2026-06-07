#ifndef SEARCHENGINE_H
#define SEARCHENGINE_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QMutex>
#include <QCache>
#include <QDate>
#include <QDateTime>
#include <QSet>
#include <functional>
#include "models/fileindex.h"

/**
 * @brief Тип поиска
 */
enum class SearchEngineType {
    Substring,    // Поиск подстроки в любом месте
    ExactMatch,   // Точное совпадение
    Wildcard,     // Маска (*.exe, test*)
    Prefix        // Префикс (начинается с)
};

/**
 * @brief Результат поиска с информацией о релевантности
 */
struct SearchResult {
    FileEntry entry;
    int score;      // Чем выше, тем релевантнее
    int matchPos;   // Позиция совпадения

    SearchResult() : score(0), matchPos(0) {}
    SearchResult(const FileEntry& e, int s, int pos = 0) : entry(e), score(s), matchPos(pos) {}
};

/**
 * @brief Фильтры для поиска
 */
struct SearchFilters {
    qint64 minSize = -1;      // Минимальный размер (-1 = без ограничений)
    qint64 maxSize = -1;      // Максимальный размер (-1 = без ограничений)
    QDate minDate;            // Минимальная дата изменения
    QDate maxDate;            // Максимальная дата изменения
    QStringList fileTypes;     // Список типов файлов (.exe, .txt и т.д.)
    bool includeDirectories = true; // Включать директории

    bool matches(const FileEntry& entry) const {
        // Проверка размера
        if (minSize >= 0 && entry.size < minSize) return false;
        if (maxSize >= 0 && entry.size > maxSize) return false;

        // Проверка даты
        if (minDate.isValid()) {
            QDate fileDate = QDateTime::fromSecsSinceEpoch(entry.modified).date();
            if (fileDate < minDate) return false;
        }
        if (maxDate.isValid()) {
            QDate fileDate = QDateTime::fromSecsSinceEpoch(entry.modified).date();
            if (fileDate > maxDate) return false;
        }

        // Проверка типа файла
        if (!fileTypes.isEmpty()) {
            QString lowerName = entry.name.toLower();
            bool matchFound = false;
            for (const QString& ext : fileTypes) {
                if (lowerName.endsWith(ext.toLower())) {
                    matchFound = true;
                    break;
                }
            }
            if (!matchFound) return false;
        }

        // Проверка директорий
        if (!includeDirectories && entry.isDirectory) return false;

        return true;
    }
};

/**
 * @brief Поисковый движок с поддержкой различных типов поиска
 *
 * Использует кэширование и оптимизированные алгоритмы поиска
 * для мгновенного отображения результатов.
 */
class SearchEngine : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Конструктор
     * @param parent Родительский объект
     */
    explicit SearchEngine(QObject* parent = nullptr);

    /**
     * @brief Деструктор
     */
    ~SearchEngine();

    /**
     * @brief Загружает все файлы в память для быстрого поиска
     * @param files Вектор файлов для индексации
     */
    void loadIndex(const QVector<FileEntry>& files);

    /**
     * @brief Очищает загруженный индекс
     */
    void clearIndex();

    /**
     * @brief Проверяет, загружен ли индекс
     */
    bool isIndexLoaded() const { return m_indexLoaded; }

    /**
     * @brief Выполняет поиск
     * @param query Поисковый запрос
     * @param maxResults Максимальное количество результатов
     * @param filters Фильтры для поиска
     * @return Отсортированный вектор результатов
     */
    QVector<SearchResult> search(const QString& query, int maxResults = 1000, const SearchFilters& filters = SearchFilters()) const;

    /**
     * @brief Асинхронный поиск с callback
     * @param query Поисковый запрос
     * @param callback Функция обратного вызова для каждого результата
     * @param maxResults Максимальное количество результатов
     * @param filters Фильтры для поиска
     */
    void searchAsync(const QString& query,
                    std::function<void(const SearchResult&)> callback,
                    int maxResults = 1000,
                    const SearchFilters& filters = SearchFilters()) const;

    /**
     * @brief Устанавливает список доступных дисков для фильтрации
     */
    void setAvailableDrives(const QSet<QString>& drives);

    bool isEntryAvailable(const FileEntry& entry) const;

    /**
     * @brief Получает все файлы из индекса (только чтение)
     */
    const QVector<FileEntry>& files() const { return m_fileEntries; }

    /**
     * @brief Получает количество файлов в индексе
     */
    qint64 fileCount() const { return m_fileEntries.size(); }

    /**
     * @brief Определяет тип поиска по запросу
     */
    static SearchEngineType detectSearchType(const QString& query);

signals:
    /**
     * @brief Сигнал о загрузке индекса
     * @param fileCount Количество загруженных файлов
     */
    void indexLoaded(qint64 fileCount);

    /**
     * @brief Сигнал о прогрессе загрузки индекса
     * @param loaded Загружено файлов
     * @param total Всего файлов
     */
    void indexLoadingProgress(int loaded, int total);

public slots:
    /**
     * @brief Устанавливает максимальное количество результатов
     */
    void setMaxResults(int max) { m_maxResults = max; }

private:
    /**
     * @brief Сравнивает два результата по релевантности
     */
    static bool compareResults(const SearchResult& a, const SearchResult& b);

    /**
     * @brief Вычисляет релевантность результата
     */
    int calculateScore(const FileEntry& entry, const QString& query, SearchEngineType type, int matchPos) const;

    /**
     * @brief Поиск подстроки (регистронезависимый)
     */
    QVector<SearchResult> searchSubstring(const QString& query, int maxResults, const SearchFilters& filters = SearchFilters()) const;

    /**
     * @brief Поиск по маске (wildcard)
     */
    QVector<SearchResult> searchWildcard(const QString& query, int maxResults, const SearchFilters& filters = SearchFilters()) const;

    /**
     * @brief Поиск точного совпадения
     */
    QVector<SearchResult> searchExact(const QString& query, int maxResults, const SearchFilters& filters = SearchFilters()) const;

    /**
     * @brief Поиск по префиксу
     */
    QVector<SearchResult> searchPrefix(const QString& query, int maxResults, const SearchFilters& filters = SearchFilters()) const;

    /**
     * @brief Поиск по размеру файла (size:19207kb)
     */
    QVector<SearchResult> searchBySize(qint64 targetBytes, int maxResults, const SearchFilters& filters) const;

    /**
     * @brief Проверяет, является ли термин фильтром size:
     */
    static bool parseSizeQuery(const QString& term, qint64& targetBytes);

    /**
     * @brief Поиск одного термина (без OR)
     */
    QVector<SearchResult> searchSingleTerm(const QString& query, int maxResults, const SearchFilters& filters) const;

    /**
     * @brief OR-поиск: Aurora | Xone
     */
    QVector<SearchResult> searchOrTerms(const QStringList& terms, int maxResults, const SearchFilters& filters) const;

    /**
     * @brief Разбивает запрос на OR-термины по символу |
     */
    static QStringList splitOrTerms(const QString& query);

    static void sortAndLimitResults(QVector<SearchResult>& results, int maxResults);

    bool isEntryAvailableUnlocked(const FileEntry& entry) const;

    struct IndexedFile {
        FileEntry entry;
        QString lowerName;
        QString lowerPath;
    };

    QVector<FileEntry> m_fileEntries;
    QVector<IndexedFile> m_files;
    QSet<QString> m_availableDrives;
    mutable QCache<QString, QVector<SearchResult>> m_cache; // Кэш результатов
    mutable QMutex m_mutex;               // Мьютекс для потокобезопасности
    bool m_indexLoaded = false;           // Флаг загрузки индекса
    int m_maxResults = 1000;             // Максимум результатов

    // Статические строки для быстрого сравнения
    static const QString s_homePath;
    static const QString s_documentsPath;
};

#endif // SEARCHENGINE_H