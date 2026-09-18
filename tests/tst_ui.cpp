//
//  tst_ui.cpp
//  Sequel Ace (Linux port) - widget tests driven offscreen against a live server.
//
//  Enable with SA_TEST_MYSQL=1 (see tst_integration.cpp for the variables).
//  Runs with QStandardPaths test mode so the user's preferences and favorites
//  are never touched.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include <QtTest>
#include <QStandardPaths>
#include <QTableWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QToolButton>

#include "SAConnectionInfo.h"
#include "SACustomQueryView.h"
#include "SADatabaseDocument.h"
#include "SAFavoritesStore.h"
#include "SAMySQLConnection.h"
#include "SAPreferences.h"
#include "SAResultModel.h"
#include "SAResultTableView.h"
#include "SAExportDialog.h"
#include "SASQLEditor.h"
#include "SATableContentView.h"
#include "SATableStructureView.h"
#include "SATablesList.h"

class UITests : public QObject {
    Q_OBJECT

    SAMySQLOptions m_options;
    QString m_db;
    SAFavoritesStore *m_store = nullptr;
    SADatabaseDocument *m_document = nullptr;

    static QString env(const char *name, const QString &fallback)
    {
        const QString v = qEnvironmentVariable(name);
        return v.isEmpty() ? fallback : v;
    }
    SAMySQLConnection *directConnection()
    {
        auto *c = new SAMySQLConnection;
        SAMySQLOptions o = m_options;
        o.database = m_db;
        c->setOptions(o);
        if (!c->connect()) { delete c; return nullptr; }
        return c;
    }

    // Restores preferences even when an assertion fails: the QTest macros
    // return from the test function, so this destructor still runs. Without it
    // a failure leaves the page limit behind in the persisted settings and the
    // earlier tests start loading one row.
    struct PreferenceGuard {
        QStringList keys;
        QVariantList values;
        explicit PreferenceGuard(const QStringList &watched) : keys(watched)
        {
            for (const QString &key : keys) values << SAPreferences::instance().value(key);
        }
        ~PreferenceGuard()
        {
            for (int i = 0; i < keys.size(); ++i) SAPreferences::instance().set(keys.at(i), values.at(i));
        }
    };

    // The export tests need rows they fully control: the shared `people` table
    // is mutated by the editing tests that run before them.
    void createExportFixture()
    {
        SAMySQLConnection c;
        c.setOptions(m_options);
        QVERIFY2(c.connect(), qPrintable(c.lastErrorMessage()));
        QVERIFY(c.selectDatabase(m_db));
        QVERIFY(c.execute(QStringLiteral("DROP TABLE IF EXISTS export_demo")));
        QVERIFY2(c.execute(QStringLiteral("CREATE TABLE export_demo (id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY, "
                                          "name VARCHAR(60) NOT NULL, score DECIMAL(6,2) NULL) ENGINE=InnoDB")), qPrintable(c.lastErrorMessage()));
        QVERIFY(c.execute(QStringLiteral("INSERT INTO export_demo (name, score) VALUES ('Ana', 1.50), ('Bruno', NULL), ('Carla', 3.00)")));
    }

