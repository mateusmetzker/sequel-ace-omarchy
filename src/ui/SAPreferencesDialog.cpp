//
//  SAPreferencesDialog.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAPreferencesDialog.h"
#include "SAEditorTheme.h"
#include "SAPreferences.h"
#include "SASecretStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

SAPreferencesDialog::SAPreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    auto *tabs = new QTabWidget;
    tabs->addTab(buildGeneral(), tr("General"));
    tabs->addTab(buildTables(), tr("Tables"));
    tabs->addTab(buildEditor(), tr("Query Editor"));
    tabs->addTab(buildNetwork(), tr("Network"));
    tabs->addTab(buildMCP(), tr("MCP Server"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { apply(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SAPreferencesDialog::apply);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
    load();
    resize(560, 520);
}

QWidget *SAPreferencesDialog::buildGeneral()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    m_defaultView = new QComboBox;
    m_defaultView->addItems({tr("Structure"), tr("Content"), tr("Relations"), tr("Triggers"), tr("Table Info"), tr("Query")});
    m_selectLastFavorite = new QCheckBox(tr("Select the last used favorite when opening a connection tab"));
    m_warnBeforeDelete = new QCheckBox(tr("Show a warning before deleting rows"));
    m_warnBeforeExecute = new QCheckBox(tr("Show a warning before executing destructive queries and row edits"));
    m_consoleLogging = new QCheckBox(tr("Log queries to the console"));
    m_savePasswords = new QCheckBox(tr("Save passwords in the system keyring (Secret Service)"));
    m_savePasswords->setEnabled(SASecretStore::isCompiledIn());
    form->addRow(tr("Default view:"), m_defaultView);
    form->addRow(QString(), m_selectLastFavorite);
    form->addRow(QString(), m_warnBeforeDelete);
    form->addRow(QString(), m_warnBeforeExecute);
    form->addRow(QString(), m_consoleLogging);
    form->addRow(QString(), m_savePasswords);
    return page;
}

QWidget *SAPreferencesDialog::buildTables()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    auto *limitRow = new QHBoxLayout;
    m_limitResults = new QCheckBox(tr("Limit result to"));
    m_limitValue = new QSpinBox;
    m_limitValue->setRange(1, 1000000);
    m_limitValue->setSuffix(tr(" rows"));
    limitRow->addWidget(m_limitResults);
    limitRow->addWidget(m_limitValue);
    limitRow->addStretch();
    form->addRow(tr("Content view:"), limitRow);
    m_nullValue = new QLineEdit;
    form->addRow(tr("Display NULL as:"), m_nullValue);
    m_reloadAfterAdd = new QCheckBox(tr("Reload table after adding a row"));
    m_reloadAfterEdit = new QCheckBox(tr("Reload table after editing a row"));
    m_reloadAfterRemove = new QCheckBox(tr("Reload table after removing rows"));
    m_showNoAffectedRows = new QCheckBox(tr("Warn when an edit affected no rows"));
    m_binaryAsHex = new QCheckBox(tr("Display binary data as hex"));
    m_showColumnTypes = new QCheckBox(tr("Show column types in table headers"));
    m_verticalGridlines = new QCheckBox(tr("Show vertical grid lines"));
    m_showTableComments = new QCheckBox(tr("Show table comments as tooltips in the tables list (slower)"));
    m_newFieldsAllowNull = new QCheckBox(tr("New fields allow NULL by default"));
    for (QCheckBox *box : {m_reloadAfterAdd, m_reloadAfterEdit, m_reloadAfterRemove, m_showNoAffectedRows, m_binaryAsHex, m_showColumnTypes, m_verticalGridlines, m_showTableComments, m_newFieldsAllowNull})
        form->addRow(QString(), box);
    m_rowCountLevel = new QComboBox;
    m_rowCountLevel->addItems({tr("Never (use the engine estimate)"), tr("For small tables only"), tr("Always")});
    form->addRow(tr("Exact row count:"), m_rowCountLevel);
    return page;
}

