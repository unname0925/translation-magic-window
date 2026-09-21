// 非官方端點的限流：每秒最多一個請求。
#include "net/rate_limiter.h"

#include <gtest/gtest.h>

#include <chrono>

#include "support/fake_clock.h"

namespace tmw::net {
namespace {

TEST(RateLimiterTest, TheFirstRequestDoesNotWait) {
    const RateLimiter limiter(std::chrono::seconds(1));
    EXPECT_EQ(limiter.waitFor(TimePoint{}), Duration::zero());
}

TEST(RateLimiterTest, WaitsForTheRestOfTheInterval) {
    RateLimiter limiter(std::chrono::seconds(1));
    const TimePoint start{};
    limiter.record(start);
    EXPECT_EQ(limiter.waitFor(start), std::chrono::seconds(1));
    EXPECT_EQ(limiter.waitFor(start + std::chrono::milliseconds(400)),
              std::chrono::milliseconds(600));
    EXPECT_EQ(limiter.waitFor(start + std::chrono::seconds(1)), Duration::zero());
    EXPECT_EQ(limiter.waitFor(start + std::chrono::seconds(30)), Duration::zero());
}

TEST(RateLimiterTest, IgnoresTimeGoingBackwards) {
    RateLimiter limiter(std::chrono::seconds(1));
    const TimePoint start{};
    limiter.record(start + std::chrono::seconds(10));
    limiter.record(start);
    EXPECT_EQ(limiter.waitFor(start + std::chrono::seconds(10)), std::chrono::seconds(1));
}

TEST(RateLimiterTest, ZeroIntervalNeverWaits) {
    RateLimiter limiter(Duration::zero());
    limiter.record(TimePoint{});
    EXPECT_EQ(limiter.waitFor(TimePoint{}), Duration::zero());
}

TEST(SleeperTest, StopsEarlyWhenCancelled) {
    std::stop_source source;
    source.request_stop();
    const auto start = std::chrono::steady_clock::now();
    defaultSleeper()(std::chrono::seconds(30), source.get_token());
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
}

}  // namespace
}  // namespace tmw::net
