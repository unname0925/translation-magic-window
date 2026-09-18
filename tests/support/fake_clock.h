#pragma once

#include "core/clock.h"

namespace tmw::test {

// 測試用的時鐘：時間只會在呼叫 advance() 時前進。
class FakeClock final : public core::IClock {
public:
    explicit FakeClock(core::TimePoint start = core::TimePoint{}) : now_(start) {}

    core::TimePoint now() const override { return now_; }
    void advance(core::Duration duration) { now_ += duration; }

private:
    core::TimePoint now_;
};

}  // namespace tmw::test
