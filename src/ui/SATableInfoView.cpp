//
//  SATableInfoView.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SATableInfoView.h"
#include "SADatabaseDocument.h"
#include "SADialogs.h"
#include "SASQLEditor.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

SATableInfoView::SATableInfoView(SADatabaseDocument *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *splitter = new QSplitter(Qt::Vertical);

    auto *top = new QWidget;
    auto *topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);

    m_optionsGroup = new QGroupBox(tr("Table options"));
    auto *form = new QFormLayout(m_optionsGroup);
    m_engine = new QComboBox;
    m_encoding = new QComboBox;
    m_collation = new QComboBox;
    m_comment = new QLineEdit;
    m_autoIncrement = new QLineEdit;
    form->addRow(tr("Engine:"), m_engine);
    form->addRow(tr("Encoding:"), m_encoding);
    form->addRow(tr("Collation:"), m_collation);
    form->addRow(tr("Comment:"), m_comment);
    form->addRow(tr("Auto increment:"), m_autoIncrement);
    connect(m_engine, &QComboBox::activated, this, [this](int) { applyOption(QStringLiteral("ENGINE"), m_engine->currentText(), false); });
    connect(m_encoding, &QComboBox::activated, this, [this](int) {
        const QString cs = m_encoding->currentText();
        applyOption(QStringLiteral("CHARACTER SET"), cs + QStringLiteral(" COLLATE ") + m_document->charsetInfo().defaultCollation.value(cs), false);
    });
    connect(m_collation, &QComboBox::activated, this, [this](int) { applyOption(QStringLiteral("COLLATE"), m_collation->currentText(), false); });
    connect(m_comment, &QLineEdit::editingFinished, this, [this]() {
        if (!m_populating && m_comment->text() != m_statusValues.value(QStringLiteral("Comment"))) applyOption(QStringLiteral("COMMENT"), m_comment->text(), true);
    });
    connect(m_autoIncrement, &QLineEdit::editingFinished, this, [this]() {
        if (!m_populating && m_autoIncrement->text().trimmed() != m_statusValues.value(QStringLiteral("Auto_increment")) && !m_autoIncrement->text().trimmed().isEmpty())
            applyOption(QStringLiteral("AUTO_INCREMENT"), m_autoIncrement->text().trimmed(), false);
    });
    topLayout->addWidget(m_optionsGroup, 1);

    auto *statusGroup = new QGroupBox(tr("Status"));
    auto *statusLayout = new QVBoxLayout(statusGroup);
    m_status = new QLabel;
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_status->setWordWrap(true);
    statusLayout->addWidget(m_status);
    topLayout->addWidget(statusGroup, 1);
    splitter->addWidget(top);

    auto *bottom = new QGroupBox(tr("Create syntax"));
    auto *bottomLayout = new QVBoxLayout(bottom);
    m_createSyntax = new SASQLEditor;
    m_createSyntax->setReadOnly(true);
    m_createSyntax->setHighlightCurrentQuery(false);
    bottomLayout->addWidget(m_createSyntax, 1);
    auto *copyRow = new QHBoxLayout;
    copyRow->addStretch();
    auto *copy = new QPushButton(tr("Copy"));
    connect(copy, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(m_createSyntax->toPlainText()); });
    copyRow->addWidget(copy);
    bottomLayout->addLayout(copyRow);
    splitter->addWidget(bottom);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter);
}

void SATableInfoView::clear()
{
    m_tableName.clear();
    m_statusValues.clear();
    m_status->clear();
    m_createSyntax->setPlainText(QString());
    m_optionsGroup->setEnabled(false);
}

