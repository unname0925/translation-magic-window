// 帶 ルビ 的文字排版（見 docs/design.md 4.7）。
//
// Qt 的 rich text 不支援 <ruby>（見整合測試 QtRichTextTest），所以自己排：
// 本文照常一個字一個字排，讀音用小一號的字畫在對應那幾個字的正上方，
// 那一行的高度留出讀音的空間。
//
// 輸入是翻譯引擎用的標記 `{本文|讀音}`（design.md 4.5）。
#pragma once

#include <QFont>
#include <QPoint>
#include <QRect>
#include <QString>
#include <vector>

class QPainter;

namespace tmw::ui {

// 一段文字。reading 是空的就是沒有 ルビ 的普通文字。
struct RubyRun {
    QString base;
    QString reading;

    friend bool operator==(const RubyRun&, const RubyRun&) = default;
};

// 把 `{本文|讀音}` 拆成一段一段。沒有配對的大括號當成普通文字。
std::vector<RubyRun> parseRubyMarkup(const QString& text);

// 排好版之後的一個單位：沒有讀音時是一個字，有讀音時是整組（不會被拆到兩行）
struct RubyPiece {
    QString base;
    QString reading;
    int x = 0;      // 相對於這一行的左邊
    int width = 0;  // 本文和讀音之中比較寬的那個
};

struct RubyLine {
    int top = 0;      // 相對於整段的頂端
    int baseTop = 0;  // 本文的頂端（上面是讀音的空間）
    int height = 0;   // 這一行佔的高度
    bool hasRuby = false;
    std::vector<RubyPiece> pieces;
};

// 一段帶 ルビ 的文字，排成指定寬度。
class RubyText {
public:
    RubyText(const QString& marked, const QFont& baseFont, double rubyScale = 0.55);

    // 依寬度重新排版，回傳總高度
    int layout(int width);

    int height() const { return height_; }
    const std::vector<RubyLine>& lines() const { return lines_; }

    // 在 topLeft 開始畫。readings 是讀音的顏色（通常比本文淡）。
    void draw(QPainter& painter, QPoint topLeft) const;

private:
    std::vector<RubyRun> runs_;
    QFont baseFont_;
    QFont rubyFont_;
    std::vector<RubyLine> lines_;
    int height_ = 0;
};

}  // namespace tmw::ui
