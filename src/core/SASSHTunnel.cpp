//
//  SASSHTunnel.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SASSHTunnel.h"
#include "SAPreferences.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>
#include <QHostAddress>
#include <QTextStream>
#include <QDebug>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

const char *SASSHTunnel::SocketEnvironmentKey = "SEQUEL_ACE_ASKPASS_SOCKET";
const char *SASSHTunnel::TokenEnvironmentKey = "SEQUEL_ACE_ASKPASS_TOKEN";

namespace {
constexpr int ProtocolVersion = 1;
}

SASSHTunnel::SASSHTunnel(const SAConnectionInfo &info, QObject *parent)
    : QObject(parent), m_info(info)
{
}

SASSHTunnel::~SASSHTunnel()
{
    m_userInitiatedDisconnect = true;
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(2000)) m_process->kill();
        }
        delete m_process;
        m_process = nullptr;
    }
    stopAskpassServer();
}

void SASSHTunnel::setState(State state)
{
    if (m_state == state) return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

void SASSHTunnel::setLastError(const QString &error)
{
    m_lastError = error;
    m_debugMessages << QStringLiteral("[Sequel Ace] %1").arg(error);
}

quint16 SASSHTunnel::findFreePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

bool SASSHTunnel::startAskpassServer()
{
    stopAskpassServer();
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    QDir().mkpath(dir);
    const QString name = QStringLiteral("%1/sequel-ace-ssh-%2-%3").arg(dir).arg(getpid()).arg(QRandomGenerator::global()->generate());
    m_askpassServer = new QLocalServer(this);
    m_askpassServer->setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(name);
    if (!m_askpassServer->listen(name)) {
        setLastError(tr("Could not create the SSH authentication socket: %1").arg(m_askpassServer->errorString()));
        delete m_askpassServer;
        m_askpassServer = nullptr;
        return false;
    }
    m_askpassSocketPath = m_askpassServer->fullServerName();
    m_askpassToken = QByteArray::number(QRandomGenerator::global()->generate64(), 16)
                   + QByteArray::number(QRandomGenerator::global()->generate64(), 16);
    connect(m_askpassServer, &QLocalServer::newConnection, this, &SASSHTunnel::handleAskpassConnection);
    return true;
}

void SASSHTunnel::stopAskpassServer()
{
    if (m_askpassServer) {
        m_askpassServer->close();
        delete m_askpassServer;
        m_askpassServer = nullptr;
    }
    if (!m_askpassSocketPath.isEmpty()) {
        QLocalServer::removeServer(m_askpassSocketPath);
        m_askpassSocketPath.clear();
    }
}

void SASSHTunnel::connectTunnel()
{
    if (m_process && m_process->state() != QProcess::NotRunning) return;
    m_userInitiatedDisconnect = false;
    m_lastError.clear();
    m_debugMessages.clear();
    m_stderrBuffer.clear();

    if (!m_localPort) m_localPort = findFreePort();
    if (!m_localPort) {
        setLastError(tr("No local port could be allocated for the SSH Tunnel."));
        setState(Failed);
        return;
    }
    if (!startAskpassServer()) {
        setState(Failed);
        return;
    }

    SAPreferences &prefs = SAPreferences::instance();
    const int connectionTimeout = prefs.intFor(SAPreferences::ConnectionTimeoutValue);
    const bool useKeepAlive = prefs.boolFor(SAPreferences::UseKeepAlive);
    const int keepAliveInterval = prefs.intFor(SAPreferences::KeepAliveInterval);
    const bool muxing = prefs.boolFor(SAPreferences::SSHMultiplexingEnabled);

    QString sshPath = prefs.stringFor(SAPreferences::SSHClientPath).trimmed();
    if (sshPath.isEmpty()) {
        sshPath = QStandardPaths::findExecutable(QStringLiteral("ssh"));
        if (sshPath.isEmpty()) sshPath = QStringLiteral("/usr/bin/ssh");
    } else {
        m_debugMessages << QStringLiteral("# Custom SSH binary enabled. Disable in Preferences to rule out incompatibilities!");
    }

    QStringList args;
    args << QStringLiteral("-v") << QStringLiteral("-N");
    if (muxing) {
        args << QStringLiteral("-o") << QStringLiteral("ControlMaster=auto");
        args << QStringLiteral("-o") << QStringLiteral("ControlPath=%1/sequel-ace-ssh-%C").arg(QDir::tempPath());
    } else {
        args << QStringLiteral("-S") << QStringLiteral("none");
        args << QStringLiteral("-o") << QStringLiteral("ControlMaster=no");
    }
    args << QStringLiteral("-o") << QStringLiteral("ExitOnForwardFailure=yes");
    if (connectionTimeout > 0) args << QStringLiteral("-o") << QStringLiteral("ConnectTimeout=%1").arg(connectionTimeout);
    args << QStringLiteral("-o") << QStringLiteral("NumberOfPasswordPrompts=3");

    const QString configFile = prefs.stringFor(SAPreferences::SSHConfigFile).trimmed();
    if (!configFile.isEmpty() && QFile::exists(configFile)) args << QStringLiteral("-F") << configFile;

    if (m_info.sshKeyLocationEnabled && !m_info.sshKeyLocation.trimmed().isEmpty()) {
        args << QStringLiteral("-i") << m_info.sshKeyLocation.trimmed();
        args << QStringLiteral("-o") << QStringLiteral("IdentitiesOnly=yes");
    }
    if (useKeepAlive && keepAliveInterval > 0) {
        args << QStringLiteral("-o") << QStringLiteral("TCPKeepAlive=yes");
        args << QStringLiteral("-o") << QStringLiteral("ServerAliveInterval=%1").arg(keepAliveInterval);
        args << QStringLiteral("-o") << QStringLiteral("ServerAliveCountMax=3");
    }
    const unsigned int sshPort = m_info.effectiveSSHPort();
    if (sshPort) args << QStringLiteral("-p") << QString::number(sshPort);

    const QString sshUser = m_info.sshUser.trimmed();
    const QString sshHost = m_info.sshHost.trimmed();
    args << (sshUser.isEmpty() ? sshHost : sshUser + QLatin1Char('@') + sshHost);

    const QString remoteSocket = m_info.sshRemoteSocketPath.trimmed();
    if (!remoteSocket.isEmpty()) {
        args << QStringLiteral("-L") << QStringLiteral("%1:%2").arg(m_localPort).arg(remoteSocket);
    } else {
        const QString remoteHost = m_info.host.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : m_info.host.trimmed();
        args << QStringLiteral("-L") << QStringLiteral("%1:%2:%3").arg(m_localPort).arg(remoteHost).arg(m_info.effectivePort());
    }

    m_process = new QProcess(this);
    m_process->setProgram(sshPath);
    m_process->setArguments(args);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setStandardInputFile(QProcess::nullDevice());
    m_process->setStandardOutputFile(QProcess::nullDevice());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SSH_ASKPASS"), QCoreApplication::applicationFilePath());
    env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
    if (!env.contains(QStringLiteral("DISPLAY"))) env.insert(QStringLiteral("DISPLAY"), QStringLiteral(":0"));
    env.insert(QString::fromLatin1(SocketEnvironmentKey), m_askpassSocketPath);
    env.insert(QString::fromLatin1(TokenEnvironmentKey), QString::fromLatin1(m_askpassToken));
    m_process->setProcessEnvironment(env);

    // Detach from the controlling terminal so OpenSSH never tries to read the
    // password from a tty (readpass.c falls back to SSH_ASKPASS only then on
    // older releases; SSH_ASKPASS_REQUIRE=force covers 8.4+).
    m_process->setChildProcessModifier([]() { setsid(); });

    connect(m_process, &QProcess::readyReadStandardError, this, &SASSHTunnel::handleStandardError);
    connect(m_process, &QProcess::finished, this, &SASSHTunnel::handleFinished);
    connect(m_process, &QProcess::errorOccurred, this, &SASSHTunnel::handleErrorOccurred);

    m_commandLine = sshPath + QLatin1Char(' ') + args.join(QLatin1Char(' '));
    m_debugMessages << QStringLiteral("Used command:  %1").arg(m_commandLine);
    setState(Connecting);
    m_process->start();
}

void SASSHTunnel::disconnectTunnel()
{
    m_userInitiatedDisconnect = true;
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(2000)) m_process->kill();
    }
    stopAskpassServer();
    setState(Idle);
}

