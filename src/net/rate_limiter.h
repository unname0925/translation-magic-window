// 非官方端點的限流：每秒最多一個請求，降低被封鎖的風險（見 docs/design.md 4.5 步驟 6）。
//
// 這裡只算「還要等多久」，真正的等待交給呼叫端，測試才不必真的睡。
#pragma once

#include <functional>
#include <mutex>
#include <stop_token>

#include "core/clock.h"

namespace tmw::net {

using core::Duration;
using core::TimePoint;

class RateLimiter {
public:
    explicit RateLimiter(Duration interval);

    // 現在送出請求還要等多久（可能是 0）
    Duration waitFor(TimePoint now) const;

    // 記下「在這個時間送出了一個請求」
    void record(TimePoint now);

private:
    Duration interval_;
    mutable std::mutex mutex_;
    bool used_ = false;
    TimePoint last_{};
};

// 可以被取消的等待。預設實作分段睡，取消時最多多等一小段。
using Sleeper = std::function<void(Duration, std::stop_token)>;

Sleeper defaultSleeper();

}  // namespace tmw::net
