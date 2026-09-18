//
//  SAMCPToolDefinitions.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAMCPToolDefinitions.h"

namespace {

QJsonObject prop(const QString &type, const QString &description, QJsonValue defaultValue = QJsonValue())
{
    QJsonObject o;
    o["type"] = type;
    o["description"] = description;
    if (!defaultValue.isUndefined())
        o["default"] = defaultValue;
    return o;
}

// Every tool accepts an optional "connection" id (empty = active tab).
QJsonObject connectionProp()
{
    return prop(QStringLiteral("string"),
                QStringLiteral("Connection id from list_connections; omitted or empty uses the active tab."));
}

QJsonObject schema(std::initializer_list<std::pair<QString, QJsonObject>> properties,
                    const QStringList &required = {})
{
    QJsonObject props;
    for (const auto &p : properties)
        props[p.first] = p.second;

    QJsonObject s;
    s["type"] = QStringLiteral("object");
    s["properties"] = props;
    if (!required.isEmpty()) {
        QJsonArray req;
        for (const auto &r : required)
            req.append(r);
        s["required"] = req;
    }
    return s;
}

QJsonObject annotations(bool readOnly, bool destructive, bool openWorld = false)
{
    QJsonObject a;
    a["readOnlyHint"] = readOnly;
    a["destructiveHint"] = destructive;
    a["openWorldHint"] = openWorld;
    return a;
}

QJsonObject databaseAndConnection(const QString &databaseDescription = QStringLiteral("Database/schema name."))
{
    return schema({
        {"database", prop(QStringLiteral("string"), databaseDescription)},
        {"connection", connectionProp()},
    }, {"database"});
}

const QVector<SAMCPToolDefinition> &buildAll()
{
    static const QVector<SAMCPToolDefinition> tools = [] {
        QVector<SAMCPToolDefinition> t;

        t.append({
            QStringLiteral("list_connections"),
            QStringLiteral("List the open database connections (tabs) in Sequel Ace, with their id, name, host, "
                            "current database and whether they are the active tab."),
            schema({}),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("list_databases"),
            QStringLiteral("List the databases/schemas visible on a connection."),
            schema({{"connection", connectionProp()}}),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("list_tables"),
            QStringLiteral("List the tables and views in a database, with their type."),
            databaseAndConnection(),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("describe_table"),
            QStringLiteral("Describe a table's columns, indexes and foreign keys."),
            schema({
                {"database", prop(QStringLiteral("string"), QStringLiteral("Database/schema name."))},
                {"table", prop(QStringLiteral("string"), QStringLiteral("Table name."))},
                {"connection", connectionProp()},
            }, {"database", "table"}),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("get_table_ddl"),
            QStringLiteral("Return the CREATE TABLE statement for a table."),
            schema({
                {"database", prop(QStringLiteral("string"), QStringLiteral("Database/schema name."))},
                {"table", prop(QStringLiteral("string"), QStringLiteral("Table name."))},
                {"connection", connectionProp()},
            }, {"database", "table"}),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("list_views"),
            QStringLiteral("List the views in a database."),
            databaseAndConnection(),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("list_procedures"),
            QStringLiteral("List the stored procedures in a database."),
            databaseAndConnection(),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("list_functions"),
            QStringLiteral("List the stored functions in a database."),
            databaseAndConnection(),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("list_triggers"),
            QStringLiteral("List the triggers in a database."),
            databaseAndConnection(),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("get_routine_definition"),
            QStringLiteral("Return the definition (SHOW CREATE) of a view, procedure, function or trigger."),
            schema({
                {"database", prop(QStringLiteral("string"), QStringLiteral("Database/schema name."))},
                {"type", prop(QStringLiteral("string"), QStringLiteral("One of: view, procedure, function, trigger."))},
                {"name", prop(QStringLiteral("string"), QStringLiteral("Routine/view/trigger name."))},
                {"connection", connectionProp()},
            }, {"database", "type", "name"}),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("run_query"),
            QStringLiteral("Execute an arbitrary SQL statement. Refused if the server is read-only and the "
                            "statement is not a SELECT/SHOW/DESCRIBE/EXPLAIN."),
            schema({
                {"sql", prop(QStringLiteral("string"), QStringLiteral("SQL statement to execute."))},
                {"params", QJsonObject{{"type", "array"}, {"description", "Positional values bound to `?` placeholders."},
                                        {"items", QJsonObject{{"type", "string"}}}}},
                {"limit", prop(QStringLiteral("integer"), QStringLiteral("Maximum rows to return."))},
                {"offset", prop(QStringLiteral("integer"), QStringLiteral("Row offset."))},
                {"connection", connectionProp()},
            }, {"sql"}),
            annotations(false, true, false),
        });

        t.append({
            QStringLiteral("explain_query"),
            QStringLiteral("Run EXPLAIN on a SQL statement (EXPLAIN ANALYZE is refused: it executes the query)."),
            schema({
                {"sql", prop(QStringLiteral("string"), QStringLiteral("SQL statement to explain."))},
                {"connection", connectionProp()},
            }, {"sql"}),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("sample_table"),
            QStringLiteral("Return a small sample of rows from a table."),
            schema({
                {"database", prop(QStringLiteral("string"), QStringLiteral("Database/schema name."))},
                {"table", prop(QStringLiteral("string"), QStringLiteral("Table name."))},
                {"limit", prop(QStringLiteral("integer"), QStringLiteral("Row count, capped at 1000."), 10)},
                {"offset", prop(QStringLiteral("integer"), QStringLiteral("Row offset."), 0)},
                {"connection", connectionProp()},
            }, {"database", "table"}),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("count_rows"),
            QStringLiteral("Return the exact row count of a table."),
            schema({
                {"database", prop(QStringLiteral("string"), QStringLiteral("Database/schema name."))},
                {"table", prop(QStringLiteral("string"), QStringLiteral("Table name."))},
                {"connection", connectionProp()},
            }, {"database", "table"}),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("kill_query"),
            QStringLiteral("Kill a running query/connection by its process id (SHOW PROCESSLIST Id)."),
            schema({
                {"process_id", prop(QStringLiteral("integer"), QStringLiteral("Process id to kill."))},
                {"connection", connectionProp()},
            }, {"process_id"}),
            annotations(false, true, false),
        });

        t.append({
            QStringLiteral("export_results"),
            QStringLiteral("Run a query and write its results to a file (json or csv) inside the configured "
                            "export folder."),
            schema({
                {"sql", prop(QStringLiteral("string"), QStringLiteral("SQL statement to export."))},
                {"format", prop(QStringLiteral("string"), QStringLiteral("\"json\" or \"csv\"."), QStringLiteral("json"))},
                {"path", prop(QStringLiteral("string"), QStringLiteral("File name (relative to the export folder)."))},
                {"connection", connectionProp()},
            }, {"sql"}),
            annotations(false, true, false),
        });

        t.append({
            QStringLiteral("server_info"),
            QStringLiteral("Return server variables, current database and host for a connection."),
            schema({{"connection", connectionProp()}}),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("table_sizes"),
            QStringLiteral("Return estimated row counts and data/index sizes for every table in a database."),
            databaseAndConnection(),
            annotations(true, false),
        });

        t.append({
            QStringLiteral("process_list"),
            QStringLiteral("Return the server's current process list (SHOW PROCESSLIST)."),
            schema({{"connection", connectionProp()}}),
            annotations(true, false),
        });

        return t;
    }();
    return tools;
}

} // namespace

namespace SAMCPToolDefinitions {

const QVector<SAMCPToolDefinition> &all()
{
    return buildAll();
}

const SAMCPToolDefinition *find(const QString &name)
{
    for (const auto &tool : all()) {
        if (tool.name == name)
            return &tool;
    }
    return nullptr;
}

} // namespace SAMCPToolDefinitions
