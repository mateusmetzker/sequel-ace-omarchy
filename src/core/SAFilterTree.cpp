//
//  SAFilterTree.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAFilterTree.h"
#include "SAContentFilters.h"

SAFilterNodePtr SAFilterNode::makeGroup(bool conjunction, bool rootGroup)
{
    auto node = std::make_shared<SAFilterNode>();
    node->isGroup = true;
    node->isConjunction = conjunction;
    node->isRootGroup = rootGroup;
    return node;
}

SAFilterNodePtr SAFilterNode::makeLeaf(const SAFilterExpr &expr)
{
    auto node = std::make_shared<SAFilterNode>();
    node->isGroup = false;
    node->expr = expr;
    return node;
}

namespace SAFilterTree {

namespace {

QString buildLeaf(const SAFilterExpr &e, bool caseSensitive, const QuoteFn &quoteIdentifier, const EscapeFn &escape)
{
    if (!e.enabled || e.column.isEmpty()) return QString();
    const QVector<SAContentFilter> &filters = SAContentFilters::filtersForType(e.filterType);
    if (e.operatorIndex < 0 || e.operatorIndex >= filters.size()) return QString();
    const SAContentFilter &filter = filters.at(e.operatorIndex);
    if (filter.numberOfArguments >= 1 && e.values.value(0).isEmpty() && filter.menuLabel != QLatin1String("is empty"))
        return QString();
    return SAContentFilters::buildClause(filter, quoteIdentifier(e.column), e.values, caseSensitive, escape);
}

QString buildNode(const SAFilterNodePtr &node, bool caseSensitive, const QuoteFn &quoteIdentifier, const EscapeFn &escape, bool isRoot)
{
    if (!node) return QString();
    if (!node->isGroup) return buildLeaf(node->expr, caseSensitive, quoteIdentifier, escape);
    QStringList parts;
    for (const SAFilterNodePtr &child : node->children) {
        const QString part = buildNode(child, caseSensitive, quoteIdentifier, escape, false);
        if (!part.isEmpty()) parts << part;
    }
    if (parts.isEmpty()) return QString();
    const QString joined = parts.join(node->isConjunction ? QStringLiteral(" AND ") : QStringLiteral(" OR "));
    if (isRoot) return joined;
    return QLatin1Char('(') + joined + QLatin1Char(')');
}

} // namespace

QString buildWhere(const SAFilterNodePtr &node, bool caseSensitive, const QuoteFn &quoteIdentifier, const EscapeFn &escape)
{
    return buildNode(node, caseSensitive, quoteIdentifier, escape, true);
}

QVariantMap toVariant(const SAFilterNodePtr &node)
{
    QVariantMap m;
    if (!node) return m;
    if (node->isGroup) {
        m.insert(QStringLiteral("filterClass"), QStringLiteral("groupNode"));
        m.insert(QStringLiteral("isConjunction"), node->isConjunction);
        if (node->isRootGroup) m.insert(QStringLiteral("rootGroup"), true);
        QVariantList children;
        for (const SAFilterNodePtr &child : node->children) children << toVariant(child);
        m.insert(QStringLiteral("children"), children);
    } else {
        m.insert(QStringLiteral("filterClass"), QStringLiteral("expressionNode"));
        m.insert(QStringLiteral("column"), node->expr.column);
        m.insert(QStringLiteral("filterType"), node->expr.filterType);
        m.insert(QStringLiteral("enabled"), node->expr.enabled);
        const QVector<SAContentFilter> &filters = SAContentFilters::filtersForType(node->expr.filterType);
        if (node->expr.operatorIndex >= 0 && node->expr.operatorIndex < filters.size())
            m.insert(QStringLiteral("filterComparison"), filters.at(node->expr.operatorIndex).menuLabel);
        m.insert(QStringLiteral("filterValues"), QVariant(node->expr.values));
    }
    return m;
}

SAFilterNodePtr fromVariant(const QVariantMap &map)
{
    if (map.isEmpty()) return nullptr;
    const QString cls = map.value(QStringLiteral("filterClass")).toString();
    if (cls == QLatin1String("groupNode")) {
        auto node = SAFilterNode::makeGroup(map.value(QStringLiteral("isConjunction"), true).toBool(),
                                            map.value(QStringLiteral("rootGroup"), false).toBool());
        for (const QVariant &childVariant : map.value(QStringLiteral("children")).toList()) {
            SAFilterNodePtr child = fromVariant(childVariant.toMap());
            if (child) node->children << child;
        }
        return node;
    }
    SAFilterExpr expr;
    expr.column = map.value(QStringLiteral("column")).toString();
    expr.filterType = map.value(QStringLiteral("filterType")).toString();
    expr.enabled = map.value(QStringLiteral("enabled"), true).toBool();
    expr.values = map.value(QStringLiteral("filterValues")).toStringList();
    const QString comparison = map.value(QStringLiteral("filterComparison")).toString();
    const QVector<SAContentFilter> &filters = SAContentFilters::filtersForType(expr.filterType);
    expr.operatorIndex = 0;
    for (int i = 0; i < filters.size(); ++i) {
        if (filters.at(i).menuLabel == comparison) { expr.operatorIndex = i; break; }
    }
    return SAFilterNode::makeLeaf(expr);
}

} // namespace SAFilterTree
