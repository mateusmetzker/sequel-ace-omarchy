//
//  SAUserManagerDialog.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAUserManagerDialog.h"
#include "SADatabaseDocument.h"
#include "SADialogs.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariant>

using namespace SAUserManager;

namespace {
constexpr int UserItemType = QTreeWidgetItem::UserType + 1;
constexpr int HostItemType = QTreeWidgetItem::UserType + 2;
}

SAUserManagerDialog::SAUserManagerDialog(SADatabaseDocument *document, QWidget *parent)
    : QDialog(parent), m_document(document)
{
    setWindowTitle(tr("Manage Users"));
    m_post576 = m_document->session()->serverInfo().serverVersionIsGreaterThanOrEqualTo(5, 7, 6)
                && !m_document->session()->serverInfo().isMariaDB;

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &SAUserManagerDialog::treeSelectionChanged);

    auto *treeButtons = new QHBoxLayout;
    auto *addUserButton = new QPushButton(tr("Add User"));
    auto *addHostButton = new QPushButton(tr("Add Host"));
    auto *removeButton = new QPushButton(tr("Remove"));
    connect(addUserButton, &QPushButton::clicked, this, &SAUserManagerDialog::addUser);
    connect(addHostButton, &QPushButton::clicked, this, &SAUserManagerDialog::addHost);
    connect(removeButton, &QPushButton::clicked, this, &SAUserManagerDialog::removeSelected);
    treeButtons->addWidget(addUserButton);
    treeButtons->addWidget(addHostButton);
    treeButtons->addWidget(removeButton);

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_tree, 1);
    leftLayout->addLayout(treeButtons);

    m_tabs = new QTabWidget;
    buildGeneralTab();
    buildGlobalPrivilegesTab();
    buildResourcesTab();
    buildSchemaPrivilegesTab();

    auto *splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 560});

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *refreshButton = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    auto *applyButton = buttons->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
    connect(refreshButton, &QPushButton::clicked, this, &SAUserManagerDialog::reload);
    connect(applyButton, &QPushButton::clicked, this, &SAUserManagerDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addWidget(buttons);
    resize(880, 620);

    reload();
}

void SAUserManagerDialog::buildGeneralTab()
{
    auto *page = new QWidget;
    auto *form = new QGridLayout(page);
    m_userNameEdit = new QLineEdit;
    m_hostEdit = new QLineEdit;
    m_passwordEdit = new QLineEdit;
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_confirmPasswordEdit = new QLineEdit;
    m_confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    form->addWidget(new QLabel(tr("User Name:")), 0, 0);
    form->addWidget(m_userNameEdit, 0, 1);
    form->addWidget(new QLabel(tr("Host:")), 1, 0);
    form->addWidget(m_hostEdit, 1, 1);
    form->addWidget(new QLabel(tr("Password:")), 2, 0);
    form->addWidget(m_passwordEdit, 2, 1);
    form->addWidget(new QLabel(tr("Confirm Password:")), 3, 0);
    form->addWidget(m_confirmPasswordEdit, 3, 1);
    form->setRowStretch(4, 1);
    connect(m_hostEdit, &QLineEdit::editingFinished, this, &SAUserManagerDialog::storeFormIntoSelectedHost);
    connect(m_passwordEdit, &QLineEdit::editingFinished, this, &SAUserManagerDialog::storeFormIntoSelectedHost);
    m_tabs->addTab(page, tr("General"));
}

void SAUserManagerDialog::buildGlobalPrivilegesTab()
{
    auto *page = new QWidget;
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget;
    auto *grid = new QGridLayout(inner);
    scroll->setWidget(inner);
    // Populated once the server's supported privileges are known, in reload().
    scroll->setProperty("grid", QVariant::fromValue(static_cast<void *>(grid)));

    auto *buttonRow = new QHBoxLayout;
    auto *checkAllButton = new QPushButton(tr("Check All"));
    auto *uncheckAllButton = new QPushButton(tr("Uncheck All"));
    connect(checkAllButton, &QPushButton::clicked, this, &SAUserManagerDialog::checkAll);
    connect(uncheckAllButton, &QPushButton::clicked, this, &SAUserManagerDialog::uncheckAll);
    buttonRow->addWidget(checkAllButton);
    buttonRow->addWidget(uncheckAllButton);
    buttonRow->addStretch();

    pageLayout->addLayout(buttonRow);
    pageLayout->addWidget(scroll, 1);
    m_tabs->addTab(page, tr("Global Privileges"));
}

