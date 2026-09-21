#include "net/rate_limiter.h"

#include <algorithm>
#include <thread>

namespace tmw::net {

RateLimiter::RateLimiter(Duration interval) : interval_(std::max(interval, Duration::zero())) {}

Duration RateLimiter::waitFor(TimePoint now) const {
    const std::lock_guard lock(mutex_);
    if (!used_) {
        return Duration::zero();
    }
    const TimePoint ready = last_ + interval_;
    return now >= ready ? Duration::zero() : ready - now;
}

void RateLimiter::record(TimePoint now) {
    const std::lock_guard lock(mutex_);
    // 系統時間跳動或多執行緒搶先時，不要讓 last_ 倒退
    last_ = used_ ? std::max(last_, now) : now;
    used_ = true;
}

Sleeper defaultSleeper() {
    return [](Duration total, std::stop_token cancel) {
        // 分段睡，使用者移動透鏡時才不用等滿一整秒
        constexpr auto kSlice = std::chrono::milliseconds(20);
        Duration left = total;
        while (left > Duration::zero() && !cancel.stop_requested()) {
            const Duration slice = std::min<Duration>(left, kSlice);
            std::this_thread::sleep_for(slice);
            left -= slice;
        }
    };
}

}  // namespace tmw::net
