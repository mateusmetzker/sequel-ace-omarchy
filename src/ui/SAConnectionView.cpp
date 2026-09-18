//
//  SAConnectionView.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAConnectionView.h"
#include "SADialogs.h"
#include "SAFavoritesModel.h"
#include "SAIcons.h"
#include "SAPreferences.h"
#include "SASecretStore.h"

#include <QAction>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimeZone>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {
constexpr int QuickConnectRole = Qt::UserRole + 50;

class FavoritesFilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QModelIndex index = sourceModel()->index(row, 0, parent);
        if (QSortFilterProxyModel::filterAcceptsRow(row, parent)) return true;
        if (sourceModel()->data(index, SAFavoritesModel::HostDescriptionRole).toString().contains(filterRegularExpression())) return true;
        // Keep groups whose children match.
        for (int i = 0; i < sourceModel()->rowCount(index); ++i)
            if (filterAcceptsRow(i, index)) return true;
        return false;
    }
};
}

SAConnectionView::SAConnectionView(SAFavoritesStore *store, QWidget *parent)
    : QWidget(parent), m_store(store)
{
    m_model = new SAFavoritesModel(store, this);
    m_proxy = new FavoritesFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setRecursiveFilteringEnabled(true);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, &SAConnectionView::commitEdits);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *left = new QWidget;
    left->setObjectName(QStringLiteral("favoritesPanel"));
    buildFavoritesPane(left);
    auto *right = new QWidget;
    buildForm(right);
    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 640});
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    m_tree->expandAll();
    connect(m_model, &SAFavoritesModel::structureChanged, m_tree, &QTreeView::expandAll);

    // Restore the last used favorite.
    SAPreferences &prefs = SAPreferences::instance();
    const int lastId = prefs.intFor(SAPreferences::LastFavoriteId);
    if (prefs.boolFor(SAPreferences::SelectLastFavoriteUsed) && lastId >= 0 && store->findFavorite(lastId)) selectFavorite(lastId);
    else selectQuickConnect();
}

// ---- favorites pane ----------------------------------------------------------------

void SAConnectionView::buildFavoritesPane(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(8, 8, 8, 6);
    layout->setSpacing(6);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Filter favorites"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(SAIcons::icon(SAIcons::Glyph::Search, palette().color(QPalette::PlaceholderText)), QLineEdit::LeadingPosition);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_proxy->setFilterFixedString(text);
        m_tree->expandAll();
    });

    m_tree = new QTreeView;
    m_tree->setModel(m_proxy);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setUniformRowHeights(true);
    m_tree->setIconSize(QSize(15, 15));
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setIndentation(14);
    m_tree->setMouseTracking(true);   // for the QSS ::item:hover rule
    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SAConnectionView::favoriteSelectionChanged);
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!m_proxy->data(index, SAFavoritesModel::IsGroupRole).toBool()) Q_EMIT connectRequested(currentInfo());
    });
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &SAConnectionView::showFavoriteContextMenu);

    // A single-row list styled identically to the tree below (see the shared
    // #favoritesPanel selectors), so it reads as a pinned first row of the
    // same source list rather than a separate toolbar button.
    auto *quickConnect = new QListWidget;
    quickConnect->setObjectName(QStringLiteral("quickConnectRow"));
    quickConnect->addItem(new QListWidgetItem(SAIcons::icon(SAIcons::Glyph::QuickConnect), tr("Quick Connect")));
    quickConnect->setFrameShape(QFrame::NoFrame);
    quickConnect->setIconSize(QSize(15, 15));
    quickConnect->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    quickConnect->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    quickConnect->setFocusPolicy(Qt::NoFocus);
    quickConnect->setFixedHeight(fontMetrics().height() + 18);
    connect(quickConnect, &QListWidget::itemClicked, this, &SAConnectionView::selectQuickConnect);

    auto *buttons = new QHBoxLayout;
    auto *add = new QToolButton;
    add->setIcon(SAIcons::icon(SAIcons::Glyph::Add));
    add->setToolTip(tr("Add a new favorite or group"));
    auto *addMenu = new QMenu(add);
    addMenu->addAction(SAIcons::icon(SAIcons::Glyph::Add), tr("New Favorite"), this, &SAConnectionView::addCurrentToFavorites);
    addMenu->addAction(SAIcons::icon(SAIcons::Glyph::AddFolder), tr("New Group"), this, &SAConnectionView::addGroup);
    add->setMenu(addMenu);
    add->setPopupMode(QToolButton::InstantPopup);
    auto *remove = new QToolButton;
    remove->setIcon(SAIcons::icon(SAIcons::Glyph::Remove));
    remove->setToolTip(tr("Remove the selected favorite or group"));
    connect(remove, &QToolButton::clicked, this, &SAConnectionView::removeSelected);
    auto *gear = new QToolButton;
    gear->setIcon(SAIcons::icon(SAIcons::Glyph::Gear));
    gear->setPopupMode(QToolButton::InstantPopup);
    auto *gearMenu = new QMenu(gear);
    gearMenu->addAction(tr("Duplicate Favorite"), this, &SAConnectionView::duplicateSelectedFavorite);
    gearMenu->addSeparator();
    gearMenu->addAction(tr("Import Favorites…"), this, &SAConnectionView::importFavorites);
    gearMenu->addAction(tr("Export Favorites…"), this, &SAConnectionView::exportFavorites);
    gear->setMenu(gearMenu);
    for (QToolButton *b : {add, remove, gear}) b->setAutoRaise(true);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    buttons->addWidget(gear);

    layout->addWidget(m_search);
    layout->addWidget(quickConnect);
    layout->addWidget(m_tree, 1);
    layout->addLayout(buttons);
}

