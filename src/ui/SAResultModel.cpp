//
//  SAResultModel.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAResultModel.h"
#include "SAContentEditing.h"

#include <QApplication>
#include <QBrush>
#include <QFont>
#include <QPalette>

#include <algorithm>

SAResultModel::SAResultModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void SAResultModel::setResult(const SAResult &result)
{
    beginResetModel();
    m_result = result;
    m_editedRows.clear();
    m_binaryColumns.clear();
    m_sortColumn = -1;   // a freshly loaded result has not been locally re-sorted yet
    endResetModel();
}

void SAResultModel::clearRows()
{
    beginResetModel();
    m_result.rows.clear();
    m_editedRows.clear();
    m_binaryColumns.clear();
    endResetModel();
}

void SAResultModel::setEditable(bool editable)
{
    m_editable = editable;
}

void SAResultModel::setNullString(const QString &value)
{
    m_nullString = value;
    if (rowCount() && columnCount()) Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

void SAResultModel::setBinaryAsHex(bool on)
{
    m_binaryAsHex = on;
    if (rowCount() && columnCount()) Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

SACell SAResultModel::cellAt(int row, int column) const
{
    return m_result.cell(row, column);
}

void SAResultModel::setCell(int row, int column, const SACell &cell, bool binary)
{
    if (row < 0 || row >= m_result.rows.size() || column < 0 || column >= m_result.fields.size()) return;
    SARow &r = m_result.rows[row];
    while (r.size() < m_result.fields.size()) r.append(SACell::null());
    r[column] = cell;
    if (binary) m_binaryColumns[row].insert(column);
    else m_binaryColumns[row].remove(column);
    m_editedRows.insert(row);
    Q_EMIT dataChanged(index(row, column), index(row, column));
}

int SAResultModel::appendBlankRow(const SARow &values)
{
    const int row = m_result.rows.size();
    beginInsertRows(QModelIndex(), row, row);
    SARow cells = values;
    while (cells.size() < m_result.fields.size()) cells.append(SACell::null());
    m_result.rows.append(cells);
    m_editedRows.insert(row);
    endInsertRows();
    return row;
}

void SAResultModel::removeRowsAt(QList<int> rows)
{
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) {
        if (row < 0 || row >= m_result.rows.size()) continue;
        beginRemoveRows(QModelIndex(), row, row);
        m_result.rows.removeAt(row);
        endRemoveRows();
    }
    m_editedRows.clear();
    m_binaryColumns.clear();
}

void SAResultModel::replaceRow(int row, const SARow &cells)
{
    if (row < 0 || row >= m_result.rows.size()) return;
    m_result.rows[row] = cells;
    Q_EMIT dataChanged(index(row, 0), index(row, qMax(0, columnCount() - 1)));
}

SARow SAResultModel::rowCells(int row) const
{
    if (row < 0 || row >= m_result.rows.size()) return SARow();
    return m_result.rows.at(row);
}

void SAResultModel::setRowEdited(int row, bool edited)
{
    if (edited) m_editedRows.insert(row);
    else { m_editedRows.remove(row); m_binaryColumns.remove(row); }
    if (row >= 0 && row < rowCount() && columnCount()) Q_EMIT dataChanged(index(row, 0), index(row, columnCount() - 1));
}

void SAResultModel::clearEditMarks()
{
    m_editedRows.clear();
    m_binaryColumns.clear();
}

void SAResultModel::setSortIndicator(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    Q_EMIT headerDataChanged(Qt::Horizontal, 0, qMax(0, columnCount() - 1));
}

namespace {

// Bit columns arrive as raw bytes (e.g. a single 0x01), not ASCII digits;
// read them as a big-endian integer instead of text.
double bitCellToNumber(const QByteArray &data)
{
    quint64 value = 0;
    for (unsigned char byte : data) value = (value << 8) | byte;
    return double(value);
}

// -1/0/1 for `a` vs `b`, treating NULL as the smallest possible value so it
// always sorts to one end regardless of direction (see SAResultModel::sort).
int compareCells(const SAField &field, const SACell &a, const SACell &b)
{
    if (a.isNull != b.isNull) return a.isNull ? -1 : 1;
    if (a.isNull) return 0;
    if (field.typeGroup == QLatin1String("bit")) {
        const double da = bitCellToNumber(a.data), db = bitCellToNumber(b.data);
        return da < db ? -1 : (da > db ? 1 : 0);
    }
    if (field.typeGroup == QLatin1String("integer") || field.typeGroup == QLatin1String("float")) {
        bool okA = false, okB = false;
        const double da = QString::fromUtf8(a.data).toDouble(&okA);
        const double db = QString::fromUtf8(b.data).toDouble(&okB);
        if (okA && okB) return da < db ? -1 : (da > db ? 1 : 0);
    }
    // Dates (MySQL's YYYY-MM-DD[ HH:MM:SS] output), strings, enums and
    // everything else compare lexicographically, which already sorts dates
    // correctly without a separate code path.
    return QString::fromUtf8(a.data).compare(QString::fromUtf8(b.data), Qt::CaseInsensitive);
}

} // namespace

void SAResultModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= m_result.fields.size()) return;
    const SAField field = m_result.fields.at(column);
    std::stable_sort(m_result.rows.begin(), m_result.rows.end(), [&](const SARow &a, const SARow &b) {
        const int cmp = compareCells(field, a.value(column), b.value(column));
        return order == Qt::AscendingOrder ? cmp < 0 : cmp > 0;
    });
    setSortIndicator(column, order);
    if (rowCount() && columnCount()) Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

int SAResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_result.rows.size();
}

int SAResultModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_result.fields.size();
}

QVariant SAResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_result.rows.size() || index.column() >= m_result.fields.size()) return QVariant();
    const SAField &field = m_result.fields.at(index.column());
    const SACell &cell = m_result.cell(index.row(), index.column());
    switch (role) {
    case Qt::DisplayRole: {
        QString text = SAContentEditing::displayString(field, cell, m_nullString, m_binaryAsHex, m_displayLimit);
        // Keep rows single-line in the grid; the field editor shows the full text.
        text.replace(QLatin1Char('\n'), QStringLiteral("⏎"));
        text.replace(QLatin1Char('\r'), QString());
        return text;
    }
    case FullTextRole:
        return SAContentEditing::displayString(field, cell, m_nullString, m_binaryAsHex, -1);
    case Qt::EditRole:
        if (cell.isNull) return m_nullString;
        if (field.typeGroup == QLatin1String("bit")) return SAContentEditing::displayString(field, cell, m_nullString, false, -1);
        if (field.isBinary()) return SAContentEditing::displayString(field, cell, m_nullString, m_binaryAsHex, -1);
        return QString::fromUtf8(cell.data);
    case Qt::ToolTipRole: {
        if (cell.isNull) return tr("NULL");
        const QString full = SAContentEditing::displayString(field, cell, m_nullString, m_binaryAsHex, 2000);
        return full.size() > 120 || full.contains(QLatin1Char('\n')) ? full : QVariant();
    }
    case Qt::ForegroundRole:
        if (cell.isNull) return QBrush(QApplication::palette().color(QPalette::Disabled, QPalette::Text));
        return QVariant();
    case Qt::BackgroundRole:
        if (m_editedRows.contains(index.row())) {
            QColor c = QApplication::palette().color(QPalette::Highlight);
            c.setAlpha(40);
            return QBrush(c);
        }
        return QVariant();
    case Qt::TextAlignmentRole:
        if (field.isNumeric() && !cell.isNull) return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    case IsNullRole:
        return cell.isNull;
    case RawBytesRole:
        return cell.data;
    case TypeGroupRole:
        return field.typeGroup;
    case FieldNameRole:
        return field.name;
    case IsBinaryRole:
        return field.isBinary();
    default:
        return QVariant();
    }
}

bool SAResultModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!m_editable || role != Qt::EditRole || !index.isValid()) return false;
    const QString text = value.toString();
    SACell cell = text == m_nullString ? SACell::null() : SACell::ofString(text);
    setCell(index.row(), index.column(), cell, false);
    return true;
}

QVariant SAResultModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Vertical) {
        if (role == Qt::DisplayRole) return section + 1;
        return QVariant();
    }
    if (section < 0 || section >= m_result.fields.size()) return QVariant();
    const SAField &f = m_result.fields.at(section);
    switch (role) {
    case Qt::DisplayRole:
        return f.name;
    case Qt::UserRole: {   // type line for the two-line header
        QString type = f.typeName;
        if (f.typeGroup == QLatin1String("string") || f.typeGroup == QLatin1String("binary") || f.typeGroup == QLatin1String("bit"))
            type += QStringLiteral("(%1)").arg(f.displayLength());
        else if (f.typeGroup == QLatin1String("float") && f.decimals)
            type += QStringLiteral("(%1,%2)").arg(f.displayLength() - (f.decimals ? 2 : 0)).arg(f.decimals);
        if (f.isPrimaryKey()) type += QStringLiteral(" 🔑");
        return type;
    }
    case Qt::ToolTipRole: {
        QStringList parts{f.typeName, f.typeGroup};
        if (f.isPrimaryKey()) parts << tr("primary key");
        if (f.isAutoIncrement()) parts << tr("auto increment");
        if (f.isNotNull()) parts << tr("NOT NULL");
        if (f.isUnsigned()) parts << tr("unsigned");
        if (!f.charset.isEmpty()) parts << f.charset;
        if (!f.orgTable.isEmpty()) parts << QStringLiteral("%1.%2").arg(f.orgTable, f.orgName);
        return parts.join(QStringLiteral(" · "));
    }
    case Qt::UserRole + 1:
        return m_sortColumn == section ? int(m_sortOrder) : -1;
    default:
        return QVariant();
    }
}

Qt::ItemFlags SAResultModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (m_editable && index.isValid()) f |= Qt::ItemIsEditable;
    return f;
}
