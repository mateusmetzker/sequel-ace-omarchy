//
//  SAOmarchyTheme.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAOmarchyTheme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

namespace {

// ~/.local/state/omarchy/current/theme is the symlink Omarchy repoints at
// the active theme's directory on every switch; the path itself is stable.
// SA_OMARCHY_THEME_DIR overrides it, for testing against a theme other than
// the one actually active on the desktop.
QString stateThemeDir()
{
    const QString override = qEnvironmentVariable("SA_OMARCHY_THEME_DIR");
    if (!override.isEmpty()) return override;
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current/theme");
}

QString colorsFilePath()
{
    const QString path = stateThemeDir() + QStringLiteral("/colors.toml");
    return QFileInfo::exists(path) ? path : QString();
}

// A palette close to the bundled "Default Dark" SQL editor theme, used when
// Omarchy is not installed (any other Linux desktop) or its file is missing.
SAOmarchyPalette fallbackPalette()
{
    SAOmarchyPalette p;
    p.themeName = QStringLiteral("Fallback");
    p.isDark = true;
    p.accent = QColor(0x56, 0x9C, 0xD6);
    p.selection = QColor(0x26, 0x4F, 0x78);
    p.muted = QColor(0x5A, 0x5A, 0x5A);
    p.background = QColor(0x1E, 0x1E, 0x1E);
    p.darkBackground = QColor(0x18, 0x18, 0x18);
    p.darkerBackground = QColor(0x12, 0x12, 0x12);
    p.lighterBackground = QColor(0x2A, 0x2D, 0x2E);
    p.foreground = QColor(0xD4, 0xD4, 0xD4);
    p.darkForeground = QColor(0x80, 0x80, 0x80);
    p.lightForeground = QColor(0xCC, 0xCC, 0xCC);
    p.brightForeground = QColor(0xFF, 0xFF, 0xFF);
    p.red = QColor(0xF4, 0x47, 0x47);
    p.yellow = QColor(0xDC, 0xDC, 0xAA);
    p.orange = QColor(0xCE, 0x91, 0x78);
    p.green = QColor(0x6A, 0x99, 0x55);
    p.cyan = QColor(0x4E, 0xC9, 0xB0);
    p.blue = p.accent;
    p.magenta = QColor(0xC5, 0x86, 0xC0);
    p.brown = QColor(0xB5, 0x89, 0x5F);
    p.brightRed = p.red.lighter(115);
    p.brightYellow = p.yellow.lighter(110);
    p.brightGreen = p.green.lighter(130);
    p.brightCyan = p.cyan.lighter(115);
    p.brightBlue = p.accent.lighter(115);
    p.brightMagenta = p.magenta.lighter(115);
    return p;
}

QColor colorOr(const QHash<QString, QString> &map, const QString &key, const QColor &fallback)
{
    const QString value = map.value(key).trimmed();
    if (value.isEmpty()) return fallback;
    const QColor c(value);
    return c.isValid() ? c : fallback;
}

} // namespace

SAOmarchyPalette SAOmarchyTheme::parse(const QString &data)
{
    const SAOmarchyPalette fallback = fallbackPalette();
    QHash<QString, QString> map;
    // colors.toml is flat key = "value" pairs, one per line; strip comments
    // and quoting rather than pulling in a full TOML parser for this.
    static const QRegularExpression line(QStringLiteral("^\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*\"([^\"]*)\""));
    for (const QString &rawLine : data.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = line.match(rawLine);
        if (m.hasMatch()) map.insert(m.captured(1), m.captured(2));
    }
    if (map.isEmpty()) return fallback;

    SAOmarchyPalette p;
    p.themeName = map.value(QStringLiteral("name"));
    p.isDark = map.value(QStringLiteral("mode")).compare(QStringLiteral("light"), Qt::CaseInsensitive) != 0;

    p.accent = colorOr(map, QStringLiteral("accent"), fallback.accent);
    p.selection = colorOr(map, QStringLiteral("selection"), fallback.selection);
    p.muted = colorOr(map, QStringLiteral("muted"), fallback.muted);

    p.background = colorOr(map, QStringLiteral("background"), fallback.background);
    p.darkBackground = colorOr(map, QStringLiteral("dark_background"), fallback.darkBackground);
    p.darkerBackground = colorOr(map, QStringLiteral("darker_background"), fallback.darkerBackground);
    p.lighterBackground = colorOr(map, QStringLiteral("lighter_background"), fallback.lighterBackground);

    p.foreground = colorOr(map, QStringLiteral("foreground"), fallback.foreground);
    p.darkForeground = colorOr(map, QStringLiteral("dark_foreground"), fallback.darkForeground);
    p.lightForeground = colorOr(map, QStringLiteral("light_foreground"), fallback.lightForeground);
    p.brightForeground = colorOr(map, QStringLiteral("bright_foreground"), fallback.brightForeground);

    p.red = colorOr(map, QStringLiteral("red"), fallback.red);
    p.yellow = colorOr(map, QStringLiteral("yellow"), fallback.yellow);
    p.orange = colorOr(map, QStringLiteral("orange"), fallback.orange);
    p.green = colorOr(map, QStringLiteral("green"), fallback.green);
    p.cyan = colorOr(map, QStringLiteral("cyan"), fallback.cyan);
    p.blue = colorOr(map, QStringLiteral("blue"), fallback.blue);
    p.magenta = colorOr(map, QStringLiteral("magenta"), fallback.magenta);
    p.brown = colorOr(map, QStringLiteral("brown"), fallback.brown);

    p.brightRed = colorOr(map, QStringLiteral("bright_red"), p.red);
    p.brightYellow = colorOr(map, QStringLiteral("bright_yellow"), p.yellow);
    p.brightGreen = colorOr(map, QStringLiteral("bright_green"), p.green);
    p.brightCyan = colorOr(map, QStringLiteral("bright_cyan"), p.cyan);
    p.brightBlue = colorOr(map, QStringLiteral("bright_blue"), p.blue);
    p.brightMagenta = colorOr(map, QStringLiteral("bright_magenta"), p.magenta);
    return p;
}

