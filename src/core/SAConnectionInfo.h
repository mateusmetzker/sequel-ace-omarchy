//
//  SAConnectionInfo.h
//  Sequel Ace (Linux port)
//
//  Value type holding every parameter needed to open a connection. Mirrors
//  SAConnectionInfo.swift and its favorite-dictionary mapping so the
//  Favorites.plist written by the macOS app round-trips unchanged.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAMySQLConnection.h"

#include <QColor>
#include <QString>
#include <QVariantMap>

enum class SAConnectionType {
    TCPIP = 0,
    Socket = 1,
    SSHTunnel = 2,
    AWSIAM = 3,     // not supported on Linux yet; kept for file compatibility
    Vault = 4,      // not supported on Linux yet; kept for file compatibility
};

enum class SATimeZoneMode {
    Server = 0,
    System = 1,
    Fixed = 2,
};

struct SAConnectionInfo {
    // Identity
    int id = -1;                       // favorite id; -1 for unsaved connections
    QString name;
    SAConnectionType type = SAConnectionType::TCPIP;
    int colorIndex = -1;

    // Basic connection
    QString host;
    QString user;
    QString password;                  // in-memory only; never written to the favorites file
    QString database;
    QString socket;
    QString port;                      // kept as text like the macOS app ("" = default 3306)
    bool useCompression = true;

    // Time zone
    SATimeZoneMode timeZoneMode = SATimeZoneMode::Server;
    QString timeZoneIdentifier;

    // Special settings
    bool allowDataLocalInfile = false;
    bool enableClearTextPlugin = false;
    bool requestServerPublicKey = false;

    // SSL
    bool useSSL = false;
    bool sslKeyFileLocationEnabled = false;
    QString sslKeyFileLocation;
    bool sslCertificateFileLocationEnabled = false;
    QString sslCertificateFileLocation;
    bool sslCACertFileLocationEnabled = false;
    QString sslCACertFileLocation;

    // SSH tunnel
    QString sshHost;
    QString sshUser;
    QString sshPassword;               // in-memory only
    bool sshKeyLocationEnabled = false;
    QString sshKeyLocation;
    QString sshPort;
    QString sshRemoteSocketPath;

    // Unsupported-on-Linux extras, preserved so re-saving does not lose them.
    QVariantMap passthrough;

    unsigned int effectivePort() const;          // 3306 when blank/invalid
    unsigned int effectiveSSHPort() const;       // 0 when blank (let ssh decide)
    QString displayName() const;                 // name, or "user@host"
    QString hostDescription() const;             // "host:port", socket path or "ssh: user@host"
    bool isValidForConnecting(QString *problem) const;

    // The host libmysqlclient should connect to. For SSH tunnels this is the
    // loopback endpoint of the forwarded port; loopback spellings are preserved
    // so "localhost"-specific grants keep working (see SAConnectionInfo.swift).
    QString resolvedMySQLHost() const;

    // libmariadb options for a direct connection. For SSH tunnels the caller
    // overrides host/port with the local forwarded endpoint.
    SAMySQLOptions toMySQLOptions() const;

    // Favorites.plist dictionary mapping (keys from SPConstants.m).
    static SAConnectionInfo fromFavoriteDictionary(const QVariantMap &favorite);
    QVariantMap toFavoriteDictionary() const;

    bool operator==(const SAConnectionInfo &other) const;
    bool operator!=(const SAConnectionInfo &other) const { return !(*this == other); }
};

// The seven colour labels favorites can carry (colorIndex 0..6), decoded from
// FavoriteColorList in the macOS PreferenceDefaults.plist.
namespace SAFavoriteColors {
int count();
QColor color(int index);          // invalid QColor for -1 / out of range
QString name(int index);          // "Red", "Orange", ...
}
