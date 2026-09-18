//
//  SATablesList.h
//  Sequel Ace (Linux port)
//
//  The tables/views/procedures/functions list with filter, action buttons and
//  the table info panel shown beneath it.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SASchemaQueries.h"

#include <QWidget>

class SADatabaseDocument;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QLabel;
class QScrollArea;
class QToolButton;

class SATablesList : public QWidget {
    Q_OBJECT
public:
    explicit SATablesList(SADatabaseDocument *document, QWidget *parent = nullptr);

    void setEntries(const QVector<SASchema::ObjectEntry> &entries);
    void clear();
    void selectName(const QString &name);
    QString selectedName() const;
    SASchema::ObjectType selectedType() const;
    QVector<SASchema::ObjectEntry> selectedEntries() const;
    void focusFilter();
    void setInfoHtml(const QString &html);

Q_SIGNALS:
    void selectionChanged();
    void addRequested();
    void removeRequested();
    void refreshRequested();
    void renameRequested();
    void duplicateRequested();
    void truncateRequested();
    void copyCreateRequested();
    void showCreateRequested();
    void exportRequested();
    void maintenanceRequested(const QString &command);

private:
    void rebuild();
    void showContextMenu(const QPoint &pos);
    QListWidgetItem *headerItem(const QString &text);

    SADatabaseDocument *m_document;
    QLineEdit *m_filter;
    QListWidget *m_list;
    QLabel *m_info;
    QScrollArea *m_infoScroll;
    QToolButton *m_removeButton;
    QVector<SASchema::ObjectEntry> m_entries;
    bool m_updating = false;
};
