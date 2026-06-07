#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <QString>
#include <QRegularExpression>
#include <QVector>
#include <QDir>
#include <algorithm>
#include "securityutils.h"

/**
 * @brief Утилиты для работы со строками при поиске
 */
namespace StringUtils {

/**
 * @brief Проверяет, содержит ли строка подстроку (регистронезависимо)
 */
inline bool contains(const QString& str, const QString& substr, Qt::CaseSensitivity cs = Qt::CaseInsensitive) {
    return str.contains(substr, cs);
}

/**
 * @brief Проверяет, начинается ли строка с префикса (регистронезависимо)
 */
inline bool startsWith(const QString& str, const QString& prefix, Qt::CaseSensitivity cs = Qt::CaseInsensitive) {
    return str.startsWith(prefix, cs);
}

/**
 * @brief Проверяет, заканчивается ли строка суффиксом (регистронезависимо)
 */
inline bool endsWith(const QString& str, const QString& suffix, Qt::CaseSensitivity cs = Qt::CaseInsensitive) {
    return str.endsWith(suffix, cs);
}

/**
 * @brief Преобразует маску в regex паттерн
 * @param mask Маска (*.exe, test*, etc.)
 * @return Regex паттерн
 */
inline QString wildcardToRegex(const QString& mask) {
    QString result = QRegularExpression::escape(mask);
    result.replace("\\*", ".*");
    result.replace("\\?", ".");
    return "^" + result + "$";
}

/**
 * @brief Вычисляет расстояние Левенштейна между двумя строками
 */
inline int levenshteinDistance(const QString& s1, const QString& s2) {
    const int len1 = s1.length();
    const int len2 = s2.length();

    if (len1 == 0) return len2;
    if (len2 == 0) return len1;

    // Создаём матрицу
    QVector<QVector<int>> d(len1 + 1, QVector<int>(len2 + 1));

    // Инициализация
    for (int i = 0; i <= len1; ++i) d[i][0] = i;
    for (int j = 0; j <= len2; ++j) d[0][j] = j;

    // Заполнение матрицы
    for (int i = 1; i <= len1; ++i) {
        for (int j = 1; j <= len2; ++j) {
            int cost = (s1[i-1].toLower() == s2[j-1].toLower()) ? 0 : 1;
            d[i][j] = qMin(
                qMin(d[i-1][j] + 1, d[i][j-1] + 1),
                d[i-1][j-1] + cost
            );
        }
    }

    return d[len1][len2];
}

/**
 * @brief Проверяет, является ли символ допустимым для имени файла
 */
inline bool isValidFileNameChar(QChar c) {
    static const QString invalidChars = "<>:\"|?*\\/\0";
    return !invalidChars.contains(c);
}

/**
 * @brief Нормализует путь к файлу
 */
inline QString normalizePath(const QString& path) {
    return QDir::toNativeSeparators(path);
}

/**
 * @brief Получает расширение файла
 */
inline QString getExtension(const QString& filename) {
    int dotIndex = filename.lastIndexOf('.');
    if (dotIndex > 0 && dotIndex < filename.length() - 1) {
        return filename.mid(dotIndex + 1).toLower();
    }
    return QString();
}

/**
 * @brief Проверяет, соответствует ли имя файла маске
 * @param filename Имя файла
 * @param mask Маска (*.exe, test*, etc.)
 * @return true если соответствует
 */
inline bool matchesWildcard(const QString& filename, const QString& mask) {
    if (mask.isEmpty()) return true;
    if (mask == "*") return true;

    QString pattern = wildcardToRegex(mask);
    QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
    return regex.match(filename).hasMatch();
}

/**
 * @brief Подсвечивает найденный текст HTML-тегами (поддерживает OR через |)
 */
inline QString highlightMatch(const QString& text, const QString& query) {
    if (query.isEmpty()) {
        return SecurityUtils::htmlEscape(text);
    }

    QStringList terms;
    for (const QString& part : query.split(QLatin1Char('|'))) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("size:"), Qt::CaseInsensitive)) {
            continue;
        }
        terms.append(trimmed);
    }
    if (terms.isEmpty()) {
        return SecurityUtils::htmlEscape(text);
    }

    const QString lowerText = text.toLower();
    QVector<QPair<int, int>> ranges;
    ranges.reserve(terms.size() * 2);

    for (const QString& term : terms) {
        const QString lowerTerm = term.toLower();
        int pos = 0;
        while ((pos = lowerText.indexOf(lowerTerm, pos)) >= 0) {
            ranges.append(qMakePair(pos, term.length()));
            pos += qMax(1, term.length());
        }
    }

    if (ranges.isEmpty()) {
        return SecurityUtils::htmlEscape(text);
    }

    std::sort(ranges.begin(), ranges.end(),
              [](const QPair<int, int>& a, const QPair<int, int>& b) {
                  return a.first < b.first;
              });

    QVector<QPair<int, int>> merged;
    merged.reserve(ranges.size());
    for (const QPair<int, int>& range : ranges) {
        if (merged.isEmpty()) {
            merged.append(range);
            continue;
        }

        QPair<int, int>& last = merged.last();
        const int lastEnd = last.first + last.second;
        if (range.first <= lastEnd) {
            const int newEnd = qMax(lastEnd, range.first + range.second);
            last.second = newEnd - last.first;
        } else {
            merged.append(range);
        }
    }

    QString result;
    int lastPos = 0;
    for (const QPair<int, int>& range : merged) {
        if (range.first > text.size()) {
            continue;
        }
        const int length = qMin(range.second, text.size() - range.first);
        result += SecurityUtils::htmlEscape(text.mid(lastPos, range.first - lastPos));
        result += QStringLiteral("<span class=\"fs-match\">");
        result += SecurityUtils::htmlEscape(text.mid(range.first, length));
        result += QStringLiteral("</span>");
        lastPos = range.first + length;
    }
    result += SecurityUtils::htmlEscape(text.mid(lastPos));
    return result;
}

} // namespace StringUtils

#endif // STRINGUTILS_H