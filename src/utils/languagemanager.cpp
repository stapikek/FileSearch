#include "languagemanager.h"
#include <QSettings>
#include <QTranslator>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>

QTranslator* LanguageManager::s_appTranslator = nullptr;

QString LanguageManager::currentLanguage() {
    QSettings settings;
    const QString saved = settings.value(QStringLiteral("Settings/language")).toString();
    return isSupported(saved) ? saved : DefaultLanguage;
}

void LanguageManager::setCurrentLanguage(const QString& languageCode) {
    const QString code = isSupported(languageCode) ? languageCode : DefaultLanguage;
    QSettings settings;
    settings.setValue(QStringLiteral("Settings/language"), code);
}

bool LanguageManager::isSupported(const QString& languageCode) {
    return languageCode == DefaultLanguage || languageCode == EnglishLanguage;
}

QString LanguageManager::displayName(const QString& languageCode) {
    if (languageCode == EnglishLanguage) {
        return QStringLiteral("English");
    }
    return QStringLiteral("Русский");
}

void LanguageManager::installTranslator(QApplication* app) {
    if (!app) {
        return;
    }

    if (s_appTranslator) {
        app->removeTranslator(s_appTranslator);
        delete s_appTranslator;
        s_appTranslator = nullptr;
    }

    const QString language = currentLanguage();
    if (language == DefaultLanguage) {
        return;
    }

    s_appTranslator = new QTranslator(app);

    const QString resourcePath = QStringLiteral(":/translations/filesearch_%1.qm").arg(language);
    const QString filePath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/translations/filesearch_%1.qm").arg(language);

    if (s_appTranslator->load(resourcePath) || s_appTranslator->load(filePath)) {
        app->installTranslator(s_appTranslator);
    } else {
        delete s_appTranslator;
        s_appTranslator = nullptr;
        qWarning() << "Failed to load translation for language:" << language;
    }
}