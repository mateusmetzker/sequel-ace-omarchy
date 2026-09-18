//
//  SAConnectionView.h
//  Sequel Ace (Linux port)
//
//  The connection screen: favorites outline on the left, connection form on
//  the right (TCP/IP, socket, SSH tunnel), SSL and advanced options. Edits to a
//  selected favorite are written back to the favorites file as they happen,
//  mirroring the macOS SPConnectionController.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAConnectionInfo.h"
#include "SAFavoritesStore.h"

#include <QWidget>

class SAFavoritesModel;
class QTreeView;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QPushButton;
class QToolButton;
class QStackedWidget;
class QGroupBox;
class QLabel;
class QButtonGroup;
class QTimer;
class QSortFilterProxyModel;

class SAConnectionView : public QWidget {
    Q_OBJECT
public:
    explicit SAConnectionView(SAFavoritesStore *store, QWidget *parent = nullptr);

    // Connection info as currently entered, including passwords.
    SAConnectionInfo currentInfo() const;
    void setInfo(const SAConnectionInfo &info);
    void selectFavorite(int id);
    void selectQuickConnect();
    void focusForm();
    bool savePasswordsEnabled() const;

    // Persists passwords of a successfully used connection in the keyring.
    void rememberPasswords(const SAConnectionInfo &info);
    void setStatusMessage(const QString &text);

public Q_SLOTS:
    void addCurrentToFavorites();
    void addGroup();
    void duplicateSelectedFavorite();
    void removeSelected();
    void importFavorites();
    void exportFavorites();

Q_SIGNALS:
    void connectRequested(const SAConnectionInfo &info);
    void testRequested(const SAConnectionInfo &info);

private:
    void buildFavoritesPane(QWidget *parent);
    void buildForm(QWidget *parent);
    void typeChanged(int index);
    void favoriteSelectionChanged();
    void formEdited();
    void commitEdits();
    void updateVisibility();
    void loadPasswordsFor(SAConnectionInfo &info) const;
    QString keyringAccount(const SAConnectionInfo &info) const;
    QLineEdit *filePicker(QWidget *parent, QCheckBox **enabledBox, const QString &caption);
    void showFavoriteContextMenu(const QPoint &pos);

    SAFavoritesStore *m_store;
    SAFavoritesModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTreeView *m_tree;
    QLineEdit *m_search;
    SAFavoriteNode *m_editingNode = nullptr;
    bool m_loading = false;
    QTimer *m_saveTimer;

    // form
    QButtonGroup *m_typeGroup;
    QLineEdit *m_name;
    QLineEdit *m_host;
    QLineEdit *m_user;
    QLineEdit *m_password;
    QLineEdit *m_database;
    QLineEdit *m_port;
    QLineEdit *m_socket;
    QComboBox *m_color;
    QLabel *m_hostLabel;
    QLabel *m_portLabel;
    QLabel *m_socketLabel;
    QWidget *m_hostRow;
    QWidget *m_portRow;
    QWidget *m_socketRow;

    QGroupBox *m_sshGroup;
    QLineEdit *m_sshHost;
    QLineEdit *m_sshUser;
    QLineEdit *m_sshPassword;
    QLineEdit *m_sshKey;
    QCheckBox *m_sshKeyEnabled;
    QLineEdit *m_sshPort;
    QLineEdit *m_sshRemoteSocket;

    QGroupBox *m_sslGroup;
    QCheckBox *m_useSSL;
    QLineEdit *m_sslKey;
    QCheckBox *m_sslKeyEnabled;
    QLineEdit *m_sslCert;
    QCheckBox *m_sslCertEnabled;
    QLineEdit *m_sslCA;
    QCheckBox *m_sslCAEnabled;

    QGroupBox *m_advancedGroup;
    QComboBox *m_timeZoneMode;
    QComboBox *m_timeZone;
    QCheckBox *m_compression;
    QCheckBox *m_localInfile;
    QCheckBox *m_clearText;
    QCheckBox *m_serverPublicKey;
    QCheckBox *m_savePasswords;

    QPushButton *m_connectButton;
    QPushButton *m_testButton;
    QPushButton *m_addFavoriteButton;
    QLabel *m_status;
    QLabel *m_formTitle;
};