void SAUserManagerDialog::buildResourcesTab()
{
    auto *page = new QWidget;
    auto *form = new QGridLayout(page);
    m_maxQueries = new QSpinBox; m_maxQueries->setRange(0, 1000000);
    m_maxUpdates = new QSpinBox; m_maxUpdates->setRange(0, 1000000);
    m_maxConnections = new QSpinBox; m_maxConnections->setRange(0, 1000000);
    m_maxUserConnections = new QSpinBox; m_maxUserConnections->setRange(0, 1000000);
    form->addWidget(new QLabel(tr("Max queries per hour (0 = unlimited):")), 0, 0);
    form->addWidget(m_maxQueries, 0, 1);
    form->addWidget(new QLabel(tr("Max updates per hour (0 = unlimited):")), 1, 0);
    form->addWidget(m_maxUpdates, 1, 1);
    form->addWidget(new QLabel(tr("Max connections per hour (0 = unlimited):")), 2, 0);
    form->addWidget(m_maxConnections, 2, 1);
    form->addWidget(new QLabel(tr("Max simultaneous connections (0 = unlimited):")), 3, 0);
    form->addWidget(m_maxUserConnections, 3, 1);
    form->setRowStretch(4, 1);
    for (QSpinBox *box : {m_maxQueries, m_maxUpdates, m_maxConnections, m_maxUserConnections})
        connect(box, &QSpinBox::editingFinished, this, &SAUserManagerDialog::storeFormIntoSelectedHost);
    m_tabs->addTab(page, tr("Resources"));
}

void SAUserManagerDialog::buildSchemaPrivilegesTab()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    m_schemaList = new QListWidget;
    m_schemaList->setMaximumWidth(200);
    connect(m_schemaList, &QListWidget::currentRowChanged, this, &SAUserManagerDialog::schemaSelectionChanged);

    m_availableSchemaPrivs = new QListWidget;
    m_availableSchemaPrivs->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grantedSchemaPrivs = new QListWidget;
    m_grantedSchemaPrivs->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto *moveButtons = new QVBoxLayout;
    auto *grantButton = new QPushButton(tr(">>"));
    auto *revokeButton = new QPushButton(tr("<<"));
    connect(grantButton, &QPushButton::clicked, this, [this]() { moveSchemaPrivilege(true); });
    connect(revokeButton, &QPushButton::clicked, this, [this]() { moveSchemaPrivilege(false); });
    moveButtons->addStretch();
    moveButtons->addWidget(grantButton);
    moveButtons->addWidget(revokeButton);
    moveButtons->addStretch();

    auto *availableBox = new QGroupBox(tr("Available Privileges"));
    auto *availableLayout = new QVBoxLayout(availableBox);
    availableLayout->addWidget(m_availableSchemaPrivs);
    auto *grantedBox = new QGroupBox(tr("Granted Privileges"));
    auto *grantedLayout = new QVBoxLayout(grantedBox);
    grantedLayout->addWidget(m_grantedSchemaPrivs);

    layout->addWidget(m_schemaList);
    layout->addWidget(availableBox, 1);
    layout->addLayout(moveButtons);
    layout->addWidget(grantedBox, 1);
    m_tabs->addTab(page, tr("Schema Privileges"));
}

