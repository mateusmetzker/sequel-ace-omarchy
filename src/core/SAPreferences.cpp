//
//  SAPreferences.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAPreferences.h"

const QString SAPreferences::LimitResults = QStringLiteral("LimitResults");
const QString SAPreferences::LimitResultsValue = QStringLiteral("LimitResultsValue");
const QString SAPreferences::NullValue = QStringLiteral("NullValue");
const QString SAPreferences::ReloadAfterAddingRow = QStringLiteral("ReloadAfterAddingRow");
const QString SAPreferences::ReloadAfterEditingRow = QStringLiteral("ReloadAfterEditingRow");
const QString SAPreferences::ReloadAfterRemovingRow = QStringLiteral("ReloadAfterRemovingRow");
const QString SAPreferences::ShowNoAffectedRowsError = QStringLiteral("ShowNoAffectedRowsError");
const QString SAPreferences::ShowWarningBeforeDeleteQuery = QStringLiteral("ShowWarningBeforeDeleteQuery");
const QString SAPreferences::ShowWarningBeforeExecuteQuery = QStringLiteral("ShowWarningBeforeExecuteQuery");
const QString SAPreferences::ConnectionTimeoutValue = QStringLiteral("ConnectionTimeoutValue");
const QString SAPreferences::UseKeepAlive = QStringLiteral("UseKeepAlive");
const QString SAPreferences::KeepAliveInterval = QStringLiteral("KeepAliveInterval");
const QString SAPreferences::CustomQueryMaxHistoryItems = QStringLiteral("CustomQueryMaxHistoryItems");
const QString SAPreferences::CustomQueryEditorTabStopWidth = QStringLiteral("CustomQueryEditorTabStopWidth");
const QString SAPreferences::CustomQueryAutoUppercaseKeywords = QStringLiteral("CustomQueryAutoUppercaseKeywords");
const QString SAPreferences::CustomQueryHighlightCurrentQuery = QStringLiteral("CustomQueryHighlightCurrentQuery");
const QString SAPreferences::CustomQueryEnableSyntaxHighlighting = QStringLiteral("CustomQueryEnableSyntaxHighlighting");
const QString SAPreferences::CustomQueryAutoPairCharacters = QStringLiteral("CustomQueryAutoPairCharacters");
const QString SAPreferences::CustomQueryAutoIndent = QStringLiteral("CustomQueryAutoIndent");
const QString SAPreferences::CustomQuerySoftIndent = QStringLiteral("CustomQuerySoftIndent");
const QString SAPreferences::CustomQuerySoftIndentWidth = QStringLiteral("CustomQuerySoftIndentWidth");
const QString SAPreferences::QueryPrimaryControlRunsAll = QStringLiteral("QueryPrimaryControlRunsAll");
const QString SAPreferences::DisplayBinaryDataAsHex = QStringLiteral("DisplayBinaryDataAsHex");
const QString SAPreferences::LoadBlobsAsNeeded = QStringLiteral("LoadBlobsAsNeeded");
const QString SAPreferences::DisplayTableViewColumnTypes = QStringLiteral("DisplayTableViewColumnTypes");
const QString SAPreferences::DisplayTableViewVerticalGridlines = QStringLiteral("DisplayTableViewVerticalGridlines");
const QString SAPreferences::DisplayCommentsInTablesList = QStringLiteral("DisplayCommentsInTablesList");
const QString SAPreferences::NewFieldsAllowNulls = QStringLiteral("NewFieldsAllowNulls");
const QString SAPreferences::ConsoleEnableLogging = QStringLiteral("ConsoleEnableLogging");
const QString SAPreferences::ConsoleEnableErrorLogging = QStringLiteral("ConsoleEnableErrorLogging");
const QString SAPreferences::ConsoleShowTimestamps = QStringLiteral("ConsoleShowTimestamps");
const QString SAPreferences::ConsoleShowConnections = QStringLiteral("ConsoleShowConnections");
const QString SAPreferences::ConsoleShowDatabases = QStringLiteral("ConsoleShowDatabases");
const QString SAPreferences::ConsoleShowSelectsAndShows = QStringLiteral("ConsoleShowSelectsAndShows");
const QString SAPreferences::EditorFont = QStringLiteral("CustomQueryEditorFont");
const QString SAPreferences::EditorTheme = QStringLiteral("CustomQueryEditorThemeName");
const QString SAPreferences::GlobalResultTableFont = QStringLiteral("GlobalResultTableFont");
const QString SAPreferences::SSHClientPath = QStringLiteral("SSHClientPath");
const QString SAPreferences::SSHConfigFile = QStringLiteral("SSHConfigFile");
const QString SAPreferences::SSHMultiplexingEnabled = QStringLiteral("SSHMultiplexingEnabled");
const QString SAPreferences::SelectLastFavoriteUsed = QStringLiteral("SelectLastFavoriteUsed");
const QString SAPreferences::LastFavoriteId = QStringLiteral("LastFavoriteId");
const QString SAPreferences::DefaultEncoding = QStringLiteral("DefaultEncoding");
const QString SAPreferences::ResetAutoIncrementAfterDeletionOfAllRows = QStringLiteral("ResetAutoIncrementAfterDeletionOfAllRows");
const QString SAPreferences::EditInSheetForLongText = QStringLiteral("EditInSheetForLongText");
const QString SAPreferences::EditInSheetForLongTextLengthThreshold = QStringLiteral("EditInSheetForLongTextLengthThreshold");
const QString SAPreferences::EditInSheetForMultiLineText = QStringLiteral("EditInSheetForMultiLineText");
const QString SAPreferences::TableRowCountQueryLevel = QStringLiteral("TableRowCountQueryLevel");
const QString SAPreferences::TableRowCountCheapLookupSizeBoundary = QStringLiteral("TableRowCountCheapLookupSizeBoundary");
const QString SAPreferences::PasswordStorage = QStringLiteral("PasswordStorage");
const QString SAPreferences::WindowGeometry = QStringLiteral("WindowGeometry");
const QString SAPreferences::WindowState = QStringLiteral("WindowState");
const QString SAPreferences::FavoritesSortedBy = QStringLiteral("FavoritesSortedBy");
const QString SAPreferences::FavoritesSortedInReverse = QStringLiteral("FavoritesSortedInReverse");
const QString SAPreferences::MCPServerEnabled = QStringLiteral("SPMCPServerEnabled");
const QString SAPreferences::MCPServerPort = QStringLiteral("SPMCPServerPort");
const QString SAPreferences::MCPReadOnly = QStringLiteral("SPMCPReadOnly");
const QString SAPreferences::MCPExportPath = QStringLiteral("SPMCPExportPath");

