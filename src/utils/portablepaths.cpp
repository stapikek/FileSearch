#include "portablepaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

namespace PortablePaths {

namespace {

bool g_checked = false;
bool g_portable = false;

void detectPortableMode() {
    if (g_checked) {
        return;
    }
    g_checked = true;
    const QString marker = QCoreApplication::applicationDirPath()
        + QStringLiteral("/portable.txt");
    g_portable = QFile::exists(marker);
}

} // namespace

bool isPortableMode() {
    detectPortableMode();
    return g_portable;
}

void install() {
    detectPortableMode();
    if (!g_portable) {
        return;
    }

    const QString dataDir = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    QDir().mkpath(dataDir);

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dataDir);
}

QString dataDirectory() {
    detectPortableMode();
    if (g_portable) {
        const QString dataDir = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
        QDir().mkpath(dataDir);
        return dataDir;
    }
    // %AppData%\FileSearch (без вложенной папки FileSearch\FileSearch)
#ifdef Q_OS_WIN
    const QString path = QDir::homePath() + QStringLiteral("/AppData/Roaming/FileSearch");
#else
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
    QDir().mkpath(path);
    return path;
}

} // namespace PortablePaths