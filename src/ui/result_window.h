// 結果視窗：原文／譯文成對顯示，可以捲動瀏覽歷史（見 docs/design.md 4.7）。
//
// 使用者自己擺放位置，不跟隨透鏡。位置和大小會記在設定裡。
#pragma once

#include <QWidget>
#include <cstddef>

#include "core/geometry.h"
#include "core/history.h"

class QCheckBox;
class QListView;
class QPushButton;
class QTimer;

namespace tmw::ui {

class CardDelegate;
class HistoryModel;

class ResultWindow : public QWidget {
    Q_OBJECT

public:
    explicit ResultWindow(QWidget* parent = nullptr);
    ~ResultWindow() override;

    // 新增一張卡片。使用者沒有往上捲時會自動捲到最新。
    void addCard(const core::HistoryCard& card);
    void clearCards();

    // 字級（點）。太小或太大都會被限制在可讀的範圍內。
    void setFontPointSize(int points);
    int fontPointSize() const { return fontPoints_; }

    void setAlwaysOnTop(bool onTop);
    bool alwaysOnTop() const;

    // 記住的位置和大小（空的代表還沒記過）
    core::RectI savedGeometry() const;
    void restoreGeometry(const core::RectI& rect);

    // 捲軸是不是在最底下（決定新卡片要不要自動捲過去）
    bool followingLatest() const;

    int cardCount() const;

signals:
    // 位置、大小、置頂、字級有變動（停下來 0.5 秒後才發），呼叫端可以存進設定
    void settingsChanged();

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void scheduleSave();
    void updateJumpButton();
    void scrollToLatest();
    void placeJumpButton();
    void applyFont();
    void excludeFromCapture();

    HistoryModel* model_ = nullptr;
    CardDelegate* delegate_ = nullptr;
    QListView* view_ = nullptr;
    QPushButton* jumpButton_ = nullptr;
    QCheckBox* onTopBox_ = nullptr;
    QTimer* saveTimer_ = nullptr;
    int fontPoints_ = 10;
};

}  // namespace tmw::ui
