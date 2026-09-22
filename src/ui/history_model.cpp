#include "ui/history_model.h"

#include <algorithm>
#include <utility>

namespace tmw::ui {

HistoryModel::HistoryModel(QObject* parent) : QAbstractListModel(parent) {}

int HistoryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(cards_.size());
}

const core::HistoryCard* HistoryModel::cardAt(int row) const {
    if (row < 0 || row >= static_cast<int>(cards_.size())) {
        return nullptr;
    }
    return &cards_[static_cast<std::size_t>(row)];
}

QVariant HistoryModel::data(const QModelIndex& index, int role) const {
    const core::HistoryCard* card = cardAt(index.row());
    if (card == nullptr || role != Qt::DisplayRole) {
        return {};
    }
    // 給輔助工具和「複製」用的純文字。實際的繪製由 CardDelegate 直接讀 cardAt()。
    QString text;
    for (const core::HistoryGroup& group : card->groups) {
        text += QString::fromStdString(group.source);
        text += QChar::LineFeed;
        text += QString::fromStdString(group.translation);
        text += QChar::LineFeed;
    }
    return text;
}

void HistoryModel::appendCard(core::HistoryCard card) {
    const int row = static_cast<int>(cards_.size());
    beginInsertRows({}, row, row);
    cards_.push_back(std::move(card));
    endInsertRows();

    if (cards_.size() > capacity_) {
        const int extra = static_cast<int>(cards_.size() - capacity_);
        beginRemoveRows({}, 0, extra - 1);
        cards_.erase(cards_.begin(), cards_.begin() + extra);
        endRemoveRows();
    }
}

void HistoryModel::clear() {
    if (cards_.empty()) {
        return;
    }
    beginResetModel();
    cards_.clear();
    endResetModel();
}

void HistoryModel::refreshLayout() {
    emit layoutChanged();
}

void HistoryModel::setCapacity(std::size_t capacity) {
    capacity_ = std::max<std::size_t>(capacity, 1);
    if (cards_.size() > capacity_) {
        const int extra = static_cast<int>(cards_.size() - capacity_);
        beginRemoveRows({}, 0, extra - 1);
        cards_.erase(cards_.begin(), cards_.begin() + extra);
        endRemoveRows();
    }
}

}  // namespace tmw::ui
