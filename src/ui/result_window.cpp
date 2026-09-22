#include "ui/result_window.h"

#include <windows.h>

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

#include "ui/card_delegate.h"
#include "ui/history_model.h"

namespace tmw::ui {
namespace {

constexpr int kMinFontPoints = 7;
constexpr int kMaxFontPoints = 28;
// 捲軸離底部這麼近就算「在最新」（使用者拖到底時未必剛好是最大值）
constexpr int kBottomTolerance = 4;

}  // namespace

ResultWindow::ResultWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("翻譯結果"));
    resize(420, 520);

    model_ = new HistoryModel(this);
    view_ = new QListView(this);
    delegate_ = new CardDelegate(view_, this);
    view_->setModel(model_);
    view_->setItemDelegate(delegate_);
    view_->setSelectionMode(QAbstractItemView::NoSelection);
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view_->setResizeMode(QListView::Adjust);
    view_->setWordWrap(true);
    view_->setUniformItemSizes(false);

    onTopBox_ = new QCheckBox(QStringLiteral("置頂"), this);
    auto* clearButton = new QPushButton(QStringLiteral("清除"), this);
    auto* smaller = new QPushButton(QStringLiteral("A-"), this);
    auto* bigger = new QPushButton(QStringLiteral("A+"), this);
    smaller->setFixedWidth(36);
    bigger->setFixedWidth(36);

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(onTopBox_);
    toolbar->addStretch();
    toolbar->addWidget(smaller);
    toolbar->addWidget(bigger);
    toolbar->addWidget(clearButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addLayout(toolbar);
    layout->addWidget(view_, 1);

    jumpButton_ = new QPushButton(QStringLiteral("↓ 回到最新"), view_);
    jumpButton_->hide();

    connect(clearButton, &QPushButton::clicked, this, &ResultWindow::clearCards);
    connect(smaller, &QPushButton::clicked, this, [this] { setFontPointSize(fontPoints_ - 1); });
    connect(bigger, &QPushButton::clicked, this, [this] { setFontPointSize(fontPoints_ + 1); });
    connect(onTopBox_, &QCheckBox::toggled, this, &ResultWindow::setAlwaysOnTop);
    connect(jumpButton_, &QPushButton::clicked, this, [this] {
        scrollToLatest();
        updateJumpButton();
    });
    connect(view_->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { updateJumpButton(); });

    // 拖動視窗時每個像素都存一次設定太浪費，停下來之後再存
    saveTimer_ = new QTimer(this);
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(500);
    connect(saveTimer_, &QTimer::timeout, this, &ResultWindow::settingsChanged);

    applyFont();
}

ResultWindow::~ResultWindow() = default;

void ResultWindow::scheduleSave() {
    saveTimer_->start();
}

int ResultWindow::cardCount() const {
    return model_->rowCount();
}

bool ResultWindow::followingLatest() const {
    const QScrollBar* bar = view_->verticalScrollBar();
    return bar->value() >= bar->maximum() - kBottomTolerance;
}

void ResultWindow::addCard(const core::HistoryCard& card) {
    const bool follow = followingLatest();
    model_->appendCard(card);
    if (follow) {
        scrollToLatest();
    }
    updateJumpButton();
}

void ResultWindow::clearCards() {
    model_->clear();
    updateJumpButton();
}

void ResultWindow::scrollToLatest() {
    if (model_->rowCount() > 0) {
        view_->scrollToBottom();
    }
}

void ResultWindow::updateJumpButton() {
    const bool needed = model_->rowCount() > 0 && !followingLatest();
    jumpButton_->setVisible(needed);
    if (needed) {
        placeJumpButton();
    }
}

void ResultWindow::placeJumpButton() {
    jumpButton_->adjustSize();
    const int margin = 12;
    jumpButton_->move(view_->viewport()->width() - jumpButton_->width() - margin,
                      view_->viewport()->height() - jumpButton_->height() - margin);
    jumpButton_->raise();
}

void ResultWindow::setFontPointSize(int points) {
    const int clamped = std::clamp(points, kMinFontPoints, kMaxFontPoints);
    if (clamped == fontPoints_) {
        return;
    }
    fontPoints_ = clamped;
    applyFont();
    scheduleSave();
}

void ResultWindow::applyFont() {
    QFont viewFont = view_->font();
    viewFont.setPointSize(fontPoints_);
    view_->setFont(viewFont);
    // 字級變了，每一列的高度都要重算
    model_->refreshLayout();
}

void ResultWindow::setAlwaysOnTop(bool onTop) {
    if (onTopBox_->isChecked() != onTop) {
        const QSignalBlocker blocker(onTopBox_);
        onTopBox_->setChecked(onTop);
    }
    if (alwaysOnTop() == onTop) {
        return;
    }
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
    if (wasVisible) {
        // 換旗標會讓視窗隱藏，要自己顯示回來
        show();
    }
    scheduleSave();
}

bool ResultWindow::alwaysOnTop() const {
    return windowFlags().testFlag(Qt::WindowStaysOnTopHint);
}

core::RectI ResultWindow::savedGeometry() const {
    const QRect rect = geometry();
    return core::RectI::fromXYWH(rect.x(), rect.y(), rect.width(), rect.height());
}

void ResultWindow::restoreGeometry(const core::RectI& rect) {
    if (rect.empty()) {
        return;
    }
    setGeometry(rect.left, rect.top, rect.width(), rect.height());
}

void ResultWindow::excludeFromCapture() {
    // 本工具的視窗都不能出現在擷取結果中，否則把透鏡拖到這裡會翻譯自己的輸出
    // （design.md 4.1）
    const auto handle = reinterpret_cast<HWND>(winId());
    if (handle != nullptr) {
        SetWindowDisplayAffinity(handle, WDA_EXCLUDEFROMCAPTURE);
    }
}

void ResultWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    excludeFromCapture();
    updateJumpButton();
}

void ResultWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // 寬度變了，換行的位置也變了
    model_->refreshLayout();
    updateJumpButton();
    scheduleSave();
}

void ResultWindow::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    scheduleSave();
}

void ResultWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        // 系統切換深色／淺色：卡片的顏色是依 palette 產生的，要重畫
        view_->viewport()->update();
    }
}

}  // namespace tmw::ui
