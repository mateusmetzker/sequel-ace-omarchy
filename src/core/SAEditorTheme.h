//
//  SAEditorTheme.h
//  Sequel Ace (Linux port)
//
//  SQL editor colour schemes. Reads the .spTheme files shipped with the macOS
//  app (TextMate-style plists) and provides the built-in default scheme.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAOmarchyTheme.h"

#include <QColor>
#include <QString>
#include <QStringList>

struct SAEditorTheme {
    QString name;
    QColor background = QColor(0xFF, 0xFF, 0xFF);
    QColor foreground = QColor(0x00, 0x00, 0x00);
    QColor caret = QColor(0x00, 0x00, 0x00);
    QColor lineHighlight = QColor(0xF2, 0xF2, 0xF2);   // current query background
    QColor selection = QColor(0xB3, 0xD7, 0xFF);
    QColor comment = QColor(0x80, 0x80, 0x80);
    QColor string = QColor(0xE6, 0x00, 0x00);          // quoted text
    QColor keyword = QColor(0x00, 0x00, 0xFF);
    QColor backtick = QColor(0x80, 0x40, 0x00);        // "User-defined constant" in spTheme
    QColor number = QColor(0x00, 0x88, 0xCC);
    QColor variable = QColor(0x80, 0x00, 0x80);

    static SAEditorTheme defaultLight();
    static SAEditorTheme defaultDark();

    // Derived from the active Omarchy desktop theme (or the built-in
    // fallback palette on other desktops); this is what an empty/unset
    // editor theme preference resolves to.
    static SAEditorTheme fromOmarchy(const SAOmarchyPalette &palette);
    static const QString SystemThemeName;

    // Names of the bundled themes plus "Default" / "Default Dark".
    static QStringList availableThemeNames();
    static SAEditorTheme themeNamed(const QString &name);
    static SAEditorTheme fromPlistFile(const QString &path, bool *ok = nullptr);
    static SAEditorTheme fromPlistData(const QByteArray &data, const QString &name, bool *ok = nullptr);
};
