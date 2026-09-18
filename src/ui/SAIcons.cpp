//
//  SAIcons.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAIcons.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>

namespace SAIcons {

namespace {

void draw(QPainter &p, Glyph glyph, const QRectF &r, const QColor &color)
{
    QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const qreal x = r.left(), y = r.top(), w = r.width(), h = r.height();

    switch (glyph) {
    case Glyph::Table: {
        QRectF box(x + 1, y + 2, w - 2, h - 4);
        p.drawRoundedRect(box, 1.5, 1.5);
        p.drawLine(QPointF(box.left(), y + h * 0.4), QPointF(box.right(), y + h * 0.4));
        p.drawLine(QPointF(box.left(), y + h * 0.68), QPointF(box.right(), y + h * 0.68));
        p.drawLine(QPointF(x + w * 0.42, y + h * 0.4), QPointF(x + w * 0.42, box.bottom()));
        break;
    }
    case Glyph::View: {
        QRectF box(x + 1, y + 2, w - 2, h - 4);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.drawRoundedRect(box, 1.5, 1.5);
        pen.setStyle(Qt::SolidLine);
        p.setPen(pen);
        p.setBrush(color);
        p.drawEllipse(QPointF(x + w / 2, y + h / 2), w * 0.14, h * 0.14);
        break;
    }
    case Glyph::Procedure: {
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(x + w * 0.25, y + h * 0.25);
        path.lineTo(x + w * 0.1, y + h * 0.5);
        path.lineTo(x + w * 0.25, y + h * 0.75);
        path.moveTo(x + w * 0.75, y + h * 0.25);
        path.lineTo(x + w * 0.9, y + h * 0.5);
        path.lineTo(x + w * 0.75, y + h * 0.75);
        path.moveTo(x + w * 0.58, y + h * 0.15);
        path.lineTo(x + w * 0.42, y + h * 0.85);
        p.drawPath(path);
        break;
    }
    case Glyph::Function: {
        QFont f = p.font();
        f.setItalic(true);
        f.setBold(true);
        f.setPixelSize(int(h * 0.85));
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, QStringLiteral("f"));
        break;
    }
    case Glyph::Event: {
        p.drawEllipse(r.adjusted(1.5, 1.5, -1.5, -1.5));
        p.drawLine(QPointF(x + w / 2, y + h * 0.3), QPointF(x + w / 2, y + h / 2));
        p.drawLine(QPointF(x + w / 2, y + h / 2), QPointF(x + w * 0.7, y + h * 0.62));
        break;
    }
    case Glyph::Database:
    case Glyph::SystemDatabase: {
        if (glyph == Glyph::SystemDatabase) { pen.setStyle(Qt::DotLine); p.setPen(pen); }
        const qreal ry = h * 0.14;
        p.drawEllipse(QRectF(x + 1.5, y + 1.5, w - 3, ry * 2));
        p.drawLine(QPointF(x + 1.5, y + 1.5 + ry), QPointF(x + 1.5, y + h - 1.5 - ry));
        p.drawLine(QPointF(x + w - 1.5, y + 1.5 + ry), QPointF(x + w - 1.5, y + h - 1.5 - ry));
        p.drawArc(QRectF(x + 1.5, y + h - 1.5 - 2 * ry, w - 3, 2 * ry), 180 * 16, 180 * 16);
        p.drawArc(QRectF(x + 1.5, y + h / 2 - ry, w - 3, 2 * ry), 180 * 16, 180 * 16);
        break;
    }
    case Glyph::Structure: {
        for (int i = 0; i < 3; ++i) {
            const qreal yy = y + h * (0.2 + i * 0.3);
            p.drawLine(QPointF(x + w * 0.15, yy), QPointF(x + w * 0.85, yy));
            p.setBrush(color);
            p.drawEllipse(QPointF(x + w * 0.15, yy), 1.4, 1.4);
            p.setBrush(Qt::NoBrush);
        }
        break;
    }
    case Glyph::Content: {
        QRectF box(x + 1.5, y + 2, w - 3, h - 4);
        p.drawRoundedRect(box, 1.5, 1.5);
        p.drawLine(QPointF(box.left(), y + h * 0.36), QPointF(box.right(), y + h * 0.36));
        p.drawLine(QPointF(x + w * 0.5, box.top()), QPointF(x + w * 0.5, box.bottom()));
        p.drawLine(QPointF(box.left(), y + h * 0.68), QPointF(box.right(), y + h * 0.68));
        break;
    }
    case Glyph::Relations: {
        QRectF a(x + 1, y + h * 0.15, w * 0.32, h * 0.32);
        QRectF b(x + w * 0.66, y + h * 0.55, w * 0.32, h * 0.32);
        p.drawRoundedRect(a, 1, 1);
        p.drawRoundedRect(b, 1, 1);
        p.drawLine(a.center() + QPointF(a.width() / 2, 0), QPointF(x + w * 0.5, a.center().y()));
        p.drawLine(QPointF(x + w * 0.5, a.center().y()), QPointF(x + w * 0.5, b.center().y()));
        p.drawLine(QPointF(x + w * 0.5, b.center().y()), b.center() - QPointF(b.width() / 2, 0));
        break;
    }
    case Glyph::Triggers: {
        QPainterPath bolt;
        bolt.moveTo(x + w * 0.6, y + h * 0.08);
        bolt.lineTo(x + w * 0.28, y + h * 0.55);
        bolt.lineTo(x + w * 0.5, y + h * 0.55);
        bolt.lineTo(x + w * 0.4, y + h * 0.92);
        bolt.lineTo(x + w * 0.74, y + h * 0.42);
        bolt.lineTo(x + w * 0.52, y + h * 0.42);
        bolt.closeSubpath();
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPath(bolt);
        break;
    }
    case Glyph::Info: {
        p.drawEllipse(r.adjusted(1.5, 1.5, -1.5, -1.5));
        p.drawLine(QPointF(x + w / 2, y + h * 0.45), QPointF(x + w / 2, y + h * 0.72));
        p.setBrush(color);
        p.drawEllipse(QPointF(x + w / 2, y + h * 0.3), 1.2, 1.2);
        break;
    }
    case Glyph::Query: {
        QRectF box(x + 1.5, y + 2, w - 3, h - 4);
        p.drawRoundedRect(box, 1.5, 1.5);
        p.drawLine(QPointF(x + w * 0.25, y + h * 0.4), QPointF(x + w * 0.45, y + h * 0.55));
        p.drawLine(QPointF(x + w * 0.45, y + h * 0.55), QPointF(x + w * 0.25, y + h * 0.7));
        p.drawLine(QPointF(x + w * 0.5, y + h * 0.7), QPointF(x + w * 0.75, y + h * 0.7));
        break;
    }
    case Glyph::Console: {
        QRectF box(x + 1.5, y + 2.5, w - 3, h - 5);
        p.drawRoundedRect(box, 1.5, 1.5);
        p.drawLine(QPointF(box.left(), y + h * 0.36), QPointF(box.right(), y + h * 0.36));
        p.drawLine(QPointF(x + w * 0.3, y + h * 0.55), QPointF(x + w * 0.5, y + h * 0.7));
        break;
    }
    case Glyph::Add:
        p.drawLine(QPointF(x + w / 2, y + h * 0.2), QPointF(x + w / 2, y + h * 0.8));
        p.drawLine(QPointF(x + w * 0.2, y + h / 2), QPointF(x + w * 0.8, y + h / 2));
        break;
    case Glyph::Remove:
        p.drawLine(QPointF(x + w * 0.2, y + h / 2), QPointF(x + w * 0.8, y + h / 2));
        break;
    case Glyph::Refresh: {
        QRectF arc = r.adjusted(2.5, 2.5, -2.5, -2.5);
        p.drawArc(arc, 30 * 16, 300 * 16);
        QPainterPath head;
        const QPointF tip(arc.center().x() + arc.width() / 2 * qCos(qDegreesToRadians(30.0)), arc.center().y() - arc.height() / 2 * qSin(qDegreesToRadians(30.0)));
        head.moveTo(tip + QPointF(-3, -1));
        head.lineTo(tip + QPointF(1, -3.2));
        head.lineTo(tip + QPointF(1.4, 1.6));
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPath(head);
        break;
    }
    case Glyph::Gear: {
        p.setBrush(Qt::NoBrush);
        const QPointF c = r.center();
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * M_PI / 4;
            p.drawLine(c + QPointF(qCos(a) * w * 0.3, qSin(a) * h * 0.3), c + QPointF(qCos(a) * w * 0.44, qSin(a) * h * 0.44));
        }
        p.drawEllipse(c, w * 0.3, h * 0.3);
        p.drawEllipse(c, w * 0.1, h * 0.1);
        break;
    }
    case Glyph::Duplicate: {
        p.drawRoundedRect(QRectF(x + 1.5, y + h * 0.3, w * 0.58, h * 0.6), 1, 1);
        p.drawRoundedRect(QRectF(x + w * 0.38, y + 1.5, w * 0.58, h * 0.6), 1, 1);
        break;
    }
    case Glyph::Filter: {
        QPainterPath funnel;
        funnel.moveTo(x + w * 0.1, y + h * 0.15);
        funnel.lineTo(x + w * 0.9, y + h * 0.15);
        funnel.lineTo(x + w * 0.58, y + h * 0.55);
        funnel.lineTo(x + w * 0.58, y + h * 0.9);
        funnel.lineTo(x + w * 0.42, y + h * 0.82);
        funnel.lineTo(x + w * 0.42, y + h * 0.55);
        funnel.closeSubpath();
        p.drawPath(funnel);
        break;
    }
    case Glyph::Run: {
        QPainterPath tri;
        tri.moveTo(x + w * 0.28, y + h * 0.15);
        tri.lineTo(x + w * 0.85, y + h * 0.5);
        tri.lineTo(x + w * 0.28, y + h * 0.85);
        tri.closeSubpath();
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPath(tri);
        break;
    }
    case Glyph::Stop:
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r.adjusted(w * 0.22, h * 0.22, -w * 0.22, -h * 0.22), 1.5, 1.5);
        break;
    case Glyph::Left:
        p.drawLine(QPointF(x + w * 0.62, y + h * 0.22), QPointF(x + w * 0.35, y + h * 0.5));
        p.drawLine(QPointF(x + w * 0.35, y + h * 0.5), QPointF(x + w * 0.62, y + h * 0.78));
        break;
    case Glyph::Right:
        p.drawLine(QPointF(x + w * 0.38, y + h * 0.22), QPointF(x + w * 0.65, y + h * 0.5));
        p.drawLine(QPointF(x + w * 0.65, y + h * 0.5), QPointF(x + w * 0.38, y + h * 0.78));
        break;
    case Glyph::Edit:
        p.drawLine(QPointF(x + w * 0.2, y + h * 0.8), QPointF(x + w * 0.75, y + h * 0.25));
        p.drawLine(QPointF(x + w * 0.2, y + h * 0.8), QPointF(x + w * 0.2, y + h * 0.62));
        p.drawLine(QPointF(x + w * 0.2, y + h * 0.8), QPointF(x + w * 0.38, y + h * 0.8));
        break;
    case Glyph::Key: {
        p.drawEllipse(QPointF(x + w * 0.32, y + h * 0.5), w * 0.2, h * 0.2);
        p.drawLine(QPointF(x + w * 0.52, y + h * 0.5), QPointF(x + w * 0.9, y + h * 0.5));
        p.drawLine(QPointF(x + w * 0.78, y + h * 0.5), QPointF(x + w * 0.78, y + h * 0.68));
        break;
    }
    case Glyph::Lock: {
        p.drawRoundedRect(QRectF(x + w * 0.2, y + h * 0.45, w * 0.6, h * 0.45), 1.5, 1.5);
        p.drawArc(QRectF(x + w * 0.3, y + h * 0.1, w * 0.4, h * 0.55), 0, 180 * 16);
        break;
    }
    case Glyph::Folder: {
        QPainterPath path;
        path.moveTo(x + 1.5, y + h * 0.3);
        path.lineTo(x + w * 0.38, y + h * 0.3);
        path.lineTo(x + w * 0.48, y + h * 0.42);
        path.lineTo(x + w - 1.5, y + h * 0.42);
        path.lineTo(x + w - 1.5, y + h * 0.85);
        path.lineTo(x + 1.5, y + h * 0.85);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Glyph::Connection: {
        p.drawEllipse(QPointF(x + w * 0.25, y + h * 0.5), w * 0.12, h * 0.12);
        p.drawEllipse(QPointF(x + w * 0.75, y + h * 0.5), w * 0.12, h * 0.12);
        p.drawLine(QPointF(x + w * 0.37, y + h * 0.5), QPointF(x + w * 0.63, y + h * 0.5));
        break;
    }
    case Glyph::Users: {
        p.drawEllipse(QPointF(x + w * 0.5, y + h * 0.32), w * 0.17, h * 0.17);
        p.drawArc(QRectF(x + w * 0.2, y + h * 0.55, w * 0.6, h * 0.6), 0, 180 * 16);
        break;
    }
    case Glyph::Export: {
        p.drawLine(QPointF(x + w * 0.5, y + h * 0.15), QPointF(x + w * 0.5, y + h * 0.65));
        p.drawLine(QPointF(x + w * 0.3, y + h * 0.45), QPointF(x + w * 0.5, y + h * 0.65));
        p.drawLine(QPointF(x + w * 0.7, y + h * 0.45), QPointF(x + w * 0.5, y + h * 0.65));
        p.drawLine(QPointF(x + w * 0.2, y + h * 0.85), QPointF(x + w * 0.8, y + h * 0.85));
        break;
    }
    case Glyph::Search: {
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(x + w * 0.15, y + h * 0.15, w * 0.55, h * 0.55));
        p.drawLine(QPointF(x + w * 0.62, y + h * 0.62), QPointF(x + w * 0.88, y + h * 0.88));
        break;
    }
    case Glyph::Socket: {
        p.drawRoundedRect(QRectF(x + w * 0.18, y + h * 0.4, w * 0.64, h * 0.42), 2, 2);
        p.drawLine(QPointF(x + w * 0.36, y + h * 0.4), QPointF(x + w * 0.36, y + h * 0.16));
        p.drawLine(QPointF(x + w * 0.64, y + h * 0.4), QPointF(x + w * 0.64, y + h * 0.16));
        break;
    }
    // Always rendered from a real vector asset instead (see svgResourceFor);
    // these cases exist only so the switch stays exhaustive.
    case Glyph::AddFolder:
    case Glyph::SelectAll:
    case Glyph::SelectNone:
    case Glyph::QuickConnect:
        break;
    }
}

