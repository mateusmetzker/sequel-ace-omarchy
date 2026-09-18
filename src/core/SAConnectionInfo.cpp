//
//  SAConnectionInfo.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAConnectionInfo.h"

#include <QObject>
#include <QSet>

namespace {

// Keys as written by the macOS app (SPConstants.m).
namespace Key {
const QString id = QStringLiteral("id");
const QString type = QStringLiteral("type");
const QString name = QStringLiteral("name");
const QString host = QStringLiteral("host");
const QString socket = QStringLiteral("socket");
const QString user = QStringLiteral("user");
const QString colorIndex = QStringLiteral("colorIndex");
const QString port = QStringLiteral("port");
const QString database = QStringLiteral("database");
const QString useCompression = QStringLiteral("useCompression");
const QString timeZoneMode = QStringLiteral("timeZoneMode");
const QString timeZoneIdentifier = QStringLiteral("timeZone");
const QString allowDataLocalInfile = QStringLiteral("allowDataLocalInfile");
const QString enableClearTextPlugin = QStringLiteral("enableClearTextPlugin");
const QString requestServerPublicKey = QStringLiteral("requestServerPublicKey");
const QString useSSL = QStringLiteral("useSSL");
const QString sslKeyFileLocationEnabled = QStringLiteral("sslKeyFileLocationEnabled");
const QString sslKeyFileLocation = QStringLiteral("sslKeyFileLocation");
const QString sslCertificateFileLocationEnabled = QStringLiteral("sslCertificateFileLocationEnabled");
const QString sslCertificateFileLocation = QStringLiteral("sslCertificateFileLocation");
const QString sslCACertFileLocationEnabled = QStringLiteral("sslCACertFileLocationEnabled");
const QString sslCACertFileLocation = QStringLiteral("sslCACertFileLocation");
const QString sshHost = QStringLiteral("sshHost");
const QString sshUser = QStringLiteral("sshUser");
const QString sshKeyLocationEnabled = QStringLiteral("sshKeyLocationEnabled");
const QString sshKeyLocation = QStringLiteral("sshKeyLocation");
const QString sshPort = QStringLiteral("sshPort");
const QString sshRemoteSocketPath = QStringLiteral("sshRemoteSocketPath");
} // namespace Key

const QSet<QString> &knownKeys()
{
    static const QSet<QString> keys = {
        Key::id, Key::type, Key::name, Key::host, Key::socket, Key::user, Key::colorIndex, Key::port, Key::database,
        Key::useCompression, Key::timeZoneMode, Key::timeZoneIdentifier, Key::allowDataLocalInfile,
        Key::enableClearTextPlugin, Key::requestServerPublicKey, Key::useSSL, Key::sslKeyFileLocationEnabled,
        Key::sslKeyFileLocation, Key::sslCertificateFileLocationEnabled, Key::sslCertificateFileLocation,
        Key::sslCACertFileLocationEnabled, Key::sslCACertFileLocation, Key::sshHost, Key::sshUser,
        Key::sshKeyLocationEnabled, Key::sshKeyLocation, Key::sshPort, Key::sshRemoteSocketPath,
    };
    return keys;
}

int intValue(const QVariant &v, int fallback)
{
    if (!v.isValid() || v.isNull()) return fallback;
    bool ok = false;
    const int i = v.toString().trimmed().toInt(&ok);
    if (ok) return i;
    if (v.typeId() == QMetaType::Bool) return v.toBool() ? 1 : 0;
    return fallback;
}

bool boolValue(const QVariant &v, bool fallback)
{
    if (!v.isValid() || v.isNull()) return fallback;
    if (v.typeId() == QMetaType::Bool) return v.toBool();
    const QString s = v.toString().trimmed().toLower();
    if (s == QLatin1String("1") || s == QLatin1String("true") || s == QLatin1String("yes")) return true;
    if (s == QLatin1String("0") || s == QLatin1String("false") || s == QLatin1String("no") || s.isEmpty()) return false;
    return fallback;
}

QString stringValue(const QVariant &v, const QString &fallback = QString())
{
    if (!v.isValid() || v.isNull()) return fallback;
    return v.toString();
}

} // namespace

