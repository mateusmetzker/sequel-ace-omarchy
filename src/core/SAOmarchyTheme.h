//
//  SAOmarchyTheme.h
//  Sequel Ace (Linux port)
//
//  Reads the color palette of the currently active Omarchy theme
//  (~/.local/state/omarchy/current/theme/colors.toml, a symlink Omarchy
//  repoints on every theme switch) and turns it into a QPalette, an
//  application-wide stylesheet, and a matching SQL editor colour scheme, so
//  Sequel Ace looks like a native part of the desktop instead of a generic
//  Qt/Fusion application. Falls back to a sensible dark palette when Omarchy
//  is not present (any other Linux desktop).
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

// The color tokens every Omarchy theme's colors.toml defines. Field names
// mirror the TOML keys (see /usr/share/omarchy/themes/*/colors.toml).
struct SAOmarchyPalette {
    QString themeName;
    bool isDark = true;

    QColor accent;
    QColor selection;
    QColor muted;

    QColor background;
    QColor darkBackground;
    QColor darkerBackground;
    QColor lighterBackground;

    QColor foreground;
    QColor darkForeground;
    QColor lightForeground;
    QColor brightForeground;

    QColor red, yellow, orange, green, cyan, blue, magenta, brown;
    QColor brightRed, brightYellow, brightGreen, brightCyan, brightBlue, brightMagenta;

    // Black or white, whichever reads better on top of `background`.
    QColor contrastFor(const QColor &background) const;
};

namespace SAOmarchyTheme {

// True when an Omarchy installation was found (colors.toml is readable).
bool isAvailable();

// Parses `data` (the raw contents of a colors.toml) into a palette. Missing
// or malformed keys fall back to the built-in defaults. Exposed for testing.
SAOmarchyPalette parse(const QString &data);

// Reads the active theme's colors.toml, or the built-in fallback palette
// when Omarchy is not installed or the file cannot be read.
SAOmarchyPalette current();

// Builds a QPalette from the given Omarchy colors.
QPalette buildQPalette(const SAOmarchyPalette &palette);

// Builds the application stylesheet (buttons, inputs, item views, tabs,
// scrollbars, menus...) driven by the given Omarchy colors.
QString buildStyleSheet(const SAOmarchyPalette &palette);

// Absolute path to the active theme's colors.toml, or empty when not found.
QString activeColorsFilePath();

} // namespace SAOmarchyTheme

// Watches ~/.local/state/omarchy/current/ and emits themeChanged() shortly
// after the user switches themes (e.g. via the Omarchy theme picker), so the
// running application can re-apply its palette without a restart.
class SAOmarchyThemeWatcher : public QObject {
    Q_OBJECT
public:
    explicit SAOmarchyThemeWatcher(QObject *parent = nullptr);

Q_SIGNALS:
    void themeChanged();

private:
    class QFileSystemWatcher *m_watcher;
    class QTimer *m_debounce;
};
