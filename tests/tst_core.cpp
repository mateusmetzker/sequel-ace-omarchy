//
//  tst_core.cpp
//  Sequel Ace (Linux port) - unit tests for the core library.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include <QtTest>

#include "SAConnectionInfo.h"
#include "SAContentEditing.h"
#include "SAContentFilters.h"
#include "SAEditorTheme.h"
#include "SAFavoritesStore.h"
#include "SAFilterTree.h"
#include "SAMCPReadOnlyGuard.h"
#include "SAMCPToolDefinitions.h"
#include "SAMySQLConnection.h"
#include "SAPlist.h"
#include "SAResultExport.h"
#include "SASQLClassifier.h"
#include "SASQLSplitter.h"
#include "SASQLTokens.h"
#include "SASQLTypes.h"
#include "SASchemaQueries.h"
#include "SAUserManagerQueries.h"

#include <mysql.h>

class CoreTests : public QObject {
    Q_OBJECT

private:
    SAContentEditing::Escaper escaper() const
    {
        SAContentEditing::Escaper e;
        e.string = [](const QString &s) { return SAMySQLConnection().escapeString(s, true); };
        e.data = [](const QByteArray &d) { return SAMySQLConnection().escapeAndQuoteData(d); };
        return e;
    }

private Q_SLOTS:
    // ---- splitter ------------------------------------------------------------
    void splitter_basic()
    {
        SASQLSplitter splitter;
        const QStringList parts = splitter.split(QStringLiteral("SELECT 1; SELECT 2;\nSELECT 3"));
        QCOMPARE(parts, QStringList({"SELECT 1", "SELECT 2", "SELECT 3"}));
    }
    void splitter_quotesAndComments()
    {
        SASQLSplitter splitter;
        const QString sql = QStringLiteral("SELECT ';' AS a, \"x;y\" AS b, `we;ird` FROM t; -- trailing; comment\n"
                                           "# hash; comment\n/* block; comment */ UPDATE t SET a = 'it''s;' WHERE b = 'q\\';z';");
        const QStringList parts = splitter.split(sql);
        QCOMPARE(parts.size(), 2);
        QVERIFY(parts.at(0).startsWith(QLatin1String("SELECT ';' AS a")));
        // Comments preceding a statement belong to it, as in the macOS app.
        QVERIFY(parts.at(1).startsWith(QLatin1String("-- trailing; comment")));
        QVERIFY(parts.at(1).contains(QLatin1String("/* block; comment */ UPDATE")));
        QVERIFY(parts.at(1).endsWith(QLatin1String("WHERE b = 'q\\';z'")));
        // A trailing comment-only fragment is not a statement.
        QCOMPARE(splitter.split(QStringLiteral("SELECT 1; -- done\n/* bye */")).size(), 1);
        QCOMPARE(splitter.split(QStringLiteral("SELECT 1; /*!40101 SET NAMES utf8 */")).size(), 2);
    }
    void splitter_delimiter()
    {
        SASQLSplitter splitter;
        const QString sql = QStringLiteral("DELIMITER //\nCREATE PROCEDURE p() BEGIN SELECT 1; SELECT 2; END//\nDELIMITER ;\nSELECT 3;");
        const QStringList parts = splitter.split(sql);
        QCOMPARE(parts.size(), 2);
        QCOMPARE(parts.at(0), QStringLiteral("CREATE PROCEDURE p() BEGIN SELECT 1; SELECT 2; END"));
        QCOMPARE(parts.at(1), QStringLiteral("SELECT 3"));
    }
    void splitter_delimiterWithoutSupport()
    {
        SASQLSplitter splitter(false);
        const QStringList parts = splitter.split(QStringLiteral("DELIMITER //\nSELECT 1;"));
        QCOMPARE(parts.size(), 1);
        QCOMPARE(parts.at(0), QStringLiteral("DELIMITER //\nSELECT 1"));
    }
    void splitter_unterminatedQuote()
    {
        SASQLSplitter splitter;
        const QStringList parts = splitter.split(QStringLiteral("SELECT 'abc; SELECT 2"));
        QCOMPARE(parts.size(), 1);
    }
    void splitter_rangeAtPosition()
    {
        SASQLSplitter splitter;
        const QString text = QStringLiteral("SELECT 1;\nSELECT 2;\n\nSELECT 3");
        const QVector<SAStatementRange> ranges = splitter.splitIntoRanges(text);
        QCOMPARE(ranges.size(), 3);

        bool lookBehind = false;
        SAStatementRange r = SASQLSplitter::rangeAtPosition(text, ranges, 3, &lookBehind);
        QCOMPARE(text.mid(r.start, r.length), QStringLiteral("SELECT 1"));

        // Caret right after the first semicolon (start of line 2) with lookbehind -> first query.
        lookBehind = true;
        r = SASQLSplitter::rangeAtPosition(text, ranges, 9, &lookBehind);
        QCOMPARE(text.mid(r.start, r.length), QStringLiteral("SELECT 1"));
        QVERIFY(lookBehind);

        // Caret inside "SELECT 2".
        lookBehind = true;
        r = SASQLSplitter::rangeAtPosition(text, ranges, 13, &lookBehind);
        QCOMPARE(text.mid(r.start, r.length), QStringLiteral("SELECT 2"));
        QVERIFY(!lookBehind);

        // Caret at the very end -> last query.
        lookBehind = true;
        r = SASQLSplitter::rangeAtPosition(text, ranges, text.size(), &lookBehind);
        QCOMPARE(text.mid(r.start, r.length), QStringLiteral("SELECT 3"));
    }

    // ---- lexer facade ----------------------------------------------------------
    void tokens_basic()
    {
        bool endsInComment = false;
        const QVector<SASQLToken> tokens = SASQLTokens::tokenize(QStringLiteral("SELECT `id`, 'João' FROM t /* open"), false, &endsInComment);
        QVERIFY(endsInComment);
        QVERIFY(!tokens.isEmpty());
        QCOMPARE(tokens.first().type, SASQLTokenType::ReservedWord);
        QCOMPARE(tokens.first().length, 6);
        // 'João' is 6 UTF-16 units including quotes; verify offsets are in QString units.
        bool sawString = false;
        for (const SASQLToken &t : tokens) {
            if (t.type == SASQLTokenType::SingleQuotedText) {
                QCOMPARE(t.length, 6);
                QCOMPARE(QStringLiteral("SELECT `id`, 'João' FROM t /* open").mid(t.start, t.length), QStringLiteral("'João'"));
                sawString = true;
            }
        }
        QVERIFY(sawString);
        QCOMPARE(SASQLTokens::uppercaseKeywords(QStringLiteral("select a from 'b' where c")), QStringLiteral("SELECT a FROM 'b' WHERE c"));
    }
    void tokens_continueInComment()
    {
        bool endsInComment = true;
        const QVector<SASQLToken> tokens = SASQLTokens::tokenize(QStringLiteral("still */ SELECT"), true, &endsInComment);
        QVERIFY(!endsInComment);
        QCOMPARE(tokens.first().type, SASQLTokenType::Comment);
        QCOMPARE(tokens.last().type, SASQLTokenType::ReservedWord);
    }

