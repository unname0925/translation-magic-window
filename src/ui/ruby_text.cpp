#include "ui/ruby_text.h"

#include <QFontMetrics>
#include <QPainter>
#include <algorithm>
#include <utility>

namespace tmw::ui {
namespace {

// 讀音和本文之間留的空隙
constexpr int kRubyGap = 1;

}  // namespace

std::vector<RubyRun> parseRubyMarkup(const QString& text) {
    std::vector<RubyRun> runs;
    QString plain;
    qsizetype at = 0;
    while (at < text.size()) {
        const qsizetype open = text.indexOf(QLatin1Char('{'), at);
        if (open < 0) {
            break;
        }
        const qsizetype bar = text.indexOf(QLatin1Char('|'), open + 1);
        const qsizetype close = text.indexOf(QLatin1Char('}'), open + 1);
        const qsizetype nextOpen = text.indexOf(QLatin1Char('{'), open + 1);
        // 下一個 { 比 | 還早出現，表示這個 { 沒有配對（後面那組才是真的）
        if (bar < 0 || close < 0 || bar > close || (nextOpen >= 0 && nextOpen < bar)) {
            // 沒有配對的大括號：當成普通文字，不要吃掉後面的內容
            plain += text.mid(at, open - at + 1);
            at = open + 1;
            continue;
        }
        plain += text.mid(at, open - at);
        if (!plain.isEmpty()) {
            runs.push_back(RubyRun{std::move(plain), {}});
            plain.clear();
        }
        runs.push_back(
            RubyRun{text.mid(open + 1, bar - open - 1), text.mid(bar + 1, close - bar - 1)});
        at = close + 1;
    }
    plain += text.mid(at);
    if (!plain.isEmpty()) {
        runs.push_back(RubyRun{std::move(plain), {}});
    }
    return runs;
}

RubyText::RubyText(const QString& marked, const QFont& baseFont, double rubyScale)
    : runs_(parseRubyMarkup(marked)), baseFont_(baseFont), rubyFont_(baseFont) {
    const double points = baseFont.pointSizeF() > 0 ? baseFont.pointSizeF() : 10.0;
    rubyFont_.setPointSizeF(std::max(points * rubyScale, 5.0));
}

int RubyText::layout(int width) {
    lines_.clear();
    height_ = 0;
    const QFontMetrics baseMetrics(baseFont_);
    const QFontMetrics rubyMetrics(rubyFont_);
    const int usable = std::max(width, 1);

    // 排版的單位：普通文字一個字一個單位，帶讀音的整組不能拆到兩行
    std::vector<RubyPiece> pieces;
    for (const RubyRun& run : runs_) {
        if (run.reading.isEmpty()) {
            for (const QChar character : run.base) {
                RubyPiece piece;
                piece.base = character;
                piece.width = baseMetrics.horizontalAdvance(piece.base);
                pieces.push_back(std::move(piece));
            }
            continue;
        }
        RubyPiece piece;
        piece.base = run.base;
        piece.reading = run.reading;
        piece.width = std::max(baseMetrics.horizontalAdvance(run.base),
                               rubyMetrics.horizontalAdvance(run.reading));
        pieces.push_back(std::move(piece));
    }

    RubyLine line;
    int x = 0;
    const auto finish = [&] {
        if (line.pieces.empty()) {
            return;
        }
        line.top = height_;
        const int rubyHeight = line.hasRuby ? rubyMetrics.height() + kRubyGap : 0;
        line.baseTop = height_ + rubyHeight;
        line.height = rubyHeight + baseMetrics.height();
        height_ += line.height;
        lines_.push_back(std::move(line));
        line = RubyLine{};
        x = 0;
    };

    for (RubyPiece& piece : pieces) {
        const bool newline = piece.base == QLatin1String("\n");
        if (newline) {
            finish();
            continue;
        }
        if (x > 0 && x + piece.width > usable) {
            finish();
        }
        piece.x = x;
        x += piece.width;
        line.hasRuby = line.hasRuby || !piece.reading.isEmpty();
        line.pieces.push_back(std::move(piece));
    }
    finish();
    return height_;
}

void RubyText::draw(QPainter& painter, QPoint topLeft) const {
    const QFontMetrics baseMetrics(baseFont_);
    const QFontMetrics rubyMetrics(rubyFont_);
    for (const RubyLine& line : lines_) {
        for (const RubyPiece& piece : line.pieces) {
            painter.setFont(baseFont_);
            const int baseWidth = baseMetrics.horizontalAdvance(piece.base);
            // 本文比讀音窄時置中，看起來才像漫畫的ルビ
            const int baseX = topLeft.x() + piece.x + (piece.width - baseWidth) / 2;
            painter.drawText(
                QRect(baseX, topLeft.y() + line.baseTop, baseWidth, baseMetrics.height()),
                Qt::AlignLeft | Qt::AlignVCenter, piece.base);
            if (piece.reading.isEmpty()) {
                continue;
            }
            painter.setFont(rubyFont_);
            const int readingWidth = rubyMetrics.horizontalAdvance(piece.reading);
            const int readingX = topLeft.x() + piece.x + (piece.width - readingWidth) / 2;
            painter.drawText(
                QRect(readingX, topLeft.y() + line.top, readingWidth, rubyMetrics.height()),
                Qt::AlignLeft | Qt::AlignVCenter, piece.reading);
        }
    }
    painter.setFont(baseFont_);
}

}  // namespace tmw::ui