QWidget *SAPreferencesDialog::buildEditor()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    auto *fontRow = new QHBoxLayout;
    m_editorFontFamily = new QFontComboBox;
    m_editorFontFamily->setFontFilters(QFontComboBox::MonospacedFonts);
    m_editorFontSize = new QSpinBox;
    m_editorFontSize->setRange(6, 48);
    fontRow->addWidget(m_editorFontFamily, 1);
    fontRow->addWidget(m_editorFontSize);
    form->addRow(tr("Font:"), fontRow);
    m_editorTheme = new QComboBox;
    m_editorTheme->addItems(SAEditorTheme::availableThemeNames());
    form->addRow(tr("Colour theme:"), m_editorTheme);
    m_tabWidth = new QSpinBox;
    m_tabWidth->setRange(1, 16);
    form->addRow(tr("Tab width:"), m_tabWidth);
    m_highlightCurrentQuery = new QCheckBox(tr("Highlight the current query"));
    m_syntaxHighlighting = new QCheckBox(tr("Syntax highlighting"));
    m_autoPair = new QCheckBox(tr("Auto-pair quotes and brackets"));
    m_autoIndent = new QCheckBox(tr("Auto-indent new lines"));
    auto *softRow = new QHBoxLayout;
    m_softIndent = new QCheckBox(tr("Indent with spaces, width"));
    m_softIndentWidth = new QSpinBox;
    m_softIndentWidth->setRange(1, 16);
    softRow->addWidget(m_softIndent);
    softRow->addWidget(m_softIndentWidth);
    softRow->addStretch();
    auto *completeRow = new QHBoxLayout;
    m_autoComplete = new QCheckBox(tr("Show completions automatically after"));
    m_autoCompleteDelay = new QDoubleSpinBox;
    m_autoCompleteDelay->setRange(0.1, 10.0);
    m_autoCompleteDelay->setSingleStep(0.1);
    m_autoCompleteDelay->setSuffix(tr(" s"));
    completeRow->addWidget(m_autoComplete);
    completeRow->addWidget(m_autoCompleteDelay);
    completeRow->addStretch();
    m_historyItems = new QSpinBox;
    m_historyItems->setRange(1, 500);
    for (QCheckBox *box : {m_highlightCurrentQuery, m_syntaxHighlighting, m_autoPair, m_autoIndent}) form->addRow(QString(), box);
    form->addRow(QString(), softRow);
    form->addRow(QString(), completeRow);
    form->addRow(tr("History entries:"), m_historyItems);
    return page;
}

QWidget *SAPreferencesDialog::buildNetwork()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    m_connectionTimeout = new QSpinBox;
    m_connectionTimeout->setRange(1, 600);
    m_connectionTimeout->setSuffix(tr(" s"));
    form->addRow(tr("Connection timeout:"), m_connectionTimeout);
    auto *keepRow = new QHBoxLayout;
    m_keepAlive = new QCheckBox(tr("Send keep-alive pings every"));
    m_keepAliveInterval = new QSpinBox;
    m_keepAliveInterval->setRange(5, 3600);
    m_keepAliveInterval->setSuffix(tr(" s"));
    keepRow->addWidget(m_keepAlive);
    keepRow->addWidget(m_keepAliveInterval);
    keepRow->addStretch();
    form->addRow(QString(), keepRow);
    m_defaultEncoding = new QComboBox;
    m_defaultEncoding->addItems({QStringLiteral("utf8mb4"), QStringLiteral("utf8"), QStringLiteral("latin1")});
    m_defaultEncoding->setEditable(true);
    form->addRow(tr("Connection encoding:"), m_defaultEncoding);
    auto pick = [this](QLineEdit *edit, const QString &caption) {
        auto *row = new QWidget;
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto *browse = new QToolButton;
        browse->setText(tr("…"));
        connect(browse, &QToolButton::clicked, this, [this, edit, caption]() {
            const QString path = QFileDialog::getOpenFileName(this, caption, edit->text().isEmpty() ? QStringLiteral("/usr/bin") : edit->text());
            if (!path.isEmpty()) edit->setText(path);
        });
        h->addWidget(edit, 1);
        h->addWidget(browse);
        return row;
    };
    m_sshClient = new QLineEdit;
    m_sshClient->setPlaceholderText(tr("Default: ssh from PATH"));
    form->addRow(tr("SSH client:"), pick(m_sshClient, tr("Choose SSH client")));
    m_sshConfig = new QLineEdit;
    m_sshConfig->setPlaceholderText(tr("Default: ~/.ssh/config"));
    form->addRow(tr("SSH config file:"), pick(m_sshConfig, tr("Choose SSH config file")));
    m_sshMuxing = new QCheckBox(tr("Enable SSH connection multiplexing (ControlMaster)"));
    form->addRow(QString(), m_sshMuxing);
    return page;
}

