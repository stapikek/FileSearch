#include "mainwindow.h"
#include "utils/languagemanager.h"
#include <QApplication>
#include <QHeaderView>
#include <QDesktopServices>
#include <QClipboard>
#include <QFileDialog>
#include <QShortcut>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QMessageBox>
#include <QFileInfo>
#include <QPushButton>
#include "utils/htmldelegate.h"
#include "utils/securityutils.h"
#include <QDebug>
#include <QSettings>
#include <QElapsedTimer>
#include <QProcess>
#include <QCloseEvent>
#include <QShowEvent>
#include <QEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidget>
#include <QIcon>
#include <QStandardPaths>
#include "utils/driveutils.h"
#include <QtConcurrent/QtConcurrent>
#include "utils/svgicons.h"
#include <QCoreApplication>
#include <QAbstractButton>
#include <QFile>

namespace {

QIcon applicationIcon() {
    static QIcon cached;
    if (!cached.isNull()) {
        return cached;
    }

    const QStringList paths = {
        QStringLiteral(":/icons/favicon.ico"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/favicon.ico"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/favicon.ico"),
        QCoreApplication::applicationFilePath(),
    };

    for (const QString& path : paths) {
        if (path.startsWith(QLatin1String(":/")) || QFile::exists(path)) {
            const QIcon icon(path);
            if (!icon.isNull() && !icon.availableSizes().isEmpty()) {
                cached = icon;
                return cached;
            }
        }
    }

    cached = QIcon(QCoreApplication::applicationFilePath());
    return cached;
}

} // namespace

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#include <combaseapi.h>
#endif

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(nullptr)
    , m_fileMenu(nullptr)
    , m_settingsMenu(nullptr)
    , m_languageMenu(nullptr)
    , m_helpMenu(nullptr)
    , m_reindexMenuAction(nullptr)
    , m_exitAction(nullptr)
    , m_languageRuAction(nullptr)
    , m_languageEnAction(nullptr)
    , m_aboutAction(nullptr)
    , m_licenseAction(nullptr)
    , m_trayShowAction(nullptr)
    , m_trayReindexAction(nullptr)
    , m_trayQuitAction(nullptr)
    , m_ctxOpenFolderAction(nullptr)
    , m_ctxShowInExplorerAction(nullptr)
    , m_ctxCopyPathAction(nullptr)
    , m_ctxCopyNameAction(nullptr)
    , m_reindexBtn(nullptr)
    , m_sizeLabel(nullptr)
    , m_dateLabel(nullptr)
    , m_typeLabel(nullptr)
    , m_searchEdit(nullptr)
    , m_resultsTable(nullptr)
    , m_resultsModel(nullptr)
    , m_loadWatcher(nullptr)
    , m_statusLabel(nullptr)
    , m_indexingLabel(nullptr)
    , m_progressBar(nullptr)
    , m_trayIcon(nullptr)
    , m_sizeFilterCombo(nullptr)
    , m_dateFilterCombo(nullptr)
    , m_typeFilterCombo(nullptr)
    , m_filterPanel(nullptr)
    , m_database(nullptr)
    , m_indexer(nullptr)
    , m_searchEngine(nullptr)
    , m_indexLoaded(false)
    , m_indexingInProgress(false)
    , m_indexingDriveTotal(0)
    , m_indexingDriveIndex(0)
    , m_totalFiles(0)
    , m_searchDebounceTimer(nullptr)
    , m_statusUpdateTimer(nullptr)
    , m_driveCheckTimer(nullptr)
    , m_historyIndex(-1)
{
    setupUi();

    // Инициализируем объекты до setupConnections
    m_database = new Database(QString());
    m_searchEngine = new SearchEngine(this);
    // Без parent — иначе moveToThread() в индексаторе не работает
    m_indexer = new FileSystemIndexer(m_database, nullptr);

    setupConnections();

    // Инициализируем базу данных
    if (!m_database->init()) {
        QMessageBox::critical(this, tr("Ошибка"), tr("Не удалось инициализировать базу данных!"));
        return;
    }

    m_availableDrives = DriveUtils::availableDriveLetters();

    // Инициализируем базу данных
    loadFilesToSearchEngineAsync();

    // Запускаем индексацию если база пуста (проверим после загрузки)

    // Инициализация системного трея
    setupTray();
    retranslateUi();

    // Создаём ярлык на рабочем столе
    createDesktopShortcut();

    // Устанавливаем фокус на поле поиска
    m_searchEdit->setFocus();

    qDebug() << "MainWindow initialized";
}

MainWindow::~MainWindow() {
    if (m_loadWatcher) {
        m_loadWatcher->cancel();
        m_loadWatcher->waitForFinished();
    }
    if (m_indexer) {
        m_indexer->stopIndexing();
        delete m_indexer;
        m_indexer = nullptr;
    }
    if (m_database) {
        m_database->close();
        delete m_database;
    }
}

