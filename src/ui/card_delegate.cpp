#include "ui/card_delegate.h"

#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QScrollBar>

#include "ui/card_text.h"
#include "ui/history_model.h"

namespace tmw::ui {
namespace {

constexpr int kPadding = 10;

QString escape(const std::string& text) {
    return QString::fromStdString(text).toHtmlEscaped();
}

}  // namespace

CardDelegate::CardDelegate(QAbstractItemView* view, QObject* parent)
    : QStyledItemDelegate(parent), view_(view) {}

int CardDelegate::availableWidth(const QStyleOptionViewItem& option) const {
    if (option.rect.width() > 0) {
        return option.rect.width();
    }
    return view_ == nullptr ? 0 : view_->viewport()->width();
}

std::unique_ptr<QTextDocument> CardDelegate::layout(const QModelIndex& index, const QFont& font,
                                                    const QPalette& palette, int width) const {
    const auto* model = qobject_cast<const HistoryModel*>(index.model());
    const core::HistoryCard* card = model == nullptr ? nullptr : model->cardAt(index.row());
    auto document = std::make_unique<QTextDocument>();
    document->setDefaultFont(font);
    document->setTextWidth(std::max(width - 2 * kPadding, 1));
    if (card == nullptr) {
        return document;
    }

    const QString dim = palette.color(QPalette::Disabled, QPalette::Text).name();
    const QString main = palette.color(QPalette::Text).name();
    const QString separator = palette.color(QPalette::Mid).name();

    QString html;
    html += QStringLiteral("<div style='color:%1; font-size:small;'>%2</div>")
                .arg(separator, cardHeader(*card));
    if (!card->error.empty()) {
        html += QStringLiteral("<div style='color:%1; font-size:small;'>⚠ %2</div>")
                    .arg(dim, escape(card->error));
    }
    for (const core::HistoryGroup& group : card->groups) {
        html += QStringLiteral("<div style='margin-top:6px; color:%1; font-size:small;'>%2</div>")
                    .arg(dim, escape(group.source));
        if (!group.translation.empty()) {
            html += QStringLiteral("<div style='color:%1;'>%2</div>")
                        .arg(main, escape(group.translation));
        }
    }
    document->setHtml(html);
    return document;
}

void CardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const {
    painter->save();
    const std::unique_ptr<QTextDocument> document =
        layout(index, option.font, option.palette, availableWidth(option));
    painter->translate(option.rect.topLeft() + QPoint(kPadding, kPadding / 2));
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = option.palette;
    document->documentLayout()->draw(painter, context);
    painter->restore();

    // 卡片之間的分隔線
    painter->save();
    painter->setPen(option.palette.color(QPalette::Mid));
    painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
    painter->restore();
}

QSize CardDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int width = availableWidth(option);
    const std::unique_ptr<QTextDocument> document =
        layout(index, option.font, option.palette, width);
    return QSize(width, static_cast<int>(document->size().height()) + kPadding);
}

}  // namespace tmw::ui