QPixmap renderDrawn(Glyph glyph, int size, const QColor &color)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    draw(p, glyph, QRectF(0, 0, size, size), color);
    return pm;
}

// Path (in the Qt resource system) of the real macOS vector icon for glyphs
// that have one, converted once from Resources/Images/*.pdf; empty for
// glyphs drawn at runtime instead. See SAIcons.h for the rationale.
QString svgResourceFor(Glyph glyph)
{
    switch (glyph) {
    case Glyph::Add:         return QStringLiteral(":/resources/icons/add.svg");
    case Glyph::Remove:      return QStringLiteral(":/resources/icons/remove.svg");
    case Glyph::Refresh:     return QStringLiteral(":/resources/icons/refresh.svg");
    case Glyph::Edit:        return QStringLiteral(":/resources/icons/edit.svg");
    case Glyph::Gear:        return QStringLiteral(":/resources/icons/gear.svg");
    case Glyph::Key:         return QStringLiteral(":/resources/icons/key.svg");
    case Glyph::Left:        return QStringLiteral(":/resources/icons/chevron-left.svg");
    case Glyph::Right:       return QStringLiteral(":/resources/icons/chevron-right.svg");
    case Glyph::Filter:      return QStringLiteral(":/resources/icons/filter.svg");
    case Glyph::Duplicate:   return QStringLiteral(":/resources/icons/duplicate.svg");
    case Glyph::AddFolder:   return QStringLiteral(":/resources/icons/add-folder.svg");
    case Glyph::SelectAll:   return QStringLiteral(":/resources/icons/select-all.svg");
    case Glyph::SelectNone:  return QStringLiteral(":/resources/icons/select-none.svg");
    default:                 return QString();
    }
}