void MainWindow::setupUi() {
    setWindowTitle("FileSearch");
    setMinimumSize(MIN_WIDTH, MIN_HEIGHT);
    resize(1024, 768);

    const QIcon icon = applicationIcon();
    if (!icon.isNull()) {
        setWindowIcon(icon);
    }

    // Темная тема - глобальный стиль
    qApp->setStyleSheet(R"(
        QMainWindow {
            background-color: #1e1e1e;
            color: #ffffff;
        }
        QWidget {
            background-color: #1e1e1e;
            color: #cccccc;
            font-family: 'Segoe UI', Arial, sans-serif;
        }
        QMenuBar {
            background-color: #2d2d2d;
            color: #ffffff;
            border-bottom: 1px solid #3d3d3d;
        }
        QMenuBar::item:selected {
            background-color: #3d3d3d;
        }
        QMenu {
            background-color: #2d2d2d;
            color: #ffffff;
            border: 1px solid #3d3d3d;
        }
        QMenu::item:selected {
            background-color: #0d47a1;
        }
        QMenu::separator {
            height: 1px;
            background-color: #3d3d3d;
        }
        QLineEdit {
            background-color: #2d2d2d;
            color: #ffffff;
            border: 2px solid #3d3d3d;
            border-radius: 6px;
            padding: 8px 12px;
            font-size: 14px;
            selection-background-color: #0d47a1;
        }
        QLineEdit:focus {
            border: 2px solid #0d47a1;
        }
        QLineEdit:placeholder {
            color: #666666;
        }
        QPushButton {
            background-color: #0d47a1;
            color: #ffffff;
            border: none;
            border-radius: 6px;
            padding: 8px 16px;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #1565c0;
        }
        QPushButton:pressed {
            background-color: #0a3d91;
        }
        QComboBox {
            background-color: #2d2d2d;
            color: #ffffff;
            border: 1px solid #3d3d3d;
            border-radius: 4px;
            padding: 5px 10px;
            selection-background-color: #0d47a1;
        }
        QComboBox:hover {
            border: 1px solid #0d47a1;
        }
        QComboBox::drop-down {
            border: none;
            width: 25px;
        }
        QComboBox::down-arrow {
            image: url(:/icons/arrow-down.svg);
            width: 10px;
            height: 6px;
            subcontrol-position: center right;
            subcontrol-origin: padding;
            margin-right: 8px;
        }
        QComboBox QAbstractItemView {
            background-color: #2d2d2d;
            color: #ffffff;
            border: 1px solid #3d3d3d;
            selection-background-color: #0d47a1;
        }
        QTableWidget {
            background-color: #1e1e1e;
            color: #ffffff;
            border: none;
            gridline-color: #2d2d2d;
            selection-background-color: #0d47a1;
            font-size: 13px;
        }
        QTableWidget::item {
            padding: 8px 4px;
            border-bottom: 1px solid #2d2d2d;
            background-color: #1e1e1e;
        }
        QTableWidget::item:selected {
            background-color: #0d47a1;
            color: #ffffff;
        }
        QTableWidget::item:hover {
            background-color: #2a2a2a;
        }
        QTableWidget::item:selected:hover {
            background-color: #0d47a1;
            color: #ffffff;
        }
        QHeaderView {
            background-color: #252526;
            color: #cccccc;
            border: none;
        }
        QHeaderView::section {
            background-color: #2d2d2d;
            color: #cccccc;
            padding: 10px 20px 10px 4px;
            border: none;
            border-bottom: 2px solid #0d47a1;
            font-weight: bold;
        }
        QHeaderView::section:hover {
            background-color: #3d3d3d;
        }
        QHeaderView::down-arrow {
            image: url(:/icons/arrow-down.svg);
            width: 10px;
            height: 6px;
            subcontrol-position: right center;
            subcontrol-origin: padding;
            margin-right: 10px;
        }
        QHeaderView::up-arrow {
            image: url(:/icons/arrow-up.svg);
            width: 10px;
            height: 6px;
            subcontrol-position: right center;
            subcontrol-origin: padding;
            margin-right: 10px;
        }
        QProgressBar {
            border: 2px solid #3d3d3d;
            border-radius: 4px;
            text-align: center;
            background-color: #2d2d2d;
            color: #ffffff;
        }
        QProgressBar::chunk {
            background-color: #0d47a1;
            border-radius: 2px;
        }
        QStatusBar {
            background-color: #2d2d2d;
            color: #cccccc;
            border-top: 1px solid #3d3d3d;
        }
        QSizeGrip {
            background-color: #2d2d2d;
            image: url(:/icons/grip.svg);
            width: 14px;
            height: 14px;
        }
        QAbstractScrollArea::corner {
            background: #1e1e1e;
            border: none;
        }
        QLabel {
            color: #cccccc;
            background-color: transparent;
        }
        QScrollBar:vertical {
            background-color: #1e1e1e;
            width: 12px;
            border: none;
        }
        QScrollBar::handle:vertical {
            background-color: #3d3d3d;
            border-radius: 6px;
            min-height: 30px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: #4d4d4d;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar:horizontal {
            background-color: #1e1e1e;
            height: 12px;
            border: none;
        }
        QScrollBar::handle:horizontal {
            background-color: #3d3d3d;
            border-radius: 6px;
            min-width: 30px;
        }
        QScrollBar::handle:horizontal:hover {
            background-color: #4d4d4d;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }
        QMessageBox {
            background-color: #2d2d2d;
            color: #ffffff;
        }
        QMessageBox QLabel {
            color: #ffffff;
        }
        QMessageBox QPushButton {
            min-width: 80px;
            padding: 8px 16px;
        }
    )");

    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    // Менюбар
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    m_fileMenu = menuBar->addMenu(QString());
    m_reindexMenuAction = new QAction(tr("Обновить"), this);
    connect(m_reindexMenuAction, &QAction::triggered, this, &MainWindow::reindex);
    m_fileMenu->addAction(m_reindexMenuAction);
    m_fileMenu->addSeparator();
    m_exitAction = new QAction(this);
    m_exitAction->setShortcut(QKeySequence::fromString("Ctrl+Q"));
    connect(m_exitAction, &QAction::triggered, this, &QMainWindow::close);
    m_fileMenu->addAction(m_exitAction);

    m_settingsMenu = menuBar->addMenu(QString());
    m_languageMenu = m_settingsMenu->addMenu(QString());
    m_languageRuAction = new QAction(this);
    m_languageRuAction->setCheckable(true);
    connect(m_languageRuAction, &QAction::triggered, this, [this]() {
        setLanguage(LanguageManager::DefaultLanguage);
    });
    m_languageMenu->addAction(m_languageRuAction);

    m_languageEnAction = new QAction(this);
    m_languageEnAction->setCheckable(true);
    connect(m_languageEnAction, &QAction::triggered, this, [this]() {
        setLanguage(LanguageManager::EnglishLanguage);
    });
    m_languageMenu->addAction(m_languageEnAction);

    m_helpMenu = menuBar->addMenu(QString());
    m_aboutAction = new QAction(this);
    connect(m_aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox aboutBox(this);
        aboutBox.setWindowTitle(tr("О программе"));
        aboutBox.setText(tr(
            "<h2>FileSearch</h2>"
            "<p><b>Версия программы:</b> 1.1</p>"
            "<p><b>Copyright © 2026 stapi</b></p>"
            "<p><a href='https://filesearch.pw/'>https://filesearch.pw/</a></p>"
        ));
        aboutBox.setIconPixmap(applicationIcon().pixmap(64, 64));
        aboutBox.exec();
    });
    m_helpMenu->addAction(m_aboutAction);

    m_licenseAction = new QAction(this);
    connect(m_licenseAction, &QAction::triggered, this, [this]() {
        QMessageBox licenseBox(this);
        licenseBox.setWindowTitle(tr("Лицензия"));
        licenseBox.setText(tr(
            "<h2>FileSearch</h2>"
            "<p><b>Copyright (c) 2026 stapi</b></p>"
            "<hr>"
            "<p>Настоящее лицензионное соглашение предоставляет право любому лицу, владеющему копиями данного "
            "программного обеспечения и сопутствующих файлов документации (далее 'Программа') на бесплатную "
            "эксплуатацию Программы, включая неограниченные права на использование, копирование, изменение, "
            "объединение, публикацию, распространение, сублицензирование и/или продажу копий Программы, "
            "при условии соблюдения следующего:</p>"
            "<p>Указанная выше информация об авторских правах и данные условия должны быть включены во все "
            "копии или существенные части Программы.</p>"
            "<hr>"
            "<p><b>ПРОГРАММА ПРЕДОСТАВЛЯЕТСЯ ПО ПРИНЦИПУ \"КАК ЕСТЬ\"</b>, БЕЗ ЛЮБЫХ ЯВНЫХ ИЛИ ПОДРАЗУМЕВАЕМЫХ ГАРАНТИЙ, "
            "ВКЛЮЧАЯ, НО НЕ ОГРАНИЧИВАЯСЬ, ГАРАНТИИ КОММЕРЧЕСКОЙ ЦЕННОСТИ, ПРИГОДНОСТИ ДЛЯ КОНКРЕТНЫХ ЦЕЛЕЙ "
            "И НЕ НАРУШЕНИЯ ПРАВ. НИ ПРИ КАКИХ ОБСТОЯТЕЛЬСТВАХ АВТОРЫ ИЛИ ВЛАДЕЛЬЦЫ АВТОРСКИХ ПРАВ НЕ НЕСУТ "
            "ОТВЕТСТВЕННОСТИ В СЛУЧАЕ ЛЮБЫХ ПРЕТЕНЗИЙ И УБЫТКОВ.</p>"
        ));
        licenseBox.setIconPixmap(applicationIcon().pixmap(64, 64));
        licenseBox.exec();
    });
    m_helpMenu->addAction(m_licenseAction);

    // Главный layout
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Верхняя панель поиска
    QWidget* topPanel = new QWidget(this);
    topPanel->setStyleSheet("background-color: #1e1e1e;");
    topPanel->setFixedHeight(60);
    QHBoxLayout* topLayout = new QHBoxLayout(topPanel);
    topLayout->setContentsMargins(15, 10, 15, 10);
    topLayout->setSpacing(15);

    // Поле поиска
    m_searchEdit = new QLineEdit(this);
    topLayout->addWidget(m_searchEdit, 1);

    // Кнопка «Обновить» с SVG-иконкой и подписью
    m_reindexBtn = new QPushButton(tr("Обновить"), this);
    m_reindexBtn->setObjectName("reindexBtn");
    m_reindexBtn->setMinimumWidth(110);
    m_reindexBtn->setFixedHeight(36);
    m_reindexBtn->setCursor(Qt::PointingHandCursor);
    m_reindexBtn->setStyleSheet(R"(
        QPushButton#reindexBtn {
            background-color: #2d2d2d;
            color: #ffffff;
            border: 1px solid #555555;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: 13px;
        }
        QPushButton#reindexBtn:hover {
            background-color: #1565c0;
            border: 1px solid #42a5f5;
            color: #ffffff;
        }
        QPushButton#reindexBtn:pressed {
            background-color: #0d47a1;
            border: 1px solid #1565c0;
        }
    )");

    const QIcon refreshIcon = SvgIcons::iconFromSvg(SvgIcons::refreshIconSvg(), QSize(20, 20));
    if (!refreshIcon.isNull()) {
        m_reindexBtn->setIcon(refreshIcon);
        m_reindexBtn->setIconSize(QSize(20, 20));
    }

    connect(m_reindexBtn, &QPushButton::clicked, this, &MainWindow::reindex);
    topLayout->addWidget(m_reindexBtn);

    mainLayout->addWidget(topPanel);

    // Панель фильтров
    m_filterPanel = new QWidget(this);
    m_filterPanel->setStyleSheet("background-color: #252526;");
    m_filterPanel->setFixedHeight(50);
    QHBoxLayout* filterLayout = new QHBoxLayout(m_filterPanel);
    filterLayout->setContentsMargins(15, 0, 15, 0);
    filterLayout->setSpacing(20);

    // Фильтр по размеру
    m_sizeLabel = new QLabel(this);
    m_sizeLabel->setStyleSheet("font-weight: bold;");
    filterLayout->addWidget(m_sizeLabel);
    m_sizeFilterCombo = new QComboBox(this);
    m_sizeFilterCombo->setFixedWidth(140);
    filterLayout->addWidget(m_sizeFilterCombo);

    // Фильтр по дате
    m_dateLabel = new QLabel(this);
    m_dateLabel->setStyleSheet("font-weight: bold;");
    filterLayout->addWidget(m_dateLabel);
    m_dateFilterCombo = new QComboBox(this);
    m_dateFilterCombo->setFixedWidth(140);
    filterLayout->addWidget(m_dateFilterCombo);

    // Фильтр по типу
    m_typeLabel = new QLabel(this);
    m_typeLabel->setStyleSheet("font-weight: bold;");
    filterLayout->addWidget(m_typeLabel);
    m_typeFilterCombo = new QComboBox(this);
    m_typeFilterCombo->setFixedWidth(140);
    filterLayout->addWidget(m_typeFilterCombo);

    filterLayout->addStretch();

    mainLayout->addWidget(m_filterPanel);

    // Таблица результатов
    m_resultsModel = new FileResultsModel(this);
    m_resultsTable = new QTableView(this);
    m_resultsTable->setModel(m_resultsModel);

    // Настройка столбцов - все столбцы можно растягивать вручную
    m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_resultsTable->horizontalHeader()->setStretchLastSection(false);
    m_resultsTable->horizontalHeader()->setSortIndicatorShown(true);
    m_resultsTable->horizontalHeader()->setSectionsClickable(true);

    // Устанавливаем начальную ширину столбцов
    m_resultsTable->setColumnWidth(0, 250);
    m_resultsTable->setColumnWidth(1, 400);
    m_resultsTable->setColumnWidth(2, 100);
    m_resultsTable->setColumnWidth(3, 150);

    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultsTable->setShowGrid(true);
    m_resultsTable->setAlternatingRowColors(false);
    m_resultsTable->setSortingEnabled(true);
    m_resultsTable->sortByColumn(0, Qt::AscendingOrder);
    m_resultsTable->setTextElideMode(Qt::ElideMiddle);
    m_resultsTable->setCursor(Qt::PointingHandCursor);
    m_resultsTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultsTable->verticalHeader()->setVisible(false);
    m_resultsTable->setItemDelegateForColumn(0, new HtmlDelegate(this));

    connect(m_resultsTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.column() == 0 && index.isValid()) {
            const FileEntry entry = m_resultsModel->entryAt(index.row());
            if (!entry.path.isEmpty()) {
                if (entry.displaysAsDirectory()) {
                    if (SecurityUtils::isSafeLocalPath(entry.path)) {
#ifdef Q_OS_WIN
                        QStringList args;
                        args << "/e," << QDir::toNativeSeparators(entry.path);
                        QProcess::startDetached("explorer.exe", args);
#endif
                    }
                } else {
                    openFileWithLogic(entry.path);
                }
            }
        }
    });

    mainLayout->addWidget(m_resultsTable, 1);

    // Строка состояния
    QStatusBar* statusBarWidget = statusBar();
    statusBarWidget->setStyleSheet("padding: 5px 10px;");

    m_statusLabel = new QLabel(this);
    statusBarWidget->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMaximumWidth(200);
    m_progressBar->setRange(0, 100);
    m_progressBar->setFormat(QStringLiteral("%p%"));
    m_progressBar->setTextVisible(true);
    m_progressBar->setVisible(false);
    statusBarWidget->addPermanentWidget(m_progressBar);

    m_indexingLabel = new QLabel("", this);
    statusBarWidget->addPermanentWidget(m_indexingLabel);

    // Контекстное меню
    m_contextMenu = new QMenu(this);
    m_ctxOpenFolderAction = m_contextMenu->addAction(QString(), this, &MainWindow::openFolder);
    m_contextMenu->addSeparator();
    m_ctxShowInExplorerAction = m_contextMenu->addAction(QString(), this, &MainWindow::openInExplorer);
    m_contextMenu->addSeparator();
    m_ctxCopyPathAction = m_contextMenu->addAction(QString(), this, &MainWindow::copyPath);
    m_ctxCopyNameAction = m_contextMenu->addAction(QString(), this, &MainWindow::copyName);

    populateFilterCombos();
}