// ---- form ------------------------------------------------------------------------------

QLineEdit *SAConnectionView::filePicker(QWidget *parent, QCheckBox **enabledBox, const QString &caption)
{
    auto *row = new QWidget(parent);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto *box = new QCheckBox;
    auto *edit = new QLineEdit;
    edit->setPlaceholderText(tr("No file selected"));
    auto *browse = new QToolButton;
    browse->setText(tr("…"));
    connect(browse, &QToolButton::clicked, this, [this, edit, caption, box]() {
        const QString path = QFileDialog::getOpenFileName(this, caption, edit->text().isEmpty() ? QDir::homePath() : edit->text());
        if (!path.isEmpty()) {
            edit->setText(path);
            box->setChecked(true);
            formEdited();
        }
    });
    connect(box, &QCheckBox::toggled, edit, &QLineEdit::setEnabled);
    connect(box, &QCheckBox::toggled, this, &SAConnectionView::formEdited);
    connect(edit, &QLineEdit::textEdited, this, &SAConnectionView::formEdited);
    edit->setEnabled(false);
    h->addWidget(box);
    h->addWidget(edit, 1);
    h->addWidget(browse);
    *enabledBox = box;
    edit->setProperty("rowWidget", QVariant::fromValue<QObject *>(row));
    return edit;
}

