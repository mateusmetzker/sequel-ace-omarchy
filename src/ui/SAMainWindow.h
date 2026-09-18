//
//  SAMainWindow.h
//  Sequel Ace (Linux port)
//
//  Main window: one tab per connection document, application menus, and the
//  shared console window.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAMCPDataSource.h"

#include <QMainWindow>

#include <memory>

class QTabWidget;
class QAction;
class QMenu;
class SADatabaseDocument;
class SAFavoritesStore;
class SAMCPServer;

class SAMainWindow : public QMainWindow, public SAMCPDataSource {
    Q_OBJECT
public:
    explicit SAMainWindow(QWidget *parent = nullptr);
    ~SAMainWindow() override;

    SADatabaseDocument *currentDocument() const;
    SADatabaseDocument *newConnectionTab();
    void openPath(const QString &path);

    // SAMCPDataSource
    QVector<SAMCPConnectionInfo> openConnections() const override;
    SADatabaseSession *sessionFor(const QString &connectionId) const override;
    const SAMCPConnectionInfo *connectionInfoFor(const QString &connectionId) const override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildMenus();
    void updateActions();
    void updateTabTitle(SADatabaseDocument *document);
    void closeTab(int index);
    void showPreferences();
    void showAbout();
    void toggleConsole();
    void applyMCPPreferences();
    SADatabaseDocument *documentForConnectionId(const QString &connectionId) const;

    SAFavoritesStore *m_favorites;
    QTabWidget *m_tabs;
    std::unique_ptr<SAMCPServer> m_mcpServer;
    mutable QVector<SAMCPConnectionInfo> m_mcpConnectionsCache;

    // Actions whose enabled state depends on the current document.
    QList<QAction *> m_connectedActions;
    QList<QAction *> m_tableActions;
    QAction *m_viewActions[6] = {};
    QAction *m_toggleTablesList = nullptr;
    QAction *m_consoleAction = nullptr;
    QAction *m_binaryAsHexAction = nullptr;
};
