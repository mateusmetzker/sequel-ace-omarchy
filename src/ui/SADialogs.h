//
//  SADialogs.h
//  Sequel Ace (Linux port)
//
//  Message helpers and the small modal dialogs used across the application
//  (add database/table, rename, index, foreign key, trigger, go-to-database,
//  SSH prompts, busy overlay).
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SASchemaQueries.h"

#include <QDialog>
#include <QStringList>
#include <QVector>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QCheckBox;
class QLabel;
class QDialogButtonBox;

namespace SADialogs {

void warning(QWidget *parent, const QString &title, const QString &text, const QString &detail = QString());
void information(QWidget *parent, const QString &title, const QString &text);
// Returns true when the user chose the primary (destructive/confirming) button.
bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &primaryButton,
             const QString &cancelButton = QString(), bool destructive = false, const QString &detail = QString());
// Three-way question used by multi-statement execution: 0 = stop, 1 = continue, 2 = run all.
int errorContinuation(QWidget *parent, const QString &title, const QString &text);
QString askText(QWidget *parent, const QString &title, const QString &label, const QString &initial, bool *ok);
QString askPassword(QWidget *parent, const QString &title, const QString &label, bool *ok);

struct CharsetCollation {
    QStringList charsets;                     // names
    QMap<QString, QStringList> collations;    // charset -> collations
    QMap<QString, QString> defaultCollation;  // charset -> default
};

} // namespace SADialogs

class SADatabaseDialog : public QDialog {
    Q_OBJECT
public:
    // Add or alter a database. When `existingName` is set the name is fixed.
    SADatabaseDialog(QWidget *parent, const SADialogs::CharsetCollation &charsets, const QString &existingName = QString(),
                     const QString &currentCharset = QString(), const QString &currentCollation = QString());
    QString databaseName() const;
    QString charset() const;
    QString collation() const;

private:
    void charsetChanged();
    QLineEdit *m_name;
    QComboBox *m_charset;
    QComboBox *m_collation;
    SADialogs::CharsetCollation m_data;
};

class SATableDialog : public QDialog {
    Q_OBJECT
public:
    SATableDialog(QWidget *parent, const QStringList &engines, const QString &defaultEngine,
                  const SADialogs::CharsetCollation &charsets, const QString &defaultCharset);
    QString tableName() const;
    QString engine() const;
    QString charset() const;
    QString collation() const;

private:
    void charsetChanged();
    QLineEdit *m_name;
    QComboBox *m_engine;
    QComboBox *m_charset;
    QComboBox *m_collation;
    SADialogs::CharsetCollation m_data;
};

class SAIndexDialog : public QDialog {
    Q_OBJECT
public:
    SAIndexDialog(QWidget *parent, const QVector<SASchema::Column> &columns, bool hasPrimaryKey, bool supportsFulltext, bool supportsSpatial);
    QString indexType() const;     // "INDEX", "UNIQUE", "PRIMARY KEY", "FULLTEXT", "SPATIAL"
    QString indexName() const;
    QStringList columns() const;
    QStringList subParts() const;
    QString storageType() const;

private:
    void typeChanged();
    QComboBox *m_type;
    QLineEdit *m_name;
    QListWidget *m_columns;
    QComboBox *m_storage;
    QVector<SASchema::Column> m_allColumns;
};

class SARelationDialog : public QDialog {
    Q_OBJECT
public:
    SARelationDialog(QWidget *parent, const QVector<SASchema::Column> &columns, const QStringList &referenceTables,
                     std::function<void(const QString &table, std::function<void(const QStringList &)>)> columnsLoader);
    QString constraintName() const;
    QStringList columns() const;
    QString referencedTable() const;
    QStringList referencedColumns() const;
    QString onDelete() const;
    QString onUpdate() const;

private:
    void referenceTableChanged();
    QLineEdit *m_name;
    QListWidget *m_columns;
    QComboBox *m_refTable;
    QListWidget *m_refColumns;
    QComboBox *m_onDelete;
    QComboBox *m_onUpdate;
    std::function<void(const QString &, std::function<void(const QStringList &)>)> m_columnsLoader;
};

class SATriggerDialog : public QDialog {
    Q_OBJECT
public:
    SATriggerDialog(QWidget *parent, const QString &table, const SASchema::Trigger *existing = nullptr);
    SASchema::Trigger trigger() const;

private:
    QString m_table;
    QLineEdit *m_name;
    QComboBox *m_timing;
    QComboBox *m_event;
    QPlainTextEdit *m_statement;
};

class SAGotoDatabaseDialog : public QDialog {
    Q_OBJECT
public:
    SAGotoDatabaseDialog(QWidget *parent, const QStringList &databases, const QString &current);
    QString selectedDatabase() const;

private:
    void filter(const QString &text);
    QLineEdit *m_filter;
    QListWidget *m_list;
    QStringList m_all;
};

// Modal progress shown while connecting / running long tasks, with Cancel.
class SABusyDialog : public QDialog {
    Q_OBJECT
public:
    explicit SABusyDialog(QWidget *parent, const QString &title, const QString &text, bool cancellable = true);
    void setText(const QString &text);
    bool wasCancelled() const { return m_cancelled; }
    // Closes the dialog when the task completed. Unlike close()/reject(), this
    // never counts as a user cancellation.
    void finish();

Q_SIGNALS:
    void cancelRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    QLabel *m_label;
    bool m_cancelled = false;
    bool m_finished = false;
};