void SAConnectionView::buildForm(QWidget *parent)
{
    auto *outer = new QVBoxLayout(parent);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    // Centered, fixed-width column instead of a form stretched across the
    // whole window — matches how macOS lays out this kind of detail form.
    auto *centeringLayout = new QHBoxLayout(content);
    centeringLayout->setContentsMargins(0, 0, 0, 0);
    auto *centered = new QWidget;
    centered->setMaximumWidth(560);
    centeringLayout->addStretch(1);
    centeringLayout->addWidget(centered);
    centeringLayout->addStretch(1);
    auto *layout = new QVBoxLayout(centered);
    layout->setContentsMargins(24, 28, 24, 18);
    layout->setSpacing(16);

    m_formTitle = new QLabel(tr("Quick Connect"));
    QFont titleFont = m_formTitle->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.5);
    titleFont.setBold(true);
    m_formTitle->setFont(titleFont);
    m_formTitle->setAlignment(Qt::AlignHCenter);
    layout->addWidget(m_formTitle);

    // A segmented control, like the real app's connection-type switch,
    // instead of a plain drop-down.
    auto *typeBar = new QWidget;
    typeBar->setObjectName(QStringLiteral("viewSwitchBar"));
    auto *typeBarLayout = new QHBoxLayout(typeBar);
    typeBarLayout->setContentsMargins(0, 0, 0, 0);
    typeBarLayout->setSpacing(0);
    m_typeGroup = new QButtonGroup(this);
    struct TypeDef { const char *text; SAIcons::Glyph glyph; SAConnectionType type; };
    const TypeDef typeDefs[3] = {
        {"TCP/IP", SAIcons::Glyph::Connection, SAConnectionType::TCPIP},
        {"Socket", SAIcons::Glyph::Socket, SAConnectionType::Socket},
        {"SSH", SAIcons::Glyph::Lock, SAConnectionType::SSHTunnel},
    };
    for (const TypeDef &def : typeDefs) {
        auto *button = new QToolButton;
        button->setText(tr(def.text));
        button->setIcon(SAIcons::icon(def.glyph));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setCheckable(true);
        button->setAutoRaise(true);
        m_typeGroup->addButton(button, int(def.type));
        typeBarLayout->addWidget(button);
    }
    m_typeGroup->button(int(SAConnectionType::TCPIP))->setChecked(true);
    connect(m_typeGroup, &QButtonGroup::idClicked, this, &SAConnectionView::typeChanged);
    auto *typeRow = new QHBoxLayout;
    typeRow->addStretch();
    typeRow->addWidget(typeBar);
    typeRow->addStretch();
    layout->addLayout(typeRow);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Optional"));
    m_host = new QLineEdit;
    m_host->setPlaceholderText(QStringLiteral("127.0.0.1"));
    m_user = new QLineEdit;
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    auto *reveal = m_password->addAction(SAIcons::icon(SAIcons::Glyph::Key), QLineEdit::TrailingPosition);
    reveal->setToolTip(tr("Show or hide the password"));
    connect(reveal, &QAction::triggered, this, [this]() {
        m_password->setEchoMode(m_password->echoMode() == QLineEdit::Password ? QLineEdit::Normal : QLineEdit::Password);
    });
    m_database = new QLineEdit;
    m_database->setPlaceholderText(tr("Optional"));
    m_port = new QLineEdit;
    m_port->setPlaceholderText(QStringLiteral("3306"));
    m_port->setValidator(new QIntValidator(1, 65535, m_port));
    m_port->setMaximumWidth(110);
    m_socket = new QLineEdit;
    m_socket->setPlaceholderText(tr("Optional – autodetected when empty"));
    m_color = new QComboBox;
    m_color->addItem(SAIcons::colorDot(QColor(), 12), tr("None"), -1);
    for (int i = 0; i < SAFavoriteColors::count(); ++i) m_color->addItem(SAIcons::colorDot(SAFavoriteColors::color(i), 12), SAFavoriteColors::name(i), i);

    form->addRow(tr("Name:"), m_name);
    m_hostLabel = new QLabel(tr("Host:"));
    form->addRow(m_hostLabel, m_host);
    m_hostRow = m_host;
    form->addRow(tr("Username:"), m_user);
    form->addRow(tr("Password:"), m_password);
    form->addRow(tr("Database:"), m_database);
    m_portLabel = new QLabel(tr("Port:"));
    form->addRow(m_portLabel, m_port);
    m_portRow = m_port;
    m_socketLabel = new QLabel(tr("Socket:"));
    form->addRow(m_socketLabel, m_socket);
    m_socketRow = m_socket;
    form->addRow(tr("Color:"), m_color);
    layout->addLayout(form);

    // SSH
    m_sshGroup = new QGroupBox(tr("SSH Tunnel"));
    auto *sshForm = new QFormLayout(m_sshGroup);
    sshForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_sshHost = new QLineEdit;
    m_sshUser = new QLineEdit;
    m_sshPassword = new QLineEdit;
    m_sshPassword->setEchoMode(QLineEdit::Password);
    m_sshPassword->setPlaceholderText(tr("Leave empty to use keys or the SSH agent"));
    m_sshKey = filePicker(m_sshGroup, &m_sshKeyEnabled, tr("Choose SSH private key"));
    m_sshPort = new QLineEdit;
    m_sshPort->setPlaceholderText(QStringLiteral("22"));
    m_sshPort->setValidator(new QIntValidator(1, 65535, m_sshPort));
    m_sshPort->setMaximumWidth(110);
    m_sshRemoteSocket = new QLineEdit;
    m_sshRemoteSocket->setPlaceholderText(tr("Optional – forward to a UNIX socket on the SSH host"));
    sshForm->addRow(tr("SSH host:"), m_sshHost);
    sshForm->addRow(tr("SSH user:"), m_sshUser);
    sshForm->addRow(tr("SSH password:"), m_sshPassword);
    sshForm->addRow(tr("SSH key:"), qobject_cast<QWidget *>(m_sshKey->property("rowWidget").value<QObject *>()));
    sshForm->addRow(tr("SSH port:"), m_sshPort);
    sshForm->addRow(tr("Remote socket:"), m_sshRemoteSocket);
    layout->addWidget(m_sshGroup);

    // SSL
    m_sslGroup = new QGroupBox(tr("SSL"));
    auto *sslForm = new QFormLayout(m_sslGroup);
    sslForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_useSSL = new QCheckBox(tr("Connect using SSL"));
    m_sslKey = filePicker(m_sslGroup, &m_sslKeyEnabled, tr("Choose client key file"));
    m_sslCert = filePicker(m_sslGroup, &m_sslCertEnabled, tr("Choose client certificate file"));
    m_sslCA = filePicker(m_sslGroup, &m_sslCAEnabled, tr("Choose CA certificate file"));
    sslForm->addRow(QString(), m_useSSL);
    sslForm->addRow(tr("Key file:"), qobject_cast<QWidget *>(m_sslKey->property("rowWidget").value<QObject *>()));
    sslForm->addRow(tr("Certificate:"), qobject_cast<QWidget *>(m_sslCert->property("rowWidget").value<QObject *>()));
    sslForm->addRow(tr("CA certificate:"), qobject_cast<QWidget *>(m_sslCA->property("rowWidget").value<QObject *>()));
    connect(m_useSSL, &QCheckBox::toggled, this, [this](bool on) {
        for (QWidget *w : {qobject_cast<QWidget *>(m_sslKey->property("rowWidget").value<QObject *>()),
                           qobject_cast<QWidget *>(m_sslCert->property("rowWidget").value<QObject *>()),
                           qobject_cast<QWidget *>(m_sslCA->property("rowWidget").value<QObject *>())}) w->setEnabled(on);
        formEdited();
    });
    layout->addWidget(m_sslGroup);

    // Advanced
    m_advancedGroup = new QGroupBox(tr("Advanced"));
    m_advancedGroup->setCheckable(true);
    m_advancedGroup->setChecked(false);
    auto *advForm = new QFormLayout(m_advancedGroup);
    advForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_timeZoneMode = new QComboBox;
    m_timeZoneMode->addItem(tr("Use server time zone"), int(SATimeZoneMode::Server));
    m_timeZoneMode->addItem(tr("Use this computer's time zone"), int(SATimeZoneMode::System));
    m_timeZoneMode->addItem(tr("Use a fixed time zone"), int(SATimeZoneMode::Fixed));
    m_timeZone = new QComboBox;
    m_timeZone->setEditable(true);
    for (const QByteArray &id : QTimeZone::availableTimeZoneIds()) m_timeZone->addItem(QString::fromUtf8(id));
    m_timeZone->setEnabled(false);
    connect(m_timeZoneMode, &QComboBox::currentIndexChanged, this, [this](int) {
        m_timeZone->setEnabled(m_timeZoneMode->currentData().toInt() == int(SATimeZoneMode::Fixed));
        formEdited();
    });
    connect(m_timeZone, &QComboBox::currentTextChanged, this, &SAConnectionView::formEdited);
    m_compression = new QCheckBox(tr("Use protocol compression"));
    m_localInfile = new QCheckBox(tr("Allow LOAD DATA LOCAL INFILE"));
    m_clearText = new QCheckBox(tr("Enable cleartext authentication plugin"));
    m_serverPublicKey = new QCheckBox(tr("Request server public key (caching_sha2_password over plain TCP)"));
    m_savePasswords = new QCheckBox(tr("Save passwords in the system keyring"));
    m_savePasswords->setChecked(SAPreferences::instance().stringFor(SAPreferences::PasswordStorage) == QLatin1String("keyring"));
    m_savePasswords->setEnabled(SASecretStore::isCompiledIn());
    if (!SASecretStore::isCompiledIn()) m_savePasswords->setToolTip(tr("Built without libsecret support."));
    advForm->addRow(tr("Time zone:"), m_timeZoneMode);
    advForm->addRow(QString(), m_timeZone);
    advForm->addRow(QString(), m_compression);
    advForm->addRow(QString(), m_localInfile);
    advForm->addRow(QString(), m_clearText);
    advForm->addRow(QString(), m_serverPublicKey);
    advForm->addRow(QString(), m_savePasswords);
    for (QCheckBox *b : {m_compression, m_localInfile, m_clearText, m_serverPublicKey}) connect(b, &QCheckBox::toggled, this, &SAConnectionView::formEdited);
    connect(m_savePasswords, &QCheckBox::toggled, this, [](bool on) {
        SAPreferences::instance().set(SAPreferences::PasswordStorage, on ? QStringLiteral("keyring") : QStringLiteral("none"));
    });
    layout->addWidget(m_advancedGroup);
    layout->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    outer->addWidget(m_status);

    auto *buttonRow = new QHBoxLayout;
    m_addFavoriteButton = new QPushButton(tr("Add to Favorites"));
    connect(m_addFavoriteButton, &QPushButton::clicked, this, &SAConnectionView::addCurrentToFavorites);
    m_testButton = new QPushButton(tr("Test Connection"));
    connect(m_testButton, &QPushButton::clicked, this, [this]() { Q_EMIT testRequested(currentInfo()); });
    m_connectButton = new QPushButton(tr("Connect"));
    m_connectButton->setDefault(true);
    connect(m_connectButton, &QPushButton::clicked, this, [this]() { Q_EMIT connectRequested(currentInfo()); });
    buttonRow->addWidget(m_addFavoriteButton);
    buttonRow->addStretch();
    buttonRow->addWidget(m_testButton);
    buttonRow->addWidget(m_connectButton);
    outer->addLayout(buttonRow);

    for (QLineEdit *e : {m_name, m_host, m_user, m_password, m_database, m_port, m_socket, m_sshHost, m_sshUser, m_sshPassword, m_sshPort, m_sshRemoteSocket})
        connect(e, &QLineEdit::textEdited, this, &SAConnectionView::formEdited);
    for (QLineEdit *e : {m_host, m_user, m_password, m_database, m_port, m_socket, m_sshHost, m_sshUser, m_sshPassword, m_sshPort})
        connect(e, &QLineEdit::returnPressed, m_connectButton, &QPushButton::click);
    connect(m_color, &QComboBox::currentIndexChanged, this, &SAConnectionView::formEdited);

    typeChanged(0);
}