void MainWindow::setupTray() {
    m_trayIcon = new QSystemTrayIcon(this);

    m_trayIcon->setIcon(applicationIcon());

    m_trayIcon->setToolTip("FileSearch");

    m_trayMenu = new QMenu(this);
    m_trayShowAction = new QAction(this);
    connect(m_trayShowAction, &QAction::triggered, this, [this]() {
        show();
        raise();
        activateWindow();
    });
    m_trayMenu->addAction(m_trayShowAction);

    m_trayReindexAction = new QAction(tr("Обновить"), this);
    connect(m_trayReindexAction, &QAction::triggered, this, &MainWindow::reindex);
    m_trayMenu->addAction(m_trayReindexAction);

    m_trayMenu->addSeparator();

    m_trayQuitAction = new QAction(this);
    connect(m_trayQuitAction, &QAction::triggered, qApp, &QApplication::quit);
    m_trayMenu->addAction(m_trayQuitAction);

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconClicked);
}

void MainWindow::setupConnections() {
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(300);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, &MainWindow::updateSearchResults);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MainWindow::onSearchReturnPressed);

    // Фильтры
    connect(m_sizeFilterCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onFiltersChanged);
    connect(m_dateFilterCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onFiltersChanged);
    connect(m_typeFilterCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onFiltersChanged);

    // Контекстное меню
    connect(m_resultsTable, &QTableView::customContextMenuRequested, this, &MainWindow::showContextMenu);

    // Индексатор
    connect(m_indexer, &FileSystemIndexer::indexingStarted, this, &MainWindow::onIndexingStarted);
    connect(m_indexer, &FileSystemIndexer::indexingFinished, this, &MainWindow::onIndexingFinished);
    connect(m_indexer, &FileSystemIndexer::indexingProgress, this, &MainWindow::onIndexingProgress);
    connect(m_indexer, &FileSystemIndexer::indexingDriveCompleted, this, &MainWindow::onIndexingDriveCompleted);
    connect(m_indexer, &FileSystemIndexer::indexingDriveStarted, this, [this](const QString& drive) {
        m_indexingLabel->setText(tr("Обновление: %1").arg(drive));
    });
    connect(m_searchEngine, &SearchEngine::indexLoaded, this, &MainWindow::onIndexLoaded);

    // История поиска
    QShortcut* upShortcut = new QShortcut(QKeySequence::fromString("Up"), this);
    connect(upShortcut, &QShortcut::activated, this, [this]() {
        if (!m_searchHistory.isEmpty() && m_historyIndex < m_searchHistory.size() - 1) {
            m_historyIndex++;
            m_searchEdit->setText(m_searchHistory[m_searchHistory.size() - 1 - m_historyIndex]);
        }
    });

    QShortcut* downShortcut = new QShortcut(QKeySequence::fromString("Down"), this);
    connect(downShortcut, &QShortcut::activated, this, [this]() {
        if (m_historyIndex > 0) {
            m_historyIndex--;
            m_searchEdit->setText(m_searchHistory[m_searchHistory.size() - 1 - m_historyIndex]);
        } else if (m_historyIndex == 0) {
            m_historyIndex = -1;
            m_searchEdit->clear();
        }
    });

    m_driveCheckTimer = new QTimer(this);
    m_driveCheckTimer->setInterval(2000);
    connect(m_driveCheckTimer, &QTimer::timeout, this, &MainWindow::syncAvailableDrives);
    m_driveCheckTimer->start();
}

