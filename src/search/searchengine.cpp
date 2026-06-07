#include "searchengine.h"
#include "utils/driveutils.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <QHash>

const QString SearchEngine::s_homePath = QDir::toNativeSeparators(QDir::homePath()).toLower();
const QString SearchEngine::s_documentsPath = QDir::toNativeSeparators(
    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toLower();

SearchEngine::SearchEngine(QObject* parent)
    : QObject(parent)
    , m_cache(50)
{
    m_cache.setMaxCost(20 * 1024 * 1024);
}

SearchEngine::~SearchEngine() {
    clearIndex();
}

void SearchEngine::setAvailableDrives(const QSet<QString>& drives) {
    QMutexLocker locker(&m_mutex);
    m_availableDrives = drives;
    m_cache.clear();
}

bool SearchEngine::isEntryAvailableUnlocked(const FileEntry& entry) const {
    const QString drive = entry.drive.isEmpty()
        ? DriveUtils::normalizeDrive(entry.path)
        : entry.drive;
    return DriveUtils::isDriveAccessible(drive);
}

bool SearchEngine::isEntryAvailable(const FileEntry& entry) const {
    QMutexLocker locker(&m_mutex);
    return isEntryAvailableUnlocked(entry);
}

void SearchEngine::loadIndex(const QVector<FileEntry>& files) {
    QMutexLocker locker(&m_mutex);

    m_fileEntries.clear();
    m_files.clear();
    m_fileEntries.reserve(files.size());
    m_files.reserve(files.size());

    for (const FileEntry& entry : files) {
        m_fileEntries.append(entry);

        IndexedFile indexed;
        indexed.entry = entry;
        indexed.lowerName = entry.name.toLower();
        indexed.lowerPath = entry.path.toLower();
        m_files.append(std::move(indexed));
    }

    m_cache.clear();
    m_indexLoaded = true;

    qDebug() << "Search index loaded with" << m_files.size() << "files";
    emit indexLoaded(m_files.size());
}

void SearchEngine::clearIndex() {
    QMutexLocker locker(&m_mutex);
    m_fileEntries.clear();
    m_files.clear();
    m_availableDrives.clear();
    m_cache.clear();
    m_indexLoaded = false;
}

SearchEngineType SearchEngine::detectSearchType(const QString& query) {
    if (query.isEmpty()) {
        return SearchEngineType::Substring;
    }

    if (query.contains('*') || query.contains('?')) {
        return SearchEngineType::Wildcard;
    }

    if (query.startsWith('.')) {
        return SearchEngineType::Wildcard;
    }

    if (query.contains('.') && !query.contains(' ') && query.length() < 100) {
        const int dotPos = query.indexOf('.');
        if (dotPos > 0 && dotPos < query.length() - 1) {
            const QString beforeDot = query.left(dotPos);
            const QString afterDot = query.mid(dotPos + 1);

            if (!beforeDot.isEmpty() && afterDot.length() >= 1 && afterDot.length() <= 15) {
                bool afterDotLooksLikeExt = true;
                for (const QChar ch : afterDot) {
                    if (!ch.isLetterOrNumber() && ch != '_' && ch != '-' && ch != '#') {
                        afterDotLooksLikeExt = false;
                        break;
                    }
                }
                if (afterDotLooksLikeExt) {
                    return SearchEngineType::ExactMatch;
                }
            }
        }
    }

    return SearchEngineType::Substring;
}

QStringList SearchEngine::splitOrTerms(const QString& query) {
    QStringList terms;
    for (const QString& part : query.split(QLatin1Char('|'))) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            terms.append(trimmed);
        }
    }
    return terms;
}

void SearchEngine::sortAndLimitResults(QVector<SearchResult>& results, int maxResults) {
    const int limit = qMin(maxResults, results.size());
    if (results.size() > limit) {
        std::partial_sort(results.begin(), results.begin() + limit, results.end(),
                          [](const SearchResult& a, const SearchResult& b) {
                              if (a.score != b.score) return a.score > b.score;
                              return a.entry.name.length() < b.entry.name.length();
                          });
        results.resize(limit);
    } else {
        std::sort(results.begin(), results.end(),
                  [](const SearchResult& a, const SearchResult& b) {
                      if (a.score != b.score) return a.score > b.score;
                      return a.entry.name.length() < b.entry.name.length();
                  });
    }
}

