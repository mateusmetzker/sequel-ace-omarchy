//
//  SAMCPDataSource.h
//  Sequel Ace (Linux port)
//
//  Abstraction the MCP server (SAMCPServer) uses to reach the application's
//  open connections, mirroring what SPAppController exposes to
//  SPMCPServer.swift on macOS. Whoever owns the open connection tabs (the
//  main window / a document controller) implements this.
//
//  TODO(integration): SAMainWindow (or a small controller it owns) should
//  implement SAMCPDataSource over its list of SADatabaseDocument tabs, and
//  own the SAMCPServer instance, starting/stopping it as
//  SAPreferences::SPMCPServerEnabled / SPMCPServerPort / SPMCPReadOnly
//  change. That wiring is not part of this file — this only defines the
//  contract SAMCPServer depends on so it can be implemented independently.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QString>
#include <QVector>

class SADatabaseSession;

struct SAMCPConnectionInfo {
    QString id;             // stable identifier, passed back as the "connection" tool argument
    QString name;           // display name (favorite name or host)
    QString host;
    QString database;       // currently selected database, may be empty
    bool active = false;    // this is the frontmost/focused tab
    QString favoriteName;
    QString favoritePath;   // path within the favorites tree, empty if not saved
};

class SAMCPDataSource {
public:
    virtual ~SAMCPDataSource() = default;

    // All connections currently open in the application (one per tab/window).
    virtual QVector<SAMCPConnectionInfo> openConnections() const = 0;

    // The session backing `connectionId`, or the active tab's session when
    // `connectionId` is empty. Returns nullptr when there is no match (no
    // open tabs, or an unknown id) — callers must report that as a tool
    // error, never dereference a null session.
    virtual SADatabaseSession *sessionFor(const QString &connectionId) const = 0;

    // Convenience: resolves the SAMCPConnectionInfo the same way sessionFor()
    // resolves a session, so tool handlers can report the connection's
    // metadata (host, current database) without a second lookup path.
    virtual const SAMCPConnectionInfo *connectionInfoFor(const QString &connectionId) const = 0;
};