void MainWindow::loadFilesToSearchEngine() {
    qDebug() << "Loading files from database...";

    m_availableDrives = DriveUtils::availableDriveLetters();
    m_searchEngine->setAvailableDrives(m_availableDrives);

    const QVector<FileEntry> files = m_database->getAllFiles();
    m_totalFiles = files.size();

    if (!files.isEmpty()) {
        m_searchEngine->loadIndex(files);
        m_indexLoaded = true;
        m_totalFiles = m_searchEngine->fileCount();
        m_statusLabel->setText(tr("Загружено: %1 файлов").arg(m_totalFiles));
        qDebug() << "Loaded" << m_totalFiles << "files into search engine";
    } else {
        m_indexLoaded = false;
        m_statusLabel->setText(tr("Список пуст. Нажмите «Обновить»."));
    }

    updateSearchResults();
}

void MainWindow::loadFilesToSearchEngineAsync() {
    m_statusLabel->setText(tr("Загрузка списка файлов..."));
    QTimer::singleShot(0, this, [this]() {
        loadFilesToSearchEngine();
        if (!m_indexLoaded && m_indexer) {
            m_indexingInProgress = true;
            m_indexer->startIndexing();
        }
    });
}

void MainWindow::syncAvailableDrives() {
    if (!m_database || !m_searchEngine || m_indexingInProgress) {
        return;
    }

    const QSet<QString> currentDrives = DriveUtils::availableDriveLetters();
    const int removedCount = m_database->removeUnavailableDrives(currentDrives);
    const bool drivesChanged = currentDrives != m_availableDrives;

    m_availableDrives = currentDrives;
    m_searchEngine->setAvailableDrives(currentDrives);

    if (removedCount > 0) {
        loadFilesToSearchEngine();
        updateSearchResults();
        m_statusLabel->setText(tr("Удалено файлов с отключённых дисков: %1").arg(removedCount));
        return;
    }

    if (drivesChanged && m_indexLoaded) {
        updateSearchResults();
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    syncAvailableDrives();
}

void MainWindow::onSearchTextChanged(const QString& text) {
    m_historyIndex = -1;

    m_searchDebounceTimer->stop();
    if (!text.isEmpty()) {
        m_searchDebounceTimer->start();
    } else {
        // Показываем все файлы при очистке поиска
        updateSearchResults();
    }
}

void MainWindow::onSearchReturnPressed() {
    QString text = m_searchEdit->text();
    if (!text.isEmpty() && (m_searchHistory.isEmpty() || m_searchHistory.last() != text)) {
        m_searchHistory.append(text);
        m_historyIndex = -1;
    }

    m_searchDebounceTimer->stop();
    updateSearchResults();

    if (m_resultsModel->rowCount() > 0) {
        m_resultsTable->selectRow(0);
        openFile();
    }
}

void MainWindow::onFiltersChanged() {
    // Сбрасываем фильтры
    m_filters = SearchFilters();

    // Размер
    switch (m_sizeFilterCombo->currentIndex()) {
        case 1: m_filters.minSize = 0; m_filters.maxSize = 1024; break;                    // < 1 KB
        case 2: m_filters.minSize = 1024; m_filters.maxSize = 100 * 1024; break;          // 1 KB - 100 KB
        case 3: m_filters.minSize = 100 * 1024; m_filters.maxSize = 10 * 1024 * 1024; break; // 100 KB - 10 MB
        case 4: m_filters.minSize = 10 * 1024 * 1024; m_filters.maxSize = 100 * 1024 * 1024; break; // 10 MB - 100 MB
        case 5: m_filters.minSize = 100 * 1024 * 1024; m_filters.maxSize = -1; break;      // > 100 MB
        default: m_filters.minSize = -1; m_filters.maxSize = -1; break;
    }

    // Дата
    QDate today = QDate::currentDate();
    switch (m_dateFilterCombo->currentIndex()) {
        case 1: m_filters.minDate = today; break;                    // Сегодня
        case 2: m_filters.minDate = today.addDays(-7); break;       // Эта неделя
        case 3: m_filters.minDate = today.addMonths(-1); break;     // Этот месяц
        case 4: m_filters.minDate = today.addYears(-1); break;      // Этот год
        default: m_filters.minDate = QDate(); break;
    }

    // Тип файла - может быть несколько расширений через /
    QString typeText = m_typeFilterCombo->currentText();
    if (m_typeFilterCombo->currentIndex() != 0) {
        QStringList extensions;

        // Разбиваем по "/" если есть несколько расширений
        QStringList parts = typeText.split("/");
        for (QString& part : parts) {
            part = part.trimmed();
            if (!part.isEmpty()) {
                if (!part.startsWith(".")) {
                    part = "." + part;
                }
                extensions.append(part.toLower());
            }
        }

        m_filters.fileTypes = extensions;
    }

    // Перезапускаем поиск
    updateSearchResults();
}

void MainWindow::updateSearchResults() {
    if (!m_indexLoaded || !m_resultsModel) {
        return;
    }

    const QString query = m_searchEdit->text().trimmed();
    QElapsedTimer timer;
    timer.start();

    if (query.isEmpty()) {
        const QVector<FileEntry>& indexedFiles = m_searchEngine->files();
        m_resultsModel->setBrowseData(indexedFiles, m_filters);
        m_currentResults.clear();
        qDebug() << "Showing all files: engine=" << indexedFiles.size()
                 << "visible=" << m_resultsModel->rowCount()
                 << "in" << timer.elapsed() << "ms";
    } else {
        QVector<SearchResult> results = m_searchEngine->search(query, 5000, m_filters);
        {
            QMutexLocker locker(&m_resultsMutex);
            m_currentResults = results;
        }
        m_resultsModel->setSearchResults(std::move(results), query);

        qDebug() << "Search for" << query << "found" << m_resultsModel->rowCount()
                 << "results in" << timer.elapsed() << "ms";
    }

    updateStatusBar();
    m_resultsTable->scrollToTop();
}

void MainWindow::onResultDoubleClicked(int row, int column) {
    Q_UNUSED(column);
    openFile();
}

void MainWindow::showContextMenu(const QPoint& point) {
    const QModelIndex index = m_resultsTable->indexAt(point);
    if (!index.isValid()) {
        return;
    }

    m_resultsTable->selectRow(index.row());
    m_contextMenu->exec(m_resultsTable->mapToGlobal(point));
}

void MainWindow::copyPath() {
    FileEntry entry = getCurrentEntry();
    if (!entry.path.isEmpty()) {
        QClipboard* clipboard = QApplication::clipboard();
        clipboard->setText(entry.path);
        m_statusLabel->setText(tr("Скопировано: %1").arg(entry.path));
    }
}

void MainWindow::copyName() {
    FileEntry entry = getCurrentEntry();
    if (!entry.name.isEmpty()) {
        QClipboard* clipboard = QApplication::clipboard();
        clipboard->setText(entry.name);
        m_statusLabel->setText(tr("Скопировано: %1").arg(entry.name));
    }
}

void MainWindow::openInExplorer() {
    FileEntry entry = getCurrentEntry();
    if (!entry.path.isEmpty() && SecurityUtils::isSafeLocalPath(entry.path)) {
#ifdef Q_OS_WIN
        QStringList args;
        args << "/select," << QDir::toNativeSeparators(entry.path);
        QProcess::startDetached("explorer.exe", args);
#endif
    }
}

void MainWindow::openFile() {
    FileEntry entry = getCurrentEntry();
    if (entry.path.isEmpty()) {
        return;
    }

    if (entry.displaysAsDirectory()) {
        if (!SecurityUtils::isSafeLocalPath(entry.path)) {
            return;
        }
#ifdef Q_OS_WIN
        QStringList args;
        args << "/e," << QDir::toNativeSeparators(entry.path);
        QProcess::startDetached("explorer.exe", args);
#endif
    } else {
        openFileWithLogic(entry.path);
    }
}

void MainWindow::openFolder() {
    FileEntry entry = getCurrentEntry();
    if (entry.path.isEmpty()) {
        return;
    }

    const QString folderPath = entry.displaysAsDirectory() ? entry.path : QFileInfo(entry.path).absolutePath();
    if (!SecurityUtils::isSafeLocalPath(folderPath)) {
        return;
    }

#ifdef Q_OS_WIN
    QStringList args;
    args << "/e," << QDir::toNativeSeparators(folderPath);
    QProcess::startDetached("explorer.exe", args);
#endif
}

FileEntry MainWindow::getCurrentEntry() const {
    const QModelIndex index = m_resultsTable->currentIndex();
    if (index.isValid() && m_resultsModel) {
        return m_resultsModel->entryAt(index.row());
    }
    return FileEntry();
}

void MainWindow::updateStatusBar() {
    const int count = m_resultsModel ? m_resultsModel->rowCount() : 0;
    QString status = tr("Найдено: %1").arg(count);
    if (!m_searchEdit->text().isEmpty()) {
        status += tr(" (запрос: %1)").arg(m_searchEdit->text());
    }
    if (m_indexLoaded) {
        status += tr(" | На устройстве файлов: %1").arg(m_totalFiles);
    }
    m_statusLabel->setText(status);
}

void MainWindow::resetIndexingProgress() {
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
}

void MainWindow::updateIndexingProgressBar(qint64 processed, qint64 estimated) {
    if (m_indexingDriveTotal <= 0) {
        return;
    }

    const int segmentSize = qMax(1, 100 / m_indexingDriveTotal);
    const int base = qBound(0, m_indexingDriveIndex * segmentSize, 99);

    int within = 0;
    if (estimated > 0 && processed > 0) {
        within = static_cast<int>((processed * segmentSize) / estimated);
    }
    within = qBound(0, segmentSize, within);

    int value = base + within;
    if (m_indexingDriveIndex >= m_indexingDriveTotal - 1) {
        value = qMin(99, qMax(base, value));
    } else {
        value = qMin(base + segmentSize - 1, value);
    }

    m_progressBar->setValue(qBound(0, 99, value));
}

void MainWindow::onIndexingStarted(int totalDrives) {
    m_indexingInProgress = true;
    m_indexingDriveTotal = totalDrives;
    m_indexingDriveIndex = 0;
    resetIndexingProgress();
}

void MainWindow::onIndexingDriveCompleted(int completedDrives, int totalDrives) {
    if (totalDrives <= 0) {
        return;
    }

    const int value = qBound(0, 100, (completedDrives * 100) / totalDrives);
    m_progressBar->setValue(qMin(99, value));
    m_indexingDriveIndex = qMin(completedDrives, totalDrives - 1);
}

void MainWindow::onIndexingFinished(qint64 totalIndexed) {
    m_indexingInProgress = false;
    m_progressBar->setValue(100);
    QTimer::singleShot(400, this, [this]() {
        m_progressBar->setVisible(false);
    });
    m_indexingLabel->setText(tr("Готово"));

    if (m_database) {
        m_database->analyze();
    }

    loadFilesToSearchEngine();
    updateSearchResults();

    QMessageBox::information(this, tr("Обновление завершено"),
                           tr("Обновлено файлов: %1").arg(totalIndexed));
}

void MainWindow::onIndexingProgress(qint64 processed, qint64 estimated) {
    m_indexingInProgress = true;
    m_progressBar->setVisible(true);
    updateIndexingProgressBar(processed, estimated);
}

void MainWindow::reindex() {
    if (m_database) {
        m_database->clearAll();
    }

    if (m_searchEngine) {
        m_searchEngine->clearIndex();
    }

    m_indexLoaded = false;
    m_totalFiles = 0;
    if (m_resultsModel) {
        m_resultsModel->clearBrowseData();
    }

    m_indexingInProgress = true;
    resetIndexingProgress();
    if (m_indexer) {
        m_indexer->startIndexing();
    }

    m_statusLabel->setText(tr("Обновление..."));
}

void MainWindow::onIndexLoaded(qint64 fileCount) {
    qDebug() << "Index loaded with" << fileCount << "files";
    m_indexLoaded = true;
}

void MainWindow::onTrayIconClicked(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        if (isVisible()) {
            hide();
        } else {
            show();
            raise();
            activateWindow();
        }
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    layoutUi();
}

void MainWindow::layoutUi() {
    // Layout обрабатывается автоматически
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Сохраняем позицию и размер окна
    QSettings settings;
    settings.setValue("MainWindow/geometry", saveGeometry());
    settings.setValue("MainWindow/state", saveState());

    // Проверяем, запущена ли индексация
    if (m_indexingInProgress) {
        // Спрашиваем пользователя
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Обновление"));
        msgBox.setText(tr("Идёт обновление файлов. Вы уверены?"));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::No);
        if (QAbstractButton* yesBtn = msgBox.button(QMessageBox::Yes)) {
            yesBtn->setText(tr("Да"));
        }
        if (QAbstractButton* noBtn = msgBox.button(QMessageBox::No)) {
            noBtn->setText(tr("Нет"));
        }

        if (msgBox.exec() == QMessageBox::No) {
            event->ignore();
            return;
        }

        // Останавливаем индексацию
        if (m_indexer) {
            m_indexer->stopIndexing();
        }
    }

    // Сворачиваем в трей вместо закрытия
    hide();
    if (m_trayIcon) {
        m_trayIcon->showMessage("FileSearch",
                              tr("Приложение свернуто в трей. Для выхода используйте меню."),
                              QSystemTrayIcon::Information, 3000);
    }
    event->ignore();
}