QWidget *SAPreferencesDialog::buildMCP()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    m_mcpEnabled = new QCheckBox(tr("Enable MCP server (lets AI assistants query this connection)"));
    form->addRow(QString(), m_mcpEnabled);
    m_mcpPort = new QSpinBox;
    m_mcpPort->setRange(1024, 65535);
    form->addRow(tr("Port:"), m_mcpPort);
    m_mcpReadOnly = new QCheckBox(tr("Read-only (block INSERT/UPDATE/DELETE and other writes)"));
    form->addRow(QString(), m_mcpReadOnly);
    auto *exportRow = new QWidget;
    auto *exportLayout = new QHBoxLayout(exportRow);
    exportLayout->setContentsMargins(0, 0, 0, 0);
    m_mcpExportPath = new QLineEdit;
    m_mcpExportPath->setPlaceholderText(tr("Default: ~/Downloads"));
    auto *browseExport = new QToolButton;
    browseExport->setText(tr("…"));
    connect(browseExport, &QToolButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose export folder"),
            m_mcpExportPath->text().isEmpty() ? QDir::homePath() : m_mcpExportPath->text());
        if (!dir.isEmpty()) m_mcpExportPath->setText(dir);
    });
    exportLayout->addWidget(m_mcpExportPath, 1);
    exportLayout->addWidget(browseExport);
    form->addRow(tr("Export folder:"), exportRow);
    m_mcpStatusLabel = new QLabel;
    m_mcpStatusLabel->setWordWrap(true);
    form->addRow(QString(), m_mcpStatusLabel);
    auto updateStatus = [this]() {
        m_mcpStatusLabel->setText(m_mcpEnabled->isChecked()
            ? tr("URL: http://127.0.0.1:%1/mcp").arg(m_mcpPort->value())
            : tr("The server is disabled."));
    };
    connect(m_mcpEnabled, &QCheckBox::toggled, this, updateStatus);
    connect(m_mcpPort, qOverload<int>(&QSpinBox::valueChanged), this, updateStatus);
    updateStatus();
    return page;
}

