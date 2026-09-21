#include "core/translator.h"

namespace tmw::core {

std::string describeTranslateError(TranslateError error) {
    switch (error) {
        case TranslateError::Network:
            return "連線失敗";
        case TranslateError::RateLimited:
            return "被限流或額度用完";
        case TranslateError::BadResponse:
            return "回應格式錯誤";
        case TranslateError::Cancelled:
            return "已取消";
        case TranslateError::Unavailable:
            return "沒有可用的引擎";
    }
    return "未知的錯誤";
}

}  // namespace tmw::core
