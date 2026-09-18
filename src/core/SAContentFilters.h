//
//  SAContentFilters.h
//  Sequel Ace (Linux port)
//
//  Filter operators for the table content view, loaded from the macOS app's
//  ContentFilters.plist (bundled as a resource). Builds WHERE clauses the same
//  way SPTableContent does: "${}" placeholders receive the escaped argument,
//  "$CURRENT_FIELD" the quoted column and "$BINARY" the optional BINARY prefix.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct SAContentFilter {
    QString menuLabel;
    QString clause;
    int numberOfArguments = 1;
    bool suppressLeadingFieldPlaceholder = false;
    QStringList conjunctionLabels;
    QString tooltip;
};

namespace SAContentFilters {

// "number", "string", "date" or "spatial" for a column type group.
QString filterTypeForTypeGroup(const QString &typeGroup);

const QVector<SAContentFilter> &filtersForType(const QString &filterType);

// Builds "`field` <clause>" (or the clause alone when the filter suppresses the
// leading field). `escape` escapes a string without adding quotes.
QString buildClause(const SAContentFilter &filter, const QString &quotedField, const QStringList &arguments,
                    bool caseSensitive, const std::function<QString(const QString &)> &escape);

} // namespace SAContentFilters
