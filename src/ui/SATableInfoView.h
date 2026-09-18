//
//  SATableInfoView.h
//  Sequel Ace (Linux port)
//
//  Table Info: status values (with editable engine, encoding, collation,
//  comment and auto increment) and the CREATE syntax, ported from
//  SPExtendedTableInfo.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SASchemaQueries.h"

#include <QWidget>

class SADatabaseDocument;
class SASQLEditor;
class QComboBox;
class QLineEdit;
class QLabel;
class QFormLayout;
class QGroupBox;

class SATableInfoView : public QWidget {
    Q_OBJECT
public:
    explicit SATableInfoView(SADatabaseDocument *document, QWidget *parent = nullptr);
    void loadTable(const QString &name, SASchema::ObjectType type);
    void reload();
    void clear();

private:
    void applyOption(const QString &option, const QString &value, bool quote);
    void populateStatus(const QMap<QString, QString> &status);

    SADatabaseDocument *m_document;
    QGroupBox *m_optionsGroup;
    QComboBox *m_engine;
    QComboBox *m_encoding;
    QComboBox *m_collation;
    QLineEdit *m_comment;
    QLineEdit *m_autoIncrement;
    QLabel *m_status;
    SASQLEditor *m_createSyntax;
    QString m_tableName;
    SASchema::ObjectType m_type = SASchema::ObjectType::None;
    bool m_populating = false;
    QMap<QString, QString> m_statusValues;
};
