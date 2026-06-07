#include <QApplication>
#include <algorithm>
#include <QTextStream>
#include "mainwindow.h"
#include "utils/languagemanager.h"
#include "utils/filesystemcheck.h"
#include "utils/portablepaths.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Устанавливаем информацию о приложении
    QApplication::setApplicationName("FileSearch");
    QApplication::setApplicationVersion("1.1.0");
    QApplication::setOrganizationName("FileSearch");
    PortablePaths::install();

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--check-filesystems"))) {
        const QVector<DriveAuditResult> results = FileSystemCheck::auditAllDrives();
        QTextStream(stdout) << FileSystemCheck::formatAuditReport(results);
        const int issues = std::count_if(results.cbegin(), results.cend(),
            [](const DriveAuditResult& r) { return r.status != QStringLiteral("OK"); });
        return issues > 0 ? 1 : 0;
    }

    LanguageManager::installTranslator(&app);

    // Устанавливаем стиль приложения
    app.setStyle("fusion");

    // Создаём и показываем главное окно
    MainWindow window;
    window.show();

    return app.exec();
}