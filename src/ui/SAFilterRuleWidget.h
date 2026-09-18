//
//  SAFilterRuleWidget.h
//  Sequel Ace (Linux port)
//
//  Recursive editor for an SAFilterNode tree: SAFilterLeafWidget renders one
//  (column, operator, value) rule; SAFilterGroupWidget renders an AND/OR
//  group containing leaves and/or nested groups. Used by SATableContentView
//  in place of the macOS app's NSRuleEditor.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAFilterTree.h"
#include "SASchemaQueries.h"

#include <QWidget>
#include <QFrame>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QToolButton;
class QVBoxLayout;

class SAFilterLeafWidget : public QWidget {
    Q_OBJECT
public:
    explicit SAFilterLeafWidget(const QVector<SASchema::Column> &columns, QWidget *parent = nullptr);

    void setColumns(const QVector<SASchema::Column> &columns);
    void setExpr(const SAFilterExpr &expr);
    SAFilterExpr expr() const;

Q_SIGNALS:
    void changed();
    void removeRequested();

private:
    void rebuildOperators(const QString &previousLabel = QString());
    void operatorChanged();

    QVector<SASchema::Column> m_columns;
    QCheckBox *m_enabled;
    QComboBox *m_column;
    QComboBox *m_operator;
    QLineEdit *m_value;
    QLabel *m_conjunction;
    QLineEdit *m_value2;
    QToolButton *m_remove;
};

class SAFilterGroupWidget : public QFrame {
    Q_OBJECT
public:
    explicit SAFilterGroupWidget(const QVector<SASchema::Column> &columns, bool isRoot, QWidget *parent = nullptr);

    void setColumns(const QVector<SASchema::Column> &columns);

    // Replaces all rows with widgets built from `node`'s children (or a single
    // blank leaf when the group is empty), and sets the AND/OR combo.
    void setNode(const SAFilterNodePtr &node);

    // Builds a fresh tree from the current widget state.
    SAFilterNodePtr node() const;

Q_SIGNALS:
    void changed();
    void removeRequested();

private:
    void addLeafRow(const SAFilterExpr &expr = SAFilterExpr());
    void addGroupRow(const SAFilterNodePtr &childNode = nullptr);
    void clearRows();

    QVector<SASchema::Column> m_columns;
    bool m_isRoot;
    QComboBox *m_conjunctionCombo;
    QVBoxLayout *m_childrenLayout;
    QToolButton *m_removeGroupButton;
    QVector<QWidget *> m_rows; // SAFilterLeafWidget* or SAFilterGroupWidget*
};
