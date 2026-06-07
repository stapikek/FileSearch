#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QTableView>
#include <QLabel>
#include <QProgressBar>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QMutex>
#include <QComboBox>
#include <QPushButton>
#include <QDateEdit>
#include <QSpinBox>
#include <QFutureWatcher>
#include <QSet>

#include "database/database.h"
#include "indexer/filesystemindexer.h"
#include "search/searchengine.h"
#include "models/fileindex.h"
#include "models/fileresultsmodel.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

/**
 * @brief Главное окно приложения FileSearch
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Конструктор
     * @param parent Родительский виджет
     */
    MainWindow(QWidget* parent = nullptr);

    /**
     * @brief Деструктор
     */
    ~MainWindow();

protected:
    /**
     * @brief Обработчик изменения размера окна
     */
    void resizeEvent(QResizeEvent* event) override;

    /**
     * @brief Обработчик закрытия окна
     */
    void closeEvent(QCloseEvent* event) override;

    /**
     * @brief Обработчик показа окна
     */
    void showEvent(QShowEvent* event) override;

    /**
     * @brief Обработчик смены языка интерфейса
     */
    void changeEvent(QEvent* event) override;

private slots:
    /**
     * @brief Обработчик изменения текста в поле поиска
     * @param text Новый текст
     */
    void onSearchTextChanged(const QString& text);

    /**
     * @brief Обработчик нажатия клавиши в поле поиска
     * @param event Событие клавиши
     */
    void onSearchReturnPressed();

    /**
     * @brief Обновляет результаты поиска
     */
    void updateSearchResults();

    /**
     * @brief Обработчик двойного клика по результату
     * @param row Строка
     * @param column Колонка
     */
    void onResultDoubleClicked(int row, int column);

    /**
     * @brief Обработчик выбора контекстного меню
     * @param action Выбранное действие
     */
    void onContextMenuAction(QAction* action);

    /**
     * @brief Показывает контекстное меню
     * @param point Позиция курсора
     */
    void showContextMenu(const QPoint& point);

    /**
     * @brief Копирует путь к файлу
     */
    void copyPath();

    /**
     * @brief Копирует имя файла
     */
    void copyName();

    /**
     * @brief Открывает файл в проводнике
     */
    void openInExplorer();

    /**
     * @brief Открывает файл
     */
    void openFile();

    /**
     * @brief Открывает папку файла
     */
    void openFolder();

    /**
     * @brief Обновляет статус индексации
     */
    void updateIndexingStatus();

    /**
     * @brief Обработчик завершения индексации
     * @param totalIndexed Количество проиндексированных файлов
     */
    void onIndexingFinished(qint64 totalIndexed);

    /**
     * @brief Обработчик прогресса индексации
     * @param processed Обработано файлов
     * @param total Всего файлов
     */
    void onIndexingStarted(int totalDrives);
    void onIndexingProgress(qint64 processed, qint64 total);
    void onIndexingDriveCompleted(int completedDrives, int totalDrives);
    void resetIndexingProgress();
    void updateIndexingProgressBar(qint64 processed, qint64 estimated);

    /**
     * @brief Перезапускает индексацию
     */
    void reindex();

    /**
     * @brief Показывает/скрывает строку состояния
     */
    void toggleStatusBar();

    /**
     * @brief Обработчик изменения состояния индекса
     */
    void onIndexLoaded(qint64 fileCount);

    /**
     * @brief Обработчик изменения фильтров
     */
    void onFiltersChanged();

    /**
     * @brief Обработчик клика по иконке в трее
     */
    void onTrayIconClicked(QSystemTrayIcon::ActivationReason reason);

    /**
     * @brief Переключает язык интерфейса
     */
    void setLanguage(const QString& languageCode);

private:
    /**
     * @brief Инициализирует UI
     */
    void setupUi();

    /**
     * @brief Инициализирует системный трей
     */
    void setupTray();

    /**
     * @brief Инициализирует соединения сигналов и слотов
     */
    void setupConnections();

    /**
     * @brief Загружает файлы из базы данных в поисковый движок
     */
    void loadFilesToSearchEngine();

    /**
     * @brief Асинхронно загружает индекс из базы данных
     */
    void loadFilesToSearchEngineAsync();

    /**
     * @brief Синхронизирует индекс с подключёнными дисками
     */
    void syncAvailableDrives();

    /**
     * @brief Обновляет строку состояния
     */
    void updateStatusBar();

    /**
     * @brief Позиционирует элементы UI
     */
    void layoutUi();

    /**
     * @brief Получает текущую выбранную запись
     */
    FileEntry getCurrentEntry() const;

    /**
     * @brief Форматирует размер файла
     */
    QString formatSize(qint64 size) const;

    /**
     * @brief Создаёт ярлык на рабочем столе
     */
    void createDesktopShortcut();

    /**
     * @brief Открывает файл с умной логикой (архивы через WinRAR, остальное через ассоциации)
     * @param filePath Путь к файлу
     */
    void openFileWithLogic(const QString& filePath);

    /**
     * @brief Обновляет тексты интерфейса после смены языка
     */
    void retranslateUi();

    /**
     * @brief Заполняет комбобоксы фильтров с сохранением выбранного индекса
     */
    void populateFilterCombos();

    Ui::MainWindow* ui;

    // UI компоненты
    QMenu* m_fileMenu;
    QMenu* m_settingsMenu;
    QMenu* m_languageMenu;
    QMenu* m_helpMenu;
    QAction* m_reindexMenuAction;
    QAction* m_exitAction;
    QAction* m_languageRuAction;
    QAction* m_languageEnAction;
    QAction* m_aboutAction;
    QAction* m_licenseAction;
    QAction* m_trayShowAction;
    QAction* m_trayReindexAction;
    QAction* m_trayQuitAction;
    QAction* m_ctxOpenFolderAction;
    QAction* m_ctxShowInExplorerAction;
    QAction* m_ctxCopyPathAction;
    QAction* m_ctxCopyNameAction;
    QPushButton* m_reindexBtn;
    QLabel* m_sizeLabel;
    QLabel* m_dateLabel;
    QLabel* m_typeLabel;
    QLineEdit* m_searchEdit;
    QTableView* m_resultsTable;
    FileResultsModel* m_resultsModel;
    QFutureWatcher<void>* m_loadWatcher;
    QLabel* m_statusLabel;
    QLabel* m_indexingLabel;
    QProgressBar* m_progressBar;
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;
    QMenu* m_contextMenu;

    // Фильтры UI
    QComboBox* m_sizeFilterCombo;
    QComboBox* m_dateFilterCombo;
    QComboBox* m_typeFilterCombo;
    QWidget* m_filterPanel;

    // Модель данных
    QVector<SearchResult> m_currentResults;
    SearchFilters m_filters;

    // Работа с данными
    Database* m_database;
    FileSystemIndexer* m_indexer;
    SearchEngine* m_searchEngine;

    // Состояние
    bool m_indexLoaded;
    bool m_indexingInProgress;
    int m_indexingDriveTotal;
    int m_indexingDriveIndex;
    int m_totalFiles;

    // Таймер debounce для поиска
    QTimer* m_searchDebounceTimer;
    QTimer* m_statusUpdateTimer;
    QTimer* m_driveCheckTimer;

    QSet<QString> m_availableDrives;

    // Мьютекс для потокобезопасности
    mutable QMutex m_resultsMutex;

    // История поиска
    QStringList m_searchHistory;
    int m_historyIndex;

    // Размеры окон
    static const int MIN_WIDTH = 800;
    static const int MIN_HEIGHT = 600;
};

#endif // MAINWINDOW_H