//
//  SAFavoritesStore.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAFavoritesStore.h"
#include "SAPlist.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
const QString RootKey = QStringLiteral("Favorites Root");
const QString ChildrenKey = QStringLiteral("Children");
const QString GroupNameKey = QStringLiteral("Name");
const QString GroupExpandedKey = QStringLiteral("IsExpanded");
const QString ExportRootKey = QStringLiteral("SPConnectionFavorites");
}

// ---- SAFavoriteNode ----------------------------------------------------------

SAFavoriteNode *SAFavoriteNode::addChild(int index)
{
    auto *node = new SAFavoriteNode(this);
    insertChild(index, node);
    return node;
}

void SAFavoriteNode::insertChild(int index, SAFavoriteNode *node)
{
    node->m_parent = this;
    if (index < 0 || index > m_children.size()) m_children.append(node);
    else m_children.insert(index, node);
}

SAFavoriteNode *SAFavoriteNode::takeChild(int index)
{
    if (index < 0 || index >= m_children.size()) return nullptr;
    SAFavoriteNode *node = m_children.takeAt(index);
    node->m_parent = nullptr;
    return node;
}

void SAFavoriteNode::removeChild(int index)
{
    delete takeChild(index);
}

QVariantMap SAFavoriteNode::toDictionary() const
{
    if (!m_isGroup) return m_info.toFavoriteDictionary();
    QVariantMap dict;
    dict.insert(GroupNameKey, m_groupName);
    dict.insert(GroupExpandedKey, m_expanded);
    QVariantList children;
    for (const SAFavoriteNode *child : m_children) children.append(child->toDictionary());
    dict.insert(ChildrenKey, children);
    return dict;
}

SAFavoriteNode *SAFavoriteNode::fromDictionary(const QVariantMap &dict, SAFavoriteNode *parent)
{
    auto *node = new SAFavoriteNode(parent);
    if (dict.contains(ChildrenKey) || (dict.contains(GroupNameKey) && !dict.contains(QStringLiteral("name")))) {
        node->setGroup(dict.value(GroupNameKey).toString(), dict.value(GroupExpandedKey, true).toBool());
        for (const QVariant &child : dict.value(ChildrenKey).toList()) {
            if (child.typeId() != QMetaType::QVariantMap) continue;
            node->m_children.append(fromDictionary(child.toMap(), node));
        }
    } else {
        node->setInfo(SAConnectionInfo::fromFavoriteDictionary(dict));
    }
    return node;
}

QList<SAFavoriteNode *> SAFavoriteNode::allFavorites() const
{
    QList<SAFavoriteNode *> result;
    for (SAFavoriteNode *child : m_children) {
        if (child->isGroup()) result += child->allFavorites();
        else result.append(child);
    }
    return result;
}

// ---- SAFavoritesStore --------------------------------------------------------

SAFavoritesStore::SAFavoritesStore(QObject *parent)
    : QObject(parent), m_root(new SAFavoriteNode)
{
    m_root->setGroup(QStringLiteral("Favorites"));
}

SAFavoritesStore::~SAFavoritesStore()
{
    delete m_root;
}

QString SAFavoritesStore::defaultFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/Favorites.plist");
}

bool SAFavoritesStore::load(const QString &path, QString *error)
{
    m_path = path.isEmpty() ? defaultFilePath() : path;
    if (!QFileInfo::exists(m_path)) {
        // First run: start with an empty root and create the file on first save.
        delete m_root;
        m_root = new SAFavoriteNode;
        m_root->setGroup(QStringLiteral("Favorites"));
        Q_EMIT changed();
        return true;
    }
    QString readError;
    const QVariant plist = SAPlist::readFile(m_path, &readError);
    if (!readError.isEmpty()) {
        if (error) *error = readError;
        return false;
    }
    const QVariantMap top = plist.toMap();
    QVariantMap rootDict = top.value(RootKey).toMap();
    if (rootDict.isEmpty() && top.contains(ChildrenKey)) rootDict = top;  // tolerate a bare root
    delete m_root;
    m_root = SAFavoriteNode::fromDictionary(rootDict, nullptr);
    if (!m_root->isGroup()) {
        // A file whose root is not a group is malformed; keep the data reachable.
        auto *wrapper = new SAFavoriteNode;
        wrapper->setGroup(QStringLiteral("Favorites"));
        wrapper->insertChild(-1, m_root);
        m_root = wrapper;
    }
    if (m_root->groupName().isEmpty()) m_root->setGroupName(QStringLiteral("Favorites"));
    Q_EMIT changed();
    return true;
}

bool SAFavoritesStore::save(const QString &path, QString *error)
{
    const QString target = path.isEmpty() ? (m_path.isEmpty() ? defaultFilePath() : m_path) : path;
    QDir().mkpath(QFileInfo(target).absolutePath());
    QVariantMap top;
    top.insert(RootKey, m_root->toDictionary());
    if (!SAPlist::writeFile(target, top, error)) return false;
    m_path = target;
    return true;
}

int SAFavoritesStore::nextFavoriteId() const
{
    // The macOS app derives ids from the current time; do the same so ids stay
    // unique when files are merged across machines.
    int candidate = static_cast<int>(QDateTime::currentSecsSinceEpoch() % 2000000000LL);
    QSet<int> used;
    for (const SAFavoriteNode *node : m_root->allFavorites()) used.insert(node->info().id);
    while (used.contains(candidate)) ++candidate;
    return candidate;
}

SAFavoriteNode *SAFavoritesStore::findFavorite(int id) const
{
    for (SAFavoriteNode *node : m_root->allFavorites())
        if (node->info().id == id) return node;
    return nullptr;
}

QList<SAFavoriteNode *> SAFavoritesStore::readExportFile(const QString &path, QString *error)
{
    QList<SAFavoriteNode *> nodes;
    QString readError;
    const QVariant plist = SAPlist::readFile(path, &readError);
    if (!readError.isEmpty()) {
        if (error) *error = readError;
        return nodes;
    }
    const QVariantMap top = plist.toMap();
    QVariantList items = top.value(ExportRootKey).toList();
    if (items.isEmpty()) items = top.value(QStringLiteral("favorites")).toList();   // legacy Sequel Pro preferences export
    if (items.isEmpty() && top.contains(RootKey)) items = top.value(RootKey).toMap().value(ChildrenKey).toList();
    if (items.isEmpty()) {
        if (error) *error = QStringLiteral("The file does not contain any Sequel Ace favorites.");
        return nodes;
    }
    for (const QVariant &item : items) {
        if (item.typeId() != QMetaType::QVariantMap) continue;
        nodes.append(SAFavoriteNode::fromDictionary(item.toMap(), nullptr));
    }
    return nodes;
}

bool SAFavoritesStore::writeExportFile(const QString &path, const QList<SAFavoriteNode *> &nodes, QString *error)
{
    QVariantList items;
    for (const SAFavoriteNode *node : nodes) items.append(node->toDictionary());
    QVariantMap top;
    top.insert(ExportRootKey, items);
    return SAPlist::writeFile(path, top, error);
}
