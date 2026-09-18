//
//  SAMainWindow.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAMainWindow.h"
#include "SAConnectionInfo.h"
#include "SAConnectionView.h"
#include "SAConsoleWindow.h"
#include "SADatabaseDocument.h"
#include "SADialogs.h"
#include "SAFavoritesStore.h"
#include "SAIcons.h"
#include "SAMCPServer.h"
#include "SAPreferences.h"
#include "SAPreferencesDialog.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>

SAMainWindow::SAMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Sequel Ace"));
    resize(1280, 820);

    m_favorites = new SAFavoritesStore(this);
    QString error;
    if (!m_favorites->load(QString(), &error)) {
        SADialogs::warning(this, tr("Favorites could not be loaded"), error);
    }

    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setElideMode(Qt::ElideRight);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &SAMainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) { updateActions(); });
    auto *newTab = new QToolButton;
    newTab->setIcon(SAIcons::icon(SAIcons::Glyph::Add));
    newTab->setToolTip(tr("New connection tab (Ctrl+T)"));
    newTab->setAutoRaise(true);
    connect(newTab, &QToolButton::clicked, this, [this]() { newConnectionTab(); });
    m_tabs->setCornerWidget(newTab, Qt::TopRightCorner);
    setCentralWidget(m_tabs);

    buildMenus();
    newConnectionTab();

    SAPreferences &prefs = SAPreferences::instance();
    const QByteArray geometry = prefs.value(SAPreferences::WindowGeometry).toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    updateActions();

    m_mcpServer = std::make_unique<SAMCPServer>(this);
    connect(&prefs, &SAPreferences::changed, this, [this](const QString &key) {
        if (key == SAPreferences::MCPServerEnabled || key == SAPreferences::MCPServerPort
            || key == SAPreferences::MCPReadOnly || key == SAPreferences::MCPExportPath)
            applyMCPPreferences();
    });
    applyMCPPreferences();
}

SAMainWindow::~SAMainWindow()
{
    // Documents emit state changes while disconnecting; stop listening before
    // the child widgets are torn down.
    for (int i = 0; i < m_tabs->count(); ++i)
        if (QWidget *w = m_tabs->widget(i)) disconnect(w, nullptr, this, nullptr);
}

void SAMainWindow::applyMCPPreferences()
{
    SAPreferences &prefs = SAPreferences::instance();
    m_mcpServer->setReadOnly(prefs.value(SAPreferences::MCPReadOnly).isValid() ? prefs.boolFor(SAPreferences::MCPReadOnly) : true);
    m_mcpServer->setExportPath(prefs.stringFor(SAPreferences::MCPExportPath));

    const bool enabled = prefs.boolFor(SAPreferences::MCPServerEnabled);
    const quint16 port = static_cast<quint16>(prefs.value(SAPreferences::MCPServerPort).isValid() ? prefs.intFor(SAPreferences::MCPServerPort) : 8765);
    if (!enabled) {
        m_mcpServer->stop();
        return;
    }
    if (m_mcpServer->isRunning() && m_mcpServer->port() == port) return;
    m_mcpServer->stop();
    QString error;
    if (!m_mcpServer->start(port, &error))
        SADialogs::warning(this, tr("MCP server could not be started"), error);
}

QVector<SAMCPConnectionInfo> SAMainWindow::openConnections() const
{
    QVector<SAMCPConnectionInfo> connections;
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *doc = qobject_cast<SADatabaseDocument *>(m_tabs->widget(i));
        if (!doc || !doc->isConnected()) continue;
        SAMCPConnectionInfo info;
        info.id = QString::number(reinterpret_cast<quintptr>(doc));
        info.name = doc->title();
        info.host = doc->connectionInfo().host;
        info.database = doc->currentDatabase();
        info.active = (doc == currentDocument());
        info.favoriteName = doc->connectionInfo().name;
        connections.append(info);
    }
    return connections;
}

