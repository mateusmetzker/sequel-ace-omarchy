//
//  SAFilterRuleWidget.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAFilterRuleWidget.h"
#include "SAContentFilters.h"
#include "SAIcons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

// ---- SAFilterLeafWidget ---------------------------------------------------------------

SAFilterLeafWidget::SAFilterLeafWidget(const QVector<SASchema::Column> &columns, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_enabled = new QCheckBox;
    m_enabled->setChecked(true);
    m_enabled->setToolTip(tr("Enable/disable this rule"));
    connect(m_enabled, &QCheckBox::toggled, this, &SAFilterLeafWidget::changed);
    m_column = new QComboBox;
    m_column->setMinimumWidth(130);
    connect(m_column, &QComboBox::currentIndexChanged, this, [this](int) { rebuildOperators(); emit changed(); });
    m_operator = new QComboBox;
    m_operator->setMinimumWidth(110);
    connect(m_operator, &QComboBox::currentIndexChanged, this, [this](int) { operatorChanged(); emit changed(); });
    m_value = new QLineEdit;
    m_value->setPlaceholderText(tr("Value"));
    connect(m_value, &QLineEdit::textChanged, this, &SAFilterLeafWidget::changed);
    m_conjunction = new QLabel(tr("AND"));
    m_conjunction->setVisible(false);
    m_value2 = new QLineEdit;
    m_value2->setPlaceholderText(tr("Value"));
    m_value2->setVisible(false);
    connect(m_value2, &QLineEdit::textChanged, this, &SAFilterLeafWidget::changed);
    m_remove = new QToolButton;
    m_remove->setIcon(SAIcons::icon(SAIcons::Glyph::Remove));
    m_remove->setAutoRaise(true);
    m_remove->setToolTip(tr("Remove this rule"));
    connect(m_remove, &QToolButton::clicked, this, &SAFilterLeafWidget::removeRequested);

    layout->addWidget(m_enabled);
    layout->addWidget(m_column);
    layout->addWidget(m_operator);
    layout->addWidget(m_value, 1);
    layout->addWidget(m_conjunction);
    layout->addWidget(m_value2, 1);
    layout->addWidget(m_remove);

    setColumns(columns);
}

void SAFilterLeafWidget::setColumns(const QVector<SASchema::Column> &columns)
{
    m_columns = columns;
    const QString previous = m_column->currentText();
    const QSignalBlocker blocker(m_column);
    m_column->clear();
    for (const SASchema::Column &c : m_columns) m_column->addItem(c.name, c.typeGroup);
    const int idx = m_column->findText(previous);
    m_column->setCurrentIndex(idx < 0 ? 0 : idx);
    rebuildOperators();
}

void SAFilterLeafWidget::rebuildOperators(const QString &previousLabel)
{
    const QString group = m_column->currentData().toString();
    const QString filterType = SAContentFilters::filterTypeForTypeGroup(group);
    const QString previous = previousLabel.isEmpty() ? m_operator->currentText() : previousLabel;
    const QSignalBlocker blocker(m_operator);
    m_operator->clear();
    const QVector<SAContentFilter> &filters = SAContentFilters::filtersForType(filterType);
    for (int i = 0; i < filters.size(); ++i) {
        m_operator->addItem(filters.at(i).menuLabel, filters.at(i).numberOfArguments);
        m_operator->setItemData(i, filterType, Qt::UserRole + 1);
        if (!filters.at(i).tooltip.isEmpty()) m_operator->setItemData(i, filters.at(i).tooltip, Qt::ToolTipRole);
    }
    int idx = m_operator->findText(previous);
    if (idx < 0) idx = filterType == QLatin1String("string") ? m_operator->findText(tr("contains")) : 0;
    m_operator->setCurrentIndex(qMax(0, idx));
    operatorChanged();
}

void SAFilterLeafWidget::operatorChanged()
{
    const int args = m_operator->currentData().toInt();
    m_value->setVisible(args >= 1);
    m_conjunction->setVisible(args >= 2);
    m_value2->setVisible(args >= 2);
}

void SAFilterLeafWidget::setExpr(const SAFilterExpr &e)
{
    {
        const QSignalBlocker blocker(m_column);
        const int idx = m_column->findText(e.column);
        if (idx >= 0) m_column->setCurrentIndex(idx);
    }
    const QVector<SAContentFilter> &filters = SAContentFilters::filtersForType(e.filterType);
    const QString label = (e.operatorIndex >= 0 && e.operatorIndex < filters.size()) ? filters.at(e.operatorIndex).menuLabel : QString();
    rebuildOperators(label);
    m_value->setText(e.values.value(0));
    m_value2->setText(e.values.value(1));
    m_enabled->setChecked(e.enabled);
}

SAFilterExpr SAFilterLeafWidget::expr() const
{
    SAFilterExpr e;
    e.column = m_column->currentText();
    e.filterType = SAContentFilters::filterTypeForTypeGroup(m_column->currentData().toString());
    e.operatorIndex = m_operator->currentIndex();
    e.enabled = m_enabled->isChecked();
    const int args = m_operator->currentData().toInt();
    if (args >= 1) e.values << m_value->text();
    if (args >= 2) e.values << m_value2->text();
    return e;
}

// ---- SAFilterGroupWidget --------------------------------------------------------------