    // ---- plist -------------------------------------------------------------------
    void plist_roundTrip()
    {
        QVariantMap root;
        root.insert("name", "Favoritos ção");
        root.insert("count", 42);
        root.insert("ratio", 0.5);
        root.insert("flag", true);
        root.insert("data", QByteArray("\x00\x01\x02", 3));
        root.insert("list", QVariantList{"a", 1, false});
        const QByteArray xml = SAPlist::write(root);
        QVERIFY(xml.contains("<!DOCTYPE plist"));
        QString error;
        const QVariantMap back = SAPlist::read(xml, &error).toMap();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(back.value("name").toString(), QStringLiteral("Favoritos ção"));
        QCOMPARE(back.value("count").toLongLong(), 42LL);
        QCOMPARE(back.value("ratio").toDouble(), 0.5);
        QCOMPARE(back.value("flag").toBool(), true);
        QCOMPARE(back.value("data").toByteArray(), QByteArray("\x00\x01\x02", 3));
        QCOMPARE(back.value("list").toList().size(), 3);
    }
    void plist_readsMacOSFavorites()
    {
        const QByteArray xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n"
            "<dict>\n"
            "	<key>Favorites Root</key>\n"
            "	<dict>\n"
            "		<key>Children</key>\n"
            "		<array>\n"
            "			<dict>\n"
            "				<key>Children</key>\n"
            "				<array>\n"
            "					<dict>\n"
            "						<key>colorIndex</key><integer>2</integer>\n"
            "						<key>database</key><string>shop</string>\n"
            "						<key>host</key><string>db.example.com</string>\n"
            "						<key>id</key><integer>1234567</integer>\n"
            "						<key>name</key><string>Production</string>\n"
            "						<key>port</key><string>3307</string>\n"
            "						<key>sshHost</key><string>bastion</string>\n"
            "						<key>sshUser</key><string>deploy</string>\n"
            "						<key>type</key><integer>2</integer>\n"
            "						<key>useCompression</key><integer>0</integer>\n"
            "						<key>useSSL</key><integer>1</integer>\n"
            "						<key>user</key><string>app</string>\n"
            "					</dict>\n"
            "				</array>\n"
            "				<key>IsExpanded</key><true/>\n"
            "				<key>Name</key><string>Work</string>\n"
            "			</dict>\n"
            "			<dict>\n"
            "				<key>host</key><string>127.0.0.1</string>\n"
            "				<key>id</key><integer>99</integer>\n"
            "				<key>name</key><string>Local</string>\n"
            "				<key>type</key><integer>0</integer>\n"
            "				<key>user</key><string>root</string>\n"
            "			</dict>\n"
            "		</array>\n"
            "		<key>IsExpanded</key><true/>\n"
            "		<key>Name</key><string>Favorites</string>\n"
            "	</dict>\n"
            "</dict>\n"
            "</plist>\n";

        const QString path = QDir::temp().filePath(QStringLiteral("sa-test-favorites-%1.plist").arg(QCoreApplication::applicationPid()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(xml);
        f.close();

        SAFavoritesStore store;
        QString error;
        QVERIFY2(store.load(path, &error), qPrintable(error));
        QCOMPARE(store.root()->childCount(), 2);
        SAFavoriteNode *group = store.root()->child(0);
        QVERIFY(group->isGroup());
        QCOMPARE(group->groupName(), QStringLiteral("Work"));
        QCOMPARE(group->childCount(), 1);
        const SAConnectionInfo &prod = group->child(0)->info();
        QCOMPARE(prod.name, QStringLiteral("Production"));
        QCOMPARE(prod.type, SAConnectionType::SSHTunnel);
        QCOMPARE(prod.effectivePort(), 3307u);
        QCOMPARE(prod.sshHost, QStringLiteral("bastion"));
        QVERIFY(prod.useSSL);
        QVERIFY(!prod.useCompression);
        QCOMPARE(prod.colorIndex, 2);
        QCOMPARE(prod.resolvedMySQLHost(), QStringLiteral("127.0.0.1"));
        QCOMPARE(store.findFavorite(99)->info().displayName(), QStringLiteral("Local"));

        // Save and reload: structure must survive.
        const QString out = path + QStringLiteral(".out");
        QVERIFY2(store.save(out, &error), qPrintable(error));
        SAFavoritesStore reloaded;
        QVERIFY(reloaded.load(out, &error));
        QCOMPARE(reloaded.root()->childCount(), 2);
        QCOMPARE(reloaded.root()->child(0)->child(0)->info(), prod);
        QFile::remove(path);
        QFile::remove(out);
    }

    // ---- connection info -----------------------------------------------------------
    void connectionInfo_dictionaryRoundTrip()
    {
        SAConnectionInfo info;
        info.id = 7;
        info.name = QStringLiteral("Test");
        info.type = SAConnectionType::Socket;
        info.socket = QStringLiteral("/run/mysqld/mysqld.sock");
        info.user = QStringLiteral("me");
        info.timeZoneMode = SATimeZoneMode::Fixed;
        info.timeZoneIdentifier = QStringLiteral("America/Sao_Paulo");
        info.sslCACertFileLocationEnabled = true;
        info.sslCACertFileLocation = QStringLiteral("/etc/ca.pem");
        const QVariantMap dict = info.toFavoriteDictionary();
        QCOMPARE(dict.value("type").toInt(), 1);
        QCOMPARE(dict.value("timeZone").toString(), QStringLiteral("America/Sao_Paulo"));
        QVERIFY(!dict.contains("password"));
        const SAConnectionInfo back = SAConnectionInfo::fromFavoriteDictionary(dict);
        QCOMPARE(back, info);
        QCOMPARE(back.toMySQLOptions().socketPath, info.socket);
        QVERIFY(back.toMySQLOptions().host.isEmpty());
    }
    void connectionInfo_defaults()
    {
        const SAConnectionInfo info = SAConnectionInfo::fromFavoriteDictionary({});
        QCOMPARE(info.type, SAConnectionType::TCPIP);
        QVERIFY(info.useCompression);
        QCOMPARE(info.colorIndex, -1);
        QCOMPARE(info.effectivePort(), 3306u);
        QCOMPARE(info.resolvedMySQLHost(), QStringLiteral("127.0.0.1"));
        QString problem;
        SAConnectionInfo bad;
        bad.port = QStringLiteral("99999");
        QVERIFY(!bad.isValidForConnecting(&problem));
        QVERIFY(!problem.isEmpty());
    }

    // ---- types -------------------------------------------------------------------
    void types_wireMapping()
    {
        QCOMPARE(SASQLTypes::typeNameForWireType(MYSQL_TYPE_LONG, 63, 0, 11, 1), QStringLiteral("INT"));
        QCOMPARE(SASQLTypes::typeNameForWireType(MYSQL_TYPE_VAR_STRING, 45, 0, 400, 4), QStringLiteral("VARCHAR"));
        QCOMPARE(SASQLTypes::typeNameForWireType(MYSQL_TYPE_VAR_STRING, 63, 0, 400, 1), QStringLiteral("VARBINARY"));
        QCOMPARE(SASQLTypes::typeNameForWireType(MYSQL_TYPE_BLOB, 45, 0, 65535 * 4, 4), QStringLiteral("TEXT"));
        QCOMPARE(SASQLTypes::typeNameForWireType(MYSQL_TYPE_BLOB, 63, 0, 65535, 1), QStringLiteral("BLOB"));
        QCOMPARE(SASQLTypes::typeNameForWireType(MYSQL_TYPE_STRING, 45, ENUM_FLAG, 10, 4), QStringLiteral("ENUM"));
        QCOMPARE(SASQLTypes::typeGroupForWireType(MYSQL_TYPE_NEWDECIMAL, 63, 0), QStringLiteral("float"));
        QCOMPARE(SASQLTypes::typeGroupForWireType(MYSQL_TYPE_BLOB, 63, 0), QStringLiteral("blobdata"));
        QCOMPARE(SASQLTypes::typeGroupForWireType(MYSQL_TYPE_BLOB, 45, 0), QStringLiteral("textdata"));
        QCOMPARE(SASQLTypes::typeGroupForTypeName(QStringLiteral("varchar")), QStringLiteral("string"));
        QCOMPARE(SASQLTypes::typeGroupForTypeName(QStringLiteral("JSON")), QStringLiteral("textdata"));
        QCOMPARE(SASQLTypes::typeGroupForTypeName(QStringLiteral("POINT")), QStringLiteral("geometry"));
        QCOMPARE(SASQLTypes::typeGroupForTypeName(QStringLiteral("WHATEVER")), QStringLiteral("blobdata"));
    }
    void types_parseColumnType()
    {
        SASQLTypes::ParsedColumnType p = SASQLTypes::parseColumnType(QStringLiteral("int(11) unsigned zerofill"));
        QCOMPARE(p.name, QStringLiteral("INT"));
        QCOMPARE(p.length, QStringLiteral("11"));
        QVERIFY(p.isUnsigned);
        QVERIFY(p.isZerofill);
        p = SASQLTypes::parseColumnType(QStringLiteral("enum('a,b','c)')"));
        QCOMPARE(p.name, QStringLiteral("ENUM"));
        QCOMPARE(p.length, QStringLiteral("'a,b','c)'"));
        p = SASQLTypes::parseColumnType(QStringLiteral("decimal(10,2)"));
        QCOMPARE(p.length, QStringLiteral("10,2"));
        p = SASQLTypes::parseColumnType(QStringLiteral("text"));
        QVERIFY(p.length.isEmpty());
    }

    // ---- escaping ------------------------------------------------------------------
    void escaping()
    {
        SAMySQLConnection c;
        QCOMPARE(c.escapeString(QStringLiteral("it's \"q\" \\ \n"), true), QStringLiteral("'it\\'s \\\"q\\\" \\\\ \\n'"));
        QCOMPARE(c.escapeAndQuoteData(QByteArray("\xDE\xAD", 2)), QStringLiteral("X'dead'"));
        QCOMPARE(SAMySQLConnection::quoteIdentifier(QStringLiteral("we`ird")), QStringLiteral("`we``ird`"));
        QCOMPARE(SAMySQLConnection::escapeLikePattern(QStringLiteral("a_b%c")), QStringLiteral("a\\_b\\%c"));
    }

    // ---- schema builders -------------------------------------------------------------
    void schema_columnDefinition()
    {
        auto esc = [](const QString &s) { return SAMySQLConnection().escapeString(s, true); };
        SASchema::Column c;
        c.name = QStringLiteral("name");
        c.type = QStringLiteral("VARCHAR");
        c.length = QStringLiteral("120");
        c.nullable = false;
        c.charset = QStringLiteral("utf8mb4");
        c.collation = QStringLiteral("utf8mb4_unicode_ci");
        c.defaultValue = QStringLiteral("n/a");
        c.hasDefault = true;
        c.comment = QStringLiteral("Display name");
        QCOMPARE(SASchema::columnDefinition(c, QStringLiteral("NULL"), esc),
                 QStringLiteral("`name` VARCHAR(120) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT 'n/a' COMMENT 'Display name'"));

        SASchema::Column ts;
        ts.name = QStringLiteral("created_at");
        ts.type = QStringLiteral("TIMESTAMP");
        ts.nullable = false;
        ts.defaultValue = QStringLiteral("CURRENT_TIMESTAMP");
        ts.hasDefault = true;
        ts.extra = QStringLiteral("on update CURRENT_TIMESTAMP");
        QCOMPARE(SASchema::columnDefinition(ts, QStringLiteral("NULL"), esc),
                 QStringLiteral("`created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"));

        SASchema::Column id;
        id.name = QStringLiteral("id");
        id.type = QStringLiteral("INT");
        id.length = QStringLiteral("11");
        id.isUnsigned = true;
        id.nullable = false;
        id.extra = QStringLiteral("auto_increment");
        QCOMPARE(SASchema::columnDefinition(id, QStringLiteral("NULL"), esc), QStringLiteral("`id` INT(11) UNSIGNED NOT NULL AUTO_INCREMENT"));
        QCOMPARE(SASchema::addColumn(QStringLiteral("t"), id, QStringLiteral("prev"), QStringLiteral("NULL"), esc),
                 QStringLiteral("ALTER TABLE `t` ADD `id` INT(11) UNSIGNED NOT NULL AUTO_INCREMENT AFTER `prev`"));

        SASchema::Column nul;
        nul.name = QStringLiteral("x");
        nul.type = QStringLiteral("INT");
        nul.nullable = true;
        nul.defaultIsNull = true;
        nul.hasDefault = true;
        QCOMPARE(SASchema::columnDefinition(nul, QStringLiteral("NULL"), esc), QStringLiteral("`x` INT NULL DEFAULT NULL"));
    }
    void schema_indexesAndKeys()
    {
        QCOMPARE(SASchema::addIndex("t", "UNIQUE", "uq", {"a", "b"}, {"", "10"}), QStringLiteral("ALTER TABLE `t` ADD UNIQUE INDEX `uq` (`a`, `b`(10))"));
        QCOMPARE(SASchema::addIndex("t", "PRIMARY KEY", "", {"id"}, {}), QStringLiteral("ALTER TABLE `t` ADD PRIMARY KEY (`id`)"));
        QCOMPARE(SASchema::dropIndex("t", "PRIMARY"), QStringLiteral("ALTER TABLE `t` DROP PRIMARY KEY"));
        QCOMPARE(SASchema::addForeignKey("orders", "fk", {"customer_id"}, "shop", "customers", {"id"}, "CASCADE", "NO ACTION"),
                 QStringLiteral("ALTER TABLE `orders` ADD CONSTRAINT `fk` FOREIGN KEY (`customer_id`) REFERENCES `shop`.`customers` (`id`) ON DELETE CASCADE ON UPDATE NO ACTION"));
        QCOMPARE(SASchema::createTrigger("trg", "before", "insert", "orders", "SET NEW.total = ABS(NEW.total)"),
                 QStringLiteral("CREATE TRIGGER `trg` BEFORE INSERT ON `orders` FOR EACH ROW SET NEW.total = ABS(NEW.total)"));
        QCOMPARE(SASchema::dropObject(SASchema::ObjectType::View, "v"), QStringLiteral("DROP VIEW `v`"));
        QCOMPARE(SASchema::maintenance("optimize", {"a", "b"}), QStringLiteral("OPTIMIZE TABLE `a`, `b`"));
    }

    // ---- content editing ----------------------------------------------------------------
    void contentEditing_updateInsertDelete()
    {
        QVector<SASchema::Column> columns(4);
        columns[0].name = "id"; columns[0].type = "INT"; columns[0].typeGroup = "integer"; columns[0].key = "PRI"; columns[0].nullable = false;
        columns[1].name = "name"; columns[1].type = "VARCHAR"; columns[1].typeGroup = "string";
        columns[2].name = "balance"; columns[2].type = "DECIMAL"; columns[2].typeGroup = "float"; columns[2].nullable = true;
        columns[3].name = "flag"; columns[3].type = "BIT"; columns[3].typeGroup = "bit"; columns[3].nullable = false;

        SARow oldRow{SACell::ofString("5"), SACell::ofString("Ana"), SACell::ofString("10.00"), SACell::ofString("0")};
        SAContentEditing::EditedRow edited;
        edited.cells = oldRow;
        edited.cells[1] = SACell::ofString("Ana Souza");
        edited.cells[2] = SACell::ofString("");        // empty numeric -> NULL
        edited.cells[3] = SACell::ofString("1");

        QString problem;
        const QString update = SAContentEditing::updateStatement("customers", columns, oldRow, edited, {"id"}, "NULL", escaper(), &problem);
        QCOMPARE(update, QStringLiteral("UPDATE `customers` SET `name` = 'Ana Souza', `balance` = NULL, `flag` = b'1' WHERE `id` = '5'"));

        // No primary key: all columns identify the row and LIMIT 1 is appended.
        const QString updateNoKey = SAContentEditing::updateStatement("customers", columns, oldRow, edited, {}, "NULL", escaper(), &problem);
        QVERIFY(updateNoKey.endsWith(QLatin1String("WHERE `id` = '5' AND `name` = 'Ana' AND `balance` = '10.00' AND `flag` = b'0' LIMIT 1")));

        // Unchanged row -> no statement.
        SAContentEditing::EditedRow same;
        same.cells = oldRow;
        QVERIFY(SAContentEditing::updateStatement("customers", columns, oldRow, same, {"id"}, "NULL", escaper()).isEmpty());

        SAContentEditing::EditedRow fresh;
        fresh.cells = {SACell::null(), SACell::ofString("Novo"), SACell::ofString("NULL"), SACell::ofString("")};
        QCOMPARE(SAContentEditing::insertStatement("customers", columns, fresh, "NULL", escaper()),
                 QStringLiteral("INSERT INTO `customers` (`id`, `name`, `balance`, `flag`) VALUES (NULL, 'Novo', NULL, b'0')"));

        const QStringList deletes = SAContentEditing::deleteStatements("customers", columns, {oldRow, SARow{SACell::ofString("6"), SACell::null(), SACell::null(), SACell::ofString("1")}}, {"id"}, escaper());
        QCOMPARE(deletes, QStringList{QStringLiteral("DELETE FROM `customers` WHERE `id` IN ('5', '6')")});

        const QStringList deletesNoKey = SAContentEditing::deleteStatements("customers", columns, {oldRow}, {}, escaper());
        QCOMPARE(deletesNoKey.size(), 1);
        QVERIFY(deletesNoKey.first().endsWith(QLatin1String("LIMIT 1")));
    }
    void contentEditing_bitLiterals()
    {
        SASchema::Column bit;
        bit.name = "flag"; bit.type = "BIT"; bit.typeGroup = "bit"; bit.nullable = false;
        QCOMPARE(SAContentEditing::valueLiteral(bit, SACell::ofString("b'1'"), false, "NULL", escaper()), QStringLiteral("b'1'"));
        QCOMPARE(SAContentEditing::valueLiteral(bit, SACell::ofString("1"), false, "NULL", escaper()), QStringLiteral("b'1'"));
        QCOMPARE(SAContentEditing::valueLiteral(bit, SACell::ofString(""), false, "NULL", escaper()), QStringLiteral("b'0'"));
        QCOMPARE(SAContentEditing::valueLiteral(bit, SACell::ofString("101"), false, "NULL", escaper()), QStringLiteral("b'101'"));
    }
    void contentEditing_display()
    {
        SAField bit;
        bit.typeGroup = "bit";
        bit.length = 1;
        QCOMPARE(SAContentEditing::displayString(bit, SACell::of(QByteArray("\x01", 1)), "NULL", false), QStringLiteral("1"));
        SAField blob;
        blob.typeGroup = "blobdata";
        blob.charsetnr = 63;
        QCOMPARE(SAContentEditing::displayString(blob, SACell::of(QByteArray("\x89PNG", 4)), "NULL", false), QStringLiteral("0x89504E47"));
        QCOMPARE(SAContentEditing::displayString(blob, SACell::of(QByteArray("plain")), "NULL", false), QStringLiteral("plain"));
        QCOMPARE(SAContentEditing::displayString(blob, SACell::null(), "NULL", false), QStringLiteral("NULL"));
    }

    // ---- content filters + themes ----------------------------------------------------------
    void contentFilters_load()
    {
        const QVector<SAContentFilter> &strings = SAContentFilters::filtersForType("string");
        QVERIFY(strings.size() >= 15);
        const SAContentFilter *contains = nullptr;
        for (const SAContentFilter &f : strings) if (f.menuLabel == QLatin1String("contains")) contains = &f;
        QVERIFY(contains);
        auto esc = [](const QString &s) { return SAMySQLConnection().escapeString(s, false); };
        QCOMPARE(SAContentFilters::buildClause(*contains, "`name`", {"o'x"}, false, esc), QStringLiteral("`name` LIKE '%o\\'x%'"));
        QCOMPARE(SAContentFilters::buildClause(*contains, "`name`", {"a"}, true, esc), QStringLiteral("`name` LIKE BINARY '%a%'"));
        QCOMPARE(SAContentFilters::filterTypeForTypeGroup("integer"), QStringLiteral("number"));
        QCOMPARE(SAContentFilters::filterTypeForTypeGroup("geometry"), QStringLiteral("spatial"));
    }
    void themes_load()
    {
        const QStringList names = SAEditorTheme::availableThemeNames();
        QVERIFY(names.contains(QStringLiteral("Dark Ace")));
        QVERIFY(names.contains(QStringLiteral("Monokai")));
        const SAEditorTheme dark = SAEditorTheme::themeNamed(QStringLiteral("Dark Ace"));
        QCOMPARE(dark.background, QColor(QStringLiteral("#000000")));
        QCOMPARE(dark.keyword, QColor(QStringLiteral("#7FBCFE")));
        QCOMPARE(dark.comment, QColor(QStringLiteral("#71FF48")));
    }

    // ---- multi-rule filter tree -------------------------------------------------------------
    static QString quoteId(const QString &c) { return QStringLiteral("`%1`").arg(c); }
    static QString escLit(const QString &s) { return SAMySQLConnection().escapeString(s, false); }

    void filterTree_singleLeafMatchesDirectClause()
    {
        SAFilterExpr expr;
        expr.column = "n";
        expr.filterType = "number";
        expr.operatorIndex = 0;   // "="
        expr.values = {"1"};
        SAFilterNodePtr leaf = SAFilterNode::makeLeaf(expr);
        const QString viaTree = SAFilterTree::buildWhere(leaf, false, quoteId, escLit);
        const SAContentFilter &eq = SAContentFilters::filtersForType("number").at(0);
        const QString direct = SAContentFilters::buildClause(eq, "`n`", {"1"}, false, escLit);
        QCOMPARE(viaTree, direct);
    }

    void filterTree_nestedAndOr()
    {
        SAFilterExpr a; a.column = "a"; a.filterType = "number"; a.operatorIndex = 0; a.values = {"1"};
        SAFilterExpr b; b.column = "b"; b.filterType = "number"; b.operatorIndex = 0; b.values = {"2"};
        SAFilterExpr c; c.column = "c"; c.filterType = "number"; c.operatorIndex = 0; c.values = {"3"};

        SAFilterNodePtr nested = SAFilterNode::makeGroup(true /*AND*/);
        nested->children << SAFilterNode::makeLeaf(b) << SAFilterNode::makeLeaf(c);

        SAFilterNodePtr root = SAFilterNode::makeGroup(false /*OR*/, true);
        root->children << SAFilterNode::makeLeaf(a) << nested;

        const QString sql = SAFilterTree::buildWhere(root, false, quoteId, escLit);
        QCOMPARE(sql, QStringLiteral("`a` = '1' OR (`b` = '2' AND `c` = '3')"));
    }

    void filterTree_disabledAndEmptyGroupsIgnored()
    {
        SAFilterExpr a; a.column = "a"; a.filterType = "number"; a.operatorIndex = 0; a.values = {"1"};
        SAFilterExpr b; b.column = "b"; b.filterType = "number"; b.operatorIndex = 0; b.values = {"2"}; b.enabled = false;

        SAFilterNodePtr emptyGroup = SAFilterNode::makeGroup(true);   // no children at all
        SAFilterNodePtr root = SAFilterNode::makeGroup(true, true);
        root->children << SAFilterNode::makeLeaf(a) << SAFilterNode::makeLeaf(b) << emptyGroup;

        QCOMPARE(SAFilterTree::buildWhere(root, false, quoteId, escLit), QStringLiteral("`a` = '1'"));
    }

    void filterTree_incompleteLeafSkipped()
    {
        SAFilterExpr empty; empty.column = "a"; empty.filterType = "number"; empty.operatorIndex = 0;   // no value
        SAFilterNodePtr leaf = SAFilterNode::makeLeaf(empty);
        QCOMPARE(SAFilterTree::buildWhere(leaf, false, quoteId, escLit), QString());
    }

    void filterTree_variantRoundTrip()
    {
        SAFilterExpr a; a.column = "a"; a.filterType = "number"; a.operatorIndex = 0; a.values = {"1"};
        SAFilterExpr b; b.column = "b"; b.filterType = "string"; b.operatorIndex = 0; b.values = {"x"};
        SAFilterNodePtr nested = SAFilterNode::makeGroup(true);
        nested->children << SAFilterNode::makeLeaf(b);
        SAFilterNodePtr root = SAFilterNode::makeGroup(false, true);
        root->children << SAFilterNode::makeLeaf(a) << nested;

        const QString before = SAFilterTree::buildWhere(root, false, quoteId, escLit);
        const SAFilterNodePtr restored = SAFilterTree::fromVariant(SAFilterTree::toVariant(root));
        QVERIFY(restored);
        const QString after = SAFilterTree::buildWhere(restored, false, quoteId, escLit);
        QCOMPARE(after, before);
    }

    // ---- MCP read-only guard -----------------------------------------------------------------
    void mcpGuard_allowsReadStatements()
    {
        for (const QString &sql : {QStringLiteral("SELECT * FROM t"), QStringLiteral("  select 1"),
                                    QStringLiteral("SHOW TABLES"), QStringLiteral("DESCRIBE t"),
                                    QStringLiteral("EXPLAIN SELECT 1")})
            QVERIFY2(SAMCPReadOnlyGuard::checkReadOnly(sql).allowed, qPrintable(sql));
    }

    void mcpGuard_blocksWrites()
    {
        for (const QString &sql : {QStringLiteral("INSERT INTO t VALUES (1)"), QStringLiteral("UPDATE t SET a=1"),
                                    QStringLiteral("DELETE FROM t"), QStringLiteral("DROP TABLE t"),
                                    QStringLiteral("TRUNCATE t"), QStringLiteral("GRANT ALL ON *.* TO u")})
            QVERIFY2(!SAMCPReadOnlyGuard::checkReadOnly(sql).allowed, qPrintable(sql));
    }

    void mcpGuard_blocksExplainAnalyzeVariants()
    {
        QVERIFY(SAMCPReadOnlyGuard::isExplainAnalyze("EXPLAIN ANALYZE SELECT 1"));
        QVERIFY(SAMCPReadOnlyGuard::isExplainAnalyze("explain analyze select 1"));
        QVERIFY(SAMCPReadOnlyGuard::isExplainAnalyze("EXPLAIN FORMAT=JSON ANALYZE SELECT 1"));
        QVERIFY(!SAMCPReadOnlyGuard::isExplainAnalyze("EXPLAIN SELECT 1"));
        QVERIFY(!SAMCPReadOnlyGuard::checkReadOnly("EXPLAIN ANALYZE SELECT 1").allowed);
    }

    void mcpGuard_multipleStatementsBlockedTrailingSemicolonAllowed()
    {
        QVERIFY(!SAMCPReadOnlyGuard::checkReadOnly("SELECT 1; SELECT 2").allowed);
        QVERIFY(SAMCPReadOnlyGuard::checkReadOnly("SELECT 1;").allowed);
        QVERIFY(SAMCPReadOnlyGuard::checkReadOnly("SELECT ';' AS x").allowed);
    }

    void mcpGuard_outfileBlockedButQuotedIdentifierAllowed()
    {
        QVERIFY(!SAMCPReadOnlyGuard::checkReadOnly("SELECT * INTO OUTFILE '/tmp/x' FROM t").allowed);
        QVERIFY(!SAMCPReadOnlyGuard::checkReadOnly("SELECT LOAD_FILE('/etc/passwd')").allowed);
        QVERIFY(SAMCPReadOnlyGuard::checkReadOnly("SELECT * FROM `outfile`").allowed);
    }

    void mcpGuard_executableCommentsBlocked()
    {
        QVERIFY(!SAMCPReadOnlyGuard::checkReadOnly("SELECT /*! 1 */ 1").allowed);
        QVERIFY(!SAMCPReadOnlyGuard::checkReadOnly("SELECT /*M! 1 */ 1").allowed);
        QVERIFY(SAMCPReadOnlyGuard::checkReadOnly("SELECT /* plain comment */ 1").allowed);
    }

    void mcpGuard_cteWithWriteBlocked()
    {
        QVERIFY(!SAMCPReadOnlyGuard::checkReadOnly("WITH cte AS (SELECT 1) DELETE FROM t").allowed);
        QVERIFY(SAMCPReadOnlyGuard::checkReadOnly("WITH cte AS (SELECT 1) SELECT * FROM cte").allowed);
    }

    void mcpGuard_bindParametersEscapesInjectionAttempt()
    {
        const QString bound = SAMCPReadOnlyGuard::bindParameters(QStringLiteral("SELECT * FROM t WHERE name = ?"),
                                                                   {QStringLiteral("x'; DROP TABLE t; --")});
        QVERIFY(SAMCPReadOnlyGuard::checkReadOnly(bound).allowed);
    }

    void mcpGuard_lineCommentNeedsServerWhitespace()
    {
        // The server opens a `--` comment only on whitespace or a control
        // character (<= 0x20). U+00A0 is whitespace to QChar::isSpace() but not
        // to MySQL, so treating it as a comment would hide from the guard text
        // that the server still parses.
        const QString nbsp = QStringLiteral("SELECT 1 --") + QChar(0x00A0) + QStringLiteral("DELETE FROM t");
        QVERIFY(SAMCPReadOnlyGuard::stripComments(nbsp).contains(QLatin1String("DELETE")));

        // A real space, and a control character, do open one.
        QVERIFY(!SAMCPReadOnlyGuard::stripComments(QStringLiteral("SELECT 1 -- DELETE FROM t")).contains(QLatin1String("DELETE")));
        const QString control = QStringLiteral("SELECT 1 --") + QChar(0x01) + QStringLiteral("DELETE FROM t");
        QVERIFY(!SAMCPReadOnlyGuard::stripComments(control).contains(QLatin1String("DELETE")));

        // bindParameters keeps the same view of where a comment ends, so a
        // placeholder is bound consistently with what checkReadOnly then sees.
        const QString placeholder = QStringLiteral("SELECT 1 --") + QChar(0x00A0) + QStringLiteral("?");
        QVERIFY(SAMCPReadOnlyGuard::bindParameters(placeholder, {QStringLiteral("x")}).endsWith(QLatin1String("'x'")));
        QVERIFY(SAMCPReadOnlyGuard::bindParameters(QStringLiteral("SELECT 1 -- ?"), {QStringLiteral("x")}).endsWith(QLatin1Char('?')));
    }

    void mcpToolDefinitions_uniqueNamesAndObjectSchemas()
    {
        const QVector<SAMCPToolDefinition> tools = SAMCPToolDefinitions::all();
        QVERIFY(tools.size() >= 15);
        QSet<QString> names;
        for (const SAMCPToolDefinition &t : tools) {
            QVERIFY(!names.contains(t.name));
            names.insert(t.name);
            QCOMPARE(t.inputSchema.value("type").toString(), QStringLiteral("object"));
        }
    }

    // ---- user manager DDL builders -----------------------------------------------------------
    static QString escId(const QString &s) { return QStringLiteral("'%1'").arg(QString(s).replace("'", "''")); }

    void userManager_createUserVariants()
    {
        QCOMPARE(SAUserManager::createUser("bob", "%", "secret", QString(), QString(), false, escId),
                 QStringLiteral("CREATE USER 'bob'@'%' IDENTIFIED BY 'secret'"));
        QCOMPARE(SAUserManager::createUser("bob", "%", "secret", QString(), "mysql_native_password", true, escId),
                 QStringLiteral("CREATE USER 'bob'@'%' IDENTIFIED WITH mysql_native_password BY 'secret'"));
        QCOMPARE(SAUserManager::createUser("bob", "%", QString(), "*HASH*", "mysql_native_password", true, escId),
                 QStringLiteral("CREATE USER 'bob'@'%' IDENTIFIED WITH mysql_native_password AS '*HASH*'"));
        QCOMPARE(SAUserManager::createUser("bob", "%", QString(), QString(), QString(), false, escId),
                 QStringLiteral("CREATE USER 'bob'@'%'"));
    }

    void userManager_grantAndRevokeStatements()
    {
        const QString grant = SAUserManager::grantStatement({"SELECT", "INSERT"}, QString(), "bob", "%", true, escId);
        QVERIFY(grant.contains("GRANT SELECT, INSERT ON *.* TO 'bob'@'%'"));
        QVERIFY(grant.contains("WITH GRANT OPTION"));
        const QString grantDb = SAUserManager::grantStatement({"SELECT"}, "mydb", "bob", "%", false, escId);
        QVERIFY(grantDb.contains("ON `mydb`.*"));
        const QString revoke = SAUserManager::revokeStatement({"SELECT"}, QString(), "bob", "%", escId);
        QVERIFY(revoke.startsWith("REVOKE SELECT ON *.* FROM"));
    }

    void userManager_privilegeNameMapping()
    {
        QCOMPARE(SAUserManager::grantNameForPrivilegeKey(SAUserManager::privilegeKeyForColumn("Grant_priv")), QStringLiteral("GRANT OPTION"));
        QCOMPARE(SAUserManager::privilegeKeyForServerPrivilegeName("CONNECTION_ADMIN"), QStringLiteral("connection_admin_priv"));
    }

    void userManager_schemaWildcardEscapeRoundTrip()
    {
        const QString escaped = SAUserManager::escapeSchemaWildcards("my_db%name");
        QCOMPARE(SAUserManager::unescapeSchemaWildcards(escaped), QStringLiteral("my_db%name"));
    }

    // ---- SQL classifier (port of SPCustomQuerySQLClassifier) ------------------

    // ---- result export (formatting extracted from SAExportDialog) ------------

    void resultExport_csvEscape()
    {
        using namespace SAResultExport;
        QCOMPARE(csvEscape(QStringLiteral("plain"), QStringLiteral("\"")), QStringLiteral("\"plain\""));
        // An enclosure inside the value is doubled.
        QCOMPARE(csvEscape(QStringLiteral("say \"hi\""), QStringLiteral("\"")), QStringLiteral("\"say \"\"hi\"\"\""));
        // No enclosure means the value is written raw.
        QCOMPARE(csvEscape(QStringLiteral("a,b"), QString()), QStringLiteral("a,b"));
        // A multi-character enclosure is doubled as a whole, so a lone "|" is not touched.
        QCOMPARE(csvEscape(QStringLiteral("a|b"), QStringLiteral("||")), QStringLiteral("||a|b||"));
        QCOMPARE(csvEscape(QStringLiteral("a||b"), QStringLiteral("||")), QStringLiteral("||a||||b||"));
    }

    void resultExport_csvRowsByTypeGroup()
    {
        using namespace SAResultExport;
        QVector<SAField> fields(5);
        // A numeric column carries the binary charset in MySQL, and must still
        // be written as a number rather than hex.
        fields[0].name = QStringLiteral("id");      fields[0].typeGroup = QStringLiteral("integer");
        fields[0].charsetnr = SASQLTypes::BinaryCharsetNumber;
        fields[1].name = QStringLiteral("name");    fields[1].typeGroup = QStringLiteral("string");
        // isBinary() reads charsetnr, not typeGroup.
        fields[2].name = QStringLiteral("blob");    fields[2].typeGroup = QStringLiteral("blobdata");
        fields[2].charsetnr = SASQLTypes::BinaryCharsetNumber;
        fields[3].name = QStringLiteral("flag");    fields[3].typeGroup = QStringLiteral("bit");
        fields[4].name = QStringLiteral("note");    fields[4].typeGroup = QStringLiteral("string");

        SARow row;
        row << SACell::of(QByteArray("42"))
            << SACell::of(QByteArray("say \"hi\", ok\nnext"))
            << SACell::of(QByteArray::fromHex("00ff10"))
            << SACell::of(QByteArray(1, char(1)))
            << SACell();   // NULL

        const QString csv = csvRows(fields, {row}, CsvOptions());
        // Numerics bare, strings enclosed with the enclosure doubled, binary as
        // 0x hex, bit as a number, and NULL as the null string *without* the
        // enclosure -- that is the only thing separating it from the text "NULL".
        QCOMPARE(csv, QStringLiteral("42,\"say \"\"hi\"\", ok\nnext\",\"0x00ff10\",1,NULL\n"));

        // A value whose text is literally NULL is enclosed, so the two differ.
        SARow literal;
        literal << SACell::of(QByteArray("1")) << SACell::of(QByteArray("NULL"))
                << SACell() << SACell() << SACell();
        QVERIFY(csvRows(fields, {literal}, CsvOptions()).contains(QStringLiteral("\"NULL\"")));
    }

    void resultExport_csvHeaderLine()
    {
        using namespace SAResultExport;
        QVector<SAField> fields(2);
        fields[0].name = QStringLiteral("a");
        fields[1].name = QStringLiteral("b,c");
        CsvOptions options;
        QCOMPARE(csvHeaderLine(fields, options), QStringLiteral("\"a\",\"b,c\"\n"));

        QVector<SASchema::Column> columns(2);
        columns[0].name = QStringLiteral("a");
        columns[1].name = QStringLiteral("b");
        QCOMPARE(csvHeaderLine(columns, options), QStringLiteral("\"a\",\"b\"\n"));

        options.header = false;
        QCOMPARE(csvHeaderLine(fields, options), QString());
        QCOMPARE(csvHeaderLine(columns, options), QString());
    }

    void resultExport_csvOptionsAreHonoured()
    {
        using namespace SAResultExport;
        QVector<SAField> fields(1);
        fields[0].name = QStringLiteral("v");
        fields[0].typeGroup = QStringLiteral("string");
        CsvOptions options;
        options.separator = QStringLiteral(";");
        options.enclosure = QString();
        options.nullString = QStringLiteral("\\N");

        SARow a; a << SACell::of(QByteArray("x"));
        SARow b; b << SACell();
        QCOMPARE(csvRows(fields, {a, b}, options), QStringLiteral("x\n\\N\n"));
    }

    void resultExport_batchSelect()
    {
        using namespace SAResultExport;
        const QStringList columns{QStringLiteral("`a`"), QStringLiteral("`b`")};
        QCOMPARE(batchSelect(QStringLiteral("`t`"), columns, QString(), QString(), 0, 2000),
                 QStringLiteral("SELECT `a`, `b` FROM `t` LIMIT 0, 2000"));
        QCOMPARE(batchSelect(QStringLiteral("`t`"), columns, QStringLiteral("`a` > 1"), QString(), 4000, 2000),
                 QStringLiteral("SELECT `a`, `b` FROM `t` WHERE `a` > 1 LIMIT 4000, 2000"));
        QCOMPARE(batchSelect(QStringLiteral("`t`"), columns, QString(), QStringLiteral("`a` DESC"), 0, 10),
                 QStringLiteral("SELECT `a`, `b` FROM `t` ORDER BY `a` DESC LIMIT 0, 10"));
        QCOMPARE(batchSelect(QStringLiteral("`t`"), columns, QStringLiteral("  `a` > 1  "), QStringLiteral("  `a`  "), 1, 2),
                 QStringLiteral("SELECT `a`, `b` FROM `t` WHERE `a` > 1 ORDER BY `a` LIMIT 1, 2"));
    }

    void resultExport_insertStatementFallsBackToAlias()
    {
        // Pins the behaviour the query-result export source relies on: a field
        // with no original column name (a computed column) uses its alias.
        QVector<SAField> fields(2);
        fields[0].name = QStringLiteral("n");
        fields[0].orgName = QStringLiteral("name");
        fields[0].typeGroup = QStringLiteral("string");
        fields[1].name = QStringLiteral("total");
        fields[1].orgName = QString();
        fields[1].typeGroup = QStringLiteral("integer");

        SARow row;
        row << SACell::of(QByteArray("Ana")) << SACell::of(QByteArray("3"));
        const QString sql = SAContentEditing::insertStatementForRows(QStringLiteral("t"), fields, {row}, false, escaper());
        QVERIFY2(sql.contains(QStringLiteral("`name`")), qPrintable(sql));
        QVERIFY2(sql.contains(QStringLiteral("`total`")), qPrintable(sql));
    }

    void sqlClassifier_explainableAcceptsOnlySelectAndWith()
    {
        using namespace SASQLClassifier;
        for (const QString &sql : {
                 QStringLiteral("SELECT * FROM t"),
                 QStringLiteral("(WITH cte AS (SELECT 1) SELECT * FROM cte)"),
                 QStringLiteral("WITH RECURSIVE cte AS (SELECT 1 AS n UNION ALL SELECT n + 1 FROM cte WHERE n < 10) SELECT * FROM cte"),
                 QStringLiteral("select * from t"),
                 QStringLiteral("(with cte as (select 1) select * from cte)"),
                 QStringLiteral("SELECT(1)"),
                 QStringLiteral("SELECT/*c*/1"),
                 QStringLiteral("((WITH x AS (SELECT 1) SELECT * FROM x))"),
             })
            QVERIFY2(isQueryExplainable(sql), qPrintable(sql));

        for (const QString &sql : {
                 QStringLiteral("EXPLAIN SELECT * FROM t"),
                 QStringLiteral("UPDATE t SET c = 1"),
                 QStringLiteral("(UPDATE t SET c = 1)"),
                 QStringLiteral("SELECTOR_TABLE"),
                 QStringLiteral("WITHOUT VALIDATION"),
                 QStringLiteral("SELECT"),
                 QStringLiteral("WITH"),
                 QStringLiteral("SHOW TABLES"),
                 QStringLiteral("DESCRIBE t"),
                 QString(),
                 QStringLiteral("   "),
             })
            QVERIFY2(!isQueryExplainable(sql), qPrintable(sql));
    }

    void sqlClassifier_explainRejectsIndeterminateGates()
    {
        using namespace SASQLClassifier;
        QVERIFY(!isQueryExplainable(QStringLiteral("/*!99999 SELECT 1 */ SELECT 2")));
        QVERIFY(!isQueryExplainable(QStringLiteral("/*M! SELECT 1 */ SELECT 2")));
        // A gate inside a string literal is data, not a comment.
        QVERIFY(isQueryExplainable(QStringLiteral("SELECT '/*!99999 DELETE FROM t */'")));
        // Server context removes the ambiguity.
        QVERIFY(isQueryExplainable(QStringLiteral("/*!99999 SELECT 1 */ SELECT 2"), ServerContext::mysql(80000)));
    }

    void sqlClassifier_safeWithoutWarningAcceptsReadsAndPlainExplain()
    {
        using namespace SASQLClassifier;
        for (const QString &sql : {
                 QStringLiteral("EXPLAIN SELECT * FROM t"),
                 QStringLiteral("EXPLAIN FORMAT=JSON UPDATE t SET c = 1"),
                 QStringLiteral("DESCRIBE t"),
                 QStringLiteral("DESC t"),
                 QStringLiteral("explain select * from t"),
                 QStringLiteral("SHOW TABLES"),
                 QStringLiteral("SELECT/* c */1"),
                 QStringLiteral("EXPLAIN    SELECT  *  FROM  t"),
                 QStringLiteral("EXPLAIN\t\tSELECT * FROM t"),
                 QStringLiteral("(SELECT * FROM t)"),
                 QStringLiteral("((SELECT 1))"),
                 QStringLiteral("EXPLAIN WITH c AS (SELECT 1) UPDATE t SET c = 1"),
                 QStringLiteral("EXPLAIN ANALYZE SELECT * FROM t"),
                 QStringLiteral("EXPLAIN ANALYZE TABLE t"),
                 QStringLiteral("SELECT '/*!99999 DELETE FROM important_table */'"),
             })
            QVERIFY2(isQuerySafeWithoutDestructiveWarning(sql), qPrintable(sql));

        for (const QString &sql : {
                 QStringLiteral("UPDATE t SET c = 1"),
                 QStringLiteral("DELETE FROM t"),
                 QStringLiteral("INSERT INTO t VALUES (1)"),
                 QStringLiteral("CREATE TABLE x (a INT)"),
                 QStringLiteral("USE db"),
                 QStringLiteral("EXPLAINER ANALYZE DELETE FROM t"),
                 QStringLiteral("SELECTOR_TABLE"),
                 QStringLiteral("(UPDATE t SET c = 1)"),
                 QString(),
                 QStringLiteral("   "),
             })
            QVERIFY2(!isQuerySafeWithoutDestructiveWarning(sql), qPrintable(sql));
    }

    void sqlClassifier_explainAnalyzeMutationsRequireWarning()
    {
        using namespace SASQLClassifier;
        for (const QString &sql : {
                 QStringLiteral("EXPLAIN ANALYZE DELETE FROM t WHERE id = 1"),
                 QStringLiteral("EXPLAIN ANALYZE UPDATE t SET c = c + 1"),
                 QStringLiteral("EXPLAIN ANALYZE FORMAT=TREE DELETE FROM t WHERE id = 1"),
                 QStringLiteral("EXPLAIN FORMAT=TREE ANALYZE UPDATE t SET c = 1"),
                 QStringLiteral("explain analyze delete from t where id = 1"),
                 QStringLiteral("DESCRIBE ANALYZE DELETE FROM t WHERE id = 1"),
                 QStringLiteral("DESC ANALYZE UPDATE t SET c = 1"),
                 QStringLiteral("/* c */ EXPLAIN /* c */ ANALYZE /* c */ DELETE FROM t"),
                 QStringLiteral("(EXPLAIN ANALYZE DELETE FROM t WHERE id = 1)"),
                 QStringLiteral("(EXPLAIN FORMAT=TREE ANALYZE UPDATE t SET c = 1)"),
                 QStringLiteral("((DESC ANALYZE DELETE FROM t WHERE id = 1))"),
                 QStringLiteral("EXPLAIN ANALYZE"),
                 QStringLiteral("EXPLAIN ANALYZE FORMAT = JSON"),
             })
            QVERIFY2(!isQuerySafeWithoutDestructiveWarning(sql), qPrintable(sql));
    }

    void sqlClassifier_explainAnalyzeCTERequiresWarning()
    {
        using namespace SASQLClassifier;
        // EXPLAIN ANALYZE executes the statement, and finding the outer verb
        // past a CTE list needs a real parser, so any CTE is refused.
        for (const QString &sql : {
                 QStringLiteral("EXPLAIN ANALYZE WITH c AS (SELECT 1 AS id) UPDATE t JOIN c ON t.id = c.id SET t.c = 1"),
                 QStringLiteral("EXPLAIN ANALYZE FORMAT=TREE WITH c AS (SELECT 1 AS id) DELETE t FROM t JOIN c ON t.id = c.id"),
                 QStringLiteral("DESCRIBE ANALYZE WITH c AS (SELECT 1) UPDATE t SET c = 1"),
                 QStringLiteral("EXPLAIN ANALYZE WITH c AS (SELECT 1) SELECT * FROM c"),
                 QStringLiteral("EXPLAIN ANALYZE /* hint */ WITH c AS (SELECT 1) UPDATE t SET c = 1"),
                 QStringLiteral("EXPLAIN ANALYZE EXTENDED WITH c AS (SELECT 1) UPDATE t SET c = 1"),
             })
            QVERIFY2(!isQuerySafeWithoutDestructiveWarning(sql), qPrintable(sql));

        // Plain EXPLAIN does not execute, so a CTE is fine.
        QVERIFY(isQuerySafeWithoutDestructiveWarning(QStringLiteral("EXPLAIN WITH c AS (SELECT 1) UPDATE t SET c = 1")));
    }

    void sqlClassifier_executableCommentGates()
    {
        using namespace SASQLClassifier;
        QCOMPARE(strippedSQL(QStringLiteral("/*!40101 USE executable_db */")).trimmed(), QStringLiteral("USE executable_db"));

        QVERIFY(!isQuerySafeWithoutDestructiveWarning(QStringLiteral("/*! EXPLAIN ANALYZE DELETE FROM t */")));
        QVERIFY(!isQuerySafeWithoutDestructiveWarning(QStringLiteral("/*!99999 DELETE FROM future_table */")));
        QVERIFY(!isQuerySafeWithoutDestructiveWarning(QStringLiteral("/*!99999 SELECT */ DELETE FROM important_table")));
        QVERIFY(!isQuerySafeWithoutDestructiveWarning(QStringLiteral("/*!99999 DELETE FROM important_table */ SELECT 1")));
        QVERIFY(!isQuerySafeWithoutDestructiveWarning(QStringLiteral("/*!99999 SELECT 1 */ SELECT 2")));
        QVERIFY(isQuerySafeWithoutDestructiveWarning(QStringLiteral("/*!99999 SELECT 1 */ SELECT 2"), ServerContext::mysql(80000)));

        QVERIFY(!isQuerySafeWithoutDestructiveWarning(QStringLiteral("/*M! SELECT 1 */ SELECT 2")));
        QVERIFY(isQuerySafeWithoutDestructiveWarning(QStringLiteral("/*M! SELECT 1 */ SELECT 2"), ServerContext::mariaDB(101100)));
        // A MariaDB-only body is dropped on MySQL, leaving the trailing SELECT.
        QVERIFY(isQuerySafeWithoutDestructiveWarning(QStringLiteral("/*M! SELECT 1 */ SELECT 2"), ServerContext::mysql(80000)));
        // MariaDB deliberately ignores MySQL 5.7+ gates in the 50700-99999 range.
        QVERIFY(isQuerySafeWithoutDestructiveWarning(QStringLiteral("/*!80000 DELETE FROM t */ SELECT 1"), ServerContext::mariaDB(101100)));

        // An unparseable version gate is treated as inactive when the server is
        // known, and as indeterminate when it is not.
        QCOMPARE(strippedSQL(QStringLiteral("/*!999999999999999999999999 USE overflow_db */"), ServerContext::mysql(80000)).trimmed(), QString());
        const StripResult overflow = stripComments(QStringLiteral("/*!999999999999999999999999 SELECT 1 */ DELETE FROM t"));
        QVERIFY(overflow.sawExecutableComment);
        QVERIFY(overflow.indeterminateExecutableComment);
    }

    void sqlClassifier_stripCommentsLexicalRules()
    {
        using namespace SASQLClassifier;
        const QString literals = QStringLiteral("SELECT '-- value', `db#name`, \"/* value */\"");
        QCOMPARE(strippedSQL(literals), literals);

        QCOMPARE(strippedSQL(QStringLiteral("SELECT 1--")), QStringLiteral("SELECT 1 "));
        QCOMPARE(strippedSQL(QStringLiteral("SELECT 1-- x")), QStringLiteral("SELECT 1 "));
        // MySQL needs whitespace after -- for it to open a comment.
        QCOMPARE(strippedSQL(QStringLiteral("SELECT 1--x")), QStringLiteral("SELECT 1--x"));
        QCOMPARE(strippedSQL(QStringLiteral("SELECT/*c*/1")), QStringLiteral("SELECT 1"));
        QCOMPARE(strippedSQL(QStringLiteral("SELECT # c\n1")), QStringLiteral("SELECT  \n1"));
        QCOMPARE(strippedSQL(QStringLiteral("SELECT 'a\\'b' /* c */")), QStringLiteral("SELECT 'a\\'b'  "));
        QCOMPARE(strippedSQL(QStringLiteral("SELECT `a``b` -- x")), QStringLiteral("SELECT `a``b`  "));
        QCOMPARE(strippedSQL(QStringLiteral("SELECT 1 /* unterminated")), QStringLiteral("SELECT 1  "));
        QCOMPARE(strippedSQL(QStringLiteral("SELECT 'abc")), QStringLiteral("SELECT 'abc"));
        // Brackets are not quotes in MySQL.
        QCOMPARE(strippedSQL(QStringLiteral("SELECT [/* comment */]")), QStringLiteral("SELECT [ ]"));
        QCOMPARE(strippedSQL(QStringLiteral("SELECT '--'")), QStringLiteral("SELECT '--'"));
        // NBSP is whitespace to QChar::isSpace() but not to MySQL (which wants
        // <= 0x20), so it does not open a comment and the DELETE stays visible.
        const QString nbsp = QStringLiteral("SELECT 1--") + QChar(0x00A0) + QStringLiteral("DELETE FROM t");
        QCOMPARE(strippedSQL(nbsp), nbsp);
        // With a real space the same text is a comment and the DELETE is gone.
        QCOMPARE(strippedSQL(QStringLiteral("SELECT 1-- DELETE FROM t")), QStringLiteral("SELECT 1 "));
    }

    void sqlClassifier_nestedExecutableCommentsAreBounded()
    {
        using namespace SASQLClassifier;
        const QString pathological = QStringLiteral("/*!").repeated(2000);
        const StripResult result = stripComments(pathological);
        QVERIFY(result.depthLimitExceeded);
        QVERIFY(result.indeterminateExecutableComment);
        QVERIFY(!isQueryExplainable(pathological));
        QVERIFY(!isQuerySafeWithoutDestructiveWarning(pathological));
    }

    void sqlClassifier_dropAndFlagPolicyMatchesTheGuard()
    {
        using namespace SASQLClassifier;
        StripOptions drop;
        drop.executableComments = ExecutableComments::DropAndFlag;
        const StripResult result = stripComments(QStringLiteral("/*!40101 DELETE FROM t */ SELECT 1"), drop);
        QVERIFY(result.sawExecutableComment);
        QCOMPARE(result.sql.simplified(), QStringLiteral("SELECT 1"));
    }

    void sqlClassifier_batchNeedsDestructiveWarning()
    {
        using namespace SASQLClassifier;
        QVERIFY(!batchNeedsDestructiveWarning({}));
        QVERIFY(!batchNeedsDestructiveWarning({QStringLiteral("SELECT 1"), QStringLiteral("SHOW TABLES")}));
        QVERIFY(!batchNeedsDestructiveWarning({QStringLiteral("EXPLAIN SELECT 1")}));
        QVERIFY(batchNeedsDestructiveWarning({QStringLiteral("SELECT 1"), QStringLiteral("DELETE FROM t")}));
        // The regex this replaced missed all four of these.
        QVERIFY(batchNeedsDestructiveWarning({QStringLiteral("  /* comment */ DELETE FROM t")}));
        QVERIFY(batchNeedsDestructiveWarning({QStringLiteral("DESC ANALYZE UPDATE t SET c = 1")}));
        QVERIFY(batchNeedsDestructiveWarning({QStringLiteral("/*!99999 SELECT 1 */ SELECT 2")}));
        QVERIFY(batchNeedsDestructiveWarning({QStringLiteral("INSERT INTO t VALUES (1)")}));
    }

    void splitter_normaliseForExecution()
    {
        QCOMPARE(SASQLSplitter::normaliseForExecution(QStringLiteral("  SELECT 1  \n")), QStringLiteral("SELECT 1"));
        QCOMPARE(SASQLSplitter::normaliseForExecution(QStringLiteral("SELECT\r\n1")), QStringLiteral("SELECT\n1"));
        QCOMPARE(SASQLSplitter::normaliseForExecution(QStringLiteral("\r\nSELECT 1\r\n")), QStringLiteral("SELECT 1"));
        // CR inside a quoted literal is data.
        QCOMPARE(SASQLSplitter::normaliseForExecution(QStringLiteral("SELECT 'a\r\nb'")), QStringLiteral("SELECT 'a\r\nb'"));
        QCOMPARE(SASQLSplitter::normaliseForExecution(QStringLiteral("SELECT `a\rb`")), QStringLiteral("SELECT `a\rb`"));
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "tst_core.moc"