unsigned int SAConnectionInfo::effectivePort() const
{
    bool ok = false;
    const unsigned int p = port.trimmed().toUInt(&ok);
    return (ok && p > 0 && p <= 65535) ? p : 3306u;
}

unsigned int SAConnectionInfo::effectiveSSHPort() const
{
    bool ok = false;
    const unsigned int p = sshPort.trimmed().toUInt(&ok);
    return (ok && p > 0 && p <= 65535) ? p : 0u;
}

QString SAConnectionInfo::displayName() const
{
    if (!name.trimmed().isEmpty()) return name;
    if (type == SAConnectionType::Socket) return user.isEmpty() ? QStringLiteral("localhost") : user + QStringLiteral("@localhost");
    const QString h = host.isEmpty() ? QStringLiteral("localhost") : host;
    return user.isEmpty() ? h : user + QLatin1Char('@') + h;
}

QString SAConnectionInfo::hostDescription() const
{
    switch (type) {
    case SAConnectionType::Socket:
        return socket.isEmpty() ? QStringLiteral("local socket") : socket;
    case SAConnectionType::SSHTunnel:
        return QStringLiteral("%1 via ssh %2%3").arg(host.isEmpty() ? QStringLiteral("127.0.0.1") : host,
                                                     sshUser.isEmpty() ? QString() : sshUser + QLatin1Char('@'), sshHost);
    default:
        return QStringLiteral("%1:%2").arg(host.isEmpty() ? QStringLiteral("127.0.0.1") : host).arg(effectivePort());
    }
}

bool SAConnectionInfo::isValidForConnecting(QString *problem) const
{
    if (type == SAConnectionType::SSHTunnel && sshHost.trimmed().isEmpty()) {
        if (problem) *problem = QStringLiteral("Please enter the SSH host to connect through.");
        return false;
    }
    if (type == SAConnectionType::AWSIAM || type == SAConnectionType::Vault) {
        if (problem) *problem = QStringLiteral("AWS IAM and Vault authentication are not available in the Linux version yet.");
        return false;
    }
    if (!port.trimmed().isEmpty()) {
        bool ok = false;
        const int p = port.trimmed().toInt(&ok);
        if (!ok || p < 1 || p > 65535) {
            if (problem) *problem = QStringLiteral("The MySQL port must be a number between 1 and 65535.");
            return false;
        }
    }
    if (type == SAConnectionType::SSHTunnel && !sshPort.trimmed().isEmpty()) {
        bool ok = false;
        const int p = sshPort.trimmed().toInt(&ok);
        if (!ok || p < 1 || p > 65535) {
            if (problem) *problem = QStringLiteral("The SSH port must be a number between 1 and 65535.");
            return false;
        }
    }
    return true;
}

QString SAConnectionInfo::resolvedMySQLHost() const
{
    const QString trimmed = host.trimmed();
    const QString lowered = trimmed.toLower();
    switch (type) {
    case SAConnectionType::Socket:
        return QString();
    case SAConnectionType::SSHTunnel:
        if (lowered == QLatin1String("localhost")) return lowered;
        if (lowered == QLatin1String("127.0.0.1") || lowered == QLatin1String("::1")) return trimmed;
        return QStringLiteral("127.0.0.1");
    default:
        return trimmed.isEmpty() ? QStringLiteral("127.0.0.1") : trimmed;
    }
}

SAMySQLOptions SAConnectionInfo::toMySQLOptions() const
{
    SAMySQLOptions o;
    o.host = resolvedMySQLHost();
    o.port = effectivePort();
    if (type == SAConnectionType::Socket) o.socketPath = socket.trimmed();
    o.user = user;
    o.password = password;
    o.database = database.trimmed();
    o.useCompression = useCompression;
    o.allowLocalInfile = allowDataLocalInfile;
    o.enableClearTextPlugin = enableClearTextPlugin;
    o.requestServerPublicKey = requestServerPublicKey;
    o.useSSL = useSSL;
    if (useSSL) {
        if (sslKeyFileLocationEnabled) o.sslKeyPath = sslKeyFileLocation;
        if (sslCertificateFileLocationEnabled) o.sslCertPath = sslCertificateFileLocation;
        if (sslCACertFileLocationEnabled) o.sslCAPath = sslCACertFileLocation;
    }
    o.timeZoneMode = static_cast<int>(timeZoneMode);
    o.timeZoneIdentifier = timeZoneIdentifier;
    return o;
}