// Renders a template SVG (a single opaque shape on a transparent background,
// Apple's convention for tintable toolbar images) at `color`: the shape is
// rasterised once as an alpha mask, then composited under a solid fill, so
// the result tracks the desktop theme regardless of the SVG's own colours.
QPixmap renderSvgTemplate(const QString &resourcePath, int size, const QColor &color)
{
    QPixmap mask(size, size);
    mask.fill(Qt::transparent);
    {
        QSvgRenderer renderer(resourcePath);
        QPainter maskPainter(&mask);
        maskPainter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&maskPainter, QRectF(0, 0, size, size));
    }
    QPixmap tinted(size, size);
    tinted.fill(Qt::transparent);
    QPainter p(&tinted);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(tinted.rect(), color);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawPixmap(0, 0, mask);
    return tinted;
}

// The "Quick Connect" brand lightning bolt keeps its own fixed colours
// (ignoring the requested tint) and is pre-rendered to PNG at each size: the
// source asset clips a raster image through two nested SVG clip paths, a
// combination QtSvg does not render correctly (it only applies the outer
// clip), so a renderer that does handle it (rsvg-convert) produced these once.
QPixmap quickConnectPixmap(int size)
{
    static const QList<int> availableSizes = {16, 24, 32, 48};
    int chosen = availableSizes.last();
    for (int s : availableSizes) {
        if (s >= size) { chosen = s; break; }
    }
    QPixmap pm(QStringLiteral(":/resources/icons/quick-connect-%1.png").arg(chosen));
    return size == chosen ? pm : pm.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QPixmap render(Glyph glyph, int size, const QColor &color)
{
    if (glyph == Glyph::QuickConnect) return quickConnectPixmap(size);
    const QString svg = svgResourceFor(glyph);
    if (!svg.isEmpty()) return renderSvgTemplate(svg, size, color);
    return renderDrawn(glyph, size, color);
}

} // namespace