void SAPreferencesDialog::load()
{
    SAPreferences &p = SAPreferences::instance();
    m_defaultView->setCurrentIndex(qBound(0, p.intFor(QStringLiteral("DefaultViewMode")), 5));
    m_selectLastFavorite->setChecked(p.boolFor(SAPreferences::SelectLastFavoriteUsed));
    m_warnBeforeDelete->setChecked(p.boolFor(SAPreferences::ShowWarningBeforeDeleteQuery));
    m_warnBeforeExecute->setChecked(p.boolFor(SAPreferences::ShowWarningBeforeExecuteQuery));
    m_consoleLogging->setChecked(p.boolFor(SAPreferences::ConsoleEnableLogging));
    m_savePasswords->setChecked(p.stringFor(SAPreferences::PasswordStorage) == QLatin1String("keyring"));
    m_limitResults->setChecked(p.boolFor(SAPreferences::LimitResults));
    m_limitValue->setValue(p.intFor(SAPreferences::LimitResultsValue));
    m_nullValue->setText(p.stringFor(SAPreferences::NullValue));
    m_reloadAfterAdd->setChecked(p.boolFor(SAPreferences::ReloadAfterAddingRow));
    m_reloadAfterEdit->setChecked(p.boolFor(SAPreferences::ReloadAfterEditingRow));
    m_reloadAfterRemove->setChecked(p.boolFor(SAPreferences::ReloadAfterRemovingRow));
    m_showNoAffectedRows->setChecked(p.boolFor(SAPreferences::ShowNoAffectedRowsError));
    m_binaryAsHex->setChecked(p.boolFor(SAPreferences::DisplayBinaryDataAsHex));
    m_showColumnTypes->setChecked(p.boolFor(SAPreferences::DisplayTableViewColumnTypes));
    m_verticalGridlines->setChecked(p.boolFor(SAPreferences::DisplayTableViewVerticalGridlines));
    m_showTableComments->setChecked(p.boolFor(SAPreferences::DisplayCommentsInTablesList));
    m_newFieldsAllowNull->setChecked(p.boolFor(SAPreferences::NewFieldsAllowNulls));
    m_rowCountLevel->setCurrentIndex(qBound(0, p.intFor(SAPreferences::TableRowCountQueryLevel), 2));
    QFont font;
    if (p.stringFor(SAPreferences::EditorFont).isEmpty() || !font.fromString(p.stringFor(SAPreferences::EditorFont))) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(11);
    }
    m_editorFontFamily->setCurrentFont(font);
    m_editorFontSize->setValue(font.pointSize() > 0 ? font.pointSize() : 11);
    const QString theme = p.stringFor(SAPreferences::EditorTheme);
    m_editorTheme->setCurrentText(theme.isEmpty() ? SAEditorTheme::SystemThemeName : theme);
    m_tabWidth->setValue(p.intFor(SAPreferences::CustomQueryEditorTabStopWidth));
    m_highlightCurrentQuery->setChecked(p.boolFor(SAPreferences::CustomQueryHighlightCurrentQuery));
    m_syntaxHighlighting->setChecked(p.boolFor(SAPreferences::CustomQueryEnableSyntaxHighlighting));
    m_autoPair->setChecked(p.boolFor(SAPreferences::CustomQueryAutoPairCharacters));
    m_autoIndent->setChecked(p.boolFor(SAPreferences::CustomQueryAutoIndent));
    m_softIndent->setChecked(p.boolFor(SAPreferences::CustomQuerySoftIndent));
    m_softIndentWidth->setValue(p.intFor(SAPreferences::CustomQuerySoftIndentWidth));
    m_autoComplete->setChecked(p.value(QStringLiteral("CustomQueryAutoComplete")).isValid() ? p.boolFor(QStringLiteral("CustomQueryAutoComplete")) : true);
    m_autoCompleteDelay->setValue(p.value(QStringLiteral("CustomQueryAutoCompleteDelay")).isValid() ? p.doubleFor(QStringLiteral("CustomQueryAutoCompleteDelay")) : 1.5);
    m_historyItems->setValue(p.intFor(SAPreferences::CustomQueryMaxHistoryItems));
    m_connectionTimeout->setValue(p.intFor(SAPreferences::ConnectionTimeoutValue));
    m_keepAlive->setChecked(p.boolFor(SAPreferences::UseKeepAlive));
    m_keepAliveInterval->setValue(p.intFor(SAPreferences::KeepAliveInterval));
    m_defaultEncoding->setCurrentText(p.stringFor(SAPreferences::DefaultEncoding));
    m_sshClient->setText(p.stringFor(SAPreferences::SSHClientPath));
    m_sshConfig->setText(p.stringFor(SAPreferences::SSHConfigFile));
    m_sshMuxing->setChecked(p.boolFor(SAPreferences::SSHMultiplexingEnabled));
    m_mcpEnabled->setChecked(p.boolFor(SAPreferences::MCPServerEnabled));
    m_mcpPort->setValue(p.value(SAPreferences::MCPServerPort).isValid() ? p.intFor(SAPreferences::MCPServerPort) : 8765);
    m_mcpReadOnly->setChecked(p.value(SAPreferences::MCPReadOnly).isValid() ? p.boolFor(SAPreferences::MCPReadOnly) : true);
    m_mcpExportPath->setText(p.stringFor(SAPreferences::MCPExportPath));
    Q_EMIT m_mcpEnabled->toggled(m_mcpEnabled->isChecked());
}

