#ifndef FILERESULTSMODEL_H
#define FILERESULTSMODEL_H

#include <QAbstractTableModel>
#include <QIcon>
#include <QVector>
#include "models/fileindex.h"
#include "search/searchengine.h"

/**
 * @brief Модель таблицы результатов — рендерит только видимые строки
 */
class FileResultsModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit FileResultsModel(QObject* parent = nullptr);

    void setBrowseData(const QVector<FileEntry>& files, const SearchFilters& filters, const QString& query = {});
    void setSearchResults(QVector<SearchResult> results, const QString& query);
    void clearBrowseData();

    FileEntry entryAt(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    void retranslateHeaders();

private:
    enum Mode { BrowseMode, SearchMode };

    const FileEntry& entryForRow(int row) const;
    static QIcon folderIcon();
    static QIcon fileIcon();

    Mode m_mode = BrowseMode;
    QVector<SearchResult> m_searchResults;
    QVector<int> m_browseIndices;
    const QVector<FileEntry>* m_allFiles = nullptr;
    QString m_query;
};

#endif // FILERESULTSMODEL_H