void SATableInfoView::loadTable(const QString &name, SASchema::ObjectType type)
{
    m_tableName = name;
    m_type = type;
    const bool isTable = type == SASchema::ObjectType::Table;
    m_optionsGroup->setEnabled(isTable);
    QStringList statements{SASchema::showCreate(type, name)};
    if (isTable || type == SASchema::ObjectType::View) statements << SASchema::tableStatusLike(name, m_document->escaper());
    else statements << SASchema::routineDefinition(type, m_document->currentDatabase(), name, m_document->escaper());
    m_document->session()->queryBatch(statements, [this, name, isTable](const QVector<SAResult> &results) {
        if (name != m_tableName || results.size() < 2) return;
        if (results[0].ok && results[0].rowCount()) {
            int col = 1;
            for (int i = 0; i < results[0].fieldCount(); ++i)
                if (results[0].fields.at(i).name.startsWith(QLatin1String("Create "))) col = i;
            m_createSyntax->setPlainText(results[0].stringAt(0, col) + QStringLiteral(";"));
        } else {
            m_createSyntax->setPlainText(results[0].ok ? QString() : tr("-- %1").arg(results[0].errorMessage));
        }
        m_statusValues = results[1].ok && results[1].rowCount() ? results[1].rowAsMap(0) : QMap<QString, QString>();
        populateStatus(m_statusValues);
        if (isTable) {
            m_populating = true;
            m_engine->clear();
            m_engine->addItems(m_document->engines());
            m_engine->setCurrentText(m_statusValues.value(QStringLiteral("Engine")));
            m_encoding->clear();
            m_encoding->addItems(m_document->charsetInfo().charsets);
            const QString collation = m_statusValues.value(QStringLiteral("Collation"));
            const QString charset = collation.section(QLatin1Char('_'), 0, 0);
            m_encoding->setCurrentText(charset);
            m_collation->clear();
            m_collation->addItems(m_document->charsetInfo().collations.value(charset));
            m_collation->setCurrentText(collation);
            m_comment->setText(m_statusValues.value(QStringLiteral("Comment")));
            m_autoIncrement->setText(m_statusValues.value(QStringLiteral("Auto_increment")));
            m_autoIncrement->setEnabled(!m_statusValues.value(QStringLiteral("Auto_increment")).isEmpty());
            m_populating = false;
        }
    }, SADatabaseSession::Silent);
}

void SATableInfoView::populateStatus(const QMap<QString, QString> &status)
{
    const QLocale locale;
    QStringList lines;
    auto add = [&](const QString &label, const QString &key, bool bytes = false, bool number = false) {
        if (!status.contains(key) || status.value(key).isEmpty()) return;
        QString value = status.value(key);
        if (bytes) value = locale.formattedDataSize(value.toLongLong());
        else if (number) value = locale.toString(value.toLongLong());
        lines << QStringLiteral("<b>%1</b> %2").arg(label, value.toHtmlEscaped());
    };
    if (m_type == SASchema::ObjectType::Procedure || m_type == SASchema::ObjectType::Function) {
        add(tr("Type:"), QStringLiteral("ROUTINE_TYPE"));
        add(tr("Returns:"), QStringLiteral("DTD_IDENTIFIER"));
        add(tr("Definer:"), QStringLiteral("DEFINER"));
        add(tr("Created:"), QStringLiteral("CREATED"));
        add(tr("Modified:"), QStringLiteral("LAST_ALTERED"));
        add(tr("Security:"), QStringLiteral("SECURITY_TYPE"));
        add(tr("Deterministic:"), QStringLiteral("IS_DETERMINISTIC"));
        add(tr("SQL data access:"), QStringLiteral("SQL_DATA_ACCESS"));
        add(tr("Comment:"), QStringLiteral("ROUTINE_COMMENT"));
    } else {
        add(tr("Engine:"), QStringLiteral("Engine"));
        add(tr("Version:"), QStringLiteral("Version"));
        add(tr("Row format:"), QStringLiteral("Row_format"));
        add(tr("Rows:"), QStringLiteral("Rows"), false, true);
        add(tr("Average row length:"), QStringLiteral("Avg_row_length"), true);
        add(tr("Data size:"), QStringLiteral("Data_length"), true);
        add(tr("Index size:"), QStringLiteral("Index_length"), true);
        add(tr("Free data:"), QStringLiteral("Data_free"), true);
        add(tr("Max data size:"), QStringLiteral("Max_data_length"), true);
        add(tr("Auto increment:"), QStringLiteral("Auto_increment"));
        add(tr("Created:"), QStringLiteral("Create_time"));
        add(tr("Updated:"), QStringLiteral("Update_time"));
        add(tr("Checked:"), QStringLiteral("Check_time"));
        add(tr("Collation:"), QStringLiteral("Collation"));
        add(tr("Checksum:"), QStringLiteral("Checksum"));
        add(tr("Create options:"), QStringLiteral("Create_options"));
        if (m_type != SASchema::ObjectType::View) add(tr("Comment:"), QStringLiteral("Comment"));
    }
    m_status->setText(lines.join(QStringLiteral("<br>")));
}

void SATableInfoView::reload()
{
    if (!m_tableName.isEmpty()) loadTable(m_tableName, m_type);
}

void SATableInfoView::applyOption(const QString &option, const QString &value, bool quote)
{
    if (m_populating || m_tableName.isEmpty()) return;
    const QString sql = SASchema::alterTableOption(m_tableName, option, value, quote, m_document->escaper());
    m_document->session()->query(sql, [this, option](const SAResult &r) {
        if (!r.ok) m_document->reportError(tr("Error"), tr("Couldn't change the table's %1.\n\nMySQL said: %2").arg(option.toLower(), r.errorMessage));
        m_document->tableStructureChanged();
        reload();
    });
}
