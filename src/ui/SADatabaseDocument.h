//
//  SADatabaseDocument.h
//  Sequel Ace (Linux port)
//
//  One connection tab: hosts the connection screen until connected, then the
//  toolbar (database selector, view switcher), the tables list and the six
//  table views. Coordinates loading between views, mirroring SPDatabaseDocument.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAConnectionInfo.h"
#include "SADatabaseSession.h"
#include "SADialogs.h"
#include "SASchemaQueries.h"

#include <QColor>
#include <QStringList>
#include <QWidget>

class SAConnectionView;
class SAFavoritesStore;
class SATablesList;
class SATableContentView;
class SATableStructureView;
class SATableRelationsView;
class SATableTriggersView;
class SATableInfoView;
class SACustomQueryView;
class QStackedWidget;
class QComboBox;
class QSplitter;
class QToolButton;
class QButtonGroup;
class QProgressBar;
class QLabel;

class SADatabaseDocument : public QWidget {
    Q_OBJECT
public:
    explicit SADatabaseDocument(SAFavoritesStore *favorites, QWidget *parent = nullptr);
    ~SADatabaseDocument() override;

    bool isConnected() const;
    SADatabaseSession *session() const { return m_session; }
    const SAConnectionInfo &connectionInfo() const { return m_info; }
    SAConnectionView *connectionView() const { return m_connectionView; }
    QString title() const;
    QString subtitle() const;
    QColor connectionColor() const;
    bool canClose();

    QString currentDatabase() const { return m_session->currentDatabase(); }
    QStringList databases() const { return m_databases; }
    QString selectedTable() const { return m_selectedTable; }
    SASchema::ObjectType selectedTableType() const { return m_selectedTableType; }
    QVector<SASchema::ObjectEntry> tableEntries() const { return m_tables; }
    QStringList tableNames(bool includeViews = true) const;

    // Cached schema metadata.
    const SADialogs::CharsetCollation &charsetInfo() const { return m_charsets; }
    QStringList engines() const { return m_engines; }
    QString defaultEngine() const { return m_defaultEngine; }
    QString databaseDefaultCharset() const { return m_databaseCharset; }

    int currentViewIndex() const;
    bool tablesListVisible() const;
    SATablesList *tablesList() const { return m_tablesList; }
    SATableContentView *contentView() const { return m_contentView; }
    SATableStructureView *structureView() const { return m_structureView; }
    SACustomQueryView *queryView() const { return m_queryView; }
    void openFile(const QString &path);

    // Helpers used by the views.
    QString escape(const QString &value) const { return SADatabaseSession::escapeString(value); }
    SASchema::EscapeFunction escaper() const { return [](const QString &v) { return SADatabaseSession::escapeString(v); }; }
    void reportError(const QString &title, const QString &message, const QString &detail = QString());
    void selectTable(const QString &name);
    void runQueryInEditor(const QString &sql, bool execute);
    void tableStructureChanged();     // structure view changed the table; content must reload
    void tableContentChanged();

public Q_SLOTS:
    void connectWithInfo(const SAConnectionInfo &info);
    void testConnection(const SAConnectionInfo &info);
    void disconnectFromServer();
    void showView(int index);
    void toggleTablesList();
    void chooseDatabase();
    void addDatabase();
    void deleteDatabase();
    void duplicateDatabase();
    void renameDatabase();
    void alterDatabase();
    void refreshDatabases();
    void refreshTables();
    void flushPrivileges();
    void showServerVariables();
    void showProcessList();
    void showUserManager();
    void addTable();
    void removeSelectedTables();
    void renameSelectedTable();
    void duplicateSelectedTable();
    void truncateSelectedTables();
    void copyCreateSyntax();
    void showCreateSyntax();
    void tableMaintenance(const QString &command);
    void exportTables();
    void exportCurrentResult();
    void explainCurrentQuery();
    void addConnectionToFavorites();
    void saveConnectionFile();
    void saveQueryFile();
    void copyWithColumnNames();
    void copyAsSQLInsert();
    void setSelectedCellsNull();
    void focusContentFilter();
    void focusTablesFilter();
    void historyBack();
    void historyForward();

Q_SIGNALS:
    void titleChanged();
    void stateChanged();
    void closeRequested();

private:
    void buildConnectedUI();
    void handleConnected();
    void handleDisconnected(const QString &reason);
    void loadDatabases(const QString &select);
    void databaseSelected(int index);
    void loadTables(const QString &reselect = QString());
    void tablesSelectionChanged();
    void loadSchemaMetadata();
    void loadCurrentViewIfNeeded();
    void updateTableInfoPanel();
    void setBusy(bool busy);
    void pushHistory();
    void updateTitle();
    void createSyntaxForSelected(std::function<void(const QString &)> done);

    SAFavoritesStore *m_favorites;
    SADatabaseSession *m_session;
    SAConnectionView *m_connectionView;
    QStackedWidget *m_pages;
    QWidget *m_connectedPage = nullptr;
    QComboBox *m_databaseBox = nullptr;
    QButtonGroup *m_viewButtons = nullptr;
    QStackedWidget *m_views = nullptr;
    QSplitter *m_splitter = nullptr;
    SATablesList *m_tablesList = nullptr;
    SATableStructureView *m_structureView = nullptr;
    SATableContentView *m_contentView = nullptr;
    SATableRelationsView *m_relationsView = nullptr;
    SATableTriggersView *m_triggersView = nullptr;
    SATableInfoView *m_infoView = nullptr;
    SACustomQueryView *m_queryView = nullptr;
    QProgressBar *m_busyIndicator = nullptr;
    QToolButton *m_stopButton = nullptr;
    QLabel *m_serverLabel = nullptr;

    SAConnectionInfo m_info;
    QStringList m_databases;
    QStringList m_systemDatabases;
    QVector<SASchema::ObjectEntry> m_tables;
    QString m_selectedTable;
    SASchema::ObjectType m_selectedTableType = SASchema::ObjectType::None;
    bool m_viewLoaded[6] = {false, false, false, false, false, false};
    SADialogs::CharsetCollation m_charsets;
    QStringList m_engines;
    QString m_defaultEngine;
    QString m_databaseCharset;
    bool m_loadingDatabases = false;
    bool m_connecting = false;
    bool m_testing = false;
    QString m_pendingQueryText;

    struct HistoryEntry { QString database; QString table; int view; };
    QVector<HistoryEntry> m_history;
    int m_historyIndex = -1;
    bool m_navigatingHistory = false;
};