SADatabaseDocument *SAMainWindow::documentForConnectionId(const QString &connectionId) const
{
    if (connectionId.isEmpty()) return currentDocument();
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *doc = qobject_cast<SADatabaseDocument *>(m_tabs->widget(i));
        if (doc && QString::number(reinterpret_cast<quintptr>(doc)) == connectionId) return doc;
    }
    return nullptr;
}

SADatabaseSession *SAMainWindow::sessionFor(const QString &connectionId) const
{
    SADatabaseDocument *doc = documentForConnectionId(connectionId);
    return (doc && doc->isConnected()) ? doc->session() : nullptr;
}

const SAMCPConnectionInfo *SAMainWindow::connectionInfoFor(const QString &connectionId) const
{
    m_mcpConnectionsCache = openConnections();
    const QString targetId = connectionId.isEmpty() && currentDocument()
        ? QString::number(reinterpret_cast<quintptr>(currentDocument())) : connectionId;
    for (const SAMCPConnectionInfo &info : std::as_const(m_mcpConnectionsCache))
        if (info.id == targetId) return &info;
    return nullptr;
}

SADatabaseDocument *SAMainWindow::currentDocument() const
{
    return qobject_cast<SADatabaseDocument *>(m_tabs->currentWidget());
}

SADatabaseDocument *SAMainWindow::newConnectionTab()
{
    auto *document = new SADatabaseDocument(m_favorites);
    const int index = m_tabs->addTab(document, tr("Connection"));
    m_tabs->setCurrentIndex(index);
    connect(document, &SADatabaseDocument::titleChanged, this, [this, document]() { updateTabTitle(document); });
    connect(document, &SADatabaseDocument::stateChanged, this, &SAMainWindow::updateActions);
    connect(document, &SADatabaseDocument::closeRequested, this, [this, document]() { closeTab(m_tabs->indexOf(document)); });
    updateTabTitle(document);
    document->connectionView()->focusForm();
    return document;
}

void SAMainWindow::openPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        SADialogs::warning(this, tr("File not found"), path);
        return;
    }
    SADatabaseDocument *document = currentDocument();
    if (!document || document->isConnected()) document = newConnectionTab();
    document->openFile(path);
}

void SAMainWindow::updateTabTitle(SADatabaseDocument *document)
{
    const int index = m_tabs->indexOf(document);
    if (index < 0) return;
    m_tabs->setTabText(index, document->title());
    m_tabs->setTabToolTip(index, document->subtitle());
    const QColor color = document->connectionColor();
    m_tabs->tabBar()->setTabTextColor(index, color.isValid() ? color.darker(140) : QColor());
    if (document == currentDocument()) setWindowTitle(document->isConnected() ? QStringLiteral("%1 — Sequel Ace").arg(document->title()) : QStringLiteral("Sequel Ace"));
}

void SAMainWindow::closeTab(int index)
{
    auto *document = qobject_cast<SADatabaseDocument *>(m_tabs->widget(index));
    if (!document) return;
    if (!document->canClose()) return;
    m_tabs->removeTab(index);
    document->deleteLater();
    if (m_tabs->count() == 0) newConnectionTab();
}

void SAMainWindow::closeEvent(QCloseEvent *event)
{
    int connected = 0;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto *doc = qobject_cast<SADatabaseDocument *>(m_tabs->widget(i)); doc && doc->isConnected()) ++connected;
    if (connected > 1 && !SADialogs::confirm(this, tr("Quit Sequel Ace?"), tr("There are %n open connections. Quit anyway?", nullptr, connected), tr("Quit"))) {
        event->ignore();
        return;
    }
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto *doc = qobject_cast<SADatabaseDocument *>(m_tabs->widget(i))) doc->disconnectFromServer();
    SAPreferences::instance().set(SAPreferences::WindowGeometry, saveGeometry());
    SAPreferences::instance().sync();
    SAConsoleWindow::shared()->close();
    event->accept();
}

