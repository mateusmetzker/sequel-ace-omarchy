//
//  SAUserManagerDialog.h
//  Sequel Ace (Linux port)
//
//  MySQL/MariaDB account manager: a tree of user@host accounts on the left,
//  tabs of General/Global Privileges/Resources/Schema Privileges on the
//  right. Ported from SPUserManager's window controller.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAUserManagerQueries.h"

#include <QDialog>
#include <QHash>
#include <QVector>

class SADatabaseDocument;
class QTreeWidget;
class QTreeWidgetItem;
class QTabWidget;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QSpinBox;
class QLabel;
class QCheckBox;

class SAUserManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit SAUserManagerDialog(SADatabaseDocument *document, QWidget *parent = nullptr);

private Q_SLOTS:
    void reload();
    void addUser();
    void removeSelected();
    void addHost();
    void checkAll();
    void uncheckAll();
    void apply();
    void treeSelectionChanged();
    void schemaSelectionChanged();
    void moveSchemaPrivilege(bool grant);

private:
    struct HostNode {
        SAUserManager::SAUserAccount account;
        QString newPassword;                       // pending plaintext, empty = unchanged
        bool isNew = false;
    };
    struct UserNode {
        QString user;
        QVector<HostNode> hosts;
        bool isNew = false;
    };

    void buildTree();
    void buildGeneralTab();
    void buildGlobalPrivilegesTab();
    void buildResourcesTab();
    void buildSchemaPrivilegesTab();
    void loadSelectedHostIntoForm();
    void storeFormIntoSelectedHost();
    UserNode *userNodeForItem(QTreeWidgetItem *item);
    HostNode *hostNodeForItem(QTreeWidgetItem *item);
    void refreshSchemaList();
    void loadSchemaPrivilegesForCurrentSchema();
    void storeSchemaPrivilegesForCurrentSchema();
    QStringList grantedPrivilegeNames(const QHash<QString, bool> &privs, bool *allGranted) const;
    void reportErrors(const QStringList &errors);

    SADatabaseDocument *m_document;
    QTreeWidget *m_tree;
    QTabWidget *m_tabs;

    // General tab
    QLineEdit *m_userNameEdit;
    QLineEdit *m_hostEdit;
    QLineEdit *m_passwordEdit;
    QLineEdit *m_confirmPasswordEdit;

    // Global privileges tab
    QVector<QPair<QString, QCheckBox *>> m_globalPrivCheckboxes;

    // Resources tab
    QSpinBox *m_maxQueries;
    QSpinBox *m_maxUpdates;
    QSpinBox *m_maxConnections;
    QSpinBox *m_maxUserConnections;

    // Schema privileges tab
    QListWidget *m_schemaList;
    QListWidget *m_availableSchemaPrivs;
    QListWidget *m_grantedSchemaPrivs;
    QHash<QString, QHash<QString, bool>> m_schemaPrivsByDatabase;   // per selected host, database -> privs

    QVector<UserNode> m_users;
    QStringList m_supportedGlobalPrivs;
    QStringList m_supportedSchemaPrivs;
    QStringList m_allDatabases;
    bool m_post576 = false;
    bool m_populatingForm = false;
    QTreeWidgetItem *m_currentHostItem = nullptr;
};
