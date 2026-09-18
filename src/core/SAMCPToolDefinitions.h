//
//  SAMCPToolDefinitions.h
//  Sequel Ace (Linux port)
//
//  Static data for the Model Context Protocol tools exposed by SAMCPServer:
//  name, description, JSON Schema input, and MCP annotations for each of the
//  17 tools ported from SAMCPToolDefinitions.swift / SPMCPServer.swift. Pure
//  data, no dependency on the application or a live connection.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct SAMCPToolDefinition {
    QString name;
    QString description;
    QJsonObject inputSchema;   // JSON Schema (type: object, properties, required)
    QJsonObject annotations;   // readOnlyHint / destructiveHint / openWorldHint
};

namespace SAMCPToolDefinitions {

// All 17 tools, in the order they should appear in tools/list.
const QVector<SAMCPToolDefinition> &all();

// Convenience lookup; returns nullptr when no tool has that name.
const SAMCPToolDefinition *find(const QString &name);

} // namespace SAMCPToolDefinitions
