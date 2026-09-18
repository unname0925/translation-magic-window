#pragma once

#include <chrono>

namespace tmw::core {

using TimePoint = std::chrono::steady_clock::time_point;
using Duration = std::chrono::steady_clock::duration;

// 所有和時間有關的邏輯都透過 IClock 取得時間，測試時換成 FakeClock，
// 這樣測試結果就不受機器快慢影響。
class IClock {
public:
    virtual ~IClock() = default;
    virtual TimePoint now() const = 0;
};

class SteadyClock final : public IClock {
public:
    TimePoint now() const override { return std::chrono::steady_clock::now(); }
};

}  // namespace tmw::core
