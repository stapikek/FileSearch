#ifndef DRIVEUTILS_H
#define DRIVEUTILS_H

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace DriveUtils {

inline QString normalizeDrive(const QString& driveOrPath) {
    if (driveOrPath.isEmpty()) {
        return {};
    }

    const QString drive = driveOrPath.left(2).toUpper();
    if (drive.length() == 2 && drive[1] == QLatin1Char(':')) {
        return drive;
    }
    return {};
}

inline QString driveRootNative(const QString& letter) {
    return QDir::toNativeSeparators(letter + QLatin1Char('\\'));
}

#ifdef Q_OS_WIN
inline UINT driveTypeForLetter(const QString& letter) {
    return GetDriveTypeW(reinterpret_cast<LPCWSTR>(driveRootNative(letter).utf16()));
}

inline QString driveKindLabel(const QString& letter) {
    switch (driveTypeForLetter(letter)) {
    case DRIVE_FIXED: return QStringLiteral("Fixed");
    case DRIVE_REMOVABLE: return QStringLiteral("Removable");
    case DRIVE_REMOTE: return QStringLiteral("Network");
    case DRIVE_CDROM: return QStringLiteral("CD/DVD");
    case DRIVE_RAMDISK: return QStringLiteral("RAM disk");
    default: return QStringLiteral("Unknown");
    }
}
#else
inline QString driveKindLabel(const QString&) {
    return QStringLiteral("Local");
}
#endif

#ifdef Q_OS_WIN
inline bool isRemovableDrive(const QString& driveOrPath) {
    const QString letter = normalizeDrive(driveOrPath);
    if (letter.isEmpty()) {
        return false;
    }

    const UINT driveType = driveTypeForLetter(letter);
    return driveType == DRIVE_REMOVABLE || driveType == DRIVE_CDROM;
}
#else
inline bool isRemovableDrive(const QString&) {
    return false;
}
#endif

// Проверка доступности тома. Файловая система не важна: NTFS, FAT32, exFAT, ReFS и т.д.
inline bool isDriveAccessible(const QString& driveOrPath) {
    const QString letter = normalizeDrive(driveOrPath);
    if (letter.isEmpty()) {
        return false;
    }

    const QString rootPath = letter + QLatin1String(":/");
    const QString nativeRoot = QDir::toNativeSeparators(rootPath);

#ifdef Q_OS_WIN
    const UINT driveType = driveTypeForLetter(letter);
    if (driveType == DRIVE_NO_ROOT_DIR) {
        return false;
    }

    const QDir dir(rootPath);
    if (!dir.exists() || !dir.isReadable()) {
        return false;
    }

    // Строгая проверка только для съёмных носителей (извлечённая флешка)
    if (driveType == DRIVE_REMOVABLE || driveType == DRIVE_CDROM) {
        if (!GetVolumeInformationW(reinterpret_cast<LPCWSTR>(nativeRoot.utf16()),
                                   nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0)) {
            // FAT/exFAT иногда не отдают метаданные, но каталог читается
            return dir.exists() && dir.isReadable();
        }

        const QStorageInfo storage(rootPath);
        if (!storage.isValid() || !storage.isReady()) {
            return dir.exists() && dir.isReadable();
        }
    }

    return true;
#else
    const QDir dir(rootPath);
    return dir.exists() && dir.isReadable();
#endif
}

inline QSet<QString> availableDriveLetters() {
    QSet<QString> drives;
    const QList<QFileInfo> driveList = QDir::drives();

    for (const QFileInfo& drive : driveList) {
        const QString letter = normalizeDrive(drive.absolutePath());
        if (!letter.isEmpty() && isDriveAccessible(letter)) {
            drives.insert(letter);
        }
    }

    return drives;
}

inline QStringList availableDriveRoots() {
    QStringList roots;
    const QSet<QString> letters = availableDriveLetters();
    roots.reserve(letters.size());

    for (const QString& letter : letters) {
        roots.append(letter + QChar(u'/'));
    }

    return roots;
}

inline bool isDriveAvailable(const QString& drive, const QSet<QString>& available) {
    const QString normalized = normalizeDrive(drive);
    if (normalized.isEmpty()) {
        return false;
    }

    if (!available.isEmpty() && !available.contains(normalized)) {
        return false;
    }

    return isDriveAccessible(normalized);
}

inline bool isPathOnAvailableDrive(const QString& path, const QSet<QString>& available) {
    if (path.size() < 2) {
        return false;
    }
    return isDriveAvailable(path.left(2), available);
}

inline bool isPathAccessible(const QString& path) {
    if (path.size() < 2) {
        return false;
    }
    return isDriveAccessible(path.left(2));
}

} // namespace DriveUtils

#endif // DRIVEUTILS_H