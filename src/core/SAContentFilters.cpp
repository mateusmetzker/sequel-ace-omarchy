//
//  SAContentFilters.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAContentFilters.h"
#include "SAPlist.h"

#include <QFile>
#include <QHash>
#include <QVariantMap>

namespace SAContentFilters {

namespace {

QHash<QString, QVector<SAContentFilter>> &table()
{
    static QHash<QString, QVector<SAContentFilter>> filters;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        QFile file(QStringLiteral(":/resources/ContentFilters.plist"));
        if (file.open(QIODevice::ReadOnly)) {
            const QVariantMap root = SAPlist::read(file.readAll()).toMap();
            for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
                QVector<SAContentFilter> list;
                for (const QVariant &entry : it.value().toList()) {
                    const QVariantMap m = entry.toMap();
                    SAContentFilter f;
                    f.menuLabel = m.value(QStringLiteral("MenuLabel")).toString();
                    f.clause = m.value(QStringLiteral("Clause")).toString();
                    f.numberOfArguments = m.value(QStringLiteral("NumberOfArguments"), 1).toInt();
                    f.suppressLeadingFieldPlaceholder = m.value(QStringLiteral("SuppressLeadingFieldPlaceholder"), false).toBool();
                    f.conjunctionLabels = m.value(QStringLiteral("ConjunctionLabels")).toStringList();
                    f.tooltip = m.value(QStringLiteral("Tooltip")).toString();
                    list.append(f);
                }
                filters.insert(it.key(), list);
            }
        }
    }
    return filters;
}

} // namespace

QString filterTypeForTypeGroup(const QString &typeGroup)
{
    if (typeGroup == QLatin1String("integer") || typeGroup == QLatin1String("float") || typeGroup == QLatin1String("bit"))
        return QStringLiteral("number");
    if (typeGroup == QLatin1String("date")) return QStringLiteral("date");
    if (typeGroup == QLatin1String("geometry")) return QStringLiteral("spatial");
    return QStringLiteral("string");
}

const QVector<SAContentFilter> &filtersForType(const QString &filterType)
{
    static const QVector<SAContentFilter> empty;
    const auto &t = table();
    auto it = t.constFind(filterType);
    if (it == t.constEnd()) {
        it = t.constFind(QStringLiteral("string"));
        if (it == t.constEnd()) return empty;
    }
    return it.value();
}

QString buildClause(const SAContentFilter &filter, const QString &quotedField, const QStringList &arguments,
                    bool caseSensitive, const std::function<QString(const QString &)> &escape)
{
    QString clause = filter.clause;
    clause.replace(QLatin1String("$BINARY"), caseSensitive ? QStringLiteral("BINARY") : QString());
    clause.replace(QLatin1String("$CURRENT_FIELD"), quotedField);
    for (int i = 0; i < filter.numberOfArguments; ++i) {
        const QString raw = i < arguments.size() ? arguments.at(i) : QString();
        // Arguments placed inside quotes in the clause are escaped; bare
        // placeholders (IN (${})) are passed through as typed, like the macOS app.
        const int pos = clause.indexOf(QLatin1String("${}"));
        if (pos < 0) break;
        // Inside a quoted literal when an odd number of single quotes precede the placeholder.
        int quotes = 0;
        for (int k = 0; k < pos; ++k)
            if (clause.at(k) == QLatin1Char('\'')) ++quotes;
        const bool insideQuotes = (quotes % 2) == 1;
        clause.replace(pos, 3, insideQuotes ? escape(raw) : raw);
    }
    clause = clause.simplified();
    if (filter.suppressLeadingFieldPlaceholder) return clause;
    return quotedField + QLatin1Char(' ') + clause;
}

} // namespace SAContentFilters