void MainWindow::onContextMenuAction(QAction* action) {
    Q_UNUSED(action);
}

void MainWindow::updateIndexingStatus() {
    // Обновление статуса индексации
}

void MainWindow::toggleStatusBar() {
    statusBar()->setVisible(!statusBar()->isVisible());
}

QString MainWindow::formatSize(qint64 size) const {
    const double KB = 1024.0;
    const double MB = KB * 1024.0;
    const double GB = MB * 1024.0;

    if (size < KB) {
        return QString("%1 B").arg(size);
    } else if (size < MB) {
        return QString("%1 KB").arg(size / KB, 0, 'f', 1);
    } else if (size < GB) {
        return QString("%1 MB").arg(size / MB, 0, 'f', 1);
    } else {
        return QString("%1 GB").arg(size / GB, 0, 'f', 2);
    }
}

void MainWindow::createDesktopShortcut() {
#ifdef Q_OS_WIN
    QString exePath = QDir::toNativeSeparators(qApp->applicationFilePath());
    QString shortcutPath = QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/FileSearch.lnk");
    QString iconPath = QDir::toNativeSeparators(qApp->applicationFilePath());

    if (QFile::exists(shortcutPath)) {
        qDebug() << "Shortcut already exists";
        return;
    }

    qDebug() << "Creating shortcut at:" << shortcutPath;
    qDebug() << "Exe path:" << exePath;
    qDebug() << "Icon path:" << iconPath;

    // Используем PowerShell для создания ярлыка
    QString script = QString(
        "$wsh = New-Object -ComObject WScript.Shell; "
        "$sc = $wsh.CreateShortcut('%1'); "
        "$sc.TargetPath = '%2'; "
        "$sc.IconLocation = '%3'; "
        "$sc.WorkingDirectory = '%4'; "
        "$sc.Description = 'FileSearch - Быстрый поиск файлов'; "
        "$sc.Save()"
    ).arg(
        SecurityUtils::escapePowerShellSingleQuoted(shortcutPath),
        SecurityUtils::escapePowerShellSingleQuoted(exePath),
        SecurityUtils::escapePowerShellSingleQuoted(iconPath),
        SecurityUtils::escapePowerShellSingleQuoted(qApp->applicationDirPath())
    );

    QProcess process;
    process.start("powershell", {"-ExecutionPolicy", "Bypass", "-Command", script});
    process.waitForFinished();

    if (process.exitCode() == 0) {
        qDebug() << "Shortcut created successfully";
    } else {
        qDebug() << "Failed to create shortcut:" << process.readAllStandardError();
    }
#endif
}

