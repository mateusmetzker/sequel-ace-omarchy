//
//  SAFavoritesModel.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAFavoritesModel.h"
#include "SAIcons.h"

#include <QDataStream>
#include <QIODevice>
#include <QMimeData>

namespace {
const QString MimeType = QStringLiteral("application/x-sequel-ace-favorite");
}

SAFavoritesModel::SAFavoritesModel(SAFavoritesStore *store, QObject *parent)
    : QAbstractItemModel(parent), m_store(store)
{
    connect(store, &SAFavoritesStore::changed, this, &SAFavoritesModel::reload);
}

void SAFavoritesModel::reload()
{
    beginResetModel();
    endResetModel();
    Q_EMIT structureChanged();
}

SAFavoriteNode *SAFavoritesModel::parentNode(const QModelIndex &parent) const
{
    return parent.isValid() ? static_cast<SAFavoriteNode *>(parent.internalPointer()) : m_store->root();
}

SAFavoriteNode *SAFavoritesModel::nodeAt(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<SAFavoriteNode *>(index.internalPointer()) : nullptr;
}

QModelIndex SAFavoritesModel::indexFor(const SAFavoriteNode *node) const
{
    if (!node || node == m_store->root() || !node->parent()) return QModelIndex();
    return createIndex(node->indexInParent(), 0, const_cast<SAFavoriteNode *>(node));
}

QModelIndex SAFavoritesModel::indexForFavoriteId(int id) const
{
    return indexFor(m_store->findFavorite(id));
}

QModelIndex SAFavoritesModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0 || row < 0) return QModelIndex();
    SAFavoriteNode *p = parentNode(parent);
    SAFavoriteNode *child = p->child(row);
    return child ? createIndex(row, 0, child) : QModelIndex();
}

QModelIndex SAFavoritesModel::parent(const QModelIndex &child) const
{
    SAFavoriteNode *node = nodeAt(child);
    if (!node) return QModelIndex();
    return indexFor(node->parent());
}

int SAFavoritesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0) return 0;
    SAFavoriteNode *p = parentNode(parent);
    return p->isGroup() ? p->childCount() : 0;
}

int SAFavoritesModel::columnCount(const QModelIndex &) const { return 1; }

QVariant SAFavoritesModel::data(const QModelIndex &index, int role) const
{
    SAFavoriteNode *node = nodeAt(index);
    if (!node) return QVariant();
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return node->displayName();
    case Qt::DecorationRole:
        if (node->isGroup()) return SAIcons::icon(SAIcons::Glyph::Folder);
        return SAIcons::colorDot(SAFavoriteColors::color(node->info().colorIndex), 12);
    case Qt::ToolTipRole:
        return node->isGroup() ? QVariant() : QVariant(node->info().hostDescription());
    case IsGroupRole:
        return node->isGroup();
    case FavoriteIdRole:
        return node->isGroup() ? -1 : node->info().id;
    case HostDescriptionRole:
        return node->isGroup() ? QString() : node->info().hostDescription();
    default:
        return QVariant();
    }
}

bool SAFavoritesModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    SAFavoriteNode *node = nodeAt(index);
    if (!node || role != Qt::EditRole) return false;
    const QString name = value.toString().trimmed();
    if (name.isEmpty()) return false;
    if (node->isGroup()) node->setGroupName(name);
    else node->info().name = name;
    Q_EMIT dataChanged(index, index);
    save();
    return true;
}

Qt::ItemFlags SAFavoritesModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::ItemIsDropEnabled;
    SAFavoriteNode *node = nodeAt(index);
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
    if (node && node->isGroup()) f |= Qt::ItemIsDropEnabled;
    return f;
}

QStringList SAFavoritesModel::mimeTypes() const { return {MimeType}; }

QMimeData *SAFavoritesModel::mimeData(const QModelIndexList &indexes) const
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    for (const QModelIndex &index : indexes) {
        if (!index.isValid() || index.column() != 0) continue;
        stream << quintptr(index.internalPointer());
    }
    auto *mime = new QMimeData;
    mime->setData(MimeType, encoded);
    return mime;
}

bool SAFavoritesModel::canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(row)
    Q_UNUSED(column)
    if (action != Qt::MoveAction || !data->hasFormat(MimeType)) return false;
    SAFavoriteNode *target = parentNode(parent);
    if (!target->isGroup()) return false;
    // Refuse dropping a group into itself or a descendant.
    QByteArray encoded = data->data(MimeType);
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    while (!stream.atEnd()) {
        quintptr ptr;
        stream >> ptr;
        auto *node = reinterpret_cast<SAFavoriteNode *>(ptr);
        for (SAFavoriteNode *p = target; p; p = p->parent())
            if (p == node) return false;
    }
    return true;
}

bool SAFavoritesModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
    if (!canDropMimeData(data, action, row, column, parent)) return false;
    SAFavoriteNode *target = parentNode(parent);
    QByteArray encoded = data->data(MimeType);
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    QList<SAFavoriteNode *> nodes;
    while (!stream.atEnd()) {
        quintptr ptr;
        stream >> ptr;
        nodes << reinterpret_cast<SAFavoriteNode *>(ptr);
    }
    int insertAt = row < 0 ? target->childCount() : row;
    beginResetModel();
    for (SAFavoriteNode *node : nodes) {
        SAFavoriteNode *oldParent = node->parent();
        const int oldIndex = node->indexInParent();
        if (oldParent == target && oldIndex < insertAt) --insertAt;
        oldParent->takeChild(oldIndex);
        target->insertChild(insertAt++, node);
    }
    endResetModel();
    save();
    Q_EMIT structureChanged();
    return true;
}

QModelIndex SAFavoritesModel::addFavorite(const QModelIndex &parent, const SAConnectionInfo &info)
{
    SAFavoriteNode *p = parentNode(parent);
    if (!p->isGroup()) p = p->parent() ? p->parent() : m_store->root();
    const QModelIndex parentIndex = indexFor(p);
    beginInsertRows(parentIndex, p->childCount(), p->childCount());
    SAFavoriteNode *node = p->addChild();
    SAConnectionInfo copy = info;
    if (copy.id < 0) copy.id = m_store->nextFavoriteId();
    copy.password.clear();
    copy.sshPassword.clear();
    node->setInfo(copy);
    endInsertRows();
    save();
    return indexFor(node);
}

QModelIndex SAFavoritesModel::addGroup(const QModelIndex &parent, const QString &name)
{
    SAFavoriteNode *p = parentNode(parent);
    if (!p->isGroup()) p = p->parent() ? p->parent() : m_store->root();
    const QModelIndex parentIndex = indexFor(p);
    beginInsertRows(parentIndex, p->childCount(), p->childCount());
    SAFavoriteNode *node = p->addChild();
    node->setGroup(name);
    endInsertRows();
    save();
    return indexFor(node);
}

void SAFavoritesModel::removeNode(const QModelIndex &index)
{
    SAFavoriteNode *node = nodeAt(index);
    if (!node || !node->parent()) return;
    const QModelIndex parentIndex = indexFor(node->parent());
    const int row = node->indexInParent();
    beginRemoveRows(parentIndex, row, row);
    node->parent()->removeChild(row);
    endRemoveRows();
    save();
}

void SAFavoritesModel::nodeChanged(const SAFavoriteNode *node)
{
    const QModelIndex index = indexFor(node);
    if (index.isValid()) Q_EMIT dataChanged(index, index);
    save();
}

void SAFavoritesModel::save()
{
    QString error;
    if (!m_store->save(QString(), &error)) qWarning("Sequel Ace: could not save favorites: %s", qPrintable(error));
}