SAConnectionInfo SAConnectionInfo::fromFavoriteDictionary(const QVariantMap &fav)
{
    SAConnectionInfo info;
    info.id = intValue(fav.value(Key::id), -1);
    const int rawType = intValue(fav.value(Key::type), 0);
    info.type = (rawType >= 0 && rawType <= 4) ? static_cast<SAConnectionType>(rawType) : SAConnectionType::TCPIP;
    info.name = stringValue(fav.value(Key::name));
    info.host = stringValue(fav.value(Key::host));
    info.socket = stringValue(fav.value(Key::socket));
    info.user = stringValue(fav.value(Key::user));
    info.colorIndex = intValue(fav.value(Key::colorIndex), -1);
    info.port = stringValue(fav.value(Key::port));
    info.database = stringValue(fav.value(Key::database));
    info.useCompression = boolValue(fav.value(Key::useCompression), true);
    const int rawTz = intValue(fav.value(Key::timeZoneMode), 0);
    info.timeZoneMode = (rawTz >= 0 && rawTz <= 2) ? static_cast<SATimeZoneMode>(rawTz) : SATimeZoneMode::Server;
    info.timeZoneIdentifier = info.timeZoneMode == SATimeZoneMode::Fixed ? stringValue(fav.value(Key::timeZoneIdentifier)) : QString();
    info.allowDataLocalInfile = boolValue(fav.value(Key::allowDataLocalInfile), false);
    info.enableClearTextPlugin = boolValue(fav.value(Key::enableClearTextPlugin), false);
    info.requestServerPublicKey = boolValue(fav.value(Key::requestServerPublicKey), false);
    info.useSSL = boolValue(fav.value(Key::useSSL), false);
    info.sslKeyFileLocationEnabled = boolValue(fav.value(Key::sslKeyFileLocationEnabled), false);
    info.sslKeyFileLocation = stringValue(fav.value(Key::sslKeyFileLocation));
    info.sslCertificateFileLocationEnabled = boolValue(fav.value(Key::sslCertificateFileLocationEnabled), false);
    info.sslCertificateFileLocation = stringValue(fav.value(Key::sslCertificateFileLocation));
    info.sslCACertFileLocationEnabled = boolValue(fav.value(Key::sslCACertFileLocationEnabled), false);
    info.sslCACertFileLocation = stringValue(fav.value(Key::sslCACertFileLocation));
    info.sshHost = stringValue(fav.value(Key::sshHost));
    info.sshUser = stringValue(fav.value(Key::sshUser));
    info.sshKeyLocationEnabled = boolValue(fav.value(Key::sshKeyLocationEnabled), false);
    info.sshKeyLocation = stringValue(fav.value(Key::sshKeyLocation));
    info.sshPort = stringValue(fav.value(Key::sshPort));
    info.sshRemoteSocketPath = stringValue(fav.value(Key::sshRemoteSocketPath));

    for (auto it = fav.constBegin(); it != fav.constEnd(); ++it)
        if (!knownKeys().contains(it.key())) info.passthrough.insert(it.key(), it.value());
    return info;
}

