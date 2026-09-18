#include "core/clock.h"

#include <gtest/gtest.h>

#include <chrono>

#include "support/fake_clock.h"

namespace tmw::core {
namespace {

using namespace std::chrono_literals;

TEST(FakeClockTest, OnlyAdvancesWhenTold) {
    test::FakeClock clock;
    const TimePoint start = clock.now();
    EXPECT_EQ(clock.now(), start);

    clock.advance(250ms);
    EXPECT_EQ(clock.now() - start, 250ms);

    clock.advance(1s);
    EXPECT_EQ(clock.now() - start, 1250ms);
}

TEST(FakeClockTest, WorksThroughInterface) {
    test::FakeClock fake;
    const IClock& clock = fake;
    const TimePoint start = clock.now();
    fake.advance(100ms);
    EXPECT_EQ(clock.now() - start, 100ms);
}

TEST(SteadyClockTest, NeverGoesBackwards) {
    SteadyClock clock;
    TimePoint previous = clock.now();
    for (int i = 0; i < 1000; ++i) {
        const TimePoint current = clock.now();
        EXPECT_GE(current, previous);
        previous = current;
    }
}

}  // namespace
}  // namespace tmw::core