namespace {

qint64 sizeUnitMultiplier(const QString& unit) {
    const QString u = unit.toLower();
    if (u.isEmpty() || u == QLatin1String("b") || u == QLatin1String("bytes")) {
        return 1;
    }
    if (u == QLatin1String("kb") || u == QLatin1String("k")) {
        return 1024;
    }
    if (u == QLatin1String("mb") || u == QLatin1String("m")) {
        return 1024LL * 1024LL;
    }
    if (u == QLatin1String("gb") || u == QLatin1String("g")) {
        return 1024LL * 1024LL * 1024LL;
    }
    return 1;
}

qint64 sizeValueToBytes(double value, const QString& unit) {
    return static_cast<qint64>(value * sizeUnitMultiplier(unit));
}

} // namespace

bool SearchEngine::parseSizeQuery(const QString& term, qint64& targetBytes) {
    static const QRegularExpression sizeRe(
        QStringLiteral(R"(^size:\s*(>=|<=|>|<)?\s*(\d+(?:\.\d+)?)\s*(b|bytes|kb|k|mb|m|gb|g)?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = sizeRe.match(term.trimmed());
    if (!match.hasMatch()) {
        return false;
    }

    const QString op = match.captured(1);
    if (!op.isEmpty()) {
        // Пока поддерживаем только точный размер, как в примере пользователя
        return false;
    }

    const double value = match.captured(2).toDouble();
    QString unit = match.captured(3).trimmed().toLower();
    if (unit == QLatin1String("bytes")) {
        unit = QLatin1String("b");
    } else if (unit == QLatin1String("k")) {
        unit = QLatin1String("kb");
    } else if (unit == QLatin1String("m")) {
        unit = QLatin1String("mb");
    } else if (unit == QLatin1String("g")) {
        unit = QLatin1String("gb");
    }

    targetBytes = sizeValueToBytes(value, unit);
    return true;
}

QVector<SearchResult> SearchEngine::searchBySize(qint64 targetBytes, int maxResults, const SearchFilters& filters) const {
    QVector<SearchResult> results;
    results.reserve(qMin(static_cast<int>(m_files.size()), maxResults * 2));

    for (const IndexedFile& indexed : m_files) {
        if (!filters.matches(indexed.entry)) {
            continue;
        }

        if (indexed.entry.displaysAsDirectory()) {
            continue;
        }

        if (indexed.entry.size != targetBytes) {
            continue;
        }

        const int score = 300 + calculateScore(indexed.entry, QString(), SearchEngineType::ExactMatch, 0);
        results.append(SearchResult(indexed.entry, score, 0));

        if (results.size() >= maxResults * 2) {
            break;
        }
    }

    sortAndLimitResults(results, maxResults);
    return results;
}

QVector<SearchResult> SearchEngine::searchSingleTerm(const QString& query, int maxResults, const SearchFilters& filters) const {
    qint64 targetBytes = 0;
    if (parseSizeQuery(query, targetBytes)) {
        return searchBySize(targetBytes, maxResults, filters);
    }

    const SearchEngineType type = detectSearchType(query);
    switch (type) {
    case SearchEngineType::Wildcard:
        return searchWildcard(query, maxResults, filters);
    case SearchEngineType::ExactMatch:
        return searchExact(query, maxResults, filters);
    case SearchEngineType::Prefix:
        return searchPrefix(query, maxResults, filters);
    case SearchEngineType::Substring:
    default:
        return searchSubstring(query, maxResults, filters);
    }
}

QVector<SearchResult> SearchEngine::searchOrTerms(const QStringList& terms, int maxResults, const SearchFilters& filters) const {
    QHash<QString, SearchResult> bestByPath;
    bestByPath.reserve(qMin(static_cast<int>(m_files.size()), maxResults * terms.size()));

    for (const QString& term : terms) {
        const QVector<SearchResult> termResults = searchSingleTerm(term, maxResults, filters);
        for (const SearchResult& result : termResults) {
            const auto it = bestByPath.find(result.entry.path);
            if (it == bestByPath.end() || result.score > it->score) {
                bestByPath.insert(result.entry.path, result);
            }
        }
    }

    QVector<SearchResult> results = bestByPath.values().toVector();
    sortAndLimitResults(results, maxResults);
    return results;
}

QVector<SearchResult> SearchEngine::search(const QString& query, int maxResults, const SearchFilters& filters) const {
    if (query.isEmpty()) {
        return {};
    }

    const QString cacheKey = query.toLower() + "_"
        + QString::number(maxResults) + "_"
        + QString::number(filters.minSize) + "_"
        + QString::number(filters.maxSize) + "_"
        + filters.minDate.toString("yyyyMMdd") + "_"
        + filters.maxDate.toString("yyyyMMdd") + "_"
        + filters.fileTypes.join(",");

    // 1. Проверяем кэш (кратковременная блокировка)
    {
        QMutexLocker locker(&m_mutex);
        if (m_cache.contains(cacheKey)) {
            return *m_cache.object(cacheKey);
        }
    }

    // 2. Выполняем поиск БЕЗ мьютекса — данные доступны только для чтения,
    //    а запись происходит только в loadIndex() в главном потоке
    QVector<SearchResult> results;
    const QStringList orTerms = splitOrTerms(query);
    if (orTerms.size() > 1) {
        results = searchOrTerms(orTerms, maxResults, filters);
    } else {
        const QString term = orTerms.isEmpty() ? query.trimmed() : orTerms.first();
        results = searchSingleTerm(term, maxResults, filters);
    }

    // 3. Сохраняем в кэш
    {
        QMutexLocker locker(&m_mutex);
        m_cache.insert(cacheKey, new QVector<SearchResult>(results));
    }

    return results;
}

void SearchEngine::searchAsync(const QString& query,
                               std::function<void(const SearchResult&)> callback,
                               int maxResults,
                               const SearchFilters& filters) const
{
    const QVector<SearchResult> results = search(query, maxResults, filters);
    for (const SearchResult& result : results) {
        callback(result);
    }
}

QVector<SearchResult> SearchEngine::searchSubstring(const QString& query, int maxResults, const SearchFilters& filters) const {
    QVector<SearchResult> results;
    results.reserve(qMin(static_cast<int>(m_files.size()), maxResults * 2));

    const QString lowerQuery = query.toLower();

    for (const IndexedFile& indexed : m_files) {
        if (!filters.matches(indexed.entry)) {
            continue;
        }

        int pos = indexed.lowerName.indexOf(lowerQuery);
        int pathPos = -1;
        if (pos < 0) {
            pathPos = indexed.lowerPath.indexOf(lowerQuery);
        }

        if (pos >= 0 || pathPos >= 0) {
            const int nameMatchPos = (pos >= 0) ? pos : -1;
            int score = calculateScore(indexed.entry, query, SearchEngineType::Substring, nameMatchPos);
            if (pathPos >= 0 && pos < 0) {
                score -= 50;
            }
            results.append(SearchResult(indexed.entry, score, nameMatchPos >= 0 ? nameMatchPos : 0));

            if (results.size() >= maxResults * 2) {
                break;
            }
        }
    }

    const int limit = qMin(maxResults, results.size());
    if (results.size() > limit) {
        std::partial_sort(results.begin(), results.begin() + limit, results.end(),
                          [](const SearchResult& a, const SearchResult& b) {
                              if (a.score != b.score) return a.score > b.score;
                              return a.entry.name.length() < b.entry.name.length();
                          });
        results.resize(limit);
    } else {
        std::sort(results.begin(), results.end(),
                  [](const SearchResult& a, const SearchResult& b) {
                      if (a.score != b.score) return a.score > b.score;
                      return a.entry.name.length() < b.entry.name.length();
                  });
    }

    return results;
}

QVector<SearchResult> SearchEngine::searchWildcard(const QString& query, int maxResults, const SearchFilters& filters) const {
    QVector<SearchResult> results;
    if (query.size() > 256) {
        return results;
    }

    results.reserve(qMin(static_cast<int>(m_files.size()), maxResults * 2));

    QString pattern = QRegularExpression::escape(query);
    pattern.replace("\\*", ".*");
    pattern.replace("\\?", ".");

    const QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid()) {
        return results;
    }

    for (const IndexedFile& indexed : m_files) {
        if (!filters.matches(indexed.entry)) {
            continue;
        }

        const QRegularExpressionMatch match = regex.match(indexed.entry.name);
        if (match.hasMatch()) {
            const int pos = match.capturedStart();
            const int score = calculateScore(indexed.entry, query, SearchEngineType::Wildcard, pos);
            results.append(SearchResult(indexed.entry, score, pos));
        }

        if (results.size() >= maxResults * 2) {
            break;
        }
    }

    const int limit = qMin(maxResults, results.size());
    if (results.size() > limit) {
        std::partial_sort(results.begin(), results.begin() + limit, results.end(),
                          [](const SearchResult& a, const SearchResult& b) {
                              if (a.score != b.score) return a.score > b.score;
                              return a.entry.name.length() < b.entry.name.length();
                          });
        results.resize(limit);
    } else {
        std::sort(results.begin(), results.end(),
                  [](const SearchResult& a, const SearchResult& b) {
                      if (a.score != b.score) return a.score > b.score;
                      return a.entry.name.length() < b.entry.name.length();
                  });
    }

    return results;
}

QVector<SearchResult> SearchEngine::searchExact(const QString& query, int maxResults, const SearchFilters& filters) const {
    QVector<SearchResult> results;
    results.reserve(maxResults);

    const QString lowerQuery = query.toLower();

    for (const IndexedFile& indexed : m_files) {
        if (!filters.matches(indexed.entry)) {
            continue;
        }

        if (indexed.lowerName == lowerQuery) {
            const int score = calculateScore(indexed.entry, query, SearchEngineType::ExactMatch, 0);
            results.append(SearchResult(indexed.entry, score, 0));
        }

        if (results.size() >= maxResults) {
            break;
        }
    }

    return results;
}

QVector<SearchResult> SearchEngine::searchPrefix(const QString& query, int maxResults, const SearchFilters& filters) const {
    QVector<SearchResult> results;
    results.reserve(qMin(static_cast<int>(m_files.size()), maxResults * 2));

    const QString lowerQuery = query.toLower();

    for (const IndexedFile& indexed : m_files) {
        if (!filters.matches(indexed.entry)) {
            continue;
        }

        if (indexed.lowerName.startsWith(lowerQuery)) {
            const int score = calculateScore(indexed.entry, query, SearchEngineType::Prefix, 0);
            results.append(SearchResult(indexed.entry, score, 0));
        }

        if (results.size() >= maxResults * 2) {
            break;
        }
    }

    const int limit = qMin(maxResults, results.size());
    if (results.size() > limit) {
        std::partial_sort(results.begin(), results.begin() + limit, results.end(),
                          [](const SearchResult& a, const SearchResult& b) {
                              if (a.score != b.score) return a.score > b.score;
                              return a.entry.name.length() < b.entry.name.length();
                          });
        results.resize(limit);
    } else {
        std::sort(results.begin(), results.end(),
                  [](const SearchResult& a, const SearchResult& b) {
                      if (a.score != b.score) return a.score > b.score;
                      return a.entry.name.length() < b.entry.name.length();
                  });
    }

    return results;
}

int SearchEngine::calculateScore(const FileEntry& entry, const QString& query,
                                  SearchEngineType type, int matchPos) const
{
    int score = 100;

    if (entry.name.compare(query, Qt::CaseInsensitive) == 0) {
        score += 1000;
    }

    if (matchPos == 0) {
        score += 200;
    }

    if (matchPos > 0 && matchPos < entry.name.size()) {
        if (!entry.name.at(matchPos - 1).isLetterOrNumber()) {
            score += 100;
        }
    }

    if (entry.name.length() < 20) {
        score += (20 - entry.name.length());
    }

    const QString lowerPath = entry.path.toLower();
    if (lowerPath.contains(s_homePath)) {
        score += 50;
    }

    if (lowerPath.contains(s_documentsPath)) {
        score += 30;
    }

    if (entry.isDirectory) {
        score += 20;
    }

    switch (type) {
    case SearchEngineType::ExactMatch:
        score += 500;
        break;
    case SearchEngineType::Prefix:
        score += 100;
        break;
    case SearchEngineType::Wildcard:
        score += 50;
        break;
    default:
        break;
    }

    return score;
}

bool SearchEngine::compareResults(const SearchResult& a, const SearchResult& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.entry.name.length() < b.entry.name.length();
}