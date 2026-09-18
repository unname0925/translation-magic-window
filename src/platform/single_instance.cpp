#include "platform/single_instance.h"

#include "platform/win_error.h"

namespace tmw::platform {

SingleInstance::SingleInstance(const std::wstring& name) {
    mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
    if (mutex_ == nullptr) {
        throwLastError("CreateMutexW failed");
    }
    first_ = GetLastError() != ERROR_ALREADY_EXISTS;
}

SingleInstance::~SingleInstance() {
    if (mutex_ != nullptr) {
        CloseHandle(mutex_);
    }
}

}  // namespace tmw::platform
