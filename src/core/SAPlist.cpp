//
//  SAPlist.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAPlist.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QTimeZone>

namespace SAPlist {

namespace {

QVariant readValue(QXmlStreamReader &xml, QString *error);

QVariant readDict(QXmlStreamReader &xml, QString *error)
{
    QVariantMap map;
    QString pendingKey;
    bool haveKey = false;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("dict")) return map;
        if (token != QXmlStreamReader::StartElement) continue;
        if (xml.name() == QLatin1String("key")) {
            pendingKey = xml.readElementText();
            haveKey = true;
            continue;
        }
        if (!haveKey) {
            if (error) *error = QStringLiteral("plist: value without key in dict (line %1)").arg(xml.lineNumber());
            return QVariant();
        }
        map.insert(pendingKey, readValue(xml, error));
        haveKey = false;
        if (error && !error->isEmpty()) return QVariant();
    }
    return map;
}

QVariant readArray(QXmlStreamReader &xml, QString *error)
{
    QVariantList list;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("array")) return list;
        if (token != QXmlStreamReader::StartElement) continue;
        list.append(readValue(xml, error));
        if (error && !error->isEmpty()) return QVariant();
    }
    return list;
}

// Assumes the reader is positioned on a StartElement of a value.
QVariant readValue(QXmlStreamReader &xml, QString *error)
{
    const QString name = xml.name().toString();
    if (name == QLatin1String("dict")) return readDict(xml, error);
    if (name == QLatin1String("array")) return readArray(xml, error);
    if (name == QLatin1String("string")) return xml.readElementText();
    if (name == QLatin1String("integer")) return xml.readElementText().trimmed().toLongLong();
    if (name == QLatin1String("real")) return xml.readElementText().trimmed().toDouble();
    if (name == QLatin1String("true")) { xml.skipCurrentElement(); return true; }
    if (name == QLatin1String("false")) { xml.skipCurrentElement(); return false; }
    if (name == QLatin1String("data")) return QByteArray::fromBase64(xml.readElementText().simplified().remove(QLatin1Char(' ')).toLatin1());
    if (name == QLatin1String("date")) {
        QDateTime dt = QDateTime::fromString(xml.readElementText().trimmed(), Qt::ISODate);
        dt.setTimeZone(QTimeZone::utc());
        return dt;
    }
    if (error) *error = QStringLiteral("plist: unsupported element <%1> (line %2)").arg(name).arg(xml.lineNumber());
    xml.skipCurrentElement();
    return QVariant();
}

void writeValue(QXmlStreamWriter &xml, const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        xml.writeStartElement(QStringLiteral("dict"));
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            xml.writeTextElement(QStringLiteral("key"), it.key());
            writeValue(xml, it.value());
        }
        xml.writeEndElement();
        return;
    }
    case QMetaType::QVariantHash: {
        writeValue(xml, QVariant(value.toMap()));
        return;
    }
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        xml.writeStartElement(QStringLiteral("array"));
        for (const QVariant &item : value.toList()) writeValue(xml, item);
        xml.writeEndElement();
        return;
    }
    case QMetaType::Bool:
        xml.writeEmptyElement(value.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
        return;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Short:
    case QMetaType::UShort:
        xml.writeTextElement(QStringLiteral("integer"), QString::number(value.toLongLong()));
        return;
    case QMetaType::Double:
    case QMetaType::Float:
        xml.writeTextElement(QStringLiteral("real"), QString::number(value.toDouble(), 'g', 17));
        return;
    case QMetaType::QByteArray:
        xml.writeTextElement(QStringLiteral("data"), QString::fromLatin1(value.toByteArray().toBase64()));
        return;
    case QMetaType::QDateTime:
        xml.writeTextElement(QStringLiteral("date"), value.toDateTime().toUTC().toString(Qt::ISODate));
        return;
    default:
        xml.writeTextElement(QStringLiteral("string"), value.toString());
        return;
    }
}

} // namespace

QVariant read(const QByteArray &data, QString *error)
{
    if (error) error->clear();
    if (data.startsWith("bplist")) {
        if (error) *error = QStringLiteral("Binary property lists are not supported; convert the file with `plutil -convert xml1` on macOS.");
        return QVariant();
    }
    QXmlStreamReader xml(data);
    QVariant root;
    bool sawPlist = false;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token != QXmlStreamReader::StartElement) continue;
        if (xml.name() == QLatin1String("plist")) { sawPlist = true; continue; }
        // The first element inside <plist> is the root object; a bare root
        // object without the <plist> wrapper is tolerated as well.
        root = readValue(xml, error);
        break;
    }
    if (xml.hasError()) {
        if (error) *error = QStringLiteral("plist: %1 (line %2)").arg(xml.errorString()).arg(xml.lineNumber());
        return QVariant();
    }
    if (!sawPlist && !root.isValid() && error && error->isEmpty()) *error = QStringLiteral("plist: no root object found");
    return root;
}

QVariant readFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not open %1: %2").arg(path, file.errorString());
        return QVariant();
    }
    return read(file.readAll(), error);
}

QByteArray write(const QVariant &root)
{
    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(-1);   // one tab per level, like Apple's writer
    xml.writeStartDocument(QStringLiteral("1.0"));
    xml.writeDTD(QStringLiteral("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"));
    xml.writeStartElement(QStringLiteral("plist"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    writeValue(xml, root);
    xml.writeEndElement();
    xml.writeEndDocument();
    return out;
}

bool writeFile(const QString &path, const QVariant &root, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(write(root));
    if (!file.commit()) {
        if (error) *error = QStringLiteral("Could not save %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace SAPlist
