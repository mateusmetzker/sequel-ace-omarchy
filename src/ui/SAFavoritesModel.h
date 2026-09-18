//
//  SAFavoritesModel.h
//  Sequel Ace (Linux port)
//
//  Item model over the favorites tree (groups and connections) with in-place
//  renaming and drag-and-drop reordering.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAFavoritesStore.h"

#include <QAbstractItemModel>

class SAFavoritesModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Roles { NodeRole = Qt::UserRole + 1, IsGroupRole, FavoriteIdRole, HostDescriptionRole };

    explicit SAFavoritesModel(SAFavoritesStore *store, QObject *parent = nullptr);

    SAFavoritesStore *store() const { return m_store; }
    SAFavoriteNode *nodeAt(const QModelIndex &index) const;
    QModelIndex indexFor(const SAFavoriteNode *node) const;
    QModelIndex indexForFavoriteId(int id) const;

    QModelIndex addFavorite(const QModelIndex &parent, const SAConnectionInfo &info);
    QModelIndex addGroup(const QModelIndex &parent, const QString &name);
    void removeNode(const QModelIndex &index);
    void nodeChanged(const SAFavoriteNode *node);
    void reload();

    // QAbstractItemModel
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;

Q_SIGNALS:
    void structureChanged();

private:
    SAFavoriteNode *parentNode(const QModelIndex &parent) const;
    void save();

    SAFavoritesStore *m_store;
};
