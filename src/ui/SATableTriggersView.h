//
//  SATableTriggersView.h
//  Sequel Ace (Linux port)
//
//  Triggers of the selected table with add, edit (drop + create) and remove,
//  ported from SPTableTriggers.
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

class SATableTriggersView : public QWidget {
    Q_OBJECT
public:
    explicit SATableTriggersView(SADatabaseDocument *document, QWidget *parent = nullptr);
    void loadTable(const QString &name, SASchema::ObjectType type);
    void reload();
    void clear();

private:
    void addTrigger();
    void editTrigger();
    void removeTrigger();

    SADatabaseDocument *m_document;
    QTableWidget *m_table;
    QToolButton *m_add;
    QToolButton *m_remove;
    QLabel *m_message;
    QString m_tableName;
    SASchema::ObjectType m_type = SASchema::ObjectType::None;
    QVector<SASchema::Trigger> m_triggers;
};