    static void clickToolButton(QWidget *parent, const QString &text)
    {
        for (QToolButton *button : parent->findChildren<QToolButton *>()) {
            if (button->text() == text) { button->click(); return; }
        }
        QFAIL(qPrintable(QStringLiteral("no tool button labelled %1").arg(text)));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        if (qEnvironmentVariableIntValue("SA_TEST_MYSQL") != 1) QSKIP("Set SA_TEST_MYSQL=1 to run the UI tests against a live server.");
        m_options.host = env("SA_TEST_MYSQL_HOST", QStringLiteral("127.0.0.1"));
        m_options.port = env("SA_TEST_MYSQL_PORT", QStringLiteral("3306")).toUInt();
        m_options.user = env("SA_TEST_MYSQL_USER", QStringLiteral("root"));
        m_options.password = env("SA_TEST_MYSQL_PASSWORD", QString());
        m_db = QStringLiteral("sa_ui_%1").arg(QCoreApplication::applicationPid());

        SAMySQLConnection c;
        c.setOptions(m_options);
        QVERIFY2(c.connect(), qPrintable(c.lastErrorMessage()));
        QVERIFY(c.execute(QStringLiteral("CREATE DATABASE %1 CHARACTER SET utf8mb4").arg(SAMySQLConnection::quoteIdentifier(m_db))));
        QVERIFY(c.selectDatabase(m_db));
        QVERIFY2(c.execute(QStringLiteral("CREATE TABLE people (id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY, name VARCHAR(60) NOT NULL, "
                                          "score DECIMAL(6,2) NULL, notes TEXT NULL, active BIT(1) NOT NULL DEFAULT b'1') ENGINE=InnoDB")), qPrintable(c.lastErrorMessage()));
        QVERIFY(c.execute(QStringLiteral("INSERT INTO people (name, score, notes) VALUES ('Ana', 1.5, NULL), ('Bruno', NULL, 'nota'), ('Carla', 3, 'x')")));
        QVERIFY(c.execute(QStringLiteral("CREATE TABLE no_pk (a INT, b VARCHAR(10))")));
        QVERIFY(c.execute(QStringLiteral("INSERT INTO no_pk VALUES (1, 'one'), (2, 'two'), (2, 'two')")));

        SAPreferences &prefs = SAPreferences::instance();
        prefs.set(SAPreferences::ShowWarningBeforeDeleteQuery, false);
        prefs.set(SAPreferences::ShowWarningBeforeExecuteQuery, false);
        prefs.set(SAPreferences::ShowNoAffectedRowsError, false);
        prefs.set(SAPreferences::PasswordStorage, QStringLiteral("none"));
        prefs.set(SAPreferences::SelectLastFavoriteUsed, false);

        m_store = new SAFavoritesStore(this);
        m_document = new SADatabaseDocument(m_store);
        m_document->resize(1200, 800);
        m_document->show();
        SAConnectionInfo info;
        info.host = m_options.host;
        info.port = QString::number(m_options.port);
        info.user = m_options.user;
        info.password = m_options.password;
        info.database = m_db;
        m_document->connectWithInfo(info);
        QTRY_VERIFY_WITH_TIMEOUT(m_document->isConnected(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(m_document->tableNames().contains(QStringLiteral("people")), 15000);
        QCOMPARE(m_document->currentDatabase(), m_db);
    }

    void cleanupTestCase()
    {
        if (m_document) {
            m_document->disconnectFromServer();
            delete m_document;
            m_document = nullptr;
        }
        SAMySQLConnection c;
        c.setOptions(m_options);
        if (c.connect()) c.execute(QStringLiteral("DROP DATABASE IF EXISTS %1").arg(SAMySQLConnection::quoteIdentifier(m_db)));
    }

    void tablesListAndContentLoad()
    {
        m_document->selectTable(QStringLiteral("people"));
        m_document->showView(1);
        QCOMPARE(m_document->selectedTable(), QStringLiteral("people"));
        SATableContentView *content = m_document->contentView();
        QTRY_COMPARE_WITH_TIMEOUT(content->model()->rowCount(), 3, 15000);
        QCOMPARE(content->model()->columnCount(), 5);
        QCOMPARE(content->model()->headerData(1, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("name"));
        QVERIFY(content->lastUsedQuery().startsWith(QLatin1String("SELECT `id`, `name`, `score`, `notes`, `active` FROM `people`")));
        QVERIFY(content->lastUsedQuery().contains(QLatin1String("LIMIT 0,")));
        // NULL rendering uses the preference string and the grey role.
        const QModelIndex nullCell = content->model()->index(1, 2);
        QCOMPARE(content->model()->data(nullCell, Qt::DisplayRole).toString(), QStringLiteral("NULL"));
        QVERIFY(content->model()->data(nullCell, SAResultModel::IsNullRole).toBool());
        // BIT(1) renders as a bit string.
        QCOMPARE(content->model()->data(content->model()->index(0, 4), Qt::DisplayRole).toString(), QStringLiteral("1"));
    }

    void editCellWritesUpdate()
    {
        SATableContentView *content = m_document->contentView();
        SAResultModel *model = content->model();
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 15000);
        int row = -1;
        for (int r = 0; r < model->rowCount(); ++r)
            if (model->data(model->index(r, 1)).toString() == QLatin1String("Ana")) row = r;
        QVERIFY(row >= 0);
        const QModelIndex nameIndex = model->index(row, 1);
        content->tableView()->setCurrentIndex(nameIndex);
        QVERIFY(model->setData(nameIndex, QStringLiteral("Ana Souza"), Qt::EditRole));
        QVERIFY(content->hasPendingEdits());
        // Also set the NULL marker on the score column of the same row.
        QVERIFY(model->setData(model->index(row, 2), QStringLiteral("NULL"), Qt::EditRole));
        QVERIFY(content->commitPendingEdits());
        // The commit is asynchronous: wait for the reload to bring the new value back.
        QTRY_VERIFY_WITH_TIMEOUT(!content->hasPendingEdits() && model->rowCount() == 3
                                     && model->data(model->index(row, 1)).toString() == QLatin1String("Ana Souza"), 15000);
        QScopedPointer<SAMySQLConnection> c(directConnection());
        QVERIFY(c);
        SAResult r = c->query(QStringLiteral("SELECT name, score FROM people WHERE id = 1"));
        QCOMPARE(r.stringAt(0, 0), QStringLiteral("Ana Souza"));
        QVERIFY(r.isNull(0, 1));
    }

    void addAndDeleteRow()
    {
        SATableContentView *content = m_document->contentView();
        SAResultModel *model = content->model();
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 15000);
        content->addRow();
        QCOMPARE(model->rowCount(), 4);
        QVERIFY(content->hasPendingEdits());
        const int row = 3;
        QVERIFY(model->setData(model->index(row, 1), QStringLiteral("Dário"), Qt::EditRole));
        QVERIFY(model->setData(model->index(row, 2), QStringLiteral("9.75"), Qt::EditRole));
        QVERIFY(content->commitPendingEdits());
        QTRY_VERIFY2_WITH_TIMEOUT(!content->hasPendingEdits() && model->rowCount() == 4 && content->model()->data(model->index(3, 1)).toString() == QStringLiteral("Dário"),
                                  qPrintable(QStringLiteral("pending=%1 rows=%2 last=%3").arg(content->hasPendingEdits()).arg(model->rowCount()).arg(model->data(model->index(model->rowCount() - 1, 1)).toString())), 15000);
        {
            QScopedPointer<SAMySQLConnection> c(directConnection());
            QVERIFY(c);
            QCOMPARE(c->queryScalar(QStringLiteral("SELECT COUNT(*) FROM people")).toInt(), 4);
            QCOMPARE(c->queryScalar(QStringLiteral("SELECT score FROM people WHERE name = 'Dário'")), QStringLiteral("9.75"));
        }
        // Delete the new row through the view (warning dialog disabled by preference).
        int newRow = -1;
        for (int r = 0; r < model->rowCount(); ++r)
            if (model->data(model->index(r, 1)).toString() == QStringLiteral("Dário")) newRow = r;
        QVERIFY(newRow >= 0);
        content->tableView()->selectRow(newRow);
        content->deleteRows();
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 15000);
        QScopedPointer<SAMySQLConnection> c(directConnection());
        QVERIFY(c);
        QCOMPARE(c->queryScalar(QStringLiteral("SELECT COUNT(*) FROM people")).toInt(), 3);
    }

