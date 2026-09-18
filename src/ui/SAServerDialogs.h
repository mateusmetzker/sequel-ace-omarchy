//
//  SAServerDialogs.h
//  Sequel Ace (Linux port)
//
//  Server variables and process list windows.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QDialog>

class SADatabaseDocument;
class QTableWidget;
class QLineEdit;
class QCheckBox;
class QTimer;
class QLabel;

class SAServerVariablesDialog : public QDialog {
    Q_OBJECT
public:
    explicit SAServerVariablesDialog(SADatabaseDocument *document, QWidget *parent = nullptr);
private:
    void reload();
    void filter();
    SADatabaseDocument *m_document;
    QTableWidget *m_table;
    QLineEdit *m_filter;
    QVector<QPair<QString, QString>> m_all;
};

class SAProcessListDialog : public QDialog {
    Q_OBJECT
public:
    explicit SAProcessListDialog(SADatabaseDocument *document, QWidget *parent = nullptr);
private:
    void reload();
    void kill(bool connection);
    SADatabaseDocument *m_document;
    QTableWidget *m_table;
    QCheckBox *m_autoRefresh;
    QCheckBox *m_showFull;
    QTimer *m_timer;
    QLabel *m_status;
};