void SAConnectionView::typeChanged(int)
{
    updateVisibility();
    formEdited();
}

void SAConnectionView::updateVisibility()
{
    const auto type = static_cast<SAConnectionType>(m_typeGroup->checkedId());
    const bool socket = type == SAConnectionType::Socket;
    const bool ssh = type == SAConnectionType::SSHTunnel;
    m_hostLabel->setVisible(!socket);
    m_hostRow->setVisible(!socket);
    m_portLabel->setVisible(!socket);
    m_portRow->setVisible(!socket);
    m_socketLabel->setVisible(socket);
    m_socketRow->setVisible(socket);
    m_sshGroup->setVisible(ssh);
    m_host->setPlaceholderText(ssh ? tr("127.0.0.1 (as seen from the SSH host)") : QStringLiteral("127.0.0.1"));
}

// ---- info <-> form ------------------------------------------------------------------------

SAConnectionInfo SAConnectionView::currentInfo() const
{
    SAConnectionInfo info;
    if (m_editingNode) info = m_editingNode->info();
    info.type = static_cast<SAConnectionType>(m_typeGroup->checkedId());
    info.name = m_name->text().trimmed();
    info.host = m_host->text().trimmed();
    info.user = m_user->text();
    info.password = m_password->text();
    info.database = m_database->text().trimmed();
    info.port = m_port->text().trimmed();
    info.socket = m_socket->text().trimmed();
    info.colorIndex = m_color->currentData().toInt();
    info.sshHost = m_sshHost->text().trimmed();
    info.sshUser = m_sshUser->text().trimmed();
    info.sshPassword = m_sshPassword->text();
    info.sshKeyLocationEnabled = m_sshKeyEnabled->isChecked();
    info.sshKeyLocation = m_sshKey->text().trimmed();
    info.sshPort = m_sshPort->text().trimmed();
    info.sshRemoteSocketPath = m_sshRemoteSocket->text().trimmed();
    info.useSSL = m_useSSL->isChecked();
    info.sslKeyFileLocationEnabled = m_sslKeyEnabled->isChecked();
    info.sslKeyFileLocation = m_sslKey->text().trimmed();
    info.sslCertificateFileLocationEnabled = m_sslCertEnabled->isChecked();
    info.sslCertificateFileLocation = m_sslCert->text().trimmed();
    info.sslCACertFileLocationEnabled = m_sslCAEnabled->isChecked();
    info.sslCACertFileLocation = m_sslCA->text().trimmed();
    info.timeZoneMode = static_cast<SATimeZoneMode>(m_timeZoneMode->currentData().toInt());
    info.timeZoneIdentifier = info.timeZoneMode == SATimeZoneMode::Fixed ? m_timeZone->currentText().trimmed() : QString();
    info.useCompression = m_compression->isChecked();
    info.allowDataLocalInfile = m_localInfile->isChecked();
    info.enableClearTextPlugin = m_clearText->isChecked();
    info.requestServerPublicKey = m_serverPublicKey->isChecked();
    return info;
}

