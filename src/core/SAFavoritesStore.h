//
//  SAFavoritesStore.h
//  Sequel Ace (Linux port)
//
//  Tree of connection favorites and groups, persisted as Favorites.plist in the
//  same XML format the macOS app writes to
//  ~/Library/Application Support/Sequel Ace/Data/Favorites.plist:
//
//    { "Favorites Root": { "Name": "Favorites", "IsExpanded": true,
//                          "Children": [ <group dict> | <favorite dict> ... ] } }
//
//  Groups carry "Name", "IsExpanded" and "Children"; favorites are the flat
//  dictionaries mapped by SAConnectionInfo. Export files use the root key
//  "SPConnectionFavorites" with the same children array.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAConnectionInfo.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

class SAFavoriteNode {
public:
    explicit SAFavoriteNode(SAFavoriteNode *parent = nullptr) : m_parent(parent) {}
    ~SAFavoriteNode() { qDeleteAll(m_children); }

    SAFavoriteNode(const SAFavoriteNode &) = delete;
    SAFavoriteNode &operator=(const SAFavoriteNode &) = delete;

    bool isGroup() const { return m_isGroup; }
    void setGroup(const QString &name, bool expanded = true) { m_isGroup = true; m_groupName = name; m_expanded = expanded; }
    QString groupName() const { return m_groupName; }
    void setGroupName(const QString &name) { m_groupName = name; }
    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded) { m_expanded = expanded; }

    const SAConnectionInfo &info() const { return m_info; }
    SAConnectionInfo &info() { return m_info; }
    void setInfo(const SAConnectionInfo &info) { m_info = info; m_isGroup = false; }

    QString displayName() const { return m_isGroup ? m_groupName : m_info.displayName(); }

    SAFavoriteNode *parent() const { return m_parent; }
    const QList<SAFavoriteNode *> &children() const { return m_children; }
    int childCount() const { return m_children.size(); }
    SAFavoriteNode *child(int index) const { return (index >= 0 && index < m_children.size()) ? m_children.at(index) : nullptr; }
    int indexInParent() const { return m_parent ? m_parent->m_children.indexOf(const_cast<SAFavoriteNode *>(this)) : 0; }

    SAFavoriteNode *addChild(int index = -1);
    SAFavoriteNode *takeChild(int index);
    void insertChild(int index, SAFavoriteNode *node);
    void removeChild(int index);

    QVariantMap toDictionary() const;
    static SAFavoriteNode *fromDictionary(const QVariantMap &dict, SAFavoriteNode *parent);

    // Depth-first list of favorite (non-group) nodes.
    QList<SAFavoriteNode *> allFavorites() const;

private:
    SAFavoriteNode *m_parent = nullptr;
    QList<SAFavoriteNode *> m_children;
    bool m_isGroup = false;
    QString m_groupName;
    bool m_expanded = true;
    SAConnectionInfo m_info;
};

class SAFavoritesStore : public QObject {
    Q_OBJECT
public:
    explicit SAFavoritesStore(QObject *parent = nullptr);
    ~SAFavoritesStore() override;

    static QString defaultFilePath();

    bool load(const QString &path = QString(), QString *error = nullptr);
    bool save(const QString &path = QString(), QString *error = nullptr);
    QString filePath() const { return m_path; }

    SAFavoriteNode *root() const { return m_root; }
    int nextFavoriteId() const;
    SAFavoriteNode *findFavorite(int id) const;

    // Import/export in the "SequelAceFavorites.plist" format.
    static QList<SAFavoriteNode *> readExportFile(const QString &path, QString *error);
    static bool writeExportFile(const QString &path, const QList<SAFavoriteNode *> &nodes, QString *error);

    void notifyChanged() { Q_EMIT changed(); }

Q_SIGNALS:
    void changed();

private:
    SAFavoriteNode *m_root = nullptr;
    QString m_path;
};
