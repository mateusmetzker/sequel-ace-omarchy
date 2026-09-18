//
//  SADatabaseDocument.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SADatabaseDocument.h"
#include "SAConnectionView.h"
#include "SAConsoleWindow.h"
#include "SACustomQueryView.h"
#include "SAExportDialog.h"
#include "SAFavoritesStore.h"
#include "SAIcons.h"
#include "SAPlist.h"
#include "SAPreferences.h"
#include "SASSHTunnel.h"
#include "SAServerDialogs.h"
#include "SAUserManagerDialog.h"
#include "SATableContentView.h"
#include "SATableInfoView.h"
#include "SATableRelationsView.h"
#include "SATableStructureView.h"
#include "SATableTriggersView.h"
#include "SATablesList.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <algorithm>

SADatabaseDocument::SADatabaseDocument(SAFavoritesStore *favorites, QWidget *parent)
    : QWidget(parent), m_favorites(favorites)
{
    m_session = new SADatabaseSession(this);
    m_session->setSSHPromptHandlers(
        [this](const QString &question) {
            return QMessageBox::question(this, tr("SSH Question"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
        },
        [this](const QString &prompt, bool *cancelled) {
            bool ok = false;
            const QString secret = SADialogs::askPassword(this, tr("SSH Authentication"), prompt.trimmed(), &ok);
            *cancelled = !ok;
            return secret;
        });
    connect(m_session, &SADatabaseSession::queryPerformed, this, [this](const QString &sql, double seconds, bool isError, const QString &error, const QString &database) {
        SAConsoleWindow::shared()->addMessage(m_info.displayName(), database, isError ? QStringLiteral("%1\n/* %2 */").arg(sql, error) : sql, isError, seconds);
    });
    connect(m_session, &SADatabaseSession::disconnected, this, &SADatabaseDocument::handleDisconnected);
    connect(m_session, &SADatabaseSession::busyChanged, this, &SADatabaseDocument::setBusy);
    connect(m_session, &SADatabaseSession::connectionLostAndRestored, this, [this]() {
        SAConsoleWindow::shared()->addMessage(m_info.displayName(), currentDatabase(), tr("/* The connection was lost and has been re-established. */"), false, 0);
    });

    m_connectionView = new SAConnectionView(favorites);
    connect(m_connectionView, &SAConnectionView::connectRequested, this, &SADatabaseDocument::connectWithInfo);
    connect(m_connectionView, &SAConnectionView::testRequested, this, &SADatabaseDocument::testConnection);

    m_pages = new QStackedWidget;
    m_pages->addWidget(m_connectionView);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_pages);
}

SADatabaseDocument::~SADatabaseDocument()
{
    m_session->disconnectFromServer();
}

bool SADatabaseDocument::isConnected() const { return m_session->isConnected(); }

QString SADatabaseDocument::title() const
{
    if (!isConnected()) return tr("Connection");
    const QString db = currentDatabase();
    return db.isEmpty() ? m_info.displayName() : QStringLiteral("%1 / %2").arg(m_info.displayName(), db);
}

QString SADatabaseDocument::subtitle() const
{
    if (!isConnected()) return QString();
    return QStringLiteral("%1 — %2").arg(m_info.hostDescription(), m_session->serverInfo().versionString);
}

QColor SADatabaseDocument::connectionColor() const
{
    return isConnected() ? SAFavoriteColors::color(m_info.colorIndex) : QColor();
}

bool SADatabaseDocument::canClose()
{
    if (m_contentView && m_contentView->hasPendingEdits()) {
        if (!SADialogs::confirm(this, tr("Discard unsaved row?"), tr("The content view has an unsaved row. Close anyway?"), tr("Discard"), QString(), true)) return false;
    }
    m_session->disconnectFromServer();
    return true;
}

QStringList SADatabaseDocument::tableNames(bool includeViews) const
{
    QStringList names;
    for (const SASchema::ObjectEntry &e : m_tables)
        if (e.type == SASchema::ObjectType::Table || (includeViews && e.type == SASchema::ObjectType::View)) names << e.name;
    return names;
}

int SADatabaseDocument::currentViewIndex() const { return m_views ? m_views->currentIndex() : -1; }
bool SADatabaseDocument::tablesListVisible() const { return m_tablesList && m_tablesList->isVisible(); }

void SADatabaseDocument::reportError(const QString &title, const QString &message, const QString &detail)
{
    SADialogs::warning(this, title, message, detail);
}

// ---- connecting -----------------------------------------------------------------------------

void SADatabaseDocument::connectWithInfo(const SAConnectionInfo &info)
{
    if (m_connecting) return;
    QString problem;
    if (!info.isValidForConnecting(&problem)) {
        reportError(tr("Cannot connect"), problem);
        return;
    }
    m_connecting = true;
    m_info = info;
    m_session->setConnectionInfo(info);

    auto *busy = new SABusyDialog(this, tr("Connecting"), tr("Connecting to %1…").arg(info.hostDescription()));
    busy->setAttribute(Qt::WA_DeleteOnClose);
    connect(busy, &SABusyDialog::cancelRequested, this, [this]() { m_session->disconnectFromServer(); });
    connect(m_session, &SADatabaseSession::sshTunnelStateChanged, busy, [this, busy]() {
        if (SASSHTunnel *tunnel = m_session->sshTunnel()) {
            switch (tunnel->state()) {
            case SASSHTunnel::Connecting: busy->setText(tr("Opening SSH tunnel to %1…").arg(m_info.sshHost)); break;
            case SASSHTunnel::WaitingForAuth: busy->setText(tr("SSH connection established, authenticating…")); break;
            case SASSHTunnel::Connected: busy->setText(tr("SSH tunnel established, connecting to MySQL…")); break;
            default: break;
            }
        }
    });
    busy->show();

    m_session->connectToServer([this, busy](bool ok, const QString &error) {
        m_connecting = false;
        const bool cancelled = busy->wasCancelled();
        busy->finish();
        if (cancelled) return;
        if (!ok) {
            QString detail;
            if (SASSHTunnel *tunnel = m_session->sshTunnel()) detail = tunnel->debugMessages().join(QLatin1Char('\n'));
            m_session->disconnectFromServer();
            qWarning("Sequel Ace: connection to %s failed: %s", qPrintable(m_info.hostDescription()), qPrintable(error));
            SADialogs::warning(this, tr("Connection failed"), tr("Unable to connect to host %1.\n\nMySQL said: %2").arg(m_info.hostDescription(), error), detail);
            return;
        }
        if (!error.isEmpty()) {
            // Connected, but the requested database could not be selected.
            SADialogs::warning(this, tr("Database unavailable"), tr("The database “%1” could not be selected.\n\nMySQL said: %2").arg(m_info.database, error));
        }
        m_connectionView->rememberPasswords(m_info);
        if (m_info.id >= 0) SAPreferences::instance().set(SAPreferences::LastFavoriteId, m_info.id);
        handleConnected();
    });
}

void SADatabaseDocument::testConnection(const SAConnectionInfo &info)
{
    if (m_connecting || isConnected()) return;
    QString problem;
    if (!info.isValidForConnecting(&problem)) {
        reportError(tr("Cannot connect"), problem);
        return;
    }
    m_connecting = true;
    m_session->setConnectionInfo(info);
    auto *busy = new SABusyDialog(this, tr("Testing Connection"), tr("Connecting to %1…").arg(info.hostDescription()));
    busy->setAttribute(Qt::WA_DeleteOnClose);
    connect(busy, &SABusyDialog::cancelRequested, this, [this]() { m_session->disconnectFromServer(); });
    busy->show();
    m_session->connectToServer([this, busy, info](bool ok, const QString &error) {
        m_connecting = false;
        const bool cancelled = busy->wasCancelled();
        busy->finish();
        const QString version = m_session->serverInfo().versionString;
        QString detail;
        if (SASSHTunnel *tunnel = m_session->sshTunnel()) detail = tunnel->debugMessages().join(QLatin1Char('\n'));
        m_session->disconnectFromServer();
        if (cancelled) return;
        if (ok) SADialogs::information(this, tr("Connection succeeded"), tr("Connected to %1 (%2).%3").arg(info.hostDescription(), version, error.isEmpty() ? QString() : tr("\n\nNote: %1").arg(error)));
        else SADialogs::warning(this, tr("Connection failed"), tr("Unable to connect to host %1.\n\nMySQL said: %2").arg(info.hostDescription(), error), detail);
    });
}

void SADatabaseDocument::handleConnected()
{
    if (!m_connectedPage) buildConnectedUI();
    m_pages->setCurrentWidget(m_connectedPage);
    m_serverLabel->setText(QStringLiteral("%1 %2").arg(m_session->serverInfo().isMariaDB ? QStringLiteral("MariaDB") : QStringLiteral("MySQL"), m_session->serverInfo().versionString)
                           + (m_session->serverInfo().ssl ? tr(" · SSL") : QString()));
    for (bool &loaded : m_viewLoaded) loaded = false;
    m_history.clear();
    m_historyIndex = -1;
    loadSchemaMetadata();
    loadDatabases(m_info.database);
    m_queryView->setQueryText(m_pendingQueryText.isEmpty() ? m_queryView->queryText() : m_pendingQueryText);
    m_pendingQueryText.clear();
    updateTitle();
    Q_EMIT stateChanged();
}

void SADatabaseDocument::handleDisconnected(const QString &reason)
{
    if (!m_connectedPage || m_pages->currentWidget() != m_connectedPage) {
        Q_EMIT stateChanged();
        return;
    }
    if (!reason.isEmpty()) {
        const bool reconnect = SADialogs::confirm(this, tr("Connection lost"),
            tr("The connection to %1 was lost.\n\n%2").arg(m_info.hostDescription(), reason), tr("Reconnect"), tr("Disconnect"));
        if (reconnect) {
            connectWithInfo(m_info);
            return;
        }
    }
    m_pages->setCurrentWidget(m_connectionView);
    m_tablesList->clear();
    m_selectedTable.clear();
    m_selectedTableType = SASchema::ObjectType::None;
    for (auto *view : {static_cast<QWidget *>(m_structureView), static_cast<QWidget *>(m_contentView), static_cast<QWidget *>(m_relationsView),
                       static_cast<QWidget *>(m_triggersView), static_cast<QWidget *>(m_infoView)}) Q_UNUSED(view);
    m_structureView->clear();
    m_contentView->clear();
    m_relationsView->clear();
    m_triggersView->clear();
    m_infoView->clear();
    updateTitle();
    Q_EMIT stateChanged();
}

void SADatabaseDocument::disconnectFromServer()
{
    if (m_contentView && m_contentView->hasPendingEdits()) m_contentView->commitPendingEdits();
    m_session->disconnectFromServer();
}

// ---- connected UI -----------------------------------------------------------------------------

void SADatabaseDocument::buildConnectedUI()
{
    m_connectedPage = new QWidget;
    auto *layout = new QVBoxLayout(m_connectedPage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    auto *toolbar = new QWidget;
    toolbar->setObjectName(QStringLiteral("documentToolbar"));
    auto *tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(8, 6, 8, 6);
    m_databaseBox = new QComboBox;
    m_databaseBox->setMinimumWidth(220);
    m_databaseBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_databaseBox->setToolTip(tr("Choose Database"));
    connect(m_databaseBox, &QComboBox::activated, this, &SADatabaseDocument::databaseSelected);
    tb->addWidget(m_databaseBox);
    auto *refresh = new QToolButton;
    refresh->setIcon(SAIcons::icon(SAIcons::Glyph::Refresh));
    refresh->setToolTip(tr("Refresh Databases"));
    refresh->setAutoRaise(true);
    connect(refresh, &QToolButton::clicked, this, &SADatabaseDocument::refreshDatabases);
    tb->addWidget(refresh);
    tb->addSpacing(16);

    m_viewButtons = new QButtonGroup(this);
    auto *viewSwitchBar = new QWidget;
    viewSwitchBar->setObjectName(QStringLiteral("viewSwitchBar"));
    auto *viewSwitchLayout = new QHBoxLayout(viewSwitchBar);
    viewSwitchLayout->setContentsMargins(0, 0, 0, 0);
    viewSwitchLayout->setSpacing(0);
    const QColor accentText = palette().color(QPalette::HighlightedText);
    const QColor offTint = palette().color(QPalette::WindowText);
    struct ViewDef { const char *text; SAIcons::Glyph glyph; };
    const ViewDef defs[6] = {
        {"Structure", SAIcons::Glyph::Structure}, {"Content", SAIcons::Glyph::Content}, {"Relations", SAIcons::Glyph::Relations},
        {"Triggers", SAIcons::Glyph::Triggers}, {"Table Info", SAIcons::Glyph::Info}, {"Query", SAIcons::Glyph::Query},
    };
    for (int i = 0; i < 6; ++i) {
        auto *button = new QToolButton;
        button->setText(tr(defs[i].text));
        button->setIcon(SAIcons::toggleIcon(defs[i].glyph, offTint, accentText));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolTip(tr("%1 (Ctrl+%2)").arg(tr(defs[i].text)).arg(i + 1));
        m_viewButtons->addButton(button, i);
        viewSwitchLayout->addWidget(button);
    }
    connect(m_viewButtons, &QButtonGroup::idClicked, this, &SADatabaseDocument::showView);
    tb->addWidget(viewSwitchBar);
    tb->addStretch();

    m_busyIndicator = new QProgressBar;
    m_busyIndicator->setRange(0, 0);
    m_busyIndicator->setMaximumWidth(90);
    m_busyIndicator->setMaximumHeight(8);
    m_busyIndicator->setTextVisible(false);
    m_busyIndicator->setVisible(false);
    tb->addWidget(m_busyIndicator);
    m_stopButton = new QToolButton;
    m_stopButton->setIcon(SAIcons::icon(SAIcons::Glyph::Stop));
    m_stopButton->setToolTip(tr("Stop the running query"));
    m_stopButton->setAutoRaise(true);
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QToolButton::clicked, m_session, &SADatabaseSession::cancelCurrentQuery);
    tb->addWidget(m_stopButton);
    tb->addSpacing(8);
    m_serverLabel = new QLabel;
    m_serverLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    tb->addWidget(m_serverLabel);
    tb->addSpacing(8);
    auto *console = new QToolButton;
    console->setIcon(SAIcons::icon(SAIcons::Glyph::Console));
    console->setToolTip(tr("Show Console"));
    console->setAutoRaise(true);
    connect(console, &QToolButton::clicked, this, []() {
        SAConsoleWindow *c = SAConsoleWindow::shared();
        c->setVisible(!c->isVisible());
        if (c->isVisible()) { c->raise(); c->activateWindow(); }
    });
    tb->addWidget(console);
    auto *users = new QToolButton;
    users->setIcon(SAIcons::icon(SAIcons::Glyph::Users));
    users->setToolTip(tr("Server Processes"));
    users->setAutoRaise(true);
    connect(users, &QToolButton::clicked, this, &SADatabaseDocument::showProcessList);
    tb->addWidget(users);
    layout->addWidget(toolbar);

    // Body
    m_splitter = new QSplitter(Qt::Horizontal);
    m_tablesList = new SATablesList(this);
    connect(m_tablesList, &SATablesList::selectionChanged, this, &SADatabaseDocument::tablesSelectionChanged);
    connect(m_tablesList, &SATablesList::addRequested, this, &SADatabaseDocument::addTable);
    connect(m_tablesList, &SATablesList::removeRequested, this, &SADatabaseDocument::removeSelectedTables);
    connect(m_tablesList, &SATablesList::refreshRequested, this, &SADatabaseDocument::refreshTables);
    connect(m_tablesList, &SATablesList::renameRequested, this, &SADatabaseDocument::renameSelectedTable);
    connect(m_tablesList, &SATablesList::duplicateRequested, this, &SADatabaseDocument::duplicateSelectedTable);
    connect(m_tablesList, &SATablesList::truncateRequested, this, &SADatabaseDocument::truncateSelectedTables);
    connect(m_tablesList, &SATablesList::copyCreateRequested, this, &SADatabaseDocument::copyCreateSyntax);
    connect(m_tablesList, &SATablesList::showCreateRequested, this, &SADatabaseDocument::showCreateSyntax);
    connect(m_tablesList, &SATablesList::maintenanceRequested, this, &SADatabaseDocument::tableMaintenance);
    connect(m_tablesList, &SATablesList::exportRequested, this, &SADatabaseDocument::exportTables);
    m_splitter->addWidget(m_tablesList);

    m_views = new QStackedWidget;
    m_structureView = new SATableStructureView(this);
    m_contentView = new SATableContentView(this);
    m_relationsView = new SATableRelationsView(this);
    m_triggersView = new SATableTriggersView(this);
    m_infoView = new SATableInfoView(this);
    m_queryView = new SACustomQueryView(this);
    m_views->addWidget(m_structureView);
    m_views->addWidget(m_contentView);
    m_views->addWidget(m_relationsView);
    m_views->addWidget(m_triggersView);
    m_views->addWidget(m_infoView);
    m_views->addWidget(m_queryView);
    m_splitter->addWidget(m_views);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({240, 900});
    layout->addWidget(m_splitter, 1);
    m_pages->addWidget(m_connectedPage);

    const int defaultView = qBound(0, SAPreferences::instance().intFor(QStringLiteral("DefaultViewMode")), 5);
    m_viewButtons->button(defaultView == 0 ? 1 : defaultView)->setChecked(true);
    m_views->setCurrentIndex(defaultView == 0 ? 1 : defaultView);
}

void SADatabaseDocument::setBusy(bool busy)
{
    if (!m_busyIndicator) return;
    m_busyIndicator->setVisible(busy);
    m_stopButton->setEnabled(busy);
}

void SADatabaseDocument::updateTitle()
{
    Q_EMIT titleChanged();
}

// ---- databases ----------------------------------------------------------------------------------

void SADatabaseDocument::loadDatabases(const QString &select)
{
    m_loadingDatabases = true;
    m_session->query(SASchema::showDatabases(), [this, select](const SAResult &r) {
        m_loadingDatabases = false;
        if (!r.ok) {
            if (r.errorNumber == 1227 || r.errorMessage.contains(QLatin1String("skip-show-database"))) {
                SADialogs::warning(this, tr("Databases cannot be listed"),
                    tr("The skip-show-database variable of the database server is set to ON, so databases cannot be listed without the SHOW DATABASES privilege. Databases are still accessible directly through SQL queries depending on your privileges."));
            } else {
                reportError(tr("Error"), tr("The databases could not be listed.\n\nMySQL said: %1").arg(r.errorMessage));
            }
        }
        m_databases.clear();
        m_systemDatabases.clear();
        const QStringList system = SASchema::systemDatabases();
        for (int i = 0; i < r.rowCount(); ++i) {
            const QString name = r.stringAt(i, 0);
            if (system.contains(name)) m_systemDatabases << name;
            else m_databases << name;
        }
        m_databases.sort(Qt::CaseInsensitive);
        m_systemDatabases.sort(Qt::CaseInsensitive);

        const QSignalBlocker blocker(m_databaseBox);
        m_databaseBox->clear();
        m_databaseBox->addItem(tr("Choose Database…"), QString());
        if (!m_databases.isEmpty()) m_databaseBox->insertSeparator(m_databaseBox->count());
        for (const QString &db : m_databases) m_databaseBox->addItem(SAIcons::icon(SAIcons::Glyph::Database), db, db);
        if (!m_systemDatabases.isEmpty()) m_databaseBox->insertSeparator(m_databaseBox->count());
        for (const QString &db : m_systemDatabases) m_databaseBox->addItem(SAIcons::icon(SAIcons::Glyph::SystemDatabase), db, db);

        const QString target = select.isEmpty() ? m_session->currentDatabase() : select;
        if (!target.isEmpty()) {
            int index = m_databaseBox->findData(target);
            if (index < 0) {
                // Not listed (no privilege to SHOW it) but selected: add it anyway.
                m_databaseBox->addItem(SAIcons::icon(SAIcons::Glyph::Database), target, target);
                index = m_databaseBox->count() - 1;
            }
            m_databaseBox->setCurrentIndex(index);
            if (m_session->currentDatabase() != target) {
                m_session->selectDatabase(target, [this](bool ok, const QString &error) {
                    if (!ok) reportError(tr("Error"), tr("Unable to select database.\n\nMySQL said: %1").arg(error));
                    loadTables();
                    updateTitle();
                });
                return;
            }
        } else {
            m_databaseBox->setCurrentIndex(0);
        }
        loadTables();
        updateTitle();
    });
}

void SADatabaseDocument::databaseSelected(int index)
{
    const QString db = m_databaseBox->itemData(index).toString();
    if (db.isEmpty() || db == m_session->currentDatabase()) return;
    if (m_contentView->hasPendingEdits() && !m_contentView->commitPendingEdits()) return;
    m_session->selectDatabase(db, [this, db](bool ok, const QString &error) {
        if (!ok) {
            reportError(tr("Error"), tr("Unable to select database “%1”.\n\nMySQL said: %2").arg(db, error));
            const QSignalBlocker blocker(m_databaseBox);
            m_databaseBox->setCurrentIndex(qMax(0, m_databaseBox->findData(m_session->currentDatabase())));
            return;
        }
        m_selectedTable.clear();
        m_selectedTableType = SASchema::ObjectType::None;
        loadTables();
        loadSchemaMetadata();
        updateTitle();
        pushHistory();
        Q_EMIT stateChanged();
    });
}

void SADatabaseDocument::chooseDatabase()
{
    SAGotoDatabaseDialog dialog(this, m_databases + m_systemDatabases, currentDatabase());
    if (dialog.exec() != QDialog::Accepted || dialog.selectedDatabase().isEmpty()) return;
    const int index = m_databaseBox->findData(dialog.selectedDatabase());
    if (index >= 0) {
        m_databaseBox->setCurrentIndex(index);
        databaseSelected(index);
    }
}

void SADatabaseDocument::refreshDatabases()
{
    loadDatabases(currentDatabase());
}

void SADatabaseDocument::loadSchemaMetadata()
{
    const QStringList statements{SASchema::characterSets(), SASchema::collations(), SASchema::storageEngines(),
                                 QStringLiteral("SELECT @@default_storage_engine"),
                                 currentDatabase().isEmpty() ? QStringLiteral("SELECT @@character_set_server") : QStringLiteral("SELECT @@character_set_database")};
    m_session->queryBatch(statements, [this](const QVector<SAResult> &results) {
        if (results.size() < 5) return;
        m_charsets = SADialogs::CharsetCollation();
        for (int i = 0; i < results[0].rowCount(); ++i) {
            const QString cs = results[0].stringAt(i, 0);
            m_charsets.charsets << cs;
            m_charsets.defaultCollation.insert(cs, results[0].stringAt(i, 1));
        }
        for (int i = 0; i < results[1].rowCount(); ++i) m_charsets.collations[results[1].stringAt(i, 1)] << results[1].stringAt(i, 0);
        m_engines.clear();
        for (int i = 0; i < results[2].rowCount(); ++i) m_engines << results[2].stringAt(i, 0);
        m_defaultEngine = results[3].firstValue();
        m_databaseCharset = results[4].firstValue();
    }, SADatabaseSession::Silent);
}

void SADatabaseDocument::addDatabase()
{
    SADatabaseDialog dialog(this, m_charsets);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString name = dialog.databaseName();
    m_session->query(SASchema::createDatabase(name, dialog.charset(), dialog.collation()), [this, name](const SAResult &r) {
        if (!r.ok) { reportError(tr("Error"), tr("Couldn't create database.\n\nMySQL said: %1").arg(r.errorMessage)); return; }
        loadDatabases(name);
    });
}

void SADatabaseDocument::deleteDatabase()
{
    const QString db = currentDatabase();
    if (db.isEmpty()) return;
    if (!SADialogs::confirm(this, tr("Delete database “%1”?").arg(db), tr("Are you sure you want to delete the database “%1”? This operation cannot be undone.").arg(db), tr("Delete"), QString(), true)) return;
    m_session->query(SASchema::dropDatabase(db), [this](const SAResult &r) {
        if (!r.ok) { reportError(tr("Error"), tr("Couldn't delete database.\n\nMySQL said: %1").arg(r.errorMessage)); return; }
        m_selectedTable.clear();
        m_tables.clear();
        m_tablesList->clear();
        loadDatabases(QString());
    });
}

void SADatabaseDocument::duplicateDatabase()
{
    const QString db = currentDatabase();
    if (db.isEmpty()) return;
    bool ok = false;
    const QString target = SADialogs::askText(this, tr("Duplicate Database"), tr("Name of the new database:"), db + QStringLiteral("_copy"), &ok);
    if (!ok || target.trimmed().isEmpty()) return;
    const bool copyContent = SADialogs::confirm(this, tr("Copy table contents?"), tr("Copy the table contents as well as the structure?"), tr("Copy Structure and Content"), tr("Structure Only"));
    // Structure first, then data. Views and routines are not copied (same as the macOS app).
    QStringList statements{SASchema::createDatabase(target.trimmed(), QString(), QString())};
    for (const SASchema::ObjectEntry &e : m_tables) {
        if (e.type != SASchema::ObjectType::Table) continue;
        const QString src = SADatabaseSession::quoteIdentifier(db) + QLatin1Char('.') + SADatabaseSession::quoteIdentifier(e.name);
        const QString dst = SADatabaseSession::quoteIdentifier(target.trimmed()) + QLatin1Char('.') + SADatabaseSession::quoteIdentifier(e.name);
        statements << QStringLiteral("CREATE TABLE %1 LIKE %2").arg(dst, src);
        if (copyContent) statements << QStringLiteral("INSERT INTO %1 SELECT * FROM %2").arg(dst, src);
    }
    m_session->queryBatch(statements, [this, target](const QVector<SAResult> &results) {
        for (const SAResult &r : results)
            if (!r.ok) { reportError(tr("Error"), tr("Couldn't duplicate database.\n\nMySQL said: %1").arg(r.errorMessage)); break; }
        loadDatabases(target.trimmed());
    }, SADatabaseSession::NoFlags, true);
}

void SADatabaseDocument::renameDatabase()
{
    const QString db = currentDatabase();
    if (db.isEmpty()) return;
    bool ok = false;
    const QString target = SADialogs::askText(this, tr("Rename Database"), tr("New name for “%1”:\n(Tables are moved with RENAME TABLE; views and routines stay behind.)").arg(db), db, &ok);
    if (!ok || target.trimmed().isEmpty() || target.trimmed() == db) return;
    QStringList statements{SASchema::createDatabase(target.trimmed(), QString(), QString())};
    QStringList renames;
    for (const SASchema::ObjectEntry &e : m_tables) {
        if (e.type != SASchema::ObjectType::Table) continue;
        renames << QStringLiteral("%1.%2 TO %3.%2").arg(SADatabaseSession::quoteIdentifier(db), SADatabaseSession::quoteIdentifier(e.name), SADatabaseSession::quoteIdentifier(target.trimmed()));
    }
    if (!renames.isEmpty()) statements << QStringLiteral("RENAME TABLE %1").arg(renames.join(QStringLiteral(", ")));
    const bool onlyTables = std::all_of(m_tables.cbegin(), m_tables.cend(), [](const SASchema::ObjectEntry &e) { return e.type == SASchema::ObjectType::Table; });
    if (onlyTables) statements << SASchema::dropDatabase(db);
    m_session->queryBatch(statements, [this, target](const QVector<SAResult> &results) {
        for (const SAResult &r : results)
            if (!r.ok) { reportError(tr("Error"), tr("Couldn't rename database.\n\nMySQL said: %1").arg(r.errorMessage)); break; }
        loadDatabases(target.trimmed());
    }, SADatabaseSession::NoFlags, true);
}

void SADatabaseDocument::alterDatabase()
{
    const QString db = currentDatabase();
    if (db.isEmpty()) return;
    m_session->queryBatch({QStringLiteral("SELECT DEFAULT_CHARACTER_SET_NAME, DEFAULT_COLLATION_NAME FROM information_schema.SCHEMATA WHERE SCHEMA_NAME = %1").arg(escape(db))},
        [this, db](const QVector<SAResult> &results) {
            const QString cs = results.isEmpty() ? QString() : results[0].stringAt(0, 0);
            const QString coll = results.isEmpty() ? QString() : results[0].stringAt(0, 1);
            SADatabaseDialog dialog(this, m_charsets, db, cs, coll);
            if (dialog.exec() != QDialog::Accepted) return;
            m_session->query(SASchema::alterDatabase(db, dialog.charset(), dialog.collation()), [this](const SAResult &r) {
                if (!r.ok) reportError(tr("Error"), tr("Couldn't alter database.\n\nMySQL said: %1").arg(r.errorMessage));
                loadSchemaMetadata();
            });
        }, SADatabaseSession::Silent);
}

void SADatabaseDocument::flushPrivileges()
{
    m_session->query(QStringLiteral("FLUSH PRIVILEGES"), [this](const SAResult &r) {
        if (!r.ok) reportError(tr("Error"), tr("Couldn't flush privileges.\n\nMySQL said: %1").arg(r.errorMessage));
        else SADialogs::information(this, tr("Flushed Privileges"), tr("Successfully flushed privileges."));
    });
}

void SADatabaseDocument::showServerVariables()
{
    auto *dialog = new SAServerVariablesDialog(this, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void SADatabaseDocument::showProcessList()
{
    auto *dialog = new SAProcessListDialog(this, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void SADatabaseDocument::showUserManager()
{
    SAUserManagerDialog dialog(this, window());
    dialog.exec();
}

// ---- tables -----------------------------------------------------------------------------------------

void SADatabaseDocument::loadTables(const QString &reselect)
{
    const QString db = currentDatabase();
    const QString previous = reselect.isEmpty() ? m_selectedTable : reselect;
    if (db.isEmpty()) {
        m_tables.clear();
        m_tablesList->clear();
        m_queryView->updateCompletion({});
        return;
    }
    const bool showComments = SAPreferences::instance().boolFor(SAPreferences::DisplayCommentsInTablesList);
    m_session->queryBatch({showComments ? SASchema::showTableStatus() : SASchema::showFullTables(), SASchema::routinesForDatabase(db, escaper())},
        [this, previous](const QVector<SAResult> &results) {
            if (results.isEmpty()) return;
            if (!results[0].ok) reportError(tr("Error"), tr("The tables could not be listed.\n\nMySQL said: %1").arg(results[0].errorMessage));
            m_tables = SASchema::parseTables(results[0]);
            if (results.size() > 1 && results[1].ok) m_tables += SASchema::parseRoutines(results[1]);
            m_tablesList->setEntries(m_tables);
            m_queryView->updateCompletion(tableNames(true));
            if (!previous.isEmpty()) m_tablesList->selectName(previous);
            if (m_tablesList->selectedName().isEmpty()) tablesSelectionChanged();
        });
}

void SADatabaseDocument::refreshTables()
{
    loadTables();
    loadSchemaMetadata();
}

void SADatabaseDocument::tablesSelectionChanged()
{
    if (m_contentView->hasPendingEdits() && !m_contentView->commitPendingEdits()) {
        m_tablesList->selectName(m_selectedTable);
        return;
    }
    const QString name = m_tablesList->selectedName();
    const SASchema::ObjectType type = m_tablesList->selectedType();
    if (name == m_selectedTable && type == m_selectedTableType && !name.isEmpty()) return;
    m_selectedTable = name;
    m_selectedTableType = type;
    for (int i = 0; i < 5; ++i) m_viewLoaded[i] = false;
    if (name.isEmpty()) {
        m_structureView->clear();
        m_contentView->clear();
        m_relationsView->clear();
        m_triggersView->clear();
        m_infoView->clear();
        m_tablesList->setInfoHtml(QString());
    } else {
        loadCurrentViewIfNeeded();
        updateTableInfoPanel();
        pushHistory();
    }
    Q_EMIT stateChanged();
}

void SADatabaseDocument::selectTable(const QString &name)
{
    m_tablesList->selectName(name);
}

void SADatabaseDocument::loadCurrentViewIfNeeded()
{
    const int index = m_views->currentIndex();
    if (index < 0 || index > 4 || m_viewLoaded[index]) return;
    if (m_selectedTable.isEmpty()) return;
    m_viewLoaded[index] = true;
    switch (index) {
    case 0: m_structureView->loadTable(m_selectedTable, m_selectedTableType); break;
    case 1: m_contentView->loadTable(m_selectedTable, m_selectedTableType); break;
    case 2: m_relationsView->loadTable(m_selectedTable, m_selectedTableType); break;
    case 3: m_triggersView->loadTable(m_selectedTable, m_selectedTableType); break;
    case 4: m_infoView->loadTable(m_selectedTable, m_selectedTableType); break;
    default: break;
    }
}

void SADatabaseDocument::showView(int index)
{
    if (!m_views || index < 0 || index > 5) return;
    if (m_views->currentIndex() == 1 && index != 1 && m_contentView->hasPendingEdits() && !m_contentView->commitPendingEdits()) {
        m_viewButtons->button(1)->setChecked(true);
        return;
    }
    m_views->setCurrentIndex(index);
    m_viewButtons->button(index)->setChecked(true);
    if (index == 5) m_queryView->focusEditor();
    loadCurrentViewIfNeeded();
    pushHistory();
    Q_EMIT stateChanged();
}

void SADatabaseDocument::toggleTablesList()
{
    if (m_tablesList) m_tablesList->setVisible(!m_tablesList->isVisible());
    Q_EMIT stateChanged();
}

void SADatabaseDocument::tableStructureChanged()
{
    m_viewLoaded[1] = false;
    m_viewLoaded[2] = false;
    m_viewLoaded[4] = false;
    updateTableInfoPanel();
    loadCurrentViewIfNeeded();
}

void SADatabaseDocument::tableContentChanged()
{
    m_viewLoaded[4] = false;
    updateTableInfoPanel();
}

void SADatabaseDocument::updateTableInfoPanel()
{
    if (m_selectedTable.isEmpty()) { m_tablesList->setInfoHtml(QString()); return; }
    const QString table = m_selectedTable;
    if (m_selectedTableType == SASchema::ObjectType::Procedure || m_selectedTableType == SASchema::ObjectType::Function) {
        m_session->query(SASchema::routineDefinition(m_selectedTableType, currentDatabase(), table, escaper()), [this, table](const SAResult &r) {
            if (m_selectedTable != table) return;
            if (!r.ok || !r.rowCount()) { m_tablesList->setInfoHtml(QString()); return; }
            const QMap<QString, QString> row = r.rowAsMap(0);
            QStringList lines{QStringLiteral("<b>%1</b>").arg(table.toHtmlEscaped())};
            lines << tr("type: %1").arg(row.value(QStringLiteral("ROUTINE_TYPE")).toLower());
            if (!row.value(QStringLiteral("DTD_IDENTIFIER")).isEmpty()) lines << tr("returns: %1").arg(row.value(QStringLiteral("DTD_IDENTIFIER")).toHtmlEscaped());
            lines << tr("definer: %1").arg(row.value(QStringLiteral("DEFINER")).toHtmlEscaped());
            lines << tr("created: %1").arg(row.value(QStringLiteral("CREATED")));
            lines << tr("security: %1").arg(row.value(QStringLiteral("SECURITY_TYPE")).toLower());
            m_tablesList->setInfoHtml(lines.join(QStringLiteral("<br>")));
        }, SADatabaseSession::Silent);
        return;
    }
    m_session->query(SASchema::tableStatusLike(table, escaper()), [this, table](const SAResult &r) {
        if (m_selectedTable != table) return;
        if (!r.ok || !r.rowCount()) { m_tablesList->setInfoHtml(QStringLiteral("<b>%1</b>").arg(table.toHtmlEscaped())); return; }
        const QMap<QString, QString> row = r.rowAsMap(0);
        QStringList lines{QStringLiteral("<b>%1</b>").arg(table.toHtmlEscaped())};
        if (m_selectedTableType == SASchema::ObjectType::View) {
            lines << tr("view");
        } else {
            if (!row.value(QStringLiteral("Create_time")).isEmpty()) lines << tr("created: %1").arg(row.value(QStringLiteral("Create_time")));
            if (!row.value(QStringLiteral("Update_time")).isEmpty()) lines << tr("updated: %1").arg(row.value(QStringLiteral("Update_time")));
            if (!row.value(QStringLiteral("Engine")).isEmpty()) lines << tr("engine: %1").arg(row.value(QStringLiteral("Engine")));
            if (!row.value(QStringLiteral("Rows")).isEmpty()) {
                const bool exact = row.value(QStringLiteral("Engine")).compare(QLatin1String("MyISAM"), Qt::CaseInsensitive) == 0;
                lines << (exact ? tr("rows: %1") : tr("rows: ~%1")).arg(QLocale().toString(row.value(QStringLiteral("Rows")).toLongLong()));
            }
            if (!row.value(QStringLiteral("Data_length")).isEmpty()) lines << tr("size: %1").arg(QLocale().formattedDataSize(row.value(QStringLiteral("Data_length")).toLongLong() + row.value(QStringLiteral("Index_length")).toLongLong()));
            if (!row.value(QStringLiteral("Auto_increment")).isEmpty()) lines << tr("auto_increment: %1").arg(row.value(QStringLiteral("Auto_increment")));
            if (!row.value(QStringLiteral("Collation")).isEmpty()) lines << tr("collation: %1").arg(row.value(QStringLiteral("Collation")));
            if (!row.value(QStringLiteral("Comment")).isEmpty()) lines << tr("comment: %1").arg(row.value(QStringLiteral("Comment")).toHtmlEscaped());
        }
        m_tablesList->setInfoHtml(lines.join(QStringLiteral("<br>")));
    }, SADatabaseSession::Silent);
}

void SADatabaseDocument::addTable()
{
    if (currentDatabase().isEmpty()) { reportError(tr("No database selected"), tr("Please select a database before adding a table.")); return; }
    SATableDialog dialog(this, m_engines, m_defaultEngine, m_charsets, m_databaseCharset);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString name = dialog.tableName();
    m_session->query(SASchema::createTable(name, dialog.engine(), dialog.charset(), dialog.collation()), [this, name](const SAResult &r) {
        if (!r.ok) { reportError(tr("Error adding table"), tr("An error occurred while trying to add the table “%1”.\n\nMySQL said: %2").arg(name, r.errorMessage)); return; }
        loadTables(name);
        showView(0);
    });
}

void SADatabaseDocument::removeSelectedTables()
{
    const QVector<SASchema::ObjectEntry> entries = m_tablesList->selectedEntries();
    if (entries.isEmpty()) return;
    QString text = entries.size() == 1
        ? tr("Are you sure you want to delete the %1 “%2”? This operation cannot be undone.").arg(SASchema::objectTypeName(entries[0].type).toLower(), entries[0].name)
        : tr("Are you sure you want to delete the selected %n items? This operation cannot be undone.", nullptr, entries.size());
    if (!SADialogs::confirm(this, tr("Delete"), text, tr("Delete"), QString(), true)) return;
    QStringList statements;
    bool hasTables = false;
    for (const SASchema::ObjectEntry &e : entries) {
        statements << SASchema::dropObject(e.type, e.name);
        if (e.type == SASchema::ObjectType::Table) hasTables = true;
    }
    if (hasTables) {
        statements.prepend(QStringLiteral("SET FOREIGN_KEY_CHECKS = 0"));
        statements.append(QStringLiteral("SET FOREIGN_KEY_CHECKS = 1"));
    }
    m_session->queryBatch(statements, [this](const QVector<SAResult> &results) {
        QStringList errors;
        for (const SAResult &r : results) if (!r.ok) errors << r.errorMessage;
        if (!errors.isEmpty()) reportError(tr("Error"), tr("Some items could not be deleted.\n\nMySQL said: %1").arg(errors.join(QLatin1Char('\n'))));
        m_selectedTable.clear();
        loadTables();
    });
}

void SADatabaseDocument::renameSelectedTable()
{
    if (m_selectedTable.isEmpty()) return;
    if (m_selectedTableType != SASchema::ObjectType::Table && m_selectedTableType != SASchema::ObjectType::View) {
        reportError(tr("Rename"), tr("Procedures and functions cannot be renamed directly; re-create them with a new name in the Query view."));
        return;
    }
    bool ok = false;
    const QString target = SADialogs::askText(this, tr("Rename"), tr("New name for “%1”:").arg(m_selectedTable), m_selectedTable, &ok);
    if (!ok || target.trimmed().isEmpty() || target.trimmed() == m_selectedTable) return;
    m_session->query(SASchema::renameObject(m_selectedTableType, m_selectedTable, target.trimmed()), [this, target](const SAResult &r) {
        if (!r.ok) { reportError(tr("Error"), tr("Couldn't rename.\n\nMySQL said: %1").arg(r.errorMessage)); return; }
        loadTables(target.trimmed());
    });
}

void SADatabaseDocument::duplicateSelectedTable()
{
    if (m_selectedTable.isEmpty() || m_selectedTableType != SASchema::ObjectType::Table) return;
    bool ok = false;
    const QString target = SADialogs::askText(this, tr("Duplicate Table"), tr("Name of the copy:"), m_selectedTable + QStringLiteral("_copy"), &ok);
    if (!ok || target.trimmed().isEmpty()) return;
    const bool copyContent = SADialogs::confirm(this, tr("Copy table content?"), tr("Copy the rows of “%1” into the new table as well?").arg(m_selectedTable), tr("Copy Structure and Content"), tr("Structure Only"));
    QStringList statements{SASchema::duplicateTable(m_selectedTable, target.trimmed(), false)};
    if (copyContent) statements << QStringLiteral("INSERT INTO %1 SELECT * FROM %2").arg(SADatabaseSession::quoteIdentifier(target.trimmed()), SADatabaseSession::quoteIdentifier(m_selectedTable));
    m_session->queryBatch(statements, [this, target](const QVector<SAResult> &results) {
        for (const SAResult &r : results)
            if (!r.ok) { reportError(tr("Error"), tr("Couldn't duplicate table.\n\nMySQL said: %1").arg(r.errorMessage)); break; }
        loadTables(target.trimmed());
    }, SADatabaseSession::NoFlags, true);
}

void SADatabaseDocument::truncateSelectedTables()
{
    const QVector<SASchema::ObjectEntry> entries = m_tablesList->selectedEntries();
    QStringList tables;
    for (const SASchema::ObjectEntry &e : entries) if (e.type == SASchema::ObjectType::Table) tables << e.name;
    if (tables.isEmpty()) return;
    if (!SADialogs::confirm(this, tr("Truncate"), tables.size() == 1 ? tr("Are you sure you want to delete ALL records in the table “%1”? This operation cannot be undone.").arg(tables.first())
                                                                     : tr("Are you sure you want to delete ALL records in the %n selected tables? This operation cannot be undone.", nullptr, tables.size()),
                            tr("Truncate"), QString(), true)) return;
    QStringList statements;
    for (const QString &t : tables) statements << SASchema::truncateTable(t);
    m_session->queryBatch(statements, [this](const QVector<SAResult> &results) {
        for (const SAResult &r : results)
            if (!r.ok) { reportError(tr("Error"), tr("Couldn't truncate table.\n\nMySQL said: %1").arg(r.errorMessage)); break; }
        m_viewLoaded[1] = false;
        tableContentChanged();
        loadCurrentViewIfNeeded();
    });
}

void SADatabaseDocument::createSyntaxForSelected(std::function<void(const QString &)> done)
{
    if (m_selectedTable.isEmpty()) return;
    m_session->query(SASchema::showCreate(m_selectedTableType, m_selectedTable), [this, done](const SAResult &r) {
        if (!r.ok || !r.rowCount()) { reportError(tr("Error"), tr("Couldn't retrieve the CREATE syntax.\n\nMySQL said: %1").arg(r.errorMessage)); return; }
        // SHOW CREATE returns the statement in the second column for tables/views, third for routines.
        int col = 1;
        for (int i = 0; i < r.fieldCount(); ++i)
            if (r.fields.at(i).name.startsWith(QLatin1String("Create "))) col = i;
        done(r.stringAt(0, col));
    });
}

void SADatabaseDocument::copyCreateSyntax()
{
    createSyntaxForSelected([](const QString &syntax) { QApplication::clipboard()->setText(syntax + QStringLiteral(";\n")); });
}

void SADatabaseDocument::showCreateSyntax()
{
    createSyntaxForSelected([this](const QString &syntax) {
        auto *dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(tr("Create syntax for %1").arg(m_selectedTable));
        auto *text = new QPlainTextEdit(syntax + QStringLiteral(";"));
        text->setReadOnly(true);
        text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        auto *copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
        connect(copy, &QPushButton::clicked, this, [text]() { QApplication::clipboard()->setText(text->toPlainText()); });
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        auto *layout = new QVBoxLayout(dialog);
        layout->addWidget(text);
        layout->addWidget(buttons);
        dialog->resize(720, 480);
        dialog->show();
    });
}

void SADatabaseDocument::tableMaintenance(const QString &command)
{
    const QVector<SASchema::ObjectEntry> entries = m_tablesList->selectedEntries();
    QStringList tables;
    for (const SASchema::ObjectEntry &e : entries) if (e.type == SASchema::ObjectType::Table) tables << e.name;
    if (tables.isEmpty()) return;
    m_session->query(SASchema::maintenance(command, tables), [this, command](const SAResult &r) {
        if (!r.ok) { reportError(tr("Error"), tr("%1 failed.\n\nMySQL said: %2").arg(command, r.errorMessage)); return; }
        QStringList lines;
        for (int i = 0; i < r.rowCount(); ++i) {
            QStringList cells;
            for (int c = 0; c < r.fieldCount(); ++c) cells << QStringLiteral("%1: %2").arg(r.fields.at(c).name, r.stringAt(i, c));
            lines << cells.join(QStringLiteral(", "));
        }
        SADialogs::information(this, tr("%1 result").arg(command.at(0) + command.mid(1).toLower()), lines.join(QLatin1Char('\n')));
        if (command == QLatin1String("OPTIMIZE") || command == QLatin1String("REPAIR")) tableContentChanged();
    });
}

void SADatabaseDocument::exportTables()
{
    QStringList preselected;
    for (const SASchema::ObjectEntry &e : m_tablesList->selectedEntries()) preselected << e.name;
    auto *dialog = new SAExportDialog(this, preselected, SAExportDialog::SelectedTables, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void SADatabaseDocument::exportCurrentResult()
{
    if (!m_session->isConnected()) return;
    // The visible view decides the source; the dialog falls back to the table
    // list when that source has no rows behind it.
    SAExportDialog::Source source = SAExportDialog::SelectedTables;
    if (m_views->currentIndex() == 1) source = SAExportDialog::FilteredContent;
    else if (m_views->currentIndex() == 5) source = SAExportDialog::QueryResult;

    QStringList preselected;
    for (const SASchema::ObjectEntry &e : m_tablesList->selectedEntries()) preselected << e.name;
    auto *dialog = new SAExportDialog(this, preselected, source, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void SADatabaseDocument::explainCurrentQuery()
{
    if (!m_session->isConnected()) return;
    if (m_views->currentIndex() != 5) showView(5);
    m_queryView->explainCurrent();
}

// ---- files & favorites -------------------------------------------------------------------------------

void SADatabaseDocument::addConnectionToFavorites()
{
    if (isConnected()) {
        SAConnectionInfo info = m_info;
        info.id = -1;
        info.database = currentDatabase();
        m_connectionView->setInfo(info);
    }
    m_connectionView->addCurrentToFavorites();
}

void SADatabaseDocument::openFile(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.suffix().compare(QLatin1String("sql"), Qt::CaseInsensitive) == 0) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { reportError(tr("Open failed"), file.errorString()); return; }
        const QString sql = QString::fromUtf8(file.readAll());
        if (isConnected()) {
            m_queryView->setQueryText(sql);
            showView(5);
        } else {
            m_pendingQueryText = sql;
            m_connectionView->setStatusMessage(tr("%1 will open in the Query view once connected.").arg(fi.fileName()));
        }
        return;
    }
    QString error;
    const QVariantMap plist = SAPlist::readFile(path, &error).toMap();
    if (!error.isEmpty()) { reportError(tr("Open failed"), error); return; }
    const QVariantMap data = plist.value(QStringLiteral("data")).toMap();
    const QVariantMap conn = data.value(QStringLiteral("connection")).toMap();
    if (conn.isEmpty()) { reportError(tr("Open failed"), tr("The file does not contain a Sequel Ace connection.")); return; }
    SAConnectionInfo info;
    info.name = conn.value(QStringLiteral("name")).toString();
    info.host = conn.value(QStringLiteral("host")).toString();
    info.user = conn.value(QStringLiteral("user")).toString();
    info.database = conn.value(QStringLiteral("database")).toString();
    info.port = conn.value(QStringLiteral("port")).toString();
    info.socket = conn.value(QStringLiteral("socket")).toString();
    info.colorIndex = conn.value(QStringLiteral("colorIndex"), -1).toInt();
    info.password = conn.value(QStringLiteral("password")).toString();
    info.useSSL = conn.value(QStringLiteral("useSSL")).toBool();
    info.sshHost = conn.value(QStringLiteral("ssh_host")).toString();
    info.sshUser = conn.value(QStringLiteral("ssh_user")).toString();
    info.sshPort = conn.value(QStringLiteral("ssh_port")).toString();
    info.sshPassword = conn.value(QStringLiteral("ssh_password")).toString();
    info.sshKeyLocation = conn.value(QStringLiteral("ssh_keyLocation")).toString();
    info.sshKeyLocationEnabled = conn.value(QStringLiteral("ssh_keyLocationEnabled")).toBool();
    const QString type = conn.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("SPSocketConnection")) info.type = SAConnectionType::Socket;
    else if (type == QLatin1String("SPSSHTunnelConnection")) info.type = SAConnectionType::SSHTunnel;
    else info.type = SAConnectionType::TCPIP;
    if (isConnected()) {
        // Open in this tab only when idle; otherwise the main window gives us a fresh tab.
        return;
    }
    m_connectionView->selectQuickConnect();
    m_connectionView->setInfo(info);
    m_connectionView->setStatusMessage(tr("Loaded %1.").arg(fi.fileName()));
    if (data.value(QStringLiteral("auto_connect")).toBool()) connectWithInfo(info);
}

void SADatabaseDocument::saveConnectionFile()
{
    const SAConnectionInfo info = isConnected() ? m_info : m_connectionView->currentInfo();
    const QString suggested = QDir::homePath() + QLatin1Char('/') + (info.displayName().isEmpty() ? QStringLiteral("connection") : info.displayName()) + QStringLiteral(".spf");
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Connection"), suggested, tr("Sequel Ace connection (*.spf)"));
    if (path.isEmpty()) return;
    const bool savePassword = !info.password.isEmpty() && SADialogs::confirm(this, tr("Save password?"), tr("Store the MySQL password in the file? It is saved unencrypted."), tr("Save Password"), tr("Don't Save"));
    QVariantMap conn;
    conn.insert(QStringLiteral("name"), info.name);
    conn.insert(QStringLiteral("host"), info.host);
    conn.insert(QStringLiteral("user"), info.user);
    conn.insert(QStringLiteral("database"), isConnected() ? currentDatabase() : info.database);
    conn.insert(QStringLiteral("port"), info.port);
    conn.insert(QStringLiteral("socket"), info.socket);
    conn.insert(QStringLiteral("colorIndex"), info.colorIndex);
    conn.insert(QStringLiteral("useSSL"), info.useSSL);
    conn.insert(QStringLiteral("rdbms_type"), QStringLiteral("mysql"));
    conn.insert(QStringLiteral("type"), info.type == SAConnectionType::Socket ? QStringLiteral("SPSocketConnection")
                                        : info.type == SAConnectionType::SSHTunnel ? QStringLiteral("SPSSHTunnelConnection") : QStringLiteral("SPTCPIPConnection"));
    if (info.type == SAConnectionType::SSHTunnel) {
        conn.insert(QStringLiteral("ssh_host"), info.sshHost);
        conn.insert(QStringLiteral("ssh_user"), info.sshUser);
        conn.insert(QStringLiteral("ssh_port"), info.sshPort);
        conn.insert(QStringLiteral("ssh_keyLocationEnabled"), info.sshKeyLocationEnabled);
        conn.insert(QStringLiteral("ssh_keyLocation"), info.sshKeyLocation);
    }
    if (savePassword) conn.insert(QStringLiteral("password"), info.password);
    QVariantMap data;
    data.insert(QStringLiteral("connection"), conn);
    data.insert(QStringLiteral("auto_connect"), true);
    if (isConnected() && m_queryView) {
        QVariantMap session;
        session.insert(QStringLiteral("queries"), m_queryView->queryText());
        session.insert(QStringLiteral("table"), m_selectedTable);
        data.insert(QStringLiteral("session"), session);
    }
    QVariantMap root;
    root.insert(QStringLiteral("data"), data);
    root.insert(QStringLiteral("encrypted"), false);
    root.insert(QStringLiteral("format"), QStringLiteral("connection"));
    root.insert(QStringLiteral("rdbms_type"), QStringLiteral("mysql"));
    root.insert(QStringLiteral("version"), 1);
    QString error;
    if (!SAPlist::writeFile(path, root, &error)) reportError(tr("Save failed"), error);
}

void SADatabaseDocument::saveQueryFile()
{
    if (!m_queryView) return;
    m_queryView->saveQueryFile();
}

void SADatabaseDocument::copyWithColumnNames()
{
    if (!m_views) return;
    if (m_views->currentIndex() == 1) m_contentView->copyWithColumnNames();
    else if (m_views->currentIndex() == 5) m_queryView->copyWithColumnNames();
}

void SADatabaseDocument::copyAsSQLInsert()
{
    if (!m_views) return;
    if (m_views->currentIndex() == 1) m_contentView->copyAsSQLInsert();
    else if (m_views->currentIndex() == 5) m_queryView->copyAsSQLInsert();
}

void SADatabaseDocument::setSelectedCellsNull()
{
    if (m_views && m_views->currentIndex() == 1) m_contentView->setSelectedCellsNull();
}

void SADatabaseDocument::focusContentFilter()
{
    showView(1);
    m_contentView->focusFilter();
}

void SADatabaseDocument::focusTablesFilter()
{
    if (m_tablesList) m_tablesList->focusFilter();
}

void SADatabaseDocument::runQueryInEditor(const QString &sql, bool execute)
{
    showView(5);
    m_queryView->setQueryText(sql);
    if (execute) m_queryView->runAll();
}

// ---- history ----------------------------------------------------------------------------------------------

void SADatabaseDocument::pushHistory()
{
    if (m_navigatingHistory || !m_views) return;
    HistoryEntry entry{currentDatabase(), m_selectedTable, m_views->currentIndex()};
    if (m_historyIndex >= 0 && m_historyIndex < m_history.size()) {
        const HistoryEntry &last = m_history.at(m_historyIndex);
        if (last.database == entry.database && last.table == entry.table && last.view == entry.view) return;
    }
    m_history.resize(m_historyIndex + 1);
    m_history.append(entry);
    m_historyIndex = m_history.size() - 1;
    while (m_history.size() > 100) { m_history.removeFirst(); --m_historyIndex; }
}

void SADatabaseDocument::historyBack()
{
    if (m_historyIndex <= 0) return;
    m_navigatingHistory = true;
    const HistoryEntry entry = m_history.at(--m_historyIndex);
    auto apply = [this, entry]() {
        m_tablesList->selectName(entry.table);
        showView(entry.view);
        m_navigatingHistory = false;
    };
    if (entry.database != currentDatabase()) {
        m_session->selectDatabase(entry.database, [this, apply](bool, const QString &) { loadTables(); apply(); });
    } else {
        apply();
    }
}

void SADatabaseDocument::historyForward()
{
    if (m_historyIndex + 1 >= m_history.size()) return;
    m_navigatingHistory = true;
    const HistoryEntry entry = m_history.at(++m_historyIndex);
    auto apply = [this, entry]() {
        m_tablesList->selectName(entry.table);
        showView(entry.view);
        m_navigatingHistory = false;
    };
    if (entry.database != currentDatabase()) {
        m_session->selectDatabase(entry.database, [this, apply](bool, const QString &) { loadTables(); apply(); });
    } else {
        apply();
    }
}
