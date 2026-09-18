//
//  SAPlist.h
//  Sequel Ace (Linux port)
//
//  Minimal Apple XML property list reader/writer. Sequel Ace stores favorites,
//  editor themes and content filters as XML plists; supporting the format
//  natively lets users copy their macOS files to Linux unchanged.
//
//  dict -> QVariantMap, array -> QVariantList, string -> QString,
//  integer -> qlonglong, real -> double, true/false -> bool,
//  data -> QByteArray, date -> QDateTime (UTC).
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QByteArray>
#include <QString>
#include <QVariant>

namespace SAPlist {

QVariant read(const QByteArray &xml, QString *error = nullptr);
QVariant readFile(const QString &path, QString *error = nullptr);

QByteArray write(const QVariant &root);
bool writeFile(const QString &path, const QVariant &root, QString *error = nullptr);

} // namespace SAPlist
