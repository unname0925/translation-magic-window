// 結果視窗的資料來源：一列一張卡片（見 docs/design.md 4.7）。
//
// 用 QAbstractListModel 搭配 QListView，500 張卡片只會繪製看得到的那幾張。
#pragma once

#include <QAbstractListModel>
#include <deque>

#include "core/history.h"

namespace tmw::ui {

class HistoryModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit HistoryModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    // 加入一張卡片。超過上限時最舊的那張會被丟掉。
    void appendCard(core::HistoryCard card);
    void clear();

    // 字級或寬度變了，每一列的高度都要重算
    void refreshLayout();

    void setCapacity(std::size_t capacity);
    std::size_t capacity() const { return capacity_; }

    const core::HistoryCard* cardAt(int row) const;

private:
    std::size_t capacity_ = core::History::kDefaultCapacity;
    std::deque<core::HistoryCard> cards_;
};

}  // namespace tmw::ui
