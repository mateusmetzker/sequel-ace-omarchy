//
//  SATableContentView.h
//  Sequel Ace (Linux port)
//
//  Browse and edit table rows: filter bar, paginated grid, add/duplicate/
//  delete rows, in-place editing committed as UPDATE/INSERT with the same
//  rules as SPTableContent.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAContentEditing.h"
#include "SAContentFilters.h"
#include "SAFilterTree.h"
#include "SAResult.h"
#include "SASchemaQueries.h"

#include <QWidget>

class SADatabaseDocument;
class SAResultModel;
class SAResultTableView;
class SAFilterGroupWidget;
class QComboBox;
class QLineEdit;
class QLabel;
class QToolButton;
class QSpinBox;
class QCheckBox;
class QStackedWidget;
class QScrollArea;

class SATableContentView : public QWidget {
    Q_OBJECT
public:
    explicit SATableContentView(SADatabaseDocument *document, QWidget *parent = nullptr);

    void loadTable(const QString &name, SASchema::ObjectType type);
    void reload();
    void clear();
    void focusFilter();

    bool hasPendingEdits() const { return m_editingRow >= 0; }
    // Saves the row being edited; returns false when the user chose to keep editing.
    bool commitPendingEdits();

    void copyWithColumnNames();
    void copyAsSQLInsert(bool skipAutoIncrement = false);
    void setSelectedCellsNull();

    SAResultModel *model() const { return m_model; }
    SAResultTableView *tableView() const { return m_table; }
    bool isLoading() const { return m_loading; }
    QString lastUsedQuery() const { return m_lastUsedQuery; }

    // For the export dialog. activeFilter() is the WHERE clause that was really
    // applied -- filterClause() must not be used there, because it reads the
    // live keyboard modifiers to decide case sensitivity.
    QString tableName() const { return m_table_name; }
    SASchema::ObjectType objectType() const { return m_type; }
    QString activeFilter() const { return m_activeFilter; }
    const QVector<SASchema::Column> &columns() const { return m_columns; }
    const QStringList &primaryKeys() const { return m_primaryKeys; }
    int sortColumnIndex() const { return m_sortColumn; }
    bool sortDescending() const { return m_sortDescending; }

public Q_SLOTS:
    void addRow();
    void duplicateRow();
    void deleteRows();

private:
    void buildUI();
    void loadColumns(std::function<void()> then);
    void loadRowCount(std::function<void()> then);
    void loadValues();
    QString filterClause(QString *problem) const;
    void applyFilter();
    void resetFilter();
    void headerClicked(int section);
    void updateCountText();
    void updatePagination();
    void goToPage(int page);

    void cellEdited(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    void currentRowChanged(const QModelIndex &current, const QModelIndex &previous);
    void beginEditingRow(int row);
    bool saveRow();
    void cancelRowEditing();
    void openFieldEditor(const QModelIndex &index);
    void filterByCell(const QModelIndex &index);
    SARow defaultRow() const;
    SAContentEditing::Escaper escaper() const;

    SADatabaseDocument *m_document;
    SAResultModel *m_model;
    SAResultTableView *m_table;

    // filter bar
    SAFilterGroupWidget *m_filterGroup;
    QScrollArea *m_filterScroll;
    QCheckBox *m_filterCustom;
    QLineEdit *m_filterWhere;
    QStackedWidget *m_filterStack;
    QToolButton *m_filterButton;

    // bottom bar
    QToolButton *m_addButton;
    QToolButton *m_duplicateButton;
    QToolButton *m_removeButton;
    QToolButton *m_reloadButton;
    QToolButton *m_prevPage;
    QToolButton *m_nextPage;
    QSpinBox *m_pageBox;
    QLabel *m_pageLabel;
    QLabel *m_countLabel;

    QString m_table_name;
    SASchema::ObjectType m_type = SASchema::ObjectType::None;
    QVector<SASchema::Column> m_columns;
    QStringList m_primaryKeys;
    QString m_activeFilter;      // WHERE clause currently applied ("" = none)
    int m_sortColumn = -1;
    bool m_sortDescending = false;
    int m_page = 1;
    qint64 m_totalRows = -1;     // -1 unknown
    bool m_totalIsExact = false;
    bool m_limited = false;
    bool m_loading = false;
    int m_editingRow = -1;
    bool m_editingNewRow = false;
    bool m_savingRow = false;
    bool m_internalChange = false;   // model mutations made by the view itself, not by the user
    SARow m_oldRow;
    SACell m_cellBeforeEdit;
    QString m_lastUsedQuery;
};