QVariantMap SAConnectionInfo::toFavoriteDictionary() const
{
    QVariantMap fav = passthrough;
    fav.insert(Key::id, static_cast<qlonglong>(id));
    fav.insert(Key::type, static_cast<qlonglong>(static_cast<int>(type)));
    fav.insert(Key::name, name);
    fav.insert(Key::host, host);
    fav.insert(Key::socket, socket);
    fav.insert(Key::user, user);
    fav.insert(Key::colorIndex, static_cast<qlonglong>(colorIndex));
    fav.insert(Key::port, port);
    fav.insert(Key::database, database);
    fav.insert(Key::useCompression, static_cast<qlonglong>(useCompression ? 1 : 0));
    fav.insert(Key::timeZoneMode, static_cast<qlonglong>(static_cast<int>(timeZoneMode)));
    fav.insert(Key::timeZoneIdentifier, timeZoneMode == SATimeZoneMode::Fixed ? timeZoneIdentifier : QString());
    fav.insert(Key::allowDataLocalInfile, static_cast<qlonglong>(allowDataLocalInfile ? 1 : 0));
    fav.insert(Key::enableClearTextPlugin, static_cast<qlonglong>(enableClearTextPlugin ? 1 : 0));
    fav.insert(Key::requestServerPublicKey, static_cast<qlonglong>(requestServerPublicKey ? 1 : 0));
    fav.insert(Key::useSSL, static_cast<qlonglong>(useSSL ? 1 : 0));
    fav.insert(Key::sslKeyFileLocationEnabled, static_cast<qlonglong>(sslKeyFileLocationEnabled ? 1 : 0));
    fav.insert(Key::sslKeyFileLocation, sslKeyFileLocation);
    fav.insert(Key::sslCertificateFileLocationEnabled, static_cast<qlonglong>(sslCertificateFileLocationEnabled ? 1 : 0));
    fav.insert(Key::sslCertificateFileLocation, sslCertificateFileLocation);
    fav.insert(Key::sslCACertFileLocationEnabled, static_cast<qlonglong>(sslCACertFileLocationEnabled ? 1 : 0));
    fav.insert(Key::sslCACertFileLocation, sslCACertFileLocation);
    fav.insert(Key::sshHost, sshHost);
    fav.insert(Key::sshUser, sshUser);
    fav.insert(Key::sshKeyLocationEnabled, static_cast<qlonglong>(sshKeyLocationEnabled ? 1 : 0));
    fav.insert(Key::sshKeyLocation, sshKeyLocation);
    fav.insert(Key::sshPort, sshPort);
    fav.insert(Key::sshRemoteSocketPath, sshRemoteSocketPath);
    return fav;
}

bool SAConnectionInfo::operator==(const SAConnectionInfo &o) const
{
    return id == o.id && name == o.name && type == o.type && colorIndex == o.colorIndex && host == o.host
        && user == o.user && password == o.password && database == o.database && socket == o.socket && port == o.port
        && useCompression == o.useCompression && timeZoneMode == o.timeZoneMode && timeZoneIdentifier == o.timeZoneIdentifier
        && allowDataLocalInfile == o.allowDataLocalInfile && enableClearTextPlugin == o.enableClearTextPlugin
        && requestServerPublicKey == o.requestServerPublicKey && useSSL == o.useSSL
        && sslKeyFileLocationEnabled == o.sslKeyFileLocationEnabled && sslKeyFileLocation == o.sslKeyFileLocation
        && sslCertificateFileLocationEnabled == o.sslCertificateFileLocationEnabled
        && sslCertificateFileLocation == o.sslCertificateFileLocation
        && sslCACertFileLocationEnabled == o.sslCACertFileLocationEnabled && sslCACertFileLocation == o.sslCACertFileLocation
        && sshHost == o.sshHost && sshUser == o.sshUser && sshPassword == o.sshPassword
        && sshKeyLocationEnabled == o.sshKeyLocationEnabled && sshKeyLocation == o.sshKeyLocation && sshPort == o.sshPort
        && sshRemoteSocketPath == o.sshRemoteSocketPath;
}

namespace SAFavoriteColors {

namespace {
const QColor kColors[] = {
    QColor(0xE4, 0x74, 0x66), QColor(0xED, 0xAE, 0x6B), QColor(0xE3, 0xD5, 0x77), QColor(0xAF, 0xD7, 0x77),
    QColor(0x76, 0xB9, 0xE8), QColor(0xCA, 0x98, 0xE0), QColor(0xB6, 0xB6, 0xB6),
};
const char *const kNames[] = {"Red", "Orange", "Yellow", "Green", "Blue", "Purple", "Gray"};
}

int count() { return 7; }

QColor color(int index)
{
    if (index < 0 || index >= count()) return QColor();
    return kColors[index];
}

QString name(int index)
{
    if (index < 0 || index >= count()) return QObject::tr("None");
    return QObject::tr(kNames[index]);
}

} // namespace SAFavoriteColors
