//
//  SAFieldEditorDialog.h
//  Sequel Ace (Linux port)
//
//  Popup editor for a single cell: text, hex and image views, NULL toggle,
//  load from / save to file. Ports the essentials of SPFieldEditorController.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAResult.h"

#include <QDialog>

class QPlainTextEdit;
class QTabWidget;
class QCheckBox;
class QLabel;

class SAFieldEditorDialog : public QDialog {
    Q_OBJECT
public:
    SAFieldEditorDialog(QWidget *parent, const QString &title, const QString &typeGroup, const QString &typeName,
                        const SACell &value, bool editable, bool allowNull);

    SACell value() const;
    bool valueIsBinary() const { return m_binaryEdited; }

private:
    void updateHexView();
    void updateImageView();
    void loadFromFile();
    void saveToFile();
    void formatJSON();

    QString m_typeGroup;
    SACell m_original;
    QByteArray m_binaryData;
    bool m_binaryEdited = false;
    bool m_editable;
    QTabWidget *m_tabs;
    QPlainTextEdit *m_text;
    QPlainTextEdit *m_hex;
    QLabel *m_image;
    QCheckBox *m_null;
    QLabel *m_info;
};