QColor SAOmarchyPalette::contrastFor(const QColor &bg) const
{
    // Perceived-brightness threshold (ITU-R BT.601); reliable across every
    // Omarchy theme without special-casing light vs dark ones.
    const double luminance = (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()) / 255.0;
    return luminance > 0.6 ? QColor(0x10, 0x10, 0x10) : QColor(0xF5, 0xF5, 0xF5);
}

bool SAOmarchyTheme::isAvailable()
{
    return !colorsFilePath().isEmpty();
}

QString SAOmarchyTheme::activeColorsFilePath()
{
    return colorsFilePath();
}

SAOmarchyPalette SAOmarchyTheme::current()
{
    const QString path = colorsFilePath();
    if (path.isEmpty()) return fallbackPalette();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return fallbackPalette();
    SAOmarchyPalette palette = parse(QString::fromUtf8(file.readAll()));
    if (palette.themeName.isEmpty()) {
        // colors.toml itself carries no theme name; the directory it lives
        // in (via the "theme" symlink) is named after it.
        palette.themeName = QFileInfo(QFileInfo(path).absolutePath()).fileName();
    }
    return palette;
}

QPalette SAOmarchyTheme::buildQPalette(const SAOmarchyPalette &t)
{
    QPalette pal;
    const QColor highlightText = t.contrastFor(t.accent);
    const QColor disabledText = QColor::fromRgb(
        (t.foreground.red() + t.background.red()) / 2,
        (t.foreground.green() + t.background.green()) / 2,
        (t.foreground.blue() + t.background.blue()) / 2);

    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool enabled = group != QPalette::Disabled;
        pal.setColor(group, QPalette::Window, t.background);
        pal.setColor(group, QPalette::WindowText, enabled ? t.foreground : disabledText);
        pal.setColor(group, QPalette::Base, t.lighterBackground);
        pal.setColor(group, QPalette::AlternateBase, t.darkBackground);
        pal.setColor(group, QPalette::Text, enabled ? t.foreground : disabledText);
        pal.setColor(group, QPalette::Button, t.lighterBackground);
        pal.setColor(group, QPalette::ButtonText, enabled ? t.foreground : disabledText);
        pal.setColor(group, QPalette::BrightText, t.brightForeground);
        pal.setColor(group, QPalette::PlaceholderText, t.darkForeground);
        pal.setColor(group, QPalette::ToolTipBase, t.darkerBackground);
        pal.setColor(group, QPalette::ToolTipText, t.foreground);
        pal.setColor(group, QPalette::Highlight, enabled ? t.accent : t.muted);
        pal.setColor(group, QPalette::HighlightedText, enabled ? highlightText : disabledText);
        pal.setColor(group, QPalette::Link, t.accent);
        pal.setColor(group, QPalette::LinkVisited, t.accent.darker(120));
        pal.setColor(group, QPalette::Mid, t.muted);
        pal.setColor(group, QPalette::Midlight, t.darkBackground);
        pal.setColor(group, QPalette::Light, t.lighterBackground.lighter(112));
        pal.setColor(group, QPalette::Dark, t.darkerBackground);
        pal.setColor(group, QPalette::Shadow, t.darkerBackground.darker(140));
    }
    return pal;
}

