#ifndef SECURITYUTILS_H
#define SECURITYUTILS_H

#include <QFileInfo>
#include <QUrl>
#include <QString>

namespace SecurityUtils {

inline QString htmlEscape(const QString& text) {
    QString escaped;
    escaped.reserve(text.size() + 16);

    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case '&': escaped += QStringLiteral("&amp;"); break;
        case '<': escaped += QStringLiteral("&lt;"); break;
        case '>': escaped += QStringLiteral("&gt;"); break;
        case '"': escaped += QStringLiteral("&quot;"); break;
        case '\'': escaped += QStringLiteral("&#39;"); break;
        default: escaped += ch; break;
        }
    }

    return escaped;
}

inline QString escapePowerShellSingleQuoted(const QString& value) {
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return escaped;
}

inline bool isSafeLocalPath(const QString& path) {
    if (path.isEmpty() || path.size() > 32767) {
        return false;
    }

    if (path.contains(QLatin1Char('\0'))) {
        return false;
    }

    if (path.startsWith(QStringLiteral("\\\\"))) {
        // UNC-пути не открываем из индекса по умолчанию
        return false;
    }

    const QUrl url = QUrl::fromLocalFile(path);
    if (!url.isLocalFile()) {
        return false;
    }

    const QFileInfo info(path);
    return info.exists() && info.isReadable();
}

} // namespace SecurityUtils

#endif // SECURITYUTILS_H