void SAConnectionView::setInfo(const SAConnectionInfo &info)
{
    m_loading = true;
    const SAConnectionType effectiveType = (info.type == SAConnectionType::AWSIAM || info.type == SAConnectionType::Vault)
        ? SAConnectionType::TCPIP : info.type;
    if (QAbstractButton *typeButton = m_typeGroup->button(int(effectiveType))) typeButton->setChecked(true);
    updateVisibility();
    m_name->setText(info.name);
    m_host->setText(info.host);
    m_user->setText(info.user);
    m_password->setText(info.password);
    m_database->setText(info.database);
    m_port->setText(info.port);
    m_socket->setText(info.socket);
    m_color->setCurrentIndex(qMax(0, m_color->findData(info.colorIndex)));
    m_sshHost->setText(info.sshHost);
    m_sshUser->setText(info.sshUser);
    m_sshPassword->setText(info.sshPassword);
    m_sshKeyEnabled->setChecked(info.sshKeyLocationEnabled);
    m_sshKey->setText(info.sshKeyLocation);
    m_sshPort->setText(info.sshPort);
    m_sshRemoteSocket->setText(info.sshRemoteSocketPath);
    m_useSSL->setChecked(info.useSSL);
    m_sslKeyEnabled->setChecked(info.sslKeyFileLocationEnabled);
    m_sslKey->setText(info.sslKeyFileLocation);
    m_sslCertEnabled->setChecked(info.sslCertificateFileLocationEnabled);
    m_sslCert->setText(info.sslCertificateFileLocation);
    m_sslCAEnabled->setChecked(info.sslCACertFileLocationEnabled);
    m_sslCA->setText(info.sslCACertFileLocation);
    m_timeZoneMode->setCurrentIndex(qMax(0, m_timeZoneMode->findData(int(info.timeZoneMode))));
    if (info.timeZoneMode == SATimeZoneMode::Fixed) m_timeZone->setCurrentText(info.timeZoneIdentifier);
    m_compression->setChecked(info.useCompression);
    m_localInfile->setChecked(info.allowDataLocalInfile);
    m_clearText->setChecked(info.enableClearTextPlugin);
    m_serverPublicKey->setChecked(info.requestServerPublicKey);
    m_loading = false;
    updateVisibility();
}

