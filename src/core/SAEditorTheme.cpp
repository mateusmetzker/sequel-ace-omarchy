//
//  SAEditorTheme.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAEditorTheme.h"
#include "SAPlist.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

const QString SAEditorTheme::SystemThemeName = QStringLiteral("System (Omarchy)");

namespace {
const QString ThemeDirectory = QStringLiteral(":/resources/themes");
const QString DefaultName = QStringLiteral("Default");
const QString DefaultDarkName = QStringLiteral("Default Dark");

QColor colorFromHex(const QString &hex, const QColor &fallback)
{
    QColor c(hex.trimmed());
    return c.isValid() ? c : fallback;
}
}

SAEditorTheme SAEditorTheme::defaultLight()
{
    // Colours decoded from the NSColor archives in the macOS app's PreferenceDefaults.plist.
    SAEditorTheme t;
    t.name = DefaultName;
    t.background = QColor(0xFF, 0xFF, 0xFF);
    t.foreground = QColor(0x00, 0x00, 0x00);
    t.caret = QColor(0x00, 0x00, 0x00);
    t.lineHighlight = QColor(0xF2, 0xF2, 0xF2);
    t.selection = QColor(0xB5, 0xD5, 0xFF);
    t.comment = QColor(0x00, 0x74, 0x00);
    t.string = QColor(0xC5, 0x1A, 0x16);
    t.keyword = QColor(0x33, 0x1A, 0xFF);
    t.backtick = QColor(0x00, 0x00, 0xA8);
    t.number = QColor(0x80, 0x43, 0x00);
    t.variable = QColor(0x7E, 0x7E, 0x7E);
    return t;
}

SAEditorTheme SAEditorTheme::defaultDark()
{
    SAEditorTheme t;
    t.name = DefaultDarkName;
    t.background = QColor(0x1E, 0x1E, 0x1E);
    t.foreground = QColor(0xD4, 0xD4, 0xD4);
    t.caret = QColor(0xFF, 0xFF, 0xFF);
    t.lineHighlight = QColor(0x2A, 0x2D, 0x2E);
    t.selection = QColor(0x26, 0x4F, 0x78);
    t.comment = QColor(0x6A, 0x99, 0x55);
    t.string = QColor(0xCE, 0x91, 0x78);
    t.keyword = QColor(0x56, 0x9C, 0xD6);
    t.backtick = QColor(0xDC, 0xDC, 0xAA);
    t.number = QColor(0xB5, 0xCE, 0xA8);
    t.variable = QColor(0xC5, 0x86, 0xC0);
    return t;
}

QStringList SAEditorTheme::availableThemeNames()
{
    QStringList names{SystemThemeName, DefaultName, DefaultDarkName};
    QDir dir(ThemeDirectory);
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.spTheme")}, QDir::Files, QDir::Name))
        names << fi.completeBaseName();
    return names;
}

SAEditorTheme SAEditorTheme::themeNamed(const QString &name)
{
    // An unset preference follows the desktop theme, matching every other
    // colour in the application; "Default"/"Default Dark" and the bundled
    // macOS themes remain available as an explicit opt-out.
    if (name.isEmpty() || name == SystemThemeName) return fromOmarchy(SAOmarchyTheme::current());
    if (name == DefaultName) return defaultLight();
    if (name == DefaultDarkName) return defaultDark();
    bool ok = false;
    SAEditorTheme t = fromPlistFile(ThemeDirectory + QLatin1Char('/') + name + QStringLiteral(".spTheme"), &ok);
    if (ok) return t;
    // User-installed themes live next to the favorites file.
    return fromOmarchy(SAOmarchyTheme::current());
}

SAEditorTheme SAEditorTheme::fromOmarchy(const SAOmarchyPalette &t)
{
    SAEditorTheme theme;
    theme.name = SystemThemeName;
    theme.background = t.background;
    theme.foreground = t.foreground;
    theme.caret = t.brightForeground;
    theme.lineHighlight = t.isDark ? t.lighterBackground : t.darkBackground;
    theme.selection = t.selection;
    theme.comment = t.darkForeground;
    theme.string = t.green;
    theme.keyword = t.accent;
    theme.backtick = t.magenta;
    theme.number = t.orange;
    theme.variable = t.cyan;
    return theme;
}

SAEditorTheme SAEditorTheme::fromPlistFile(const QString &path, bool *ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        return defaultLight();
    }
    return fromPlistData(file.readAll(), QFileInfo(path).completeBaseName(), ok);
}

SAEditorTheme SAEditorTheme::fromPlistData(const QByteArray &data, const QString &name, bool *ok)
{
    SAEditorTheme t = defaultLight();
    t.name = name;
    QString error;
    const QVariantMap root = SAPlist::read(data, &error).toMap();
    const QVariantList settings = root.value(QStringLiteral("settings")).toList();
    if (!error.isEmpty() || settings.isEmpty()) {
        if (ok) *ok = false;
        return t;
    }
    for (const QVariant &entry : settings) {
        const QVariantMap m = entry.toMap();
        const QVariantMap s = m.value(QStringLiteral("settings")).toMap();
        const QString scope = m.value(QStringLiteral("name")).toString();
        if (scope.isEmpty()) {
            t.background = colorFromHex(s.value(QStringLiteral("background")).toString(), t.background);
            t.foreground = colorFromHex(s.value(QStringLiteral("foreground")).toString(), t.foreground);
            t.caret = colorFromHex(s.value(QStringLiteral("caret")).toString(), t.caret);
            t.lineHighlight = colorFromHex(s.value(QStringLiteral("lineHighlight")).toString(), t.lineHighlight);
            t.selection = colorFromHex(s.value(QStringLiteral("selection")).toString(), t.selection);
            continue;
        }
        const QColor fg = colorFromHex(s.value(QStringLiteral("foreground")).toString(), QColor());
        if (!fg.isValid()) continue;
        if (scope == QLatin1String("Comment")) t.comment = fg;
        else if (scope == QLatin1String("String")) t.string = fg;
        else if (scope == QLatin1String("Keyword")) t.keyword = fg;
        else if (scope == QLatin1String("User-defined constant")) t.backtick = fg;
        else if (scope == QLatin1String("Number")) t.number = fg;
        else if (scope == QLatin1String("Variable")) t.variable = fg;
    }
    if (ok) *ok = true;
    return t;
}