QIcon icon(Glyph glyph, const QColor &tint)
{
    static QHash<QString, QIcon> cache;
    const QColor color = tint.isValid() ? tint : qApp->palette().color(QPalette::Active, QPalette::WindowText);
    const QString key = QString::number(int(glyph)) + color.name(QColor::HexArgb);
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();
    QIcon ic;
    for (int size : {16, 24, 32, 48}) {
        ic.addPixmap(render(glyph, size, color), QIcon::Normal, QIcon::Off);
        QColor disabled = color;
        disabled.setAlphaF(0.4);
        ic.addPixmap(render(glyph, size, disabled), QIcon::Disabled, QIcon::Off);
    }
    cache.insert(key, ic);
    return ic;
}

QIcon toggleIcon(Glyph glyph, const QColor &offTint, const QColor &onTint)
{
    static QHash<QString, QIcon> cache;
    const QString key = QString::number(int(glyph)) + offTint.name(QColor::HexArgb) + onTint.name(QColor::HexArgb);
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();
    QIcon ic;
    for (int size : {16, 24, 32, 48}) {
        ic.addPixmap(render(glyph, size, offTint), QIcon::Normal, QIcon::Off);
        ic.addPixmap(render(glyph, size, onTint), QIcon::Normal, QIcon::On);
        QColor disabled = offTint;
        disabled.setAlphaF(0.4);
        ic.addPixmap(render(glyph, size, disabled), QIcon::Disabled, QIcon::Off);
    }
    cache.insert(key, ic);
    return ic;
}

QIcon colorDot(const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (color.isValid()) {
        p.setPen(QPen(color.darker(120), 1));
        p.setBrush(color);
    } else {
        p.setPen(QPen(qApp->palette().color(QPalette::Mid), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
    }
    p.drawEllipse(QRectF(1, 1, size - 2, size - 2));
    return QIcon(pm);
}

} // namespace SAIcons