void MainWindow::openFileWithLogic(const QString& filePath) {
#ifdef Q_OS_WIN
    if (!SecurityUtils::isSafeLocalPath(filePath)) {
        qWarning() << "Blocked unsafe or missing path:" << filePath;
        QMessageBox::warning(this, tr("Ошибка"), tr("Файл недоступен или путь небезопасен."));
        return;
    }

    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();
    
    // Список архивных расширений
    static const QStringList archiveExtensions = {
        "zip", "rar", "7z", "tar", "gz", "bz2", "xz", 
        "arj", "cab", "iso", "lzh", "tar.gz", "tar.bz2",
        "tar.xz", "tgz", "tbz2", "zipx"
    };
    
    // Проверяем, является ли файл архивом
    if (archiveExtensions.contains(extension)) {
        // Пытаемся найти WinRAR
        QStringList winrarPaths = {
            "C:\\Program Files\\WinRAR\\WinRAR.exe",
            "C:\\Program Files (x86)\\WinRAR\\WinRAR.exe",
            QDir::homePath() + "\\AppData\\Local\\Programs\\WinRAR\\WinRAR.exe"
        };
        
        QString winrarPath;
        for (const QString& path : winrarPaths) {
            if (QFile::exists(path)) {
                winrarPath = path;
                break;
            }
        }
        
        if (!winrarPath.isEmpty()) {
            // Открываем через WinRAR
            QStringList args;
            args << QDir::toNativeSeparators(filePath);
            if (QProcess::startDetached(winrarPath, args)) {
                qDebug() << "Opened archive with WinRAR:" << filePath;
                return;
            }
        }
        
        // Fallback: открываем через проводник
        qDebug() << "WinRAR not found, opening archive with Explorer:" << filePath;
        QStringList args;
        args << "/select," << QDir::toNativeSeparators(filePath);
        QProcess::startDetached("explorer.exe", args);
        return;
    }
    
    // Для .exe файлов - спрашиваем подтверждение
    if (extension == "exe") {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Запуск программы"));
        msgBox.setText(tr("Вы уверены, что хотите запустить программу?"));
        msgBox.setInformativeText(fileInfo.fileName());
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::No);
        msgBox.setIcon(QMessageBox::Question);
        if (QAbstractButton* yesBtn = msgBox.button(QMessageBox::Yes)) {
            yesBtn->setText(tr("Да"));
        }
        if (QAbstractButton* noBtn = msgBox.button(QMessageBox::No)) {
            noBtn->setText(tr("Нет"));
        }

        if (msgBox.exec() != QMessageBox::Yes) {
            return;
        }
    }
    
    // Для всех остальных файлов - используем стандартную ассоциацию Windows
    // ShellExecute обрабатывает все правильно: .mp3 → плеер, .txt → редактор и т.д.
    HINSTANCE result = ShellExecuteW(
        nullptr,
        L"open",
        reinterpret_cast<const wchar_t*>(filePath.utf16()),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );
    
    if (reinterpret_cast<qintptr>(result) <= 32) {
        qDebug() << "Failed to open file with ShellExecute:" << filePath;
        // Fallback на QDesktopServices
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
    }
