#include "ui/card_text.h"

#include <QDateTime>

namespace tmw::ui {

QString languageName(core::Language language) {
    switch (language) {
        case core::Language::Japanese:
            return QStringLiteral("日文");
        case core::Language::English:
            return QStringLiteral("英文");
        case core::Language::Korean:
            return QStringLiteral("韓文");
        case core::Language::Unknown:
            break;
    }
    return QStringLiteral("未知語言");
}

QString cardHeader(const core::HistoryCard& card) {
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(card.time.time_since_epoch()).count();
    const QString time = QDateTime::fromMSecsSinceEpoch(milliseconds)
                             .toLocalTime()
                             .toString(QStringLiteral("HH:mm:ss"));
    return QStringLiteral("%1 · 透鏡 %2 · %3")
        .arg(time)
        .arg(card.lens)
        .arg(languageName(card.language));
}

}  // namespace tmw::ui
