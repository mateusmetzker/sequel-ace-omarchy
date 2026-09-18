//
//  main.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAConsoleWindow.h"
#include "SADatabaseDocument.h"
#include "SAEditorTheme.h"
#include "SAMainWindow.h"
#include "SAOmarchyTheme.h"
#include "SAPreferences.h"
#include "SASSHTunnel.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QStyleFactory>
#include <QTimer>

namespace {

// Paints the whole application in the colours of the active Omarchy desktop
// theme instead of the generic default Qt/Fusion look. Re-applied live when
// the user switches themes (see SAOmarchyThemeWatcher).
void applyDesktopTheme()
{
    const SAOmarchyPalette theme = SAOmarchyTheme::current();
    qApp->setPalette(SAOmarchyTheme::buildQPalette(theme));
    qApp->setStyleSheet(SAOmarchyTheme::buildStyleSheet(theme));
    // The SQL editor theme preference tracks the desktop theme when unset;
    // nudge every open editor to re-read it now that it resolves differently.
    if (SAPreferences::instance().stringFor(SAPreferences::EditorTheme).isEmpty())
        SAPreferences::instance().notifyExternalChange(SAPreferences::EditorTheme);
}

// Hidden automation used by the test suite: connects with the SA_TEST_MYSQL_*
// environment, walks through every view and writes screenshots to a folder.
void runSmokeTest(SAMainWindow *window, const QString &outputDir)
{
    QDir().mkpath(outputDir);
    auto grab = [window, outputDir](const QString &name) {
        window->grab().save(QStringLiteral("%1/%2.png").arg(outputDir, name));
    };
    auto env = [](const char *name, const QString &fallback) {
        const QString v = qEnvironmentVariable(name);
        return v.isEmpty() ? fallback : v;
    };
    SADatabaseDocument *document = window->currentDocument();
    SAConnectionInfo info;
    info.name = QStringLiteral("Smoke test");
    info.host = env("SA_TEST_MYSQL_HOST", QStringLiteral("127.0.0.1"));
    info.port = env("SA_TEST_MYSQL_PORT", QStringLiteral("3306"));
    info.user = env("SA_TEST_MYSQL_USER", QStringLiteral("root"));
    info.password = env("SA_TEST_MYSQL_PASSWORD", QString());
    info.database = env("SA_TEST_MYSQL_DATABASE", QString());
    info.colorIndex = 4;
    const QString table = env("SA_TEST_MYSQL_TABLE", QString());

    struct Step { int delayMs; std::function<void()> action; };
    auto steps = std::make_shared<QVector<Step>>();
    steps->append({300, [=]() { grab(QStringLiteral("01-connection")); document->connectWithInfo(info); }});
    steps->append({2500, [=]() { if (!table.isEmpty()) document->selectTable(table); }});
    steps->append({2000, [=]() { document->showView(1); }});
    steps->append({1500, [=]() { grab(QStringLiteral("02-content")); document->showView(0); }});
    steps->append({1500, [=]() { grab(QStringLiteral("03-structure")); document->showView(2); }});
    steps->append({1200, [=]() { grab(QStringLiteral("04-relations")); document->showView(3); }});
    steps->append({1200, [=]() { grab(QStringLiteral("05-triggers")); document->showView(4); }});
    steps->append({1200, [=]() { grab(QStringLiteral("06-info")); document->runQueryInEditor(QStringLiteral("SELECT c.id, c.name, c.status, c.balance, COUNT(o.id) AS orders\nFROM customers c\nLEFT JOIN orders o ON o.customer_id = c.id\nGROUP BY c.id\nORDER BY orders DESC;"), true); }});
    steps->append({2000, [=]() { grab(QStringLiteral("07-query")); SAConsoleWindow::shared()->show(); SAConsoleWindow::shared()->resize(900, 360); }});
    steps->append({800, [=]() { SAConsoleWindow::shared()->grab().save(QStringLiteral("%1/08-console.png").arg(outputDir)); }});
    steps->append({300, [=]() { SAConsoleWindow::shared()->hide(); document->explainCurrentQuery(); }});
    steps->append({1500, [=]() { grab(QStringLiteral("09-explain")); }});
    steps->append({500, [=]() { document->disconnectFromServer(); QCoreApplication::exit(0); }});

    auto runNext = std::make_shared<std::function<void(int)>>();
    *runNext = [steps, runNext](int index) {
        if (index >= steps->size()) return;
        QTimer::singleShot(steps->at(index).delayMs, [steps, runNext, index]() {
            steps->at(index).action();
            (*runNext)(index + 1);
        });
    };
    (*runNext)(0);
}

} // namespace

int main(int argc, char *argv[])
{
    // OpenSSH invokes this binary as SSH_ASKPASS while a tunnel is being opened.
    if (SASSHAskpass::isAskpassInvocation(argc, argv)) {
        QCoreApplication app(argc, argv);
        return SASSHAskpass::run(app.arguments());
    }

    QCoreApplication::setOrganizationName(QStringLiteral("sequel-ace"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("sequel-ace.com"));
    QCoreApplication::setApplicationName(QStringLiteral("sequel-ace"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SA_VERSION_STRING));

    // A fixed base style keeps the desktop-theme stylesheet's colours and
    // metrics predictable across distributions, instead of inheriting
    // whatever native-looking style the platform theme plugin picks.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication app(argc, argv);
    QApplication::setApplicationDisplayName(QStringLiteral("Sequel Ace"));
    QApplication::setDesktopFileName(QStringLiteral("com.sequel-ace.SequelAce"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/packaging/icons/sequel-ace-256.png")));

    applyDesktopTheme();
    auto *themeWatcher = new SAOmarchyThemeWatcher(&app);
    QObject::connect(themeWatcher, &SAOmarchyThemeWatcher::themeChanged, &app, &applyDesktopTheme);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Sequel Ace - MySQL and MariaDB database management"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("A .spf connection file or .sql script to open."));
    QCommandLineOption smokeTest(QStringLiteral("smoke-test"), QStringLiteral("Connect using SA_TEST_MYSQL_* and write screenshots of every view to <dir>."), QStringLiteral("dir"));
    smokeTest.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(smokeTest);
    parser.process(app);

    SAMainWindow window;
    window.show();
    for (const QString &path : parser.positionalArguments()) window.openPath(path);
    if (parser.isSet(smokeTest)) runSmokeTest(&window, parser.value(smokeTest));
    return app.exec();
}