QString SAConnectionView::keyringAccount(const SAConnectionInfo &info) const
{
    if (info.id >= 0) return QStringLiteral("favorite:%1").arg(info.id);
    return QStringLiteral("%1@%2:%3/%4").arg(info.user, info.host, QString::number(info.effectivePort()), info.database);
}

void SAConnectionView::loadPasswordsFor(SAConnectionInfo &info) const
{
    if (info.id < 0 || !SASecretStore::isCompiledIn()) return;
    if (SAPreferences::instance().stringFor(SAPreferences::PasswordStorage) != QLatin1String("keyring")) return;
    bool found = false;
    const QString account = keyringAccount(info);
    const QString password = SASecretStore::lookup(SASecretStore::Kind::MySQL, account, &found);
    if (found) info.password = password;
    const QString sshPassword = SASecretStore::lookup(SASecretStore::Kind::SSH, account, &found);
    if (found) info.sshPassword = sshPassword;
}

bool SAConnectionView::savePasswordsEnabled() const
{
    return m_savePasswords->isChecked() && SASecretStore::isCompiledIn();
}

void SAConnectionView::rememberPasswords(const SAConnectionInfo &info)
{
    if (!savePasswordsEnabled() || info.id < 0) return;
    if (!SASecretStore::isAvailable()) {
        setStatusMessage(tr("No keyring service is running; the password was not saved."));
        return;
    }
    const QString account = keyringAccount(info);
    const QString label = QStringLiteral("Sequel Ace : %1 (%2)").arg(info.displayName()).arg(info.id);
    if (info.password.isEmpty()) SASecretStore::remove(SASecretStore::Kind::MySQL, account);
    else SASecretStore::store(SASecretStore::Kind::MySQL, account, label, info.password);
    if (info.sshPassword.isEmpty()) SASecretStore::remove(SASecretStore::Kind::SSH, account);
    else SASecretStore::store(SASecretStore::Kind::SSH, account, label + QStringLiteral(" SSH"), info.sshPassword);
}