void SAPreferencesDialog::apply()
{
    SAPreferences &p = SAPreferences::instance();
    p.set(QStringLiteral("DefaultViewMode"), m_defaultView->currentIndex());
    p.set(SAPreferences::SelectLastFavoriteUsed, m_selectLastFavorite->isChecked());
    p.set(SAPreferences::ShowWarningBeforeDeleteQuery, m_warnBeforeDelete->isChecked());
    p.set(SAPreferences::ShowWarningBeforeExecuteQuery, m_warnBeforeExecute->isChecked());
    p.set(SAPreferences::ConsoleEnableLogging, m_consoleLogging->isChecked());
    p.set(SAPreferences::PasswordStorage, m_savePasswords->isChecked() ? QStringLiteral("keyring") : QStringLiteral("none"));
    p.set(SAPreferences::LimitResults, m_limitResults->isChecked());
    p.set(SAPreferences::LimitResultsValue, m_limitValue->value());
    p.set(SAPreferences::NullValue, m_nullValue->text().isEmpty() ? QStringLiteral("NULL") : m_nullValue->text());
    p.set(SAPreferences::ReloadAfterAddingRow, m_reloadAfterAdd->isChecked());
    p.set(SAPreferences::ReloadAfterEditingRow, m_reloadAfterEdit->isChecked());
    p.set(SAPreferences::ReloadAfterRemovingRow, m_reloadAfterRemove->isChecked());
    p.set(SAPreferences::ShowNoAffectedRowsError, m_showNoAffectedRows->isChecked());
    p.set(SAPreferences::DisplayBinaryDataAsHex, m_binaryAsHex->isChecked());
    p.set(SAPreferences::DisplayTableViewColumnTypes, m_showColumnTypes->isChecked());
    p.set(SAPreferences::DisplayTableViewVerticalGridlines, m_verticalGridlines->isChecked());
    p.set(SAPreferences::DisplayCommentsInTablesList, m_showTableComments->isChecked());
    p.set(SAPreferences::NewFieldsAllowNulls, m_newFieldsAllowNull->isChecked());
    p.set(SAPreferences::TableRowCountQueryLevel, m_rowCountLevel->currentIndex());
    QFont font = m_editorFontFamily->currentFont();
    font.setPointSize(m_editorFontSize->value());
    p.set(SAPreferences::EditorFont, font.toString());
    p.set(SAPreferences::EditorTheme, m_editorTheme->currentText() == SAEditorTheme::SystemThemeName ? QString() : m_editorTheme->currentText());
    p.set(SAPreferences::CustomQueryEditorTabStopWidth, m_tabWidth->value());
    p.set(SAPreferences::CustomQueryHighlightCurrentQuery, m_highlightCurrentQuery->isChecked());
    p.set(SAPreferences::CustomQueryEnableSyntaxHighlighting, m_syntaxHighlighting->isChecked());
    p.set(SAPreferences::CustomQueryAutoPairCharacters, m_autoPair->isChecked());
    p.set(SAPreferences::CustomQueryAutoIndent, m_autoIndent->isChecked());
    p.set(SAPreferences::CustomQuerySoftIndent, m_softIndent->isChecked());
    p.set(SAPreferences::CustomQuerySoftIndentWidth, m_softIndentWidth->value());
    p.set(QStringLiteral("CustomQueryAutoComplete"), m_autoComplete->isChecked());
    p.set(QStringLiteral("CustomQueryAutoCompleteDelay"), m_autoCompleteDelay->value());
    p.set(SAPreferences::CustomQueryMaxHistoryItems, m_historyItems->value());
    p.set(SAPreferences::ConnectionTimeoutValue, m_connectionTimeout->value());
    p.set(SAPreferences::UseKeepAlive, m_keepAlive->isChecked());
    p.set(SAPreferences::KeepAliveInterval, m_keepAliveInterval->value());
    p.set(SAPreferences::DefaultEncoding, m_defaultEncoding->currentText().trimmed().isEmpty() ? QStringLiteral("utf8mb4") : m_defaultEncoding->currentText().trimmed());
    p.set(SAPreferences::SSHClientPath, m_sshClient->text().trimmed());
    p.set(SAPreferences::SSHConfigFile, m_sshConfig->text().trimmed());
    p.set(SAPreferences::SSHMultiplexingEnabled, m_sshMuxing->isChecked());
    p.set(SAPreferences::MCPServerEnabled, m_mcpEnabled->isChecked());
    p.set(SAPreferences::MCPServerPort, m_mcpPort->value());
    p.set(SAPreferences::MCPReadOnly, m_mcpReadOnly->isChecked());
    p.set(SAPreferences::MCPExportPath, m_mcpExportPath->text().trimmed());
    p.sync();
}
