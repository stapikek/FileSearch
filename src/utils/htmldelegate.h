#ifndef HTMLDELEGATE_H
#define HTMLDELEGATE_H

#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QTextOption>
#include <QPainter>

/**
 * @brief Делегат для отображения HTML + иконки в ячейках таблицы
 */
class HtmlDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit HtmlDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem optionV4 = option;
        initStyleOption(&optionV4, index);

        painter->save();

        // Рисуем фон
        QRect rect = optionV4.rect;
        if (optionV4.state & QStyle::State_Selected) {
            painter->fillRect(rect, optionV4.palette.highlight());
        } else {
            painter->fillRect(rect, optionV4.palette.base());
        }

        // Рисуем иконку слева
        if (optionV4.icon.isNull() == false) {
            QSize iconSize = QSize(20, 20);
            QRect iconRect = QRect(rect.x() + 5, rect.y() + (rect.height() - iconSize.height()) / 2, iconSize.width(), iconSize.height());
            optionV4.icon.paint(painter, iconRect, Qt::AlignCenter);
        }

        // Подготавливаем текст для HTML
        QString text = optionV4.text;
        const int textLeft = rect.left() + 30;
        const int textWidth = qMax(0, rect.width() - 35);
        const QRect textRect(textLeft, rect.top(), textWidth, rect.height());

        if (text.contains(QStringLiteral("class=\"fs-match\""))) {
            // HTML с подсветкой — цвет текста из палитры (иначе QTextDocument рисует чёрным на тёмном фоне)
            const bool selected = optionV4.state & QStyle::State_Selected;
            const QColor textColor = optionV4.palette.color(
                selected ? QPalette::HighlightedText : QPalette::Text);

            QTextDocument doc;
            doc.setDefaultStyleSheet(
                QStringLiteral("body, p, div, span { color: %1; } "
                               "span.fs-match { background-color: #ffeb3b; color: #000000; font-weight: bold; } "
                               "a { color: %1; text-decoration: none; }")
                    .arg(textColor.name(QColor::HexRgb)));
            doc.setHtml(QStringLiteral("<body>%1</body>").arg(text));
            doc.setDocumentMargin(0);
            doc.setDefaultFont(optionV4.font);

            QTextOption textOption;
            textOption.setWrapMode(QTextOption::NoWrap);
            doc.setDefaultTextOption(textOption);
            doc.setTextWidth(-1);

            const qreal yOffset = rect.top() + (rect.height() - doc.size().height()) / 2.0;
            painter->setClipRect(textRect);
            painter->translate(textLeft, yOffset);
            doc.drawContents(painter);
        } else {
            // Обычный текст без подсветки
            painter->setPen(optionV4.palette.color(optionV4.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
            painter->setFont(optionV4.font);
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, optionV4.text);
        }

        painter->restore();
    }

    QString displayText(const QVariant& value, const QLocale& locale) const override {
        Q_UNUSED(locale);
        return value.toString();
    }
};

#endif // HTMLDELEGATE_H