#ifndef FILESYSTEMCHECK_H
#define FILESYSTEMCHECK_H

#include <QString>
#include <QVector>

struct DriveAuditResult {
    QString letter;
    QString root;
    QString driveKind;
    QString fileSystem;
    bool accessible = false;
    bool readable = false;
    bool canEnumerate = false;
    int probeEntries = 0;
    bool supported = false;
    QString status;
    QString note;
};

namespace FileSystemCheck {

QVector<DriveAuditResult> auditAllDrives();
bool isSupportedFileSystem(const QString& fileSystem);
QString formatAuditReport(const QVector<DriveAuditResult>& results);

} // namespace FileSystemCheck

#endif // FILESYSTEMCHECK_H