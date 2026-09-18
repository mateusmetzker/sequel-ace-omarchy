//
//  SAFieldEditorDialog.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAFieldEditorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

SAFieldEditorDialog::SAFieldEditorDialog(QWidget *parent, const QString &title, const QString &typeGroup, const QString &typeName,
                                         const SACell &value, bool editable, bool allowNull)
    : QDialog(parent), m_typeGroup(typeGroup), m_original(value), m_editable(editable)
{
    setWindowTitle(editable ? tr("Edit %1").arg(title) : tr("View %1").arg(title));
    m_binaryData = value.isNull ? QByteArray() : value.data;
    const bool isBinary = typeGroup == QLatin1String("blobdata") || typeGroup == QLatin1String("binary") || typeGroup == QLatin1String("geometry");
    const bool textLike = !isBinary || (!value.isNull && !value.data.contains('\0') && QString::fromUtf8(value.data).toUtf8() == value.data);

    m_tabs = new QTabWidget;
    m_text = new QPlainTextEdit;
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setReadOnly(!editable);
    m_text->setPlainText(value.isNull ? QString() : QString::fromUtf8(value.data));
    m_tabs->addTab(m_text, tr("Text"));
    m_hex = new QPlainTextEdit;
    m_hex->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_hex->setReadOnly(true);
    m_tabs->addTab(m_hex, tr("Hex"));
    m_image = new QLabel;
    m_image->setAlignment(Qt::AlignCenter);
    auto *imageScroll = new QScrollArea;
    imageScroll->setWidget(m_image);
    imageScroll->setWidgetResizable(true);
    m_tabs->addTab(imageScroll, tr("Image"));
    updateHexView();
    updateImageView();
    if (isBinary && !textLike) m_tabs->setCurrentIndex(1);
    connect(m_text, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_tabs->currentIndex() == 0) { m_binaryData = m_text->toPlainText().toUtf8(); m_binaryEdited = false; updateHexView(); }
    });

    m_null = new QCheckBox(tr("NULL"));
    m_null->setChecked(value.isNull);
    m_null->setEnabled(editable && allowNull);
    connect(m_null, &QCheckBox::toggled, this, [this](bool on) { m_tabs->setEnabled(!on); });
    m_tabs->setEnabled(!value.isNull);

    m_info = new QLabel;
    m_info->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    const QLocale locale;
    m_info->setText(tr("%1 · %2").arg(typeName, value.isNull ? tr("NULL") : tr("%1 bytes, %2 characters").arg(locale.toString(value.data.size())).arg(locale.toString(QString::fromUtf8(value.data).size()))));

    auto *buttons = new QDialogButtonBox(editable ? (QDialogButtonBox::Save | QDialogButtonBox::Cancel) : QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *saveFile = new QPushButton(tr("Save to File…"));
    connect(saveFile, &QPushButton::clicked, this, &SAFieldEditorDialog::saveToFile);
    auto *loadFile = new QPushButton(tr("Load from File…"));
    loadFile->setEnabled(editable);
    connect(loadFile, &QPushButton::clicked, this, &SAFieldEditorDialog::loadFromFile);
    auto *json = new QPushButton(tr("Format JSON"));
    connect(json, &QPushButton::clicked, this, &SAFieldEditorDialog::formatJSON);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_null);
    bottom->addWidget(m_info, 1);
    bottom->addWidget(json);
    bottom->addWidget(loadFile);
    bottom->addWidget(saveFile);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addLayout(bottom);
    layout->addWidget(buttons);
    resize(760, 520);
}

void SAFieldEditorDialog::updateHexView()
{
    QString out;
    const QByteArray &d = m_binaryData;
    const int shown = qMin(d.size(), 64 * 1024);
    for (int offset = 0; offset < shown; offset += 16) {
        QString hex, ascii;
        for (int i = 0; i < 16; ++i) {
            if (offset + i < shown) {
                const unsigned char c = static_cast<unsigned char>(d.at(offset + i));
                hex += QStringLiteral("%1 ").arg(c, 2, 16, QLatin1Char('0'));
                ascii += (c >= 32 && c < 127) ? QChar(c) : QLatin1Char('.');
            } else {
                hex += QStringLiteral("   ");
            }
            if (i == 7) hex += QLatin1Char(' ');
        }
        out += QStringLiteral("%1  %2 |%3|\n").arg(offset, 8, 16, QLatin1Char('0')).arg(hex, ascii);
    }
    if (shown < d.size()) out += tr("… %1 more bytes").arg(d.size() - shown);
    m_hex->setPlainText(out);
}

void SAFieldEditorDialog::updateImageView()
{
    QPixmap pm;
    if (!m_binaryData.isEmpty() && pm.loadFromData(m_binaryData)) {
        m_image->setPixmap(pm);
        m_tabs->setTabEnabled(2, true);
        if (m_typeGroup == QLatin1String("blobdata")) m_tabs->setCurrentIndex(2);
    } else {
        m_image->setText(tr("Not an image"));
        m_tabs->setTabEnabled(2, false);
    }
}

void SAFieldEditorDialog::loadFromFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Field Content"), QDir::homePath());
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    m_binaryData = file.readAll();
    m_binaryEdited = true;
    m_null->setChecked(false);
    const bool textLike = !m_binaryData.contains('\0') && QString::fromUtf8(m_binaryData).toUtf8() == m_binaryData;
    {
        const QSignalBlocker blocker(m_text);
        m_text->setPlainText(textLike ? QString::fromUtf8(m_binaryData) : QString());
    }
    updateHexView();
    updateImageView();
    if (!textLike) m_tabs->setCurrentIndex(m_tabs->isTabEnabled(2) ? 2 : 1);
}

void SAFieldEditorDialog::saveToFile()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Field Content"), QDir::homePath() + QStringLiteral("/field.bin"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(m_binaryEdited || m_tabs->currentIndex() != 0 ? m_binaryData : m_text->toPlainText().toUtf8());
}

void SAFieldEditorDialog::formatJSON()
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(m_text->toPlainText().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        m_info->setText(tr("Not valid JSON: %1").arg(error.errorString()));
        return;
    }
    m_text->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
    m_tabs->setCurrentIndex(0);
}

SACell SAFieldEditorDialog::value() const
{
    if (m_null->isChecked()) return SACell::null();
    if (m_binaryEdited) return SACell::of(m_binaryData);
    return SACell::ofString(m_text->toPlainText());
}
