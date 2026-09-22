#include "ui/card_delegate.h"

#include <QAbstractItemView>
#include <QFontMetrics>
#include <QPainter>
#include <QScrollBar>
#include <algorithm>
#include <utility>

#include "ui/card_text.h"
#include "ui/history_model.h"
#include "ui/ruby_text.h"

namespace tmw::ui {
namespace {

constexpr int kPadding = 10;
constexpr int kGroupGap = 8;
// 原文比譯文小一點、淡一點，一眼就看得出哪一行是譯文
constexpr double kSourceScale = 0.85;

QFont scaled(const QFont& font, double factor) {
    QFont out = font;
    const double points = font.pointSizeF() > 0 ? font.pointSizeF() : 10.0;
    out.setPointSizeF(std::max(points * factor, 5.0));
    return out;
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

CardDelegate::Layout CardDelegate::layout(const QModelIndex& index, const QFont& font,
                                          int width) const {
    Layout out;
    const auto* model = qobject_cast<const HistoryModel*>(index.model());
    const core::HistoryCard* card = model == nullptr ? nullptr : model->cardAt(index.row());
    out.width = std::max(width - 2 * kPadding, 1);
    if (card == nullptr) {
        return out;
    }

    const QFont smallFont = scaled(font, kSourceScale);
    out.header = cardHeader(*card);
    out.headerHeight = QFontMetrics(smallFont).height();
    out.height = out.headerHeight;
    if (!card->error.empty()) {
        out.error = QStringLiteral("⚠ ") + QString::fromStdString(card->error);
        out.errorHeight = QFontMetrics(smallFont).height();
        out.height += out.errorHeight;
    }

    for (const core::HistoryGroup& group : card->groups) {
        Layout::Group laid;
        laid.source = std::make_shared<RubyText>(QString::fromStdString(group.source), smallFont);
        laid.translation =
            std::make_shared<RubyText>(QString::fromStdString(group.translation), font);
        out.height += kGroupGap;
        laid.top = out.height;
        out.height += laid.source->layout(out.width);
        laid.translationTop = out.height;
        out.height += laid.translation->layout(out.width);
        out.groups.push_back(std::move(laid));
    }
    out.height += kPadding;
    return out;
}

void CardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const {
    const Layout laid = layout(index, option.font, availableWidth(option));
    const QPoint origin = option.rect.topLeft() + QPoint(kPadding, kPadding / 2);
    const QFont smallFont = scaled(option.font, kSourceScale);
    const QColor dim = option.palette.color(QPalette::Disabled, QPalette::Text);
    const QColor main = option.palette.color(QPalette::Text);
    const QColor separator = option.palette.color(QPalette::Mid);

    painter->save();
    painter->setFont(smallFont);
    painter->setPen(separator);
    painter->drawText(QRect(origin.x(), origin.y(), laid.width, laid.headerHeight),
                      Qt::AlignLeft | Qt::AlignVCenter, laid.header);
    if (!laid.error.isEmpty()) {
        painter->setPen(dim);
        painter->drawText(
            QRect(origin.x(), origin.y() + laid.headerHeight, laid.width, laid.errorHeight),
            Qt::AlignLeft | Qt::AlignVCenter, laid.error);
    }
    for (const Layout::Group& group : laid.groups) {
        painter->setPen(dim);
        group.source->draw(*painter, QPoint(origin.x(), origin.y() + group.top));
        painter->setPen(main);
        group.translation->draw(*painter, QPoint(origin.x(), origin.y() + group.translationTop));
    }
    painter->restore();

    // 卡片之間的分隔線
    painter->save();
    painter->setPen(separator);
    painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
    painter->restore();
}

QSize CardDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int width = availableWidth(option);
    const Layout laid = layout(index, option.font, width);
    return QSize(width, laid.height + kPadding);
}

}  // namespace tmw::ui
