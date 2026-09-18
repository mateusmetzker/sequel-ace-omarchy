//
//  SASSHTunnel.h
//  Sequel Ace (Linux port)
//
//  SSH port forwarding through the system OpenSSH client, ported from
//  SPSSHTunnel. ssh is started with the same arguments as on macOS, its
//  verbose stderr is parsed to track the tunnel state, and password / key
//  passphrase / host-key prompts are answered through SSH_ASKPASS: the
//  application re-executes itself in "--ssh-askpass" mode and talks back to
//  the running instance over a private UNIX socket (SASSHAskpass).
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAConnectionInfo.h"

#include <QObject>
#include <QProcess>
#include <QStringList>

#include <functional>

class QLocalServer;
class QLocalSocket;

class SASSHTunnel : public QObject {
    Q_OBJECT
public:
    enum State {
        Idle,
        Connecting,
        WaitingForAuth,
        Connected,
        ForwardingFailed,   // tunnel is up but the remote MySQL port refused the connection
        Failed,
    };
    Q_ENUM(State)

    explicit SASSHTunnel(const SAConnectionInfo &info, QObject *parent = nullptr);
    ~SASSHTunnel() override;

    void connectTunnel();
    void disconnectTunnel();

    State state() const { return m_state; }
    quint16 localPort() const { return m_localPort; }
    QString lastError() const { return m_lastError; }
    QStringList debugMessages() const { return m_debugMessages; }
    QString commandLine() const { return m_commandLine; }

    // Prompt handlers, called on the main thread while ssh waits. The tunnel
    // answers "password:" prompts itself from the connection info; anything
    // else (host key questions, key passphrases) is forwarded here. When no
    // handler is installed the prompt is refused.
    void setQuestionHandler(std::function<bool(const QString &question)> handler) { m_questionHandler = std::move(handler); }
    void setPassphraseHandler(std::function<QString(const QString &prompt, bool *cancelled)> handler) { m_passphraseHandler = std::move(handler); }

    // Environment variable names shared with the askpass helper.
    static const char *SocketEnvironmentKey;
    static const char *TokenEnvironmentKey;

Q_SIGNALS:
    void stateChanged(SASSHTunnel::State state);

private:
    void setState(State state);
    void setLastError(const QString &error);
    void handleStandardError();
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    void handleErrorOccurred(QProcess::ProcessError error);
    void handleAskpassConnection();
    void handleAskpassRequest(QLocalSocket *socket, const QByteArray &line);
    bool startAskpassServer();
    void stopAskpassServer();
    static quint16 findFreePort();

    SAConnectionInfo m_info;
    QProcess *m_process = nullptr;
    QLocalServer *m_askpassServer = nullptr;
    QString m_askpassSocketPath;
    QByteArray m_askpassToken;
    State m_state = Idle;
    quint16 m_localPort = 0;
    QString m_lastError;
    QStringList m_debugMessages;
    QString m_commandLine;
    QByteArray m_stderrBuffer;
    bool m_userInitiatedDisconnect = false;
    std::function<bool(const QString &)> m_questionHandler;
    std::function<QString(const QString &, bool *)> m_passphraseHandler;
};

// The "--ssh-askpass <prompt>" entry point. Returns the process exit code.
namespace SASSHAskpass {
int run(const QStringList &arguments);
bool isAskpassInvocation(int argc, char **argv);
}
