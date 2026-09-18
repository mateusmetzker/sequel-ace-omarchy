//
//  SAQueryHistory.h
//  Sequel Ace (Linux port)
//
//  Global custom-query history, persisted as JSON in the application data
//  directory and capped by the CustomQueryMaxHistoryItems preference.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QObject>
#include <QStringList>

class SAQueryHistory : public QObject {
    Q_OBJECT
public:
    static SAQueryHistory &instance();

    QStringList items() const { return m_items; }
    void add(const QString &query);
    void remove(int index);
    void clear();
    void save() const;

Q_SIGNALS:
    void changed();

private:
    SAQueryHistory();
    void load();
    static QString filePath();

    QStringList m_items;
};