// ---- menus ---------------------------------------------------------------------

void SAMainWindow::buildMenus()
{
    auto doc = [this]() { return currentDocument(); };
    auto addDocAction = [&](QMenu *menu, const QString &text, const QKeySequence &shortcut, auto method, QList<QAction *> *group) {
        QAction *action = menu->addAction(text, this, [doc, method]() { if (SADatabaseDocument *d = doc()) (d->*method)(); });
        if (!shortcut.isEmpty()) action->setShortcut(shortcut);
        if (group) group->append(action);
        return action;
    };

    // File
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("New Connection Tab"), QKeySequence(Qt::CTRL | Qt::Key_T), this, [this]() { newConnectionTab(); });
    file->addAction(tr("Open Connection or SQL File…"), QKeySequence::Open, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open"), QDir::homePath(), tr("Sequel Ace connections and SQL (*.spf *.sql);;All files (*)"));
        if (!path.isEmpty()) openPath(path);
    });
    file->addSeparator();
    addDocAction(file, tr("Add Current Connection to Favorites"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), &SADatabaseDocument::addConnectionToFavorites, nullptr);
    addDocAction(file, tr("Save Connection As .spf…"), QKeySequence::Save, &SADatabaseDocument::saveConnectionFile, &m_connectedActions);
    addDocAction(file, tr("Save Query…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), &SADatabaseDocument::saveQueryFile, &m_connectedActions);
    file->addSeparator();
    addDocAction(file, tr("Export…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), &SADatabaseDocument::exportTables, &m_connectedActions);
    // No shortcut: Ctrl+Alt+E belongs to Explain Current Query in the query editor.
    addDocAction(file, tr("Export Result…"), QKeySequence(), &SADatabaseDocument::exportCurrentResult, &m_connectedActions);
    file->addSeparator();
    file->addAction(tr("Close Tab"), QKeySequence::Close, this, [this]() { closeTab(m_tabs->currentIndex()); });
    file->addAction(tr("Quit"), QKeySequence::Quit, this, &QWidget::close);

    // Edit
    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    auto focusAction = [this, edit](const QString &text, const QKeySequence &seq, const char *slot) {
        QAction *a = edit->addAction(text, this, [slot]() {
            if (QWidget *w = QApplication::focusWidget()) QMetaObject::invokeMethod(w, slot);
        });
        a->setShortcut(seq);
        return a;
    };
    focusAction(tr("Undo"), QKeySequence::Undo, "undo");
    focusAction(tr("Redo"), QKeySequence::Redo, "redo");
    edit->addSeparator();
    focusAction(tr("Cut"), QKeySequence::Cut, "cut");
    focusAction(tr("Copy"), QKeySequence::Copy, "copy");
    focusAction(tr("Paste"), QKeySequence::Paste, "paste");
    focusAction(tr("Select All"), QKeySequence::SelectAll, "selectAll");
    edit->addSeparator();
    addDocAction(edit, tr("Copy with Column Names"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), &SADatabaseDocument::copyWithColumnNames, &m_connectedActions);
    addDocAction(edit, tr("Copy as SQL INSERT"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C), &SADatabaseDocument::copyAsSQLInsert, &m_connectedActions);
    addDocAction(edit, tr("Insert NULL value"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), &SADatabaseDocument::setSelectedCellsNull, &m_connectedActions);
    edit->addSeparator();
    edit->addAction(tr("Preferences…"), QKeySequence::Preferences, this, &SAMainWindow::showPreferences);

    // View
    QMenu *view = menuBar()->addMenu(tr("&View"));
    const QString viewNames[6] = {tr("Table Structure"), tr("Table Content"), tr("Table Relations"), tr("Table Triggers"), tr("Table Info"), tr("Custom Query")};
    for (int i = 0; i < 6; ++i) {
        m_viewActions[i] = view->addAction(viewNames[i], this, [doc, i]() { if (SADatabaseDocument *d = doc()) d->showView(i); });
        m_viewActions[i]->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)));
        m_viewActions[i]->setCheckable(true);
        m_connectedActions.append(m_viewActions[i]);
    }
    view->addSeparator();
    m_toggleTablesList = addDocAction(view, tr("Show Tables List"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), &SADatabaseDocument::toggleTablesList, &m_connectedActions);
    m_toggleTablesList->setCheckable(true);
    m_consoleAction = view->addAction(tr("Show Console"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K), this, &SAMainWindow::toggleConsole);
    view->addAction(tr("Clear Console"), this, []() { SAConsoleWindow::shared()->clear(); });
    view->addSeparator();
    m_binaryAsHexAction = view->addAction(tr("Display Binary Data as Hex"), this, [this](bool on) {
        SAPreferences::instance().set(SAPreferences::DisplayBinaryDataAsHex, on);
    });
    m_binaryAsHexAction->setCheckable(true);
    m_binaryAsHexAction->setChecked(SAPreferences::instance().boolFor(SAPreferences::DisplayBinaryDataAsHex));
    view->addSeparator();
    addDocAction(view, tr("Back In History"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left), &SADatabaseDocument::historyBack, &m_connectedActions);
    addDocAction(view, tr("Forward In History"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right), &SADatabaseDocument::historyForward, &m_connectedActions);

    // Database
    QMenu *database = menuBar()->addMenu(tr("&Database"));
    addDocAction(database, tr("Go to Database…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), &SADatabaseDocument::chooseDatabase, &m_connectedActions);
    addDocAction(database, tr("Add Database…"), QKeySequence(), &SADatabaseDocument::addDatabase, &m_connectedActions);
    addDocAction(database, tr("Delete Database…"), QKeySequence(), &SADatabaseDocument::deleteDatabase, &m_connectedActions);
    addDocAction(database, tr("Duplicate Database…"), QKeySequence(), &SADatabaseDocument::duplicateDatabase, &m_connectedActions);
    addDocAction(database, tr("Rename Database…"), QKeySequence(), &SADatabaseDocument::renameDatabase, &m_connectedActions);
    addDocAction(database, tr("Alter Database…"), QKeySequence(), &SADatabaseDocument::alterDatabase, &m_connectedActions);
    database->addSeparator();
    addDocAction(database, tr("Refresh Tables"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), &SADatabaseDocument::refreshTables, &m_connectedActions);
    addDocAction(database, tr("Refresh Databases"), QKeySequence(), &SADatabaseDocument::refreshDatabases, &m_connectedActions);
    addDocAction(database, tr("Flush Privileges"), QKeySequence(), &SADatabaseDocument::flushPrivileges, &m_connectedActions);
    database->addSeparator();
    addDocAction(database, tr("Show Server Variables…"), QKeySequence(), &SADatabaseDocument::showServerVariables, &m_connectedActions);
    addDocAction(database, tr("Show Server Processes…"), QKeySequence(), &SADatabaseDocument::showProcessList, &m_connectedActions);
    addDocAction(database, tr("Manage Users…"), QKeySequence(), &SADatabaseDocument::showUserManager, &m_connectedActions);
    database->addSeparator();
    addDocAction(database, tr("Disconnect"), QKeySequence(), &SADatabaseDocument::disconnectFromServer, &m_connectedActions);

    // Table
    QMenu *table = menuBar()->addMenu(tr("&Table"));
    addDocAction(table, tr("Add Table…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), &SADatabaseDocument::addTable, &m_connectedActions);
    addDocAction(table, tr("Filter Content"), QKeySequence(Qt::CTRL | Qt::Key_F), &SADatabaseDocument::focusContentFilter, &m_tableActions);
    addDocAction(table, tr("Filter Tables"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F), &SADatabaseDocument::focusTablesFilter, &m_connectedActions);
    table->addSeparator();
    addDocAction(table, tr("Copy Create Table Syntax"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::ALT | Qt::Key_C), &SADatabaseDocument::copyCreateSyntax, &m_tableActions);
    addDocAction(table, tr("Show Create Table Syntax…"), QKeySequence(), &SADatabaseDocument::showCreateSyntax, &m_tableActions);
    table->addSeparator();
    addDocAction(table, tr("Rename Table…"), QKeySequence(), &SADatabaseDocument::renameSelectedTable, &m_tableActions);
    addDocAction(table, tr("Duplicate Table…"), QKeySequence(), &SADatabaseDocument::duplicateSelectedTable, &m_tableActions);
    addDocAction(table, tr("Truncate Table…"), QKeySequence(), &SADatabaseDocument::truncateSelectedTables, &m_tableActions);
    addDocAction(table, tr("Delete Table…"), QKeySequence(), &SADatabaseDocument::removeSelectedTables, &m_tableActions);
    table->addSeparator();
    for (const char *cmd : {"Check", "Repair", "Analyze", "Optimize", "Flush", "Checksum"}) {
        const QString command = QLatin1String(cmd);
        QAction *a = table->addAction(tr("%1 Table").arg(command), this, [doc, command]() { if (SADatabaseDocument *d = doc()) d->tableMaintenance(command.toUpper()); });
        m_tableActions.append(a);
    }

    // Help
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Sequel Ace Help"), this, []() { QDesktopServices::openUrl(QUrl(QStringLiteral("https://sequel-ace.com/"))); });
    help->addAction(tr("Report an Issue"), this, []() { QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Sequel-Ace/Sequel-Ace/issues"))); });
    help->addSeparator();
    help->addAction(tr("About Sequel Ace"), this, &SAMainWindow::showAbout);
}

void SAMainWindow::updateActions()
{
    SADatabaseDocument *document = currentDocument();
    const bool connected = document && document->isConnected();
    const bool hasTable = connected && !document->selectedTable().isEmpty();
    for (QAction *a : m_connectedActions) a->setEnabled(connected);
    for (QAction *a : m_tableActions) a->setEnabled(hasTable);
    for (int i = 0; i < 6; ++i)
        if (m_viewActions[i]) m_viewActions[i]->setChecked(connected && document->currentViewIndex() == i);
    if (m_toggleTablesList) m_toggleTablesList->setChecked(connected && document->tablesListVisible());
    if (document) updateTabTitle(document);
    if (m_consoleAction) m_consoleAction->setText(SAConsoleWindow::shared()->isVisible() ? tr("Hide Console") : tr("Show Console"));
}

void SAMainWindow::toggleConsole()
{
    SAConsoleWindow *console = SAConsoleWindow::shared();
    if (console->isVisible()) console->hide();
    else {
        console->show();
        console->raise();
        console->activateWindow();
    }
    updateActions();
}

void SAMainWindow::showPreferences()
{
    SAPreferencesDialog dialog(this);
    dialog.exec();
    if (m_binaryAsHexAction) m_binaryAsHexAction->setChecked(SAPreferences::instance().boolFor(SAPreferences::DisplayBinaryDataAsHex));
}

void SAMainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Sequel Ace"),
        tr("<h3>Sequel Ace for Linux %1</h3>"
           "<p>A MySQL and MariaDB database management application, ported from the macOS Sequel Ace.</p>"
           "<p>Copyright © 2020-2026 Moballo, LLC. Forked from Sequel Pro, Copyright © 2002-2019 Sequel Pro &amp; CocoaMySQL Teams.</p>"
           "<p>Licensed under the MIT license. Built with Qt %2 and libmariadb.</p>"
           "<p><a href=\"https://sequel-ace.com\">sequel-ace.com</a></p>").arg(QStringLiteral(SA_VERSION_STRING), QString::fromLatin1(qVersion())));
}
