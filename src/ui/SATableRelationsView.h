//
//  SATableRelationsView.h
//  Sequel Ace (Linux port)
//
//  Foreign keys of the selected table (information_schema based) with add and
//  remove, ported from SPTableRelations.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SASchemaQueries.h"

#include <QWidget>

class SADatabaseDocument;
class QTableWidget;
class QToolButton;
class QLabel;

class SATableRelationsView : public QWidget {
    Q_OBJECT
public:
    explicit SATableRelationsView(SADatabaseDocument *document, QWidget *parent = nullptr);
    void loadTable(const QString &name, SASchema::ObjectType type);
    void reload();
    void clear();

private:
    void addRelation();
    void removeRelation();

    SADatabaseDocument *m_document;
    QTableWidget *m_table;
    QToolButton *m_add;
    QToolButton *m_remove;
    QLabel *m_message;
    QString m_tableName;
    SASchema::ObjectType m_type = SASchema::ObjectType::None;
    QVector<SASchema::ForeignKey> m_keys;
    QVector<SASchema::Column> m_columns;
    bool m_isInnoDB = false;
};