void SASSHTunnel::handleStandardError()
{
    if (!m_process) return;
    m_stderrBuffer += m_process->readAllStandardError();
    int newline;
    while ((newline = m_stderrBuffer.indexOf('\n')) >= 0) {
        const QString message = QString::fromUtf8(m_stderrBuffer.left(newline)).trimmed();
        m_stderrBuffer.remove(0, newline + 1);
        if (message.isEmpty()) continue;
        m_debugMessages << message;

        if (m_state != Connected && (message.contains(QLatin1String("Local forwarding listening on"))
                                     || message.contains(QLatin1String("mux_client_request_session: master session id: ")))) {
            setState(Connected);
        }
        if (message.contains(QLatin1String("Connection established"))) {
            setState(WaitingForAuth);
        }
        if (message.contains(QLatin1String("bind: Address already in use"))) {
            setLastError(tr("The SSH Tunnel was unable to bind to the local port. This error may occur if you already have an SSH connection to the same server and are using a 'LocalForward' setting in your SSH configuration."));
            m_process->terminate();
        }
        if (message.contains(QLatin1String("closed by remote host."))) {
            setLastError(tr("The SSH Tunnel was closed 'by the remote host'. This may indicate a networking issue or a network timeout."));
            m_process->terminate();
        }
        if (message.contains(QLatin1String("Permission denied (")) || message.contains(QLatin1String("No more authentication methods to try"))) {
            setLastError(tr("The SSH Tunnel could not authenticate with the remote host. Please check your password and ensure you still have access."));
            m_process->terminate();
        }
        if (message.contains(QLatin1String("connect failed: Connection refused"))) {
            setLastError(tr("The SSH Tunnel was established successfully, but could not forward data to the remote port as the remote port refused the connection."));
            setState(ForwardingFailed);
        }
        if (message.contains(QLatin1String("Operation timed out")) || message.contains(QLatin1String("Connection timed out"))
            || message.contains(QLatin1String("Could not resolve hostname"))) {
            setLastError(tr("The SSH Tunnel was unable to connect to host %1, or the request timed out.\n\nBe sure that the address is correct and that you have the necessary privileges, or try increasing the connection timeout (currently %2 seconds).")
                             .arg(m_info.sshHost).arg(SAPreferences::instance().intFor(SAPreferences::ConnectionTimeoutValue)));
            m_process->terminate();
        }
        if (message.contains(QLatin1String("Host key verification failed"))) {
            setLastError(tr("The SSH host key could not be verified. Check ~/.ssh/known_hosts or connect once with the ssh command line to accept the key."));
            m_process->terminate();
        }
    }
}