void SAUserManagerDialog::reload()
{
    m_document->session()->query(showPrivileges(), [this](const SAResult &privResult) {
        if (privResult.ok) m_supportedGlobalPrivs = parseSupportedPrivileges(privResult);
        m_document->session()->query(showColumnsFromUserTable(), [this](const SAResult &colResult) {
            if (m_supportedGlobalPrivs.isEmpty()) m_supportedGlobalPrivs = parseSupportedPrivilegesFromColumns(colResult);
            m_document->session()->query(QStringLiteral("SHOW COLUMNS FROM mysql.db"), [this](const SAResult &dbColResult) {
                m_supportedSchemaPrivs = parseSupportedPrivilegesFromColumns(dbColResult);

                // Rebuild the global-privileges checkbox grid now that the supported set is known.
                auto *scroll = m_tabs->widget(1)->findChild<QScrollArea *>();
                auto *grid = static_cast<QGridLayout *>(scroll->property("grid").value<void *>());
                QLayoutItem *child;
                while ((child = grid->takeAt(0)) != nullptr) { delete child->widget(); delete child; }
                m_globalPrivCheckboxes.clear();
                const int columns = 3;
                for (int i = 0; i < m_supportedGlobalPrivs.size(); ++i) {
                    const QString key = m_supportedGlobalPrivs.at(i);
                    auto *box = new QCheckBox(grantNameForPrivilegeKey(key));
                    connect(box, &QCheckBox::toggled, this, &SAUserManagerDialog::storeFormIntoSelectedHost);
                    m_globalPrivCheckboxes.append({key, box});
                    grid->addWidget(box, i / columns, i % columns);
                }

                m_document->session()->query(QStringLiteral("SHOW DATABASES"), [this](const SAResult &dbResult) {
                    m_allDatabases.clear();
                    for (int i = 0; i < dbResult.rowCount(); ++i) m_allDatabases << dbResult.stringAt(i, 0);

                    m_document->session()->query(listUsers(), [this](const SAResult &userResult) {
                        if (!userResult.ok) { reportErrors({userResult.errorMessage}); return; }
                        const QVector<SAUserAccount> accounts = parseUsers(userResult, m_post576);

                        m_document->session()->query(listSchemaPrivileges(), [this, accounts](const SAResult &schemaResult) {
                            const QVector<SASchemaPrivilege> schemaPrivs = parseSchemaPrivileges(schemaResult);

                            m_users.clear();
                            for (const SAUserAccount &account : accounts) {
                                UserNode *node = nullptr;
                                for (UserNode &existing : m_users)
                                    if (existing.user == account.user) { node = &existing; break; }
                                if (!node) { m_users.append(UserNode{account.user, {}, false}); node = &m_users.last(); }
                                HostNode host;
                                host.account = account;
                                node->hosts.append(host);
                            }

                            m_schemaPrivsByDatabase.clear();
                            for (const SASchemaPrivilege &priv : schemaPrivs) {
                                const QString accountKey = priv.user + QLatin1Char('@') + priv.host + QLatin1Char('|') + priv.database;
                                m_schemaPrivsByDatabase[accountKey] = priv.privs;
                            }

                            buildTree();
                        });
                    });
                });
            });
        });
    }, SADatabaseSession::Silent);
}

void SAUserManagerDialog::buildTree()
{
    m_tree->clear();
    for (UserNode &user : m_users) {
        auto *userItem = new QTreeWidgetItem(m_tree, QStringList{user.user.isEmpty() ? tr("(anonymous)") : user.user}, UserItemType);
        userItem->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void *>(&user)));
        for (HostNode &host : user.hosts) {
            auto *hostItem = new QTreeWidgetItem(userItem, QStringList{host.account.host}, HostItemType);
            hostItem->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void *>(&host)));
        }
        userItem->setExpanded(true);
    }
    if (m_tree->topLevelItemCount() > 0) m_tree->setCurrentItem(m_tree->topLevelItem(0)->child(0));
}

SAUserManagerDialog::UserNode *SAUserManagerDialog::userNodeForItem(QTreeWidgetItem *item)
{
    if (!item) return nullptr;
    if (item->type() == HostItemType) item = item->parent();
    if (!item || item->type() != UserItemType) return nullptr;
    return static_cast<UserNode *>(item->data(0, Qt::UserRole).value<void *>());
}

SAUserManagerDialog::HostNode *SAUserManagerDialog::hostNodeForItem(QTreeWidgetItem *item)
{
    if (!item || item->type() != HostItemType) return nullptr;
    return static_cast<HostNode *>(item->data(0, Qt::UserRole).value<void *>());
}

void SAUserManagerDialog::treeSelectionChanged()
{
    storeFormIntoSelectedHost();
    m_currentHostItem = m_tree->currentItem();
    loadSelectedHostIntoForm();
    refreshSchemaList();
}

