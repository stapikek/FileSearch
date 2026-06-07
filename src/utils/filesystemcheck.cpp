#include "filesystemcheck.h"
#include "driveutils.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString detectFileSystem(const QString& root, const QString& letter) {
    const QStorageInfo storage(root);
    if (storage.isValid()) {
        const QByteArray fs = storage.fileSystemType();
        if (!fs.isEmpty()) {
            return QString::fromLatin1(fs);
        }
    }

#ifdef Q_OS_WIN
    const QString nativeRoot = DriveUtils::driveRootNative(letter);
    wchar_t fsName[256] = {};
    if (GetVolumeInformationW(reinterpret_cast<LPCWSTR>(nativeRoot.utf16()),
                              nullptr, 0, nullptr, nullptr, nullptr,
                              fsName, static_cast<DWORD>(sizeof(fsName) / sizeof(fsName[0])))) {
        return QString::fromWCharArray(fsName);
    }
#endif

    return QStringLiteral("Unknown");
}

int probeDirectory(const QString& root, bool* canEnumerate) {
    *canEnumerate = false;

    QDir dir(root);
    if (!dir.isReadable()) {
        return 0;
    }

    int count = 0;
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::NoIteratorFlags);

    while (it.hasNext() && count < 32) {
        it.next();
        ++count;
    }

    const bool isEmpty = dir.entryList(QDir::NoDotAndDotDot | QDir::Hidden).isEmpty();
    *canEnumerate = count > 0 || isEmpty;
    return count;
}

QString buildStatus(const DriveAuditResult& result) {
    if (!result.accessible) {
        return QStringLiteral("Unavailable");
    }
    if (!result.readable) {
        return QStringLiteral("Not readable");
    }
    if (!result.canEnumerate) {
        return QStringLiteral("Cannot list files");
    }
    if (!result.supported) {
        return QStringLiteral("Unsupported FS");
    }
    return QStringLiteral("OK");
}

} // namespace

namespace FileSystemCheck {

bool isSupportedFileSystem(const QString& fileSystem) {
    const QString fs = fileSystem.trimmed().toUpper();
    if (fs.isEmpty() || fs == QStringLiteral("UNKNOWN")) {
        // Неизвестная ФС: пробуем через QDir, не блокируем заранее
        return true;
    }

    static const QStringList supported = {
        QStringLiteral("NTFS"),
        QStringLiteral("FAT"),
        QStringLiteral("FAT32"),
        QStringLiteral("EXFAT"),
        QStringLiteral("REFS"),
        QStringLiteral("UDF"),
        QStringLiteral("CDFS"),
        QStringLiteral("CSVFS"),   // Cloud placeholders (OneDrive)
        QStringLiteral("MS-NFS"),  // NFS client
        QStringLiteral("NFS"),
    };

    for (const QString& known : supported) {
        if (fs.startsWith(known)) {
            return true;
        }
    }

    // Нестандартные, но часто рабочие через Win32-API
    if (fs.contains(QStringLiteral("FAT")) || fs.contains(QStringLiteral("NTFS"))) {
        return true;
    }

    return false;
}

QVector<DriveAuditResult> auditAllDrives() {
    QVector<DriveAuditResult> results;

    for (const QFileInfo& driveInfo : QDir::drives()) {
        DriveAuditResult result;
        result.letter = DriveUtils::normalizeDrive(driveInfo.absolutePath());
        if (result.letter.isEmpty()) {
            continue;
        }

        result.root = result.letter + QChar(u'/');
        result.driveKind = DriveUtils::driveKindLabel(result.letter);
        result.fileSystem = detectFileSystem(result.root, result.letter);

        const QStorageInfo storage(result.root);
        const QDir dir(result.root);
        result.readable = dir.isReadable();
        result.accessible = (storage.isValid() && storage.isReady())
            || DriveUtils::isDriveAccessible(result.letter)
            || result.readable;

        if (result.accessible) {
            result.probeEntries = probeDirectory(result.root, &result.canEnumerate);
            if (!result.readable && result.canEnumerate) {
                result.readable = true;
            }
        }

        result.supported = isSupportedFileSystem(result.fileSystem)
            || (result.canEnumerate && !result.fileSystem.isEmpty());

        if (result.fileSystem.compare(QStringLiteral("RAW"), Qt::CaseInsensitive) == 0) {
            result.supported = false;
            result.note = QStringLiteral("Unformatted volume");
        } else if (result.driveKind == QStringLiteral("CD/DVD") && !result.canEnumerate) {
            result.note = QStringLiteral("No media in optical drive");
        } else if (result.driveKind == QStringLiteral("Network") && result.canEnumerate) {
            result.note = QStringLiteral("Network share");
        } else if (!isSupportedFileSystem(result.fileSystem) && result.canEnumerate) {
            result.note = QStringLiteral("Non-standard FS, but directory listing works");
            result.supported = true;
        }

        result.status = buildStatus(result);
        results.append(std::move(result));
    }

    return results;
}

QString formatAuditReport(const QVector<DriveAuditResult>& results) {
    QString report;
    QTextStream out(&report);

    out << "FileSearch filesystem audit\n";
    out << "===========================\n";

    int okCount = 0;
    for (const DriveAuditResult& r : results) {
        if (r.status == QStringLiteral("OK")) {
            ++okCount;
        }

        out << r.letter << "  kind=" << r.driveKind
            << "  fs=" << (r.fileSystem.isEmpty() ? QStringLiteral("-") : r.fileSystem)
            << "  status=" << r.status;

        if (r.probeEntries > 0) {
            out << "  probe=" << r.probeEntries << "+ entries";
        }
        if (!r.note.isEmpty()) {
            out << "  (" << r.note << ')';
        }
        out << '\n';
    }

    out << "---------------------------\n";
    out << "Total drives: " << results.size()
        << "  OK: " << okCount
        << "  Issues: " << (results.size() - okCount) << '\n';

    out << "Supported file systems: NTFS, FAT, FAT32, exFAT, ReFS, UDF, CDFS, network shares\n";
    return report;
}

} // namespace FileSystemCheck