void SASSHTunnel::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(status)
    handleStandardError();
    if (!m_stderrBuffer.trimmed().isEmpty()) {
        m_debugMessages << QString::fromUtf8(m_stderrBuffer).trimmed();
        m_stderrBuffer.clear();
    }
    stopAskpassServer();
    if (!m_userInitiatedDisconnect && m_lastError.isEmpty()) {
        setLastError(tr("The SSH Tunnel has unexpectedly closed."));
    }
    if (m_userInitiatedDisconnect) setState(Idle);
    else setState(m_state == Connected ? Idle : Failed);
}

void SASSHTunnel::handleErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        setLastError(tr("The SSH client could not be started (%1). Install OpenSSH or set the client path in Preferences.").arg(m_process ? m_process->program() : QStringLiteral("ssh")));
        stopAskpassServer();
        setState(Failed);
    }
}

// ---- askpass server side -----------------------------------------------------

void SASSHTunnel::handleAskpassConnection()
{
    while (m_askpassServer && m_askpassServer->hasPendingConnections()) {
        QLocalSocket *socket = m_askpassServer->nextPendingConnection();
        // Only accept requests from processes running as this user.
        struct ucred cred {};
        socklen_t len = sizeof(cred);
        if (getsockopt(static_cast<int>(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0 && cred.uid != getuid()) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            while (socket->canReadLine()) handleAskpassRequest(socket, socket->readLine());
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void SASSHTunnel::handleAskpassRequest(QLocalSocket *socket, const QByteArray &line)
{
    auto reply = [socket](const QJsonObject &object) {
        QJsonObject o = object;
        o.insert(QStringLiteral("v"), ProtocolVersion);
        socket->write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
        socket->flush();
        socket->disconnectFromServer();
    };

    const QJsonObject request = QJsonDocument::fromJson(line.trimmed()).object();
    if (request.value(QStringLiteral("token")).toString().toLatin1() != m_askpassToken) {
        m_debugMessages << QStringLiteral("[Sequel Ace] Rejected an SSH prompt with an invalid token.");
        reply({{QStringLiteral("kind"), QStringLiteral("refused")}});
        return;
    }
    const QString prompt = request.value(QStringLiteral("prompt")).toString();
    m_debugMessages << QStringLiteral("[Sequel Ace] SSH prompt: %1").arg(prompt.simplified());

    // Host key and similar yes/no questions.
    if (prompt.contains(QLatin1String("(yes/no"))) {
        const bool answer = m_questionHandler ? m_questionHandler(prompt) : false;
        reply({{QStringLiteral("kind"), QStringLiteral("answer")}, {QStringLiteral("value"), answer}});
        return;
    }
    // Account password.
    if (prompt.contains(QLatin1String("password:"), Qt::CaseInsensitive) && !prompt.contains(QLatin1String("passphrase"), Qt::CaseInsensitive)) {
        if (!m_info.sshPassword.isEmpty()) {
            reply({{QStringLiteral("kind"), QStringLiteral("secret")}, {QStringLiteral("value"), m_info.sshPassword}});
            return;
        }
    }
    // Key passphrases and anything else go to the UI.
    if (m_passphraseHandler) {
        bool cancelled = false;
        const QString secret = m_passphraseHandler(prompt, &cancelled);
        if (!cancelled) {
            reply({{QStringLiteral("kind"), QStringLiteral("secret")}, {QStringLiteral("value"), secret}});
            return;
        }
    }
    reply({{QStringLiteral("kind"), QStringLiteral("refused")}});
}

// ---- askpass helper side -----------------------------------------------------

namespace SASSHAskpass {

bool isAskpassInvocation(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--ssh-askpass") == 0) return true;
    // OpenSSH invokes SSH_ASKPASS with the prompt as its only argument; the
    // socket variable identifies that situation even without our flag.
    return argc >= 2 && qEnvironmentVariableIsSet(SASSHTunnel::SocketEnvironmentKey);
}

int run(const QStringList &arguments)
{
    QString prompt;
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QLatin1String("--ssh-askpass")) continue;
        prompt = arguments.at(i);
    }
    const QString socketPath = qEnvironmentVariable(SASSHTunnel::SocketEnvironmentKey);
    const QByteArray token = qgetenv(SASSHTunnel::TokenEnvironmentKey);
    if (socketPath.isEmpty() || token.isEmpty()) {
        fprintf(stderr, "Sequel Ace askpass: no authentication socket in the environment\n");
        return 1;
    }
    QLocalSocket socket;
    socket.connectToServer(socketPath);
    if (!socket.waitForConnected(5000)) {
        fprintf(stderr, "Sequel Ace askpass: unable to reach the application (%s)\n", qPrintable(socket.errorString()));
        return 1;
    }
    QJsonObject request;
    request.insert(QStringLiteral("v"), ProtocolVersion);
    request.insert(QStringLiteral("kind"), QStringLiteral("prompt"));
    request.insert(QStringLiteral("prompt"), prompt);
    request.insert(QStringLiteral("token"), QString::fromLatin1(token));
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();

    // The user may take a while to answer a dialog.
    while (!socket.canReadLine()) {
        if (!socket.waitForReadyRead(10 * 60 * 1000)) {
            fprintf(stderr, "Sequel Ace askpass: timed out waiting for an answer\n");
            return 1;
        }
    }
    const QJsonObject response = QJsonDocument::fromJson(socket.readLine().trimmed()).object();
    const QString kind = response.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("answer")) {
        printf("%s\n", response.value(QStringLiteral("value")).toBool() ? "yes" : "no");
        return 0;
    }
    if (kind == QLatin1String("secret")) {
        const QByteArray secret = response.value(QStringLiteral("value")).toString().toUtf8();
        fwrite(secret.constData(), 1, static_cast<size_t>(secret.size()), stdout);
        fputc('\n', stdout);
        fflush(stdout);
        return 0;
    }
    return 1;
}

} // namespace SASSHAskpass
