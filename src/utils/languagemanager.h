#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QString>
#include <QApplication>

class QTranslator;

/**
 * @brief Управление языком интерфейса (ru по умолчанию, en опционально)
 */
class LanguageManager {
public:
    static constexpr const char* DefaultLanguage = "ru";
    static constexpr const char* EnglishLanguage = "en";

    static QString currentLanguage();
    static void setCurrentLanguage(const QString& languageCode);
    static void installTranslator(QApplication* app);

    static QString displayName(const QString& languageCode);
    static bool isSupported(const QString& languageCode);

private:
    static QTranslator* s_appTranslator;
};

#endif // LANGUAGEMANAGER_H