SAFilterGroupWidget::SAFilterGroupWidget(const QVector<SASchema::Column> &columns, bool isRoot, QWidget *parent)
    : QFrame(parent), m_columns(columns), m_isRoot(isRoot)
{
    setFrameShape(isRoot ? QFrame::NoFrame : QFrame::StyledPanel);
    if (!isRoot) setStyleSheet(QStringLiteral("SAFilterGroupWidget { border: 1px solid palette(mid); border-radius: 4px; margin-top: 2px; margin-bottom: 2px; }"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(isRoot ? 0 : 6, isRoot ? 0 : 6, isRoot ? 0 : 6, isRoot ? 0 : 6);

    auto *header = new QHBoxLayout;
    m_conjunctionCombo = new QComboBox;
    m_conjunctionCombo->addItem(tr("AND"));
    m_conjunctionCombo->addItem(tr("OR"));
    connect(m_conjunctionCombo, &QComboBox::currentIndexChanged, this, &SAFilterGroupWidget::changed);
    header->addWidget(new QLabel(tr("Match")));
    header->addWidget(m_conjunctionCombo);
    header->addWidget(new QLabel(tr("of the following:")));
    header->addStretch();
    auto *addRule = new QToolButton;
    addRule->setIcon(SAIcons::icon(SAIcons::Glyph::Add));
    addRule->setText(tr("Rule"));
    addRule->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addRule->setAutoRaise(true);
    connect(addRule, &QToolButton::clicked, this, [this]() { addLeafRow(); emit changed(); });
    header->addWidget(addRule);
    auto *addGroup = new QToolButton;
    addGroup->setIcon(SAIcons::icon(SAIcons::Glyph::AddFolder));
    addGroup->setText(tr("Group"));
    addGroup->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addGroup->setAutoRaise(true);
    connect(addGroup, &QToolButton::clicked, this, [this]() { addGroupRow(); emit changed(); });
    header->addWidget(addGroup);
    m_removeGroupButton = new QToolButton;
    m_removeGroupButton->setIcon(SAIcons::icon(SAIcons::Glyph::Remove));
    m_removeGroupButton->setToolTip(tr("Remove this group"));
    m_removeGroupButton->setAutoRaise(true);
    m_removeGroupButton->setVisible(!isRoot);
    connect(m_removeGroupButton, &QToolButton::clicked, this, &SAFilterGroupWidget::removeRequested);
    header->addWidget(m_removeGroupButton);
    outer->addLayout(header);

    m_childrenLayout = new QVBoxLayout;
    m_childrenLayout->setContentsMargins(isRoot ? 0 : 12, 0, 0, 0);
    outer->addLayout(m_childrenLayout);
}

void SAFilterGroupWidget::setColumns(const QVector<SASchema::Column> &columns)
{
    m_columns = columns;
    for (QWidget *w : m_rows) {
        if (auto *leaf = qobject_cast<SAFilterLeafWidget *>(w)) leaf->setColumns(columns);
        else if (auto *group = qobject_cast<SAFilterGroupWidget *>(w)) group->setColumns(columns);
    }
}

void SAFilterGroupWidget::clearRows()
{
    for (QWidget *w : m_rows) { m_childrenLayout->removeWidget(w); w->deleteLater(); }
    m_rows.clear();
}

void SAFilterGroupWidget::addLeafRow(const SAFilterExpr &expr)
{
    auto *leaf = new SAFilterLeafWidget(m_columns, this);
    leaf->setExpr(expr);
    connect(leaf, &SAFilterLeafWidget::changed, this, &SAFilterGroupWidget::changed);
    connect(leaf, &SAFilterLeafWidget::removeRequested, this, [this, leaf]() {
        m_rows.removeOne(leaf);
        m_childrenLayout->removeWidget(leaf);
        leaf->deleteLater();
        emit changed();
    });
    m_childrenLayout->addWidget(leaf);
    m_rows << leaf;
}

void SAFilterGroupWidget::addGroupRow(const SAFilterNodePtr &childNode)
{
    auto *group = new SAFilterGroupWidget(m_columns, false, this);
    group->setNode(childNode ? childNode : SAFilterNode::makeGroup());
    connect(group, &SAFilterGroupWidget::changed, this, &SAFilterGroupWidget::changed);
    connect(group, &SAFilterGroupWidget::removeRequested, this, [this, group]() {
        m_rows.removeOne(group);
        m_childrenLayout->removeWidget(group);
        group->deleteLater();
        emit changed();
    });
    m_childrenLayout->addWidget(group);
    m_rows << group;
}

void SAFilterGroupWidget::setNode(const SAFilterNodePtr &node)
{
    clearRows();
    const QSignalBlocker blocker(m_conjunctionCombo);
    m_conjunctionCombo->setCurrentIndex((node && node->isGroup && !node->isConjunction) ? 1 : 0);
    if (!node || !node->isGroup || node->children.isEmpty()) {
        addLeafRow();
        return;
    }
    for (const SAFilterNodePtr &child : node->children) {
        if (child && child->isGroup) addGroupRow(child);
        else addLeafRow(child ? child->expr : SAFilterExpr());
    }
}

SAFilterNodePtr SAFilterGroupWidget::node() const
{
    SAFilterNodePtr group = SAFilterNode::makeGroup(m_conjunctionCombo->currentIndex() == 0, m_isRoot);
    for (QWidget *w : m_rows) {
        if (auto *leaf = qobject_cast<SAFilterLeafWidget *>(w)) group->children << SAFilterNode::makeLeaf(leaf->expr());
        else if (auto *nested = qobject_cast<SAFilterGroupWidget *>(w)) group->children << nested->node();
    }
    return group;
}
