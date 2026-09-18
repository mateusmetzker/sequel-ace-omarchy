//
//  SAPreferencesDialog.h
//  Sequel Ace (Linux port)
//
//  Preferences: General, Tables, Editor, Network and Security tabs bound to
//  SAPreferences.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QDialog>

class QCheckBox;
class QSpinBox;
class QLineEdit;
class QComboBox;
class QFontComboBox;
class QDoubleSpinBox;
class QLabel;

class SAPreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit SAPreferencesDialog(QWidget *parent = nullptr);

private:
    void load();
    void apply();
    QWidget *buildGeneral();
    QWidget *buildTables();
    QWidget *buildEditor();
    QWidget *buildNetwork();
    QWidget *buildMCP();

    QCheckBox *m_warnBeforeDelete;
    QCheckBox *m_warnBeforeExecute;
    QCheckBox *m_selectLastFavorite;
    QCheckBox *m_consoleLogging;
    QCheckBox *m_savePasswords;
    QComboBox *m_defaultView;

    QCheckBox *m_limitResults;
    QSpinBox *m_limitValue;
    QLineEdit *m_nullValue;
    QCheckBox *m_reloadAfterAdd;
    QCheckBox *m_reloadAfterEdit;
    QCheckBox *m_reloadAfterRemove;
    QCheckBox *m_showNoAffectedRows;
    QCheckBox *m_binaryAsHex;
    QCheckBox *m_showColumnTypes;
    QCheckBox *m_verticalGridlines;
    QCheckBox *m_showTableComments;
    QCheckBox *m_newFieldsAllowNull;
    QComboBox *m_rowCountLevel;

    QFontComboBox *m_editorFontFamily;
    QSpinBox *m_editorFontSize;
    QComboBox *m_editorTheme;
    QSpinBox *m_tabWidth;
    QCheckBox *m_highlightCurrentQuery;
    QCheckBox *m_syntaxHighlighting;
    QCheckBox *m_autoPair;
    QCheckBox *m_autoIndent;
    QCheckBox *m_softIndent;
    QSpinBox *m_softIndentWidth;
    QCheckBox *m_autoComplete;
    QDoubleSpinBox *m_autoCompleteDelay;
    QSpinBox *m_historyItems;

    QSpinBox *m_connectionTimeout;
    QCheckBox *m_keepAlive;
    QSpinBox *m_keepAliveInterval;
    QLineEdit *m_sshClient;
    QLineEdit *m_sshConfig;
    QCheckBox *m_sshMuxing;
    QComboBox *m_defaultEncoding;

    QCheckBox *m_mcpEnabled;
    QSpinBox *m_mcpPort;
    QCheckBox *m_mcpReadOnly;
    QLineEdit *m_mcpExportPath;
    QLabel *m_mcpStatusLabel;
};
