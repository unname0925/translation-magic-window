#include "platform/single_instance.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <string>

namespace tmw::platform {
namespace {

// 每個測試用不同的名稱，避免和正在執行的程式或其他測試互相干擾
std::wstring uniqueName(const wchar_t* suffix) {
    return L"Local\\TranslationMagicWindow.Test." + std::to_wstring(GetCurrentProcessId()) + L"." +
           suffix;
}

TEST(SingleInstanceTest, FirstInstanceIsFirst) {
    const SingleInstance instance(uniqueName(L"First"));
    EXPECT_TRUE(instance.isFirst());
}

TEST(SingleInstanceTest, SecondInstanceIsNotFirst) {
    const std::wstring name = uniqueName(L"Second");
    const SingleInstance first(name);
    const SingleInstance second(name);
    EXPECT_TRUE(first.isFirst());
    EXPECT_FALSE(second.isFirst());
}

TEST(SingleInstanceTest, NameIsReleasedAfterFirstInstanceEnds) {
    const std::wstring name = uniqueName(L"Released");
    {
        const SingleInstance first(name);
    }
    const SingleInstance next(name);
    EXPECT_TRUE(next.isFirst());
}

}  // namespace
}  // namespace tmw::platform
