//
//  SASecretStore.h
//  Sequel Ace (Linux port)
//
//  Password storage in the desktop keyring through libsecret (Secret Service
//  API: GNOME Keyring, KDE Wallet 5.97+, KeePassXC...). Replaces the macOS
//  Keychain. When libsecret is unavailable at build or run time every call
//  fails gracefully and passwords are kept in memory only.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QString>

namespace SASecretStore {

enum class Kind { MySQL, SSH, SSHKeyPassphrase };

bool isCompiledIn();
// Probes the Secret Service once; cached afterwards.
bool isAvailable();

// `account` identifies the item (favorite id or a host/user key).
bool store(Kind kind, const QString &account, const QString &label, const QString &secret, QString *error = nullptr);
QString lookup(Kind kind, const QString &account, bool *found = nullptr);
bool remove(Kind kind, const QString &account, QString *error = nullptr);

QString kindName(Kind kind);

} // namespace SASecretStore
