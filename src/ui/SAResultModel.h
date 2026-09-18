//
//  SAResultModel.h
//  Sequel Ace (Linux port)
//
//  Table model over an SAResult with an editing overlay used by the content
//  view (cell edits, blank rows for inserts, row snapshots for UPDATE
//  generation).
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAResult.h"

#include <QAbstractTableModel>
#include <QSet>

class SAResultModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Roles { IsNullRole = Qt::UserRole + 1, RawBytesRole, TypeGroupRole, FieldNameRole, IsBinaryRole, FullTextRole };

    explicit SAResultModel(QObject *parent = nullptr);

    void setResult(const SAResult &result);
    const SAResult &result() const { return m_result; }
    void clearRows();

    void setEditable(bool editable);
    bool isEditable() const { return m_editable; }
    void setNullString(const QString &value);
    QString nullString() const { return m_nullString; }
    void setBinaryAsHex(bool on);
    void setDisplayLimit(int characters) { m_displayLimit = characters; }

    // Editing overlay.
    SACell cellAt(int row, int column) const;
    void setCell(int row, int column, const SACell &cell, bool binary = false);
    int appendBlankRow(const SARow &values);
    void removeRowsAt(QList<int> rows);   // descending order applied internally
    void replaceRow(int row, const SARow &cells);
    SARow rowCells(int row) const;
    QSet<int> binaryEditedColumns(int row) const { return m_binaryColumns.value(row); }
    void setRowEdited(int row, bool edited);
    bool isRowEdited(int row) const { return m_editedRows.contains(row); }
    void clearEditMarks();

    void setSortIndicator(int column, Qt::SortOrder order);
    int sortColumn() const { return m_sortColumn; }
    Qt::SortOrder sortOrder() const { return m_sortOrder; }

    // Client-side sort of the already-loaded rows (for grids that hold the
    // full result set, e.g. the query view). Type-aware: numeric/date columns
    // compare by value, everything else by text; NULLs sort first ascending,
    // last descending, and updates the header sort indicator itself.
    void sort(int column, Qt::SortOrder order) override;

    // QAbstractTableModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    SAResult m_result;
    bool m_editable = false;
    QString m_nullString = QStringLiteral("NULL");
    bool m_binaryAsHex = false;
    int m_displayLimit = 512;
    QSet<int> m_editedRows;
    QHash<int, QSet<int>> m_binaryColumns;
    int m_sortColumn = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};
