#ifndef SVGICONS_H
#define SVGICONS_H

#include <QByteArray>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QSize>

namespace SvgIcons {

inline QIcon iconFromSvg(const QByteArray& svg, const QSize& size) {
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        return {};
    }

    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);

    return QIcon(pixmap);
}

inline QByteArray refreshIconSvg() {
    return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24">
  <path d="M12 4V1L8 5l4 4V6c3.31 0 6 2.69 6 6 0 1.01-.25 1.97-.7 2.8l1.46 1.46A7.932 7.932 0 0019.5 12c0-4.42-3.58-8-8-8zm0 14c-3.31 0-6-2.69-6-6 0-1.01.25-1.97.7-2.8L5.24 7.74A7.932 7.932 0 004.5 12c0 4.42 3.58 8 8 8v3l4-4-4-4v3z" fill="#cccccc"/>
</svg>)";
}

} // namespace SvgIcons

#endif // SVGICONS_H