QString SAOmarchyTheme::buildStyleSheet(const SAOmarchyPalette &t)
{
    auto rgba = [](const QColor &c, double alpha) {
        QColor copy = c;
        copy.setAlphaF(qBound(0.0, alpha, 1.0));
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(copy.red()).arg(copy.green()).arg(copy.blue()).arg(copy.alphaF());
    };
    const QString accent = t.accent.name();
    const QString accentText = t.contrastFor(t.accent).name();
    const QString fg = t.foreground.name();
    const QString mutedBorder = rgba(t.muted, 0.55);
    const QString mutedFaint = rgba(t.muted, 0.22);
    const QString hoverTint = rgba(t.accent, 0.16);
    const QString pressedTint = rgba(t.accent, 0.28);
    const QString base = t.lighterBackground.name();
    const QString dark = t.darkBackground.name();
    const QString darker = t.darkerBackground.name();
    const QString bg = t.background.name();
    const QString selection = t.selection.name();
    const QString scrollHandle = rgba(t.muted, 0.6);
    const QString scrollHandleHover = rgba(t.accent, 0.65);

    // clang-format off
    QString result = QStringLiteral(
    "QToolTip {"
    "  background: %1; color: %2; border: 1px solid %3; border-radius: 5px; padding: 4px 8px; }"

    "QPushButton, QToolButton {"
    "  background: transparent; color: %2; border: 1px solid transparent;"
    "  border-radius: 6px; padding: 4px 10px; }"
    "QPushButton:hover, QToolButton:hover { background: %4; }"
    "QPushButton:pressed, QToolButton:pressed { background: %5; }"
    "QPushButton:checked, QToolButton:checked { background: %6; color: %7; }"
    "QPushButton:disabled, QToolButton:disabled { color: %3; }"
    "QPushButton:default { border: 1px solid %6; }"
    "QToolButton::menu-indicator { width: 0px; }"

    "QLineEdit, QComboBox, QAbstractSpinBox, QListWidget#queryFavoritesList {"
    "  background: %8; color: %2; border: 1px solid %3; border-radius: 6px; padding: 3px 6px;"
    "  selection-background-color: %6; selection-color: %7; }"
    "QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus {"
    "  border: 1px solid %6; }"
    "QLineEdit:disabled, QComboBox:disabled { color: %3; }"
    "QComboBox::drop-down { border: none; width: 20px; }"
    "QComboBox QAbstractItemView {"
    "  background: %9; color: %2; border: 1px solid %3; selection-background-color: %6; selection-color: %7; }"

    "QGroupBox {"
    "  border: 1px solid %3; border-radius: 8px; margin-top: 14px; padding-top: 6px; font-weight: 600; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"

    "QTableView, QTreeView, QListView {"
    "  background: %8; alternate-background-color: %10; color: %2;"
    "  border: 1px solid %3; border-radius: 6px; gridline-color: %11; outline: none; }"
    "QTableView::item, QTreeView::item, QListView::item { padding: 2px 4px; }"
    "QTableView::item:selected, QTreeView::item:selected, QListView::item:selected {"
    "  background: %6; color: %7; }"
    "QHeaderView::section {"
    "  background: %10; color: %2; border: none; border-right: 1px solid %11;"
    "  border-bottom: 1px solid %11; padding: 4px 6px; }"
    "QHeaderView::section:first { border-top-left-radius: 6px; }"
    "QTableCornerButton::section { background: %10; border: none; }"

    "#favoritesPanel, #tablesListPanel {"
    "  background: %12; }"
    "#favoritesPanel QLineEdit {"
    "  background: %12; border: 1px solid %11; border-radius: 9px; padding: 4px 8px; }"
    "#favoritesPanel QTreeView, #favoritesPanel QListWidget, #tablesListPanel QListWidget {"
    "  background: %12; border: none; border-radius: 0px; show-decoration-selected: 1; }"
    "#favoritesPanel QTreeView::branch { background: transparent; }"
    "#favoritesPanel QTreeView::item, #favoritesPanel QListWidget::item, #tablesListPanel QListWidget::item {"
    "  padding: 5px 6px; margin: 1px 6px; border-radius: 6px; border: none; }"
    "#favoritesPanel QTreeView::item:hover:!selected, #favoritesPanel QListWidget::item:hover:!selected,"
    " #tablesListPanel QListWidget::item:hover:!selected {"
    "  background: %4; }"
    "#quickConnectRow { border: none; margin: 1px 6px 6px 6px; }"
    "#tablesListInfo { background: %10; border: none; border-top: 1px solid %11; }"

    "#documentToolbar { background: %9; border-bottom: 1px solid %11; }"
    "#viewSwitchBar { background: %10; border-radius: 8px; padding: 2px; }"
    "#viewSwitchBar QToolButton {"
    "  border-radius: 6px; padding: 4px 12px; }"
    "#viewSwitchBar QToolButton:checked { background: %6; color: %7; }"

    "QTabWidget::pane { border: none; border-top: 1px solid %11; top: -1px; }"
    "QTabBar::tab {"
    "  background: transparent; color: %2; padding: 6px 14px; margin-right: 2px;"
    "  border-top-left-radius: 6px; border-top-right-radius: 6px; }"
    "QTabBar::tab:selected { background: %9; border-bottom: 2px solid %6; }"
    "QTabBar::tab:hover:!selected { background: %4; }"
    "QTabBar::close-button { padding: 2px; }"

    "QMenuBar { background: %13; color: %2; border-bottom: 1px solid %11; }"
    "QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 5px; }"
    "QMenuBar::item:selected { background: %4; }"
    "QMenu { background: %9; color: %2; border: 1px solid %3; border-radius: 8px; padding: 4px; }"
    "QMenu::item { padding: 5px 24px 5px 12px; border-radius: 5px; }"
    "QMenu::item:selected { background: %6; color: %7; }"
    "QMenu::separator { height: 1px; background: %11; margin: 4px 8px; }"

    "QSplitter::handle { background: %11; }"
    "QSplitter::handle:horizontal { width: 1px; }"
    "QSplitter::handle:vertical { height: 1px; }"
    "QSplitter::handle:hover { background: %6; }"

    "QProgressBar { background: %10; border: none; border-radius: 4px; text-align: center; color: %2; }"
    "QProgressBar::chunk { background: %6; border-radius: 4px; }"

    "QCheckBox::indicator, QRadioButton::indicator {"
    "  width: 15px; height: 15px; border: 1px solid %3; background: %8; }"
    "QCheckBox::indicator { border-radius: 4px; }"
    "QRadioButton::indicator { border-radius: 8px; }"
    "QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: %6; border-color: %6; }"

    "QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }"
    "QScrollBar::handle:vertical { background: %14; border-radius: 5px; min-height: 24px; }"
    "QScrollBar::handle:vertical:hover { background: %15; }"
    "QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }"
    "QScrollBar::handle:horizontal { background: %14; border-radius: 5px; min-width: 24px; }"
    "QScrollBar::handle:horizontal:hover { background: %15; }"
    "QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; border: none; }"
    "QScrollBar::add-page, QScrollBar::sub-page { background: none; }"

    "QStatusBar { background: %9; border-top: 1px solid %11; }"
    "QDialog { background: %16; }"
    );
    const QStringList args = {darker, fg, mutedBorder, hoverTint, pressedTint, accent, accentText, base, bg, dark, mutedFaint, darker, dark, scrollHandle, scrollHandleHover, bg};
    for (const QString &value : args) result = result.arg(value);
    return result;
    // clang-format on
}

