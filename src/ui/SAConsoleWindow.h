//
//  SAConsoleWindow.h
//  Sequel Ace (Linux port)
//
//  Shared console listing every statement the application sent, with
//  timestamps, connection and database, and filters like SPQueryController.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QWidget>

class QTableView;
class QStandardItemModel;
class QSortFilterProxyModel;
class QLineEdit;
class QCheckBox;

class SAConsoleWindow : public QWidget {
    Q_OBJECT
public:
    static SAConsoleWindow *shared();

    void addMessage(const QString &connection, const QString &database, const QString &message, bool isError, double seconds);
    void clear();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    explicit SAConsoleWindow(QWidget *parent = nullptr);
    void applyFilters();
    void saveAs();
    void copySelection();

    QStandardItemModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTableView *m_table;
    QLineEdit *m_filter;
    QCheckBox *m_showSelects;
    QCheckBox *m_showTimestamps;
    QCheckBox *m_showConnections;
    QCheckBox *m_showDatabases;
};
