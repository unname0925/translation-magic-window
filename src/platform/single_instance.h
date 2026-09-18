#pragma once

#include <windows.h>

#include <string>

namespace tmw::platform {

// 用具名 mutex 確保同一個使用者工作階段中只有一個執行個體。
// 否則會出現兩組透鏡，全域快捷鍵也會註冊失敗（見 docs/design.md 4.1）。
class SingleInstance {
public:
    // name 建議加上 "Local\\" 前綴，只在目前的登入工作階段內生效。
    explicit SingleInstance(const std::wstring& name);
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // 是否為第一個（唯一的）執行個體。
    bool isFirst() const { return first_; }

private:
    HANDLE mutex_ = nullptr;
    bool first_ = false;
};

}  // namespace tmw::platform