void SAConnectionView::setStatusMessage(const QString &text)
{
    m_status->setText(text);
}

// ---- selection / editing ---------------------------------------------------------------------

void SAConnectionView::favoriteSelectionChanged()
{
    commitEdits();
    const QModelIndexList selected = m_tree->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    SAFavoriteNode *node = m_model->nodeAt(m_proxy->mapToSource(selected.first()));
    if (!node || node->isGroup()) {
        m_editingNode = nullptr;
        m_formTitle->setText(node ? node->groupName() : tr("Favorites"));
        return;
    }
    m_editingNode = node;
    SAConnectionInfo info = node->info();
    loadPasswordsFor(info);
    setInfo(info);
    m_formTitle->setText(info.displayName());
    m_addFavoriteButton->setText(tr("Duplicate Favorite"));
    m_status->clear();
}

void SAConnectionView::selectFavorite(int id)
{
    const QModelIndex source = m_model->indexForFavoriteId(id);
    if (!source.isValid()) return;
    const QModelIndex proxyIndex = m_proxy->mapFromSource(source);
    m_tree->setCurrentIndex(proxyIndex);
    m_tree->scrollTo(proxyIndex);
}

void SAConnectionView::selectQuickConnect()
{
    commitEdits();
    m_tree->clearSelection();
    m_tree->setCurrentIndex(QModelIndex());
    m_editingNode = nullptr;
    SAConnectionInfo blank;
    blank.host = QString();
    setInfo(blank);
    m_formTitle->setText(tr("Quick Connect"));
    m_addFavoriteButton->setText(tr("Add to Favorites"));
    m_status->clear();
    m_host->setFocus();
}

void SAConnectionView::focusForm()
{
    if (m_typeGroup->checkedId() == int(SAConnectionType::Socket)) m_user->setFocus();
    else m_host->setFocus();
}

void SAConnectionView::formEdited()
{
    if (m_loading || !m_editingNode) return;
    m_saveTimer->start();
}

void SAConnectionView::commitEdits()
{
    if (!m_editingNode || m_loading) return;
    m_saveTimer->stop();
    SAConnectionInfo info = currentInfo();
    info.id = m_editingNode->info().id;
    SAConnectionInfo stored = info;
    stored.password.clear();
    stored.sshPassword.clear();
    SAConnectionInfo current = m_editingNode->info();
    current.password.clear();
    current.sshPassword.clear();
    if (stored == current) return;
    m_editingNode->setInfo(stored);
    m_model->nodeChanged(m_editingNode);
    m_formTitle->setText(stored.displayName());
}

// ---- actions ---------------------------------------------------------------------------------------

void SAConnectionView::addCurrentToFavorites()
{
    commitEdits();
    SAConnectionInfo info = currentInfo();
    info.id = -1;
    if (m_editingNode) info.name = info.name.isEmpty() ? tr("Copy") : tr("%1 copy").arg(info.name);
    else if (info.name.isEmpty()) info.name = info.displayName();
    QModelIndex parent;
    const QModelIndexList selected = m_tree->selectionModel()->selectedIndexes();
    if (!selected.isEmpty()) {
        const QModelIndex source = m_proxy->mapToSource(selected.first());
        SAFavoriteNode *node = m_model->nodeAt(source);
        parent = (node && node->isGroup()) ? source : source.parent();
    }
    const QModelIndex added = m_model->addFavorite(parent, info);
    m_tree->expandAll();
    m_tree->setCurrentIndex(m_proxy->mapFromSource(added));
    // Keep the typed passwords for the new favorite and store them.
    SAConnectionInfo withSecrets = m_model->nodeAt(added)->info();
    withSecrets.password = info.password;
    withSecrets.sshPassword = info.sshPassword;
    m_password->setText(info.password);
    m_sshPassword->setText(info.sshPassword);
    rememberPasswords(withSecrets);
    m_tree->edit(m_proxy->mapFromSource(added));
}

