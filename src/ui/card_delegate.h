// 畫一張卡片：標題（時間、透鏡、語言）加上一組一組的原文和譯文（見 docs/design.md 4.7）。
//
// 原文用淡色小字、譯文用主色，這樣一眼就看得出哪一行是譯文。
#pragma once

#include <QStyledItemDelegate>
#include <QTextDocument>
#include <memory>

class QAbstractItemView;

namespace tmw::ui {

class CardDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    // 需要 view 才知道能用多寬來排版。Qt 有可能用空的 option.rect 問 sizeHint，
    // 照著用會讓卡片的高度算成 0、視窗一片空白，所以退回用 viewport 的寬度。
    // （防呆：目前的測試沒有成功讓 Qt 走到這條路徑）
    explicit CardDelegate(QAbstractItemView* view, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    // 把一張卡片排版成 HTML。寬度會影響換行，所以要一起指定。
    std::unique_ptr<QTextDocument> layout(const QModelIndex& index, const QFont& font,
                                          const QPalette& palette, int width) const;
    // 排版可以用的寬度
    int availableWidth(const QStyleOptionViewItem& option) const;

    QAbstractItemView* view_ = nullptr;
};

}  // namespace tmw::ui