void SAUserManagerDialog::loadSelectedHostIntoForm()
{
    m_populatingForm = true;
    HostNode *host = hostNodeForItem(m_currentHostItem);
    UserNode *user = userNodeForItem(m_currentHostItem);
    const bool enabled = host != nullptr;
    m_userNameEdit->setEnabled(enabled);
    m_hostEdit->setEnabled(enabled);
    m_passwordEdit->setEnabled(enabled);
    m_confirmPasswordEdit->setEnabled(enabled);
    for (auto &pair : m_globalPrivCheckboxes) pair.second->setEnabled(enabled);
    m_maxQueries->setEnabled(enabled);
    m_maxUpdates->setEnabled(enabled);
    m_maxConnections->setEnabled(enabled);
    m_maxUserConnections->setEnabled(enabled);

    if (!enabled || !user) {
        m_userNameEdit->clear(); m_hostEdit->clear(); m_passwordEdit->clear(); m_confirmPasswordEdit->clear();
        for (auto &pair : m_globalPrivCheckboxes) pair.second->setChecked(false);
        m_maxQueries->setValue(0); m_maxUpdates->setValue(0); m_maxConnections->setValue(0); m_maxUserConnections->setValue(0);
        m_populatingForm = false;
        return;
    }
    m_userNameEdit->setText(user->user);
    m_hostEdit->setText(host->account.host);
    m_passwordEdit->clear();
    m_confirmPasswordEdit->clear();
    m_passwordEdit->setPlaceholderText(host->account.hasPassword || !host->newPassword.isEmpty() ? tr("(unchanged)") : tr("(none)"));
    for (auto &pair : m_globalPrivCheckboxes) pair.second->setChecked(host->account.globalPrivs.value(pair.first, false));
    m_maxQueries->setValue(host->account.maxQueries);
    m_maxUpdates->setValue(host->account.maxUpdates);
    m_maxConnections->setValue(host->account.maxConnections);
    m_maxUserConnections->setValue(host->account.maxUserConnections);
    m_populatingForm = false;
}

void SAUserManagerDialog::storeFormIntoSelectedHost()
{
    if (m_populatingForm) return;
    HostNode *host = hostNodeForItem(m_currentHostItem);
    UserNode *user = userNodeForItem(m_currentHostItem);
    if (!host || !user) return;
    user->user = m_userNameEdit->text().trimmed();
    host->account.user = user->user;
    host->account.host = m_hostEdit->text().trimmed();
    if (m_currentHostItem) m_currentHostItem->setText(0, host->account.host);
    if (m_currentHostItem && m_currentHostItem->parent()) m_currentHostItem->parent()->setText(0, user->user.isEmpty() ? tr("(anonymous)") : user->user);
    if (!m_passwordEdit->text().isEmpty()) {
        if (m_passwordEdit->text() != m_confirmPasswordEdit->text()) {
            SADialogs::warning(this, tr("Password Mismatch"), tr("The password and its confirmation do not match."));
        } else {
            host->newPassword = m_passwordEdit->text();
        }
    }
    for (auto &pair : m_globalPrivCheckboxes) host->account.globalPrivs[pair.first] = pair.second->isChecked();
    host->account.maxQueries = m_maxQueries->value();
    host->account.maxUpdates = m_maxUpdates->value();
    host->account.maxConnections = m_maxConnections->value();
    host->account.maxUserConnections = m_maxUserConnections->value();
}

void SAUserManagerDialog::refreshSchemaList()
{
    m_schemaList->clear();
    m_availableSchemaPrivs->clear();
    m_grantedSchemaPrivs->clear();
    HostNode *host = hostNodeForItem(m_currentHostItem);
    m_schemaList->setEnabled(host != nullptr);
    if (!host) return;
    m_schemaList->addItems(m_allDatabases);
    if (m_schemaList->count() > 0) m_schemaList->setCurrentRow(0);
}

void SAUserManagerDialog::schemaSelectionChanged()
{
    loadSchemaPrivilegesForCurrentSchema();
}

void SAUserManagerDialog::loadSchemaPrivilegesForCurrentSchema()
{
    m_availableSchemaPrivs->clear();
    m_grantedSchemaPrivs->clear();
    HostNode *host = hostNodeForItem(m_currentHostItem);
    UserNode *user = userNodeForItem(m_currentHostItem);
    QListWidgetItem *dbItem = m_schemaList->currentItem();
    if (!host || !user || !dbItem) return;
    const QString key = user->user + QLatin1Char('@') + host->account.host + QLatin1Char('|') + dbItem->text();
    const QHash<QString, bool> privs = m_schemaPrivsByDatabase.value(key);
    for (const QString &privKey : m_supportedSchemaPrivs) {
        const bool granted = privs.value(privKey, false);
        auto *item = new QListWidgetItem(grantNameForPrivilegeKey(privKey));
        item->setData(Qt::UserRole, privKey);
        (granted ? m_grantedSchemaPrivs : m_availableSchemaPrivs)->addItem(item);
    }
}

