#include "fileresultsmodel.h"
#include "utils/stringutils.h"
#include "utils/driveutils.h"
#include <QPainter>
#include <QColor>
#include <algorithm>
#include <numeric>

namespace {

bool isDefaultFilters(const SearchFilters& filters) {
    return filters.minSize == -1
        && filters.maxSize == -1
        && !filters.minDate.isValid()
        && !filters.maxDate.isValid()
        && filters.fileTypes.isEmpty()
        && filters.includeDirectories;
}

bool isEntryVisible(const FileEntry& entry, const SearchFilters& filters) {
    return filters.matches(entry);
}

} // namespace

FileResultsModel::FileResultsModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void FileResultsModel::clearBrowseData() {
    beginResetModel();
    m_mode = BrowseMode;
    m_allFiles = nullptr;
    m_searchResults.clear();
    m_browseIndices.clear();
    m_query.clear();
    endResetModel();
}

void FileResultsModel::setBrowseData(const QVector<FileEntry>& files, const SearchFilters& filters, const QString& query) {
    beginResetModel();
    m_mode = BrowseMode;
    m_allFiles = &files;
    m_searchResults.clear();
    m_query = query;
    m_browseIndices.clear();
    m_browseIndices.reserve(files.size());

    if (isDefaultFilters(filters)) {
        m_browseIndices.resize(files.size());
        std::iota(m_browseIndices.begin(), m_browseIndices.end(), 0);
    } else {
        for (int i = 0; i < files.size(); ++i) {
            if (!filters.matches(files[i])) {
                continue;
            }
            m_browseIndices.append(i);
        }
    }

    endResetModel();
}

void FileResultsModel::setSearchResults(QVector<SearchResult> results, const QString& query) {
    beginResetModel();
    m_mode = SearchMode;
    m_allFiles = nullptr;
    m_searchResults = std::move(results);
    m_browseIndices.clear();
    m_query = query;
    endResetModel();
}

FileEntry FileResultsModel::entryAt(int row) const {
    if (row < 0) {
        return FileEntry();
    }
    return entryForRow(row);
}

const FileEntry& FileResultsModel::entryForRow(int row) const {
    if (m_mode == SearchMode) {
        return m_searchResults.at(row).entry;
    }
    return m_allFiles->at(m_browseIndices.at(row));
}

int FileResultsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_mode == SearchMode ? m_searchResults.size() : m_browseIndices.size();
}

int FileResultsModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return 4;
}

QVariant FileResultsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return {};
    }

    const FileEntry& entry = entryForRow(index.row());
    const int column = index.column();

    if (role == Qt::UserRole) {
        return entry.path;
    }
    if (role == Qt::UserRole + 1) {
        return entry.displaysAsDirectory();
    }
    if (role == Qt::ToolTipRole) {
        return column == 0 ? entry.name : entry.path;
    }

    if (role == Qt::DecorationRole && column == 0) {
        return entry.displaysAsDirectory() ? folderIcon() : fileIcon();
    }

    if (role == Qt::TextAlignmentRole) {
        if (column == 2) {
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
        if (column == 3) {
            return static_cast<int>(Qt::AlignCenter);
        }
    }

    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (column) {
    case 0:
        return m_query.isEmpty() ? entry.name : StringUtils::highlightMatch(entry.name, m_query);
    case 1:
        return entry.path;
    case 2:
        return entry.formattedSize();
    case 3:
        return entry.formattedDate();
    default:
        return {};
    }
}

QVariant FileResultsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case 0: return tr("Имя");
    case 1: return tr("Путь");
    case 2: return tr("Размер");
    case 3: return tr("Дата изменения");
    default: return {};
    }
}

void FileResultsModel::retranslateHeaders() {
    emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
}

void FileResultsModel::sort(int column, Qt::SortOrder order) {
    const bool ascending = order == Qt::AscendingOrder;

    auto compareEntries = [column, ascending](const FileEntry& a, const FileEntry& b) {
        int cmp = 0;
        switch (column) {
        case 0: cmp = QString::compare(a.name, b.name, Qt::CaseInsensitive); break;
        case 1: cmp = QString::compare(a.path, b.path, Qt::CaseInsensitive); break;
        case 2: cmp = (a.size < b.size) ? -1 : (a.size > b.size ? 1 : 0); break;
        case 3: cmp = (a.modified < b.modified) ? -1 : (a.modified > b.modified ? 1 : 0); break;
        default: cmp = 0; break;
        }
        return ascending ? cmp < 0 : cmp > 0;
    };

    beginResetModel();

    if (m_mode == SearchMode) {
        if (m_searchResults.isEmpty()) {
            endResetModel();
            return;
        }
        std::sort(m_searchResults.begin(), m_searchResults.end(),
                  [compareEntries](const SearchResult& a, const SearchResult& b) {
                      return compareEntries(a.entry, b.entry);
                  });
    } else {
        if (!m_allFiles || m_browseIndices.isEmpty()) {
            endResetModel();
            return;
        }
        std::sort(m_browseIndices.begin(), m_browseIndices.end(),
                  [this, compareEntries](int ia, int ib) {
                      return compareEntries(m_allFiles->at(ia), m_allFiles->at(ib));
                  });
    }

    endResetModel();
}

QIcon FileResultsModel::folderIcon() {
    static QIcon icon = []() {
        QPixmap pix(24, 24);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(255, 200, 0));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(2, 6, 20, 16, 2, 2);
        p.setBrush(QColor(255, 220, 100));
        p.drawRoundedRect(2, 2, 20, 8, 2, 2);
        return QIcon(pix);
    }();
    return icon;
}

QIcon FileResultsModel::fileIcon() {
    static QIcon icon = []() {
        QPixmap pix(24, 24);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(100, 150, 200));
        p.setPen(QColor(50, 100, 150));
        p.drawRoundedRect(4, 2, 16, 20, 2, 2);
        p.drawLine(14, 2, 14, 8);
        return QIcon(pix);
    }();
    return icon;
}