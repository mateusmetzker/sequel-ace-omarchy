//
//  SAPreferences.h
//  Sequel Ace (Linux port)
//
//  QSettings-backed preferences with the defaults from the macOS app's
//  PreferenceDefaults.plist for every key the Linux port reads.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>

class SAPreferences : public QObject {
    Q_OBJECT
public:
    static SAPreferences &instance();

    QVariant value(const QString &key) const;
    bool boolFor(const QString &key) const { return value(key).toBool(); }
    int intFor(const QString &key) const { return value(key).toInt(); }
    double doubleFor(const QString &key) const { return value(key).toDouble(); }
    QString stringFor(const QString &key) const { return value(key).toString(); }
    void set(const QString &key, const QVariant &value);
    void reset(const QString &key);
    QVariant defaultValue(const QString &key) const { return m_defaults.value(key); }
    void sync() { m_settings.sync(); }

    // Tells listeners bound to `key` (e.g. the SQL editor watching
    // EditorTheme) to re-read it, without the value itself having changed —
    // used when an external source the value derives from changes instead
    // (the desktop theme, for an unset EditorTheme).
    void notifyExternalChange(const QString &key) { Q_EMIT changed(key); }

    // Preference keys (names match the macOS app where the concept exists).
    static const QString LimitResults;
    static const QString LimitResultsValue;
    static const QString NullValue;
    static const QString ReloadAfterAddingRow;
    static const QString ReloadAfterEditingRow;
    static const QString ReloadAfterRemovingRow;
    static const QString ShowNoAffectedRowsError;
    static const QString ShowWarningBeforeDeleteQuery;
    static const QString ShowWarningBeforeExecuteQuery;
    static const QString ConnectionTimeoutValue;
    static const QString UseKeepAlive;
    static const QString KeepAliveInterval;
    static const QString CustomQueryMaxHistoryItems;
    static const QString CustomQueryEditorTabStopWidth;
    static const QString CustomQueryAutoUppercaseKeywords;
    static const QString CustomQueryHighlightCurrentQuery;
    static const QString CustomQueryEnableSyntaxHighlighting;
    static const QString CustomQueryAutoPairCharacters;
    static const QString CustomQueryAutoIndent;
    static const QString CustomQuerySoftIndent;
    static const QString CustomQuerySoftIndentWidth;
    static const QString QueryPrimaryControlRunsAll;
    static const QString DisplayBinaryDataAsHex;
    static const QString LoadBlobsAsNeeded;
    static const QString DisplayTableViewColumnTypes;
    static const QString DisplayTableViewVerticalGridlines;
    static const QString DisplayCommentsInTablesList;
    static const QString NewFieldsAllowNulls;
    static const QString ConsoleEnableLogging;
    static const QString ConsoleEnableErrorLogging;
    static const QString ConsoleShowTimestamps;
    static const QString ConsoleShowConnections;
    static const QString ConsoleShowDatabases;
    static const QString ConsoleShowSelectsAndShows;
    static const QString EditorFont;
    static const QString EditorTheme;
    static const QString GlobalResultTableFont;
    static const QString SSHClientPath;
    static const QString SSHConfigFile;
    static const QString SSHMultiplexingEnabled;
    static const QString SelectLastFavoriteUsed;
    static const QString LastFavoriteId;
    static const QString DefaultEncoding;
    static const QString ResetAutoIncrementAfterDeletionOfAllRows;
    static const QString EditInSheetForLongText;
    static const QString EditInSheetForLongTextLengthThreshold;
    static const QString EditInSheetForMultiLineText;
    static const QString TableRowCountQueryLevel;
    static const QString TableRowCountCheapLookupSizeBoundary;
    static const QString PasswordStorage;   // "keyring" | "none"
    static const QString WindowGeometry;
    static const QString WindowState;
    static const QString FavoritesSortedBy;
    static const QString FavoritesSortedInReverse;
    static const QString MCPServerEnabled;
    static const QString MCPServerPort;
    static const QString MCPReadOnly;
    static const QString MCPExportPath;

Q_SIGNALS:
    void changed(const QString &key);

private:
    SAPreferences();
    QSettings m_settings;
    QVariantMap m_defaults;
};