// ---- live theme switching ----------------------------------------------------

SAOmarchyThemeWatcher::SAOmarchyThemeWatcher(QObject *parent)
    : QObject(parent)
{
    m_watcher = new QFileSystemWatcher(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);
    connect(m_debounce, &QTimer::timeout, this, &SAOmarchyThemeWatcher::themeChanged);

    // Theme switches replace the "current" symlink (and rewrite files inside
    // the target directory); watch the parent so a directory replacement is
    // seen even though the old watched path stops existing.
    const QString currentDir = QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
    if (QFileInfo::exists(currentDir)) m_watcher->addPath(currentDir);
    const QString colors = SAOmarchyTheme::activeColorsFilePath();
    if (!colors.isEmpty()) m_watcher->addPath(colors);

    auto restart = [this, currentDir]() {
        m_debounce->start();
        // A watched file can disappear across a theme switch (new inode);
        // re-arm on the new one so future switches keep being noticed.
        const QString colors = SAOmarchyTheme::activeColorsFilePath();
        if (!colors.isEmpty() && !m_watcher->files().contains(colors)) m_watcher->addPath(colors);
        if (QFileInfo::exists(currentDir) && !m_watcher->directories().contains(currentDir)) m_watcher->addPath(currentDir);
    };
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, restart);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, restart);
}
