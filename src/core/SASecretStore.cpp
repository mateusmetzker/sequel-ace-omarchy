//
//  SASecretStore.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

// GLib headers must come before Qt: they declare members named "signals".
#ifdef SA_HAVE_LIBSECRET
#include <libsecret/secret.h>
#endif

#include "SASecretStore.h"

#include <QDebug>

namespace SASecretStore {

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::MySQL: return QStringLiteral("mysql");
    case Kind::SSH: return QStringLiteral("ssh");
    case Kind::SSHKeyPassphrase: return QStringLiteral("ssh-key");
    }
    return QStringLiteral("mysql");
}

#ifdef SA_HAVE_LIBSECRET

namespace {

const SecretSchema *schema()
{
    static const SecretSchema s = {
        "com.sequel-ace.SequelAce.Password", SECRET_SCHEMA_NONE,
        {
            {"application", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {"kind", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
        },
        0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
    };
    return &s;
}

QString errorText(GError *error)
{
    QString text = error && error->message ? QString::fromUtf8(error->message) : QStringLiteral("unknown error");
    if (error) g_error_free(error);
    return text;
}

} // namespace

bool isCompiledIn() { return true; }

bool isAvailable()
{
    static int cached = -1;
    if (cached >= 0) return cached == 1;
    GError *error = nullptr;
    SecretService *service = secret_service_get_sync(SECRET_SERVICE_NONE, nullptr, &error);
    if (!service) {
        qInfo() << "Sequel Ace: Secret Service unavailable, passwords will not be saved:" << errorText(error);
        cached = 0;
        return false;
    }
    g_object_unref(service);
    cached = 1;
    return true;
}

bool store(Kind kind, const QString &account, const QString &label, const QString &secret, QString *errorOut)
{
    GError *error = nullptr;
    const QByteArray kindBytes = kindName(kind).toUtf8();
    const QByteArray accountBytes = account.toUtf8();
    const QByteArray labelBytes = label.toUtf8();
    const QByteArray secretBytes = secret.toUtf8();
    const gboolean ok = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, labelBytes.constData(),
                                                   secretBytes.constData(), nullptr, &error,
                                                   "application", "sequel-ace",
                                                   "kind", kindBytes.constData(),
                                                   "account", accountBytes.constData(),
                                                   nullptr);
    if (!ok) {
        const QString text = errorText(error);
        if (errorOut) *errorOut = text;
        qWarning() << "Sequel Ace: could not store password:" << text;
        return false;
    }
    return true;
}

QString lookup(Kind kind, const QString &account, bool *found)
{
    GError *error = nullptr;
    const QByteArray kindBytes = kindName(kind).toUtf8();
    const QByteArray accountBytes = account.toUtf8();
    gchar *password = secret_password_lookup_sync(schema(), nullptr, &error,
                                                  "application", "sequel-ace",
                                                  "kind", kindBytes.constData(),
                                                  "account", accountBytes.constData(),
                                                  nullptr);
    if (error) {
        qWarning() << "Sequel Ace: password lookup failed:" << errorText(error);
        if (found) *found = false;
        return QString();
    }
    if (!password) {
        if (found) *found = false;
        return QString();
    }
    const QString result = QString::fromUtf8(password);
    secret_password_free(password);
    if (found) *found = true;
    return result;
}

bool remove(Kind kind, const QString &account, QString *errorOut)
{
    GError *error = nullptr;
    const QByteArray kindBytes = kindName(kind).toUtf8();
    const QByteArray accountBytes = account.toUtf8();
    secret_password_clear_sync(schema(), nullptr, &error,
                               "application", "sequel-ace",
                               "kind", kindBytes.constData(),
                               "account", accountBytes.constData(),
                               nullptr);
    if (error) {
        const QString text = errorText(error);
        if (errorOut) *errorOut = text;
        return false;
    }
    return true;
}

#else // !SA_HAVE_LIBSECRET

bool isCompiledIn() { return false; }
bool isAvailable() { return false; }
bool store(Kind, const QString &, const QString &, const QString &, QString *error)
{
    if (error) *error = QStringLiteral("Sequel Ace was built without libsecret; passwords cannot be saved.");
    return false;
}
QString lookup(Kind, const QString &, bool *found)
{
    if (found) *found = false;
    return QString();
}
bool remove(Kind, const QString &, QString *) { return false; }

#endif

} // namespace SASecretStore