void SAConnectionView::addGroup()
{
    bool ok = false;
    const QString name = SADialogs::askText(this, tr("New Group"), tr("Group name:"), tr("New Group"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QModelIndex parent;
    const QModelIndexList selected = m_tree->selectionModel()->selectedIndexes();
    if (!selected.isEmpty()) {
        const QModelIndex source = m_proxy->mapToSource(selected.first());
        SAFavoriteNode *node = m_model->nodeAt(source);
        parent = (node && node->isGroup()) ? source : source.parent();
    }
    const QModelIndex added = m_model->addGroup(parent, name.trimmed());
    m_tree->expandAll();
    m_tree->setCurrentIndex(m_proxy->mapFromSource(added));
}

void SAConnectionView::duplicateSelectedFavorite()
{
    if (!m_editingNode) return;
    addCurrentToFavorites();
}

void SAConnectionView::removeSelected()
{
    const QModelIndexList selected = m_tree->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    const QModelIndex source = m_proxy->mapToSource(selected.first());
    SAFavoriteNode *node = m_model->nodeAt(source);
    if (!node) return;
    const QString what = node->isGroup()
        ? tr("Delete the group “%1” and the %n favorite(s) it contains?", nullptr, node->allFavorites().size()).arg(node->groupName())
        : tr("Delete the favorite “%1”?").arg(node->displayName());
    if (!SADialogs::confirm(this, tr("Delete Favorite"), what, tr("Delete"), QString(), true)) return;
    if (savePasswordsEnabled()) {
        for (SAFavoriteNode *fav : node->isGroup() ? node->allFavorites() : QList<SAFavoriteNode *>{node}) {
            const QString account = keyringAccount(fav->info());
            SASecretStore::remove(SASecretStore::Kind::MySQL, account);
            SASecretStore::remove(SASecretStore::Kind::SSH, account);
        }
    }
    if (m_editingNode == node || (node->isGroup() && node->allFavorites().contains(m_editingNode))) m_editingNode = nullptr;
    m_model->removeNode(source);
    selectQuickConnect();
}

void SAConnectionView::importFavorites()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Favorites"), QDir::homePath(), tr("Property lists (*.plist);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    const QList<SAFavoriteNode *> nodes = SAFavoritesStore::readExportFile(path, &error);
    if (nodes.isEmpty()) {
        SADialogs::warning(this, tr("Import Failed"), error.isEmpty() ? tr("No favorites were found in the file.") : error);
        return;
    }
    int count = 0;
    std::function<void(SAFavoriteNode *, const QModelIndex &)> add = [&](SAFavoriteNode *node, const QModelIndex &parent) {
        if (node->isGroup()) {
            const QModelIndex g = m_model->addGroup(parent, node->groupName());
            for (SAFavoriteNode *child : node->children()) add(child, g);
        } else {
            SAConnectionInfo info = node->info();
            info.id = -1;   // assign fresh ids to avoid clashes
            m_model->addFavorite(parent, info);
            ++count;
        }
    };
    for (SAFavoriteNode *node : nodes) add(node, QModelIndex());
    qDeleteAll(nodes);
    m_tree->expandAll();
    setStatusMessage(tr("Imported %n favorite(s).", nullptr, count));
}

void SAConnectionView::exportFavorites()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Favorites"), QDir::homePath() + QStringLiteral("/SequelAceFavorites.plist"), tr("Property lists (*.plist)"));
    if (path.isEmpty()) return;
    QString error;
    if (!SAFavoritesStore::writeExportFile(path, m_store->root()->children(), &error)) SADialogs::warning(this, tr("Export Failed"), error);
    else setStatusMessage(tr("Favorites exported to %1. Passwords are never included.").arg(path));
}

void SAConnectionView::showFavoriteContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_tree->indexAt(pos);
    QMenu menu(this);
    if (index.isValid()) {
        m_tree->setCurrentIndex(index);
        const bool isGroup = m_proxy->data(index, SAFavoritesModel::IsGroupRole).toBool();
        if (!isGroup) {
            menu.addAction(tr("Connect"), this, [this]() { Q_EMIT connectRequested(currentInfo()); });
            menu.addAction(tr("Duplicate"), this, &SAConnectionView::duplicateSelectedFavorite);
        }
        menu.addAction(tr("Rename"), this, [this, index]() { m_tree->edit(index); });
        menu.addAction(isGroup ? tr("Delete Group") : tr("Delete Favorite"), this, &SAConnectionView::removeSelected);
        menu.addSeparator();
    }
    menu.addAction(SAIcons::icon(SAIcons::Glyph::Add), tr("New Favorite"), this, &SAConnectionView::addCurrentToFavorites);
    menu.addAction(SAIcons::icon(SAIcons::Glyph::AddFolder), tr("New Group"), this, &SAConnectionView::addGroup);
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}