#else
    // Для не-Windows платформ используем QDesktopServices
    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
#endif
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::setLanguage(const QString& languageCode) {
    if (LanguageManager::currentLanguage() == languageCode) {
        return;
    }

    LanguageManager::setCurrentLanguage(languageCode);
    LanguageManager::installTranslator(qApp);

    QEvent languageEvent(QEvent::LanguageChange);
    QApplication::sendEvent(this, &languageEvent);

    if (m_resultsModel) {
        m_resultsModel->retranslateHeaders();
    }

    updateStatusBar();
}

void MainWindow::populateFilterCombos() {
    const auto refillCombo = [](QComboBox* combo, const QStringList& items) {
        if (!combo) {
            return;
        }
        const QSignalBlocker blocker(combo);
        const int currentIndex = combo->currentIndex();
        combo->clear();
        combo->addItems(items);
        combo->setCurrentIndex(currentIndex >= 0 && currentIndex < combo->count() ? currentIndex : 0);
    };

    refillCombo(m_sizeFilterCombo, {
        tr("Любой"),
        tr("< 1 KB"),
        tr("1 KB - 100 KB"),
        tr("100 KB - 10 MB"),
        tr("10 MB - 100 MB"),
        tr("> 100 MB")
    });

    refillCombo(m_dateFilterCombo, {
        tr("Любая"),
        tr("Сегодня"),
        tr("Эта неделя"),
        tr("Этот месяц"),
        tr("Этот год")
    });

    refillCombo(m_typeFilterCombo, {
        tr("Все"),
        QStringLiteral(".exe"),
        QStringLiteral(".txt"),
        QStringLiteral(".pdf"),
        QStringLiteral(".doc/.docx"),
        QStringLiteral(".jpg/.png"),
        QStringLiteral(".mp3"),
        QStringLiteral(".mp4"),
        QStringLiteral(".zip/.rar")
    });
}