    void tableWithoutPrimaryKeyUsesAllColumns()
    {
        m_document->selectTable(QStringLiteral("no_pk"));
        SATableContentView *content = m_document->contentView();
        SAResultModel *model = content->model();
        QTRY_VERIFY_WITH_TIMEOUT(model->rowCount() == 3 && model->columnCount() == 2, 15000);
        // Edit the first row (1, 'one') -> (1, 'uno'); WHERE must use both columns and LIMIT 1.
        const QModelIndex idx = model->index(0, 1);
        content->tableView()->setCurrentIndex(idx);
        QVERIFY(model->setData(idx, QStringLiteral("uno"), Qt::EditRole));
        QVERIFY(content->commitPendingEdits());
        QTRY_VERIFY_WITH_TIMEOUT(!content->hasPendingEdits(), 15000);
        QScopedPointer<SAMySQLConnection> c(directConnection());
        QVERIFY(c);
        QCOMPARE(c->queryScalar(QStringLiteral("SELECT b FROM no_pk WHERE a = 1")), QStringLiteral("uno"));
        // Deleting one of two identical rows removes exactly one.
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 15000);
        int dupRow = -1;
        for (int r = 0; r < model->rowCount(); ++r)
            if (model->data(model->index(r, 1)).toString() == QLatin1String("two")) { dupRow = r; break; }
        QVERIFY(dupRow >= 0);
        content->tableView()->selectRow(dupRow);
        content->deleteRows();
        QTRY_VERIFY_WITH_TIMEOUT(c->queryScalar(QStringLiteral("SELECT COUNT(*) FROM no_pk WHERE a = 2")).toInt() == 1, 15000);
    }

    void structureAddField()
    {
        m_document->selectTable(QStringLiteral("people"));
        m_document->showView(0);
        SATableStructureView *structure = m_document->structureView();
        QTRY_COMPARE_WITH_TIMEOUT(structure->columns().size(), 5, 15000);
        structure->addField();
        QTableWidget *fields = structure->fieldsTable();
        QCOMPARE(fields->rowCount(), 6);
        fields->item(5, 0)->setText(QStringLiteral("email"));
        fields->item(5, 1)->setText(QStringLiteral("VARCHAR"));
        fields->item(5, 2)->setText(QStringLiteral("190"));
        fields->item(5, 12)->setText(QStringLiteral("Contact"));
        QVERIFY(structure->commitPendingEdit());
        QTRY_COMPARE_WITH_TIMEOUT(structure->columns().size(), 6, 15000);
        QCOMPARE(structure->columns().last().name, QStringLiteral("email"));
        QCOMPARE(structure->columns().last().length, QStringLiteral("190"));
        QCOMPARE(structure->columns().last().comment, QStringLiteral("Contact"));
        QVERIFY(structure->columns().last().nullable);
    }

    void structureToggleNullOnAnotherRowAfterRevertedEdit()
    {
        m_document->selectTable(QStringLiteral("people"));
        m_document->showView(0);
        SATableStructureView *structure = m_document->structureView();
        QTRY_VERIFY_WITH_TIMEOUT(structure->columns().size() >= 5, 15000);
        QTableWidget *fields = structure->fieldsTable();
        const int nullCol = 6;
        // Toggle Allow Null twice on one row: the row is marked as being edited
        // but its definition equals the original.
        fields->setCurrentCell(1, nullCol);
        QTableWidgetItem *first = fields->item(1, nullCol);
        const Qt::CheckState originalFirst = first->checkState();
        first->setCheckState(first->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        first->setCheckState(first->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        // Toggle Allow Null on another row without the current cell having
        // moved through that row first.
        QTableWidgetItem *second = fields->item(2, nullCol);
        const Qt::CheckState toggled = second->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked;
        second->setCheckState(toggled);
        QTest::qWait(200);
        // The reverted row was cancelled and the table rebuilt; the click on
        // the other row survives and that row is now the one being edited.
        QVERIFY(fields->rowCount() >= 5);
        QCOMPARE(fields->item(2, nullCol)->checkState(), toggled);
        QCOMPARE(fields->item(1, nullCol)->checkState(), originalFirst);
    }

    void queryViewRunsStatements()
    {
        m_document->showView(5);
        SACustomQueryView *query = m_document->queryView();
        query->setQueryText(QStringLiteral("SELECT 1 AS first;\nSELECT COUNT(*) AS n FROM people;"));
        query->runAll();
        QTRY_VERIFY_WITH_TIMEOUT(!query->isRunning(), 15000);
        QCOMPARE(query->model()->columnCount(), 1);
        QCOMPARE(query->model()->headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("n"));
        QCOMPARE(query->model()->data(query->model()->index(0, 0)).toString(), QStringLiteral("3"));
        QVERIFY(query->statusText().startsWith(QLatin1String("No errors")));
        QVERIFY(query->errorText().isEmpty());

        query->setQueryText(QStringLiteral("SELECT * FROM does_not_exist"));
        query->runAll();
        QTRY_VERIFY_WITH_TIMEOUT(!query->isRunning(), 15000);
        QVERIFY(query->statusText().startsWith(QLatin1String("Errors")));
        QVERIFY(query->errorText().contains(QLatin1String("does_not_exist")));

        // Schema changes made in the editor refresh the tables list.
        query->setQueryText(QStringLiteral("CREATE TABLE made_in_editor (id INT PRIMARY KEY)"));
        query->runAll();
        QTRY_VERIFY_WITH_TIMEOUT(!query->isRunning(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(m_document->tableNames().contains(QStringLiteral("made_in_editor")), 15000);
    }

    void queryViewExplainsCurrentQuery()
    {
        m_document->showView(5);
        SACustomQueryView *query = m_document->queryView();

        // A plain SELECT is explained: the grid holds the plan, whose columns
        // come from EXPLAIN and not from the table.
        query->setQueryText(QStringLiteral("SELECT * FROM people WHERE score IS NOT NULL"));
        query->explainCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!query->isRunning(), 15000);
        QStringList planColumns;
        for (int c = 0; c < query->model()->columnCount(); ++c)
            planColumns << query->model()->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
        QVERIFY2(planColumns.contains(QStringLiteral("select_type")), qPrintable(planColumns.join(QLatin1Char(','))));
        QVERIFY(planColumns.contains(QStringLiteral("table")));
        QVERIFY(query->errorText().isEmpty());

        // A non-SELECT is refused before anything reaches the server, and the
        // reason lands in both the status line and the error box.
        const int planColumnCount = query->model()->columnCount();
        query->setQueryText(QStringLiteral("UPDATE people SET name = name"));
        query->explainCurrent();
        QVERIFY(!query->isRunning());
        QCOMPARE(query->model()->columnCount(), planColumnCount);
        QVERIFY(query->statusText().contains(QLatin1String("only supported for a single SELECT")));
        QVERIFY(query->errorText().contains(QLatin1String("only supported for a single SELECT")));

        // Two statements in the selection are refused; a trailing comment is
        // not a statement, so that one is explained.
        query->setQueryText(QStringLiteral("SELECT 1; SELECT 2"));
        query->editor()->selectAll();
        query->explainCurrent();
        QVERIFY(!query->isRunning());
        QVERIFY(query->statusText().contains(QLatin1String("only supported for a single SELECT")));

        query->setQueryText(QStringLiteral("SELECT 1; -- trailing\n"));
        query->editor()->selectAll();
        query->explainCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!query->isRunning(), 15000);
        QVERIFY2(query->statusText().startsWith(QLatin1String("No errors")), qPrintable(query->statusText()));

        // An empty editor says so instead of failing silently.
        query->setQueryText(QString());
        query->explainCurrent();
        QVERIFY(!query->isRunning());
        QVERIFY(query->statusText().contains(QLatin1String("No query at the cursor position")));
    }

    void exportsQueryResultToCsvAndSql()
    {
        createExportFixture();
        m_document->showView(5);
        SACustomQueryView *query = m_document->queryView();
        query->setQueryText(QStringLiteral("SELECT name, score FROM export_demo"));
        query->runAll();
        QTRY_VERIFY_WITH_TIMEOUT(!query->isRunning(), 15000);
        QCOMPARE(query->model()->rowCount(), 3);
        // The model sorts SAResult in place, so the file must follow the grid.
        query->model()->sort(0, Qt::DescendingOrder);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString csvPath = dir.filePath(QStringLiteral("result.csv"));
        {
            SAExportDialog dialog(m_document, {}, SAExportDialog::QueryResult);
            dialog.setOutputPath(csvPath);
            dialog.startExport();
            QTRY_VERIFY_WITH_TIMEOUT(!dialog.isRunning(), 15000);
        }
        QFile csv(csvPath);
        QVERIFY2(csv.open(QIODevice::ReadOnly), qPrintable(csvPath));
        const QStringList lines = QString::fromUtf8(csv.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 4);
        QCOMPARE(lines.at(0), QStringLiteral("\"name\",\"score\""));
        QCOMPARE(lines.at(1), QStringLiteral("\"Carla\",3.00"));
        QCOMPARE(lines.at(2), QStringLiteral("\"Bruno\",NULL"));
        QCOMPARE(lines.at(3), QStringLiteral("\"Ana\",1.50"));

        // SQL uses the table every field came from, without asking, and cannot
        // offer a CREATE statement for a result set.
        const QString sqlPath = dir.filePath(QStringLiteral("result.sql"));
        {
            SAExportDialog dialog(m_document, {}, SAExportDialog::QueryResult);
            dialog.setOutputPath(sqlPath);
            bool pickedSql = false;
            for (QComboBox *combo : dialog.findChildren<QComboBox *>())
                for (int i = 0; i < combo->count(); ++i)
                    if (combo->itemText(i).contains(QLatin1String("SQL dump"))) { combo->setCurrentIndex(i); pickedSql = true; }
            QVERIFY(pickedSql);
            dialog.startExport();
            QTRY_VERIFY_WITH_TIMEOUT(!dialog.isRunning(), 15000);
        }
        QFile sql(sqlPath);
        QVERIFY(sql.open(QIODevice::ReadOnly));
        const QString dump = QString::fromUtf8(sql.readAll());
        QVERIFY2(dump.contains(QStringLiteral("INSERT INTO `export_demo`")), qPrintable(dump.left(400)));
        QVERIFY(dump.contains(QStringLiteral("`name`")));
        QVERIFY(!dump.contains(QStringLiteral("CREATE TABLE")));
    }

    void exportsEveryFilteredRowNotJustTheLoadedPage()
    {
        // The decisive difference between re-running the SELECT and dumping the
        // grid: with one row per page and a filter matching two, the file must
        // still hold both.
        SAPreferences &prefs = SAPreferences::instance();
        const PreferenceGuard guard({SAPreferences::LimitResults, SAPreferences::LimitResultsValue});
        prefs.set(SAPreferences::LimitResults, true);
        prefs.set(SAPreferences::LimitResultsValue, 1);

        createExportFixture();
        m_document->refreshTables();
        QTRY_VERIFY_WITH_TIMEOUT(m_document->tableNames().contains(QStringLiteral("export_demo")), 15000);
        m_document->selectTable(QStringLiteral("export_demo"));
        m_document->showView(1);
        SATableContentView *content = m_document->contentView();
        QTRY_VERIFY_WITH_TIMEOUT(!content->isLoading() && content->model()->rowCount() == 1, 15000);

        content->findChild<QCheckBox *>()->setChecked(true);   // Custom WHERE
        QLineEdit *where = nullptr;
        for (QLineEdit *edit : content->findChildren<QLineEdit *>())
            if (edit->placeholderText().contains(QLatin1String("WHERE clause"))) where = edit;
        QVERIFY(where);
        where->setText(QStringLiteral("score IS NOT NULL"));
        clickToolButton(content, QStringLiteral("Filter"));
        QTRY_VERIFY_WITH_TIMEOUT(!content->isLoading(), 15000);
        QCOMPARE(content->activeFilter(), QStringLiteral("score IS NOT NULL"));
        QCOMPARE(content->model()->rowCount(), 1);   // still one row per page

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString allRows = dir.filePath(QStringLiteral("filtered.csv"));
        {
            SAExportDialog dialog(m_document, {}, SAExportDialog::FilteredContent);
            dialog.setOutputPath(allRows);
            dialog.startExport();
            QTRY_VERIFY_WITH_TIMEOUT(!dialog.isRunning(), 15000);
            QVERIFY2(!dialog.statusMessage().contains(QLatin1String("Error")), qPrintable(dialog.statusMessage()));
        }
        QFile file(allRows);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY2(lines.size() == 3, qPrintable(QStringLiteral("expected header + 2 rows, got: %1").arg(lines.join(QLatin1Char('/')))));

        // The escape hatch exports what the page shows instead.
        const QString pageOnly = dir.filePath(QStringLiteral("page.csv"));
        {
            SAExportDialog dialog(m_document, {}, SAExportDialog::FilteredContent);
            dialog.setOutputPath(pageOnly);
            for (QCheckBox *box : dialog.findChildren<QCheckBox *>())
                if (box->text().contains(QLatin1String("currently loaded"))) box->setChecked(true);
            dialog.startExport();
            QTRY_VERIFY_WITH_TIMEOUT(!dialog.isRunning(), 15000);
        }
        QFile paged(pageOnly);
        QVERIFY(paged.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(paged.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts).size(), 2);

        content->findChild<QCheckBox *>()->setChecked(false);
        clickToolButton(content, QStringLiteral("Reset"));
        QTRY_VERIFY_WITH_TIMEOUT(!content->isLoading(), 15000);
    }

    void queryViewSortsByClickingAColumnHeader()
    {
        // Clicking a header sorts the already-loaded result set locally
        // (the query view never re-issues the SQL for this, unlike the
        // paginated Content view). Uses its own table with known, fixed
        // values instead of "people", which earlier tests mutate.
        {
            QScopedPointer<SAMySQLConnection> c(directConnection());
            QVERIFY(c);
            QVERIFY(c->execute(QStringLiteral("DROP TABLE IF EXISTS sort_demo")));
            QVERIFY(c->execute(QStringLiteral("CREATE TABLE sort_demo (name VARCHAR(20), score DECIMAL(6,2))")));
            QVERIFY(c->execute(QStringLiteral("INSERT INTO sort_demo VALUES ('Ana', 1.5), ('Bruno', NULL), ('Carla', 3.00)")));
        }
        m_document->showView(5);
        SACustomQueryView *query = m_document->queryView();
        query->setQueryText(QStringLiteral("SELECT name, score FROM sort_demo"));
        query->runAll();
        QTRY_VERIFY_WITH_TIMEOUT(!query->isRunning(), 15000);
        SAResultModel *model = query->model();
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 15000);

        auto columnValues = [&](int column) {
            QStringList values;
            for (int r = 0; r < model->rowCount(); ++r) values << model->data(model->index(r, column)).toString();
            return values;
        };

        // Ascending on the numeric "score" column: the NULL row (Bruno) sorts first.
        model->sort(1, Qt::AscendingOrder);
        QCOMPARE(columnValues(1), QStringList({"NULL", "1.50", "3.00"}));
        QCOMPARE(columnValues(0), QStringList({"Bruno", "Ana", "Carla"}));
        QCOMPARE(model->sortColumn(), 1);
        QCOMPARE(model->sortOrder(), Qt::AscendingOrder);

        // Descending puts the same NULL last instead.
        model->sort(1, Qt::DescendingOrder);
        QCOMPARE(columnValues(1), QStringList({"3.00", "1.50", "NULL"}));

        // Sorting by name (text) is independent of the numeric column's sort.
        model->sort(0, Qt::AscendingOrder);
        QCOMPARE(columnValues(0), QStringList({"Ana", "Bruno", "Carla"}));

        // A fresh query result starts with no sort indicator of its own.
        query->setQueryText(QStringLiteral("SELECT name, score FROM people"));
        query->runAll();
        QTRY_VERIFY_WITH_TIMEOUT(!query->isRunning(), 15000);
        QCOMPARE(query->model()->sortColumn(), -1);
    }
};

QTEST_MAIN(UITests)
#include "tst_ui.moc"
