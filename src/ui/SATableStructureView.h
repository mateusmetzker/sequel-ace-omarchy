//
//  SATableStructureView.h
//  Sequel Ace (Linux port)
//
//  Column and index editor. Edits to a column row are committed as
//  ALTER TABLE ... CHANGE/ADD using SASchema::columnDefinition (ported from
//  SPTableStructure); indexes are added and dropped through dialogs.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SASchemaQueries.h"

#include <QStyledItemDelegate>
#include <QWidget>

class SADatabaseDocument;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;
class QLabel;

class SAComboDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit SAComboDelegate(std::function<QStringList(const QModelIndex &)> items, bool editable, QObject *parent = nullptr);
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
private:
    std::function<QStringList(const QModelIndex &)> m_items;
    bool m_editable;
};

class SATableStructureView : public QWidget {
    Q_OBJECT
public:
    explicit SATableStructureView(SADatabaseDocument *document, QWidget *parent = nullptr);

    void loadTable(const QString &name, SASchema::ObjectType type);
    void reload();
    void clear();
    const QVector<SASchema::Column> &columns() const { return m_columns; }
    QTableWidget *fieldsTable() const { return m_fields; }
    bool commitPendingEdit();

public Q_SLOTS:
    void addField();

private:
    enum Col { ColName, ColType, ColLength, ColUnsigned, ColZerofill, ColBinary, ColNull, ColKey, ColDefault, ColExtra, ColEncoding, ColCollation, ColComment, ColCount };

    void buildUI();
    void populateColumns();
    void populateIndexes();
    void itemChanged(QTableWidgetItem *item);
    void currentCellChanged(int currentRow, int currentColumn, int previousRow, int previousColumn);
    SASchema::Column columnFromRow(int row) const;
    void setRowFromColumn(int row, const SASchema::Column &column);
    void cancelEdit();
    void removeField();
    void addIndex();
    void removeIndex();
    void afterStructureChange();

    SADatabaseDocument *m_document;
    QTableWidget *m_fields;
    QTableWidget *m_indexes;
    QToolButton *m_addField;
    QToolButton *m_removeField;
    QToolButton *m_addIndex;
    QToolButton *m_removeIndex;
    QLabel *m_message;

    QString m_table;
    SASchema::ObjectType m_type = SASchema::ObjectType::None;
    QVector<SASchema::Column> m_columns;
    QVector<SASchema::Index> m_indexList;
    bool m_populating = false;
    int m_editingRow = -1;
    bool m_editingNewRow = false;
    bool m_saving = false;
    int m_generation = 0;
    QString m_editingOriginalName;
};
