//
//  SAIcons.h
//  Sequel Ace (Linux port)
//
//  Toolbar and object icons. Where the macOS app ships a matching vector
//  template image (Resources/Images/*Template.pdf, converted once to SVG
//  under resources/icons/), that asset is recoloured to match the active
//  desktop theme at render time; glyphs with no macOS equivalent (the table
//  object types, the view-switch icons...) are drawn at runtime with
//  QPainter instead, so the port has no dependency on an icon theme.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QIcon>
#include <QString>

namespace SAIcons {

enum class Glyph {
    Table, View, Procedure, Function, Event, Database, SystemDatabase,
    Structure, Content, Relations, Triggers, Info, Query, Console,
    Add, Remove, Refresh, Gear, Duplicate, Filter, Run, Stop, Left, Right, Edit, Key, Lock, Folder, Connection, Users, Export,
    AddFolder, SelectAll, SelectNone, Search, Socket, QuickConnect
};

QIcon icon(Glyph glyph, const QColor &tint = QColor());

// Distinct pixmaps for the unchecked and checked (QIcon::On) states, for
// checkable QToolButtons whose checked background changes the icon's contrast
// needs (e.g. the view-switch segmented control).
QIcon toggleIcon(Glyph glyph, const QColor &offTint, const QColor &onTint);

QIcon colorDot(const QColor &color, int size = 12);

} // namespace SAIcons
