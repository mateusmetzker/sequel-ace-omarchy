//
//  SAFilterTree.h
//  Sequel Ace (Linux port)
//
//  A nested AND/OR tree of filter rules for the table content view, mirroring
//  the macOS app's SARuleFilterRootConjunction: leaves reuse SAContentFilters
//  to turn a (column, operator, values) triple into a SQL fragment, groups
//  join their children with " AND "/" OR " and parenthesize themselves unless
//  they are the tree's root. Serialized with the same plist keys as the
//  macOS ContentFilters group format so a saved filter round-trips between
//  platforms.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <memory>

struct SAFilterExpr {
    QString column;
    QString filterType;     // "number", "string", "date" or "spatial" (see SAContentFilters::filterTypeForTypeGroup)
    int operatorIndex = 0;  // index into SAContentFilters::filtersForType(filterType)
    QStringList values;
    bool enabled = true;
};

struct SAFilterNode;
using SAFilterNodePtr = std::shared_ptr<SAFilterNode>;

struct SAFilterNode {
    bool isGroup = false;
    bool isConjunction = true;  // group only: true = AND, false = OR
    bool isRootGroup = false;   // group only: marks the tree's top-level group
    SAFilterExpr expr;          // leaf only
    QVector<SAFilterNodePtr> children; // group only

    static SAFilterNodePtr makeGroup(bool conjunction = true, bool rootGroup = false);
    static SAFilterNodePtr makeLeaf(const SAFilterExpr &expr = SAFilterExpr());
};

namespace SAFilterTree {

using QuoteFn = std::function<QString(const QString &)>;
using EscapeFn = std::function<QString(const QString &)>;

// Builds the WHERE fragment for the whole tree, or an empty string when
// nothing enabled/complete remains. The root node is never wrapped in
// parentheses; nested groups always are.
QString buildWhere(const SAFilterNodePtr &node, bool caseSensitive, const QuoteFn &quoteIdentifier, const EscapeFn &escape);

// Serialization compatible with the macOS ContentFilters group plist format
// (filterClass, isConjunction, children, column, filterType, filterComparison,
// filterValues, enabled, rootGroup).
QVariantMap toVariant(const SAFilterNodePtr &node);
SAFilterNodePtr fromVariant(const QVariantMap &map);

} // namespace SAFilterTree