void SAUserManagerDialog::storeSchemaPrivilegesForCurrentSchema()
{
    HostNode *host = hostNodeForItem(m_currentHostItem);
    UserNode *user = userNodeForItem(m_currentHostItem);
    QListWidgetItem *dbItem = m_schemaList->currentItem();
    if (!host || !user || !dbItem) return;
    const QString key = user->user + QLatin1Char('@') + host->account.host + QLatin1Char('|') + dbItem->text();
    QHash<QString, bool> privs;
    for (int i = 0; i < m_grantedSchemaPrivs->count(); ++i)
        privs[m_grantedSchemaPrivs->item(i)->data(Qt::UserRole).toString()] = true;
    for (int i = 0; i < m_availableSchemaPrivs->count(); ++i)
        privs[m_availableSchemaPrivs->item(i)->data(Qt::UserRole).toString()] = false;
    m_schemaPrivsByDatabase[key] = privs;
}

void SAUserManagerDialog::moveSchemaPrivilege(bool grant)
{
    QListWidget *from = grant ? m_availableSchemaPrivs : m_grantedSchemaPrivs;
    QListWidget *to = grant ? m_grantedSchemaPrivs : m_availableSchemaPrivs;
    const QList<QListWidgetItem *> selected = from->selectedItems();
    for (QListWidgetItem *item : selected) {
        from->takeItem(from->row(item));
        to->addItem(item);
    }
    storeSchemaPrivilegesForCurrentSchema();
}

void SAUserManagerDialog::addUser()
{
    storeFormIntoSelectedHost();
    UserNode node;
    node.user = tr("new_user");
    node.isNew = true;
    HostNode host;
    host.account.user = node.user;
    host.account.host = QStringLiteral("%");
    host.isNew = true;
    node.hosts.append(host);
    m_users.append(node);
    buildTree();
    if (m_tree->topLevelItemCount() > 0) {
        QTreeWidgetItem *item = m_tree->topLevelItem(m_tree->topLevelItemCount() - 1);
        m_tree->setCurrentItem(item->child(0));
        m_userNameEdit->setFocus();
        m_userNameEdit->selectAll();
    }
}

void SAUserManagerDialog::addHost()
{
    storeFormIntoSelectedHost();
    UserNode *user = userNodeForItem(m_tree->currentItem());
    if (!user) return;
    HostNode host;
    host.account.user = user->user;
    host.account.host = QStringLiteral("%");
    host.isNew = true;
    user->hosts.append(host);
    buildTree();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (static_cast<UserNode *>(item->data(0, Qt::UserRole).value<void *>()) == user)
            m_tree->setCurrentItem(item->child(item->childCount() - 1));
    }
}

void SAUserManagerDialog::removeSelected()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item) return;
    if (item->type() == HostItemType) {
        UserNode *user = userNodeForItem(item);
        HostNode *host = hostNodeForItem(item);
        if (!user || !host) return;
        if (!SADialogs::confirm(this, tr("Remove host?"), tr("Remove host \"%1\" for user \"%2\"?").arg(host->account.host, user->user),
                                tr("Remove"), QString(), true)) return;
        for (int i = 0; i < user->hosts.size(); ++i) {
            if (&user->hosts[i] == host) { user->hosts.remove(i); break; }
        }
        if (user->hosts.isEmpty()) {
            for (int i = 0; i < m_users.size(); ++i)
                if (&m_users[i] == user) { m_users.remove(i); break; }
        }
    } else {
        UserNode *user = userNodeForItem(item);
        if (!user) return;
        if (!SADialogs::confirm(this, tr("Remove user?"), tr("Remove user \"%1\" and all its hosts?").arg(user->user),
                                tr("Remove"), QString(), true)) return;
        for (int i = 0; i < m_users.size(); ++i)
            if (&m_users[i] == user) { m_users.remove(i); break; }
    }
    m_currentHostItem = nullptr;
    buildTree();
}

void SAUserManagerDialog::checkAll()
{
    for (auto &pair : m_globalPrivCheckboxes) pair.second->setChecked(true);
    storeFormIntoSelectedHost();
}