SAPreferences &SAPreferences::instance()
{
    static SAPreferences prefs;
    return prefs;
}

SAPreferences::SAPreferences()
{
    // Values from Resources/Plists/PreferenceDefaults.plist.
    m_defaults = {
        {LimitResults, true},
        {LimitResultsValue, 1000},
        {NullValue, QStringLiteral("NULL")},
        {ReloadAfterAddingRow, true},
        {ReloadAfterEditingRow, true},
        {ReloadAfterRemovingRow, false},
        {ShowNoAffectedRowsError, true},
        {ShowWarningBeforeDeleteQuery, true},
        {ShowWarningBeforeExecuteQuery, false},
        {ConnectionTimeoutValue, 10},
        {UseKeepAlive, true},
        {KeepAliveInterval, 60},
        {CustomQueryMaxHistoryItems, 20},
        {CustomQueryEditorTabStopWidth, 4},
        {CustomQueryAutoUppercaseKeywords, false},
        {CustomQueryHighlightCurrentQuery, true},
        {CustomQueryEnableSyntaxHighlighting, true},
        {CustomQueryAutoPairCharacters, true},
        {CustomQueryAutoIndent, true},
        {CustomQuerySoftIndent, false},
        {CustomQuerySoftIndentWidth, 2},
        {QueryPrimaryControlRunsAll, false},
        {DisplayBinaryDataAsHex, false},
        {LoadBlobsAsNeeded, false},
        {DisplayTableViewColumnTypes, true},
        {DisplayTableViewVerticalGridlines, false},
        {DisplayCommentsInTablesList, false},
        {NewFieldsAllowNulls, true},
        {ConsoleEnableLogging, true},
        {ConsoleEnableErrorLogging, true},
        {ConsoleShowTimestamps, true},
        {ConsoleShowConnections, true},
        {ConsoleShowDatabases, true},
        {ConsoleShowSelectsAndShows, true},
        {EditorFont, QString()},
        {EditorTheme, QString()},
        {GlobalResultTableFont, QString()},
        {SSHClientPath, QString()},
        {SSHConfigFile, QString()},
        {SSHMultiplexingEnabled, false},
        {SelectLastFavoriteUsed, true},
        {LastFavoriteId, -1},
        {DefaultEncoding, QStringLiteral("utf8mb4")},
        {ResetAutoIncrementAfterDeletionOfAllRows, true},
        {EditInSheetForLongText, true},
        {EditInSheetForLongTextLengthThreshold, 15},
        {EditInSheetForMultiLineText, true},
        {TableRowCountQueryLevel, 1},
        {TableRowCountCheapLookupSizeBoundary, 5242880},
        {PasswordStorage, QStringLiteral("keyring")},
        {WindowGeometry, QByteArray()},
        {WindowState, QByteArray()},
        {FavoritesSortedBy, 0},
        {FavoritesSortedInReverse, false},
        {MCPServerEnabled, false},
        {MCPServerPort, 8765},
        {MCPReadOnly, true},
        {MCPExportPath, QString()},
    };
}

QVariant SAPreferences::value(const QString &key) const
{
    return m_settings.value(key, m_defaults.value(key));
}

void SAPreferences::set(const QString &key, const QVariant &value)
{
    if (m_settings.value(key, m_defaults.value(key)) == value) return;
    m_settings.setValue(key, value);
    Q_EMIT changed(key);
}

void SAPreferences::reset(const QString &key)
{
    m_settings.remove(key);
    Q_EMIT changed(key);
}