void MainWindow::retranslateUi() {
    if (m_fileMenu) {
        m_fileMenu->setTitle(tr("Файл"));
    }
    if (m_settingsMenu) {
        m_settingsMenu->setTitle(tr("Настройки"));
    }
    if (m_languageMenu) {
        m_languageMenu->setTitle(tr("Язык"));
    }
    if (m_helpMenu) {
        m_helpMenu->setTitle(tr("Справка"));
    }

    if (m_reindexMenuAction) {
        m_reindexMenuAction->setText(tr("Обновить"));
    }
    if (m_exitAction) {
        m_exitAction->setText(tr("Выход"));
    }
    if (m_aboutAction) {
        m_aboutAction->setText(tr("О программе"));
    }
    if (m_licenseAction) {
        m_licenseAction->setText(tr("Лицензия"));
    }

    const QString currentLanguage = LanguageManager::currentLanguage();
    if (m_languageRuAction) {
        m_languageRuAction->setText(tr("Русский"));
        m_languageRuAction->setChecked(currentLanguage == LanguageManager::DefaultLanguage);
    }
    if (m_languageEnAction) {
        m_languageEnAction->setText(tr("English"));
        m_languageEnAction->setChecked(currentLanguage == LanguageManager::EnglishLanguage);
    }

    if (m_searchEdit) {
        m_searchEdit->setPlaceholderText(tr("Введите имя файла..."));
    }
    if (m_reindexBtn) {
        m_reindexBtn->setText(tr("Обновить"));
    }

    if (m_sizeLabel) {
        m_sizeLabel->setText(tr("Размер:"));
    }
    if (m_dateLabel) {
        m_dateLabel->setText(tr("Дата:"));
    }
    if (m_typeLabel) {
        m_typeLabel->setText(tr("Тип:"));
    }
    populateFilterCombos();

    if (m_ctxOpenFolderAction) {
        m_ctxOpenFolderAction->setText(tr("Открыть папку"));
    }
    if (m_ctxShowInExplorerAction) {
        m_ctxShowInExplorerAction->setText(tr("Перейти к файлу"));
    }
    if (m_ctxCopyPathAction) {
        m_ctxCopyPathAction->setText(tr("Копировать путь"));
    }
    if (m_ctxCopyNameAction) {
        m_ctxCopyNameAction->setText(tr("Копировать имя"));
    }

    if (m_trayShowAction) {
        m_trayShowAction->setText(tr("Показать"));
    }
    if (m_trayReindexAction) {
        m_trayReindexAction->setText(tr("Обновить"));
    }
    if (m_trayQuitAction) {
        m_trayQuitAction->setText(tr("Выход"));
    }
}