void SAUserManagerDialog::uncheckAll()
{
    for (auto &pair : m_globalPrivCheckboxes) pair.second->setChecked(false);
    storeFormIntoSelectedHost();
}

QStringList SAUserManagerDialog::grantedPrivilegeNames(const QHash<QString, bool> &privs, bool *allGranted) const
{
    QStringList names;
    bool all = !m_supportedGlobalPrivs.isEmpty();
    for (const QString &key : m_supportedGlobalPrivs) {
        if (key == QLatin1String("grant_option_priv")) continue;   // handled separately via WITH GRANT OPTION
        const bool granted = privs.value(key, false);
        if (granted) names << grantNameForPrivilegeKey(key);
        else all = false;
    }
    if (allGranted) *allGranted = all;
    return names;
}

void SAUserManagerDialog::apply()
{
    storeFormIntoSelectedHost();
    storeSchemaPrivilegesForCurrentSchema();

    auto errors = std::make_shared<QStringList>();
    const auto escape = m_document->escaper();
    SADatabaseSession *session = m_document->session();

    for (UserNode &user : m_users) {
        for (HostNode &host : user.hosts) {
            QStringList statements;
            const QString plugin = host.account.plugin.isEmpty() ? QStringLiteral("mysql_native_password") : host.account.plugin;
            if (host.isNew) {
                statements << createUser(host.account.user, host.account.host, host.newPassword, QString(), m_post576 ? plugin : QString(), m_post576, escape);
            } else if (!host.newPassword.isEmpty()) {
                if (m_post576) statements << alterUserPassword({userAtHost(host.account.user, host.account.host, escape)}, plugin, host.newPassword, escape);
                else statements << setPasswordLegacy(host.account.user, host.account.host, host.newPassword, escape);
            }

            statements << alterUserResources(host.account.user, host.account.host, host.account.maxQueries, host.account.maxUpdates,
                                              host.account.maxConnections, host.account.maxUserConnections, escape);

            bool allGranted = false;
            const QStringList grantNames = grantedPrivilegeNames(host.account.globalPrivs, &allGranted);
            const bool withGrant = host.account.globalPrivs.value(QStringLiteral("grant_option_priv"), false);
            if (!host.isNew) statements << revokeAllStatement(QString(), host.account.user, host.account.host, escape);
            if (allGranted) statements << grantStatement({QStringLiteral("ALL PRIVILEGES")}, QString(), host.account.user, host.account.host, withGrant, escape);
            else if (!grantNames.isEmpty()) statements << grantStatement(grantNames, QString(), host.account.user, host.account.host, withGrant, escape);
            else if (withGrant) statements << grantStatement({QStringLiteral("USAGE")}, QString(), host.account.user, host.account.host, true, escape);

            for (const QString &database : m_allDatabases) {
                const QString key = user.user + QLatin1Char('@') + host.account.host + QLatin1Char('|') + database;
                if (!m_schemaPrivsByDatabase.contains(key)) continue;
                const QHash<QString, bool> privs = m_schemaPrivsByDatabase.value(key);
                QStringList schemaGrantNames;
                for (const QString &privKey : m_supportedSchemaPrivs)
                    if (privs.value(privKey, false)) schemaGrantNames << grantNameForPrivilegeKey(privKey);
                statements << revokeAllStatement(database, host.account.user, host.account.host, escape);
                if (!schemaGrantNames.isEmpty())
                    statements << grantStatement(schemaGrantNames, database, host.account.user, host.account.host, false, escape);
            }

            session->queryBatch(statements, [errors](const QVector<SAResult> &results) {
                for (const SAResult &r : results)
                    if (!r.ok) *errors << r.errorMessage;
            }, SADatabaseSession::NoFlags, false);
        }
    }

    session->query(flushPrivileges(), [this, errors](const SAResult &flushResult) {
        if (!flushResult.ok) *errors << flushResult.errorMessage;
        if (!errors->isEmpty()) { reportErrors(*errors); return; }
        reload();
    });
}

void SAUserManagerDialog::reportErrors(const QStringList &errors)
{
    if (errors.isEmpty()) return;
    SADialogs::warning(this, tr("User Manager"), tr("One or more statements failed while applying changes."), errors.join(QStringLiteral("\n")));
}
