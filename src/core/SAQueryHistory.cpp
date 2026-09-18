//
//  SAQueryHistory.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAQueryHistory.h"
#include "SAPreferences.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

SAQueryHistory &SAQueryHistory::instance()
{
    static SAQueryHistory history;
    return history;
}

SAQueryHistory::SAQueryHistory()
{
    load();
}

QString SAQueryHistory::filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/QueryHistory.json");
}

void SAQueryHistory::load()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    m_items.clear();
    for (const QJsonValue &value : array) {
        const QString item = value.toString();
        if (!item.isEmpty()) m_items << item;
    }
}

void SAQueryHistory::save() const
{
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    QJsonArray array;
    for (const QString &item : m_items) array.append(item);
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    file.commit();
}

void SAQueryHistory::add(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) return;
    m_items.removeAll(trimmed);
    m_items.prepend(trimmed);
    const int max = qMax(1, SAPreferences::instance().intFor(SAPreferences::CustomQueryMaxHistoryItems));
    while (m_items.size() > max) m_items.removeLast();
    save();
    Q_EMIT changed();
}

void SAQueryHistory::remove(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    m_items.removeAt(index);
    save();
    Q_EMIT changed();
}

void SAQueryHistory::clear()
{
    m_items.clear();
    save();
    Q_EMIT changed();
}
