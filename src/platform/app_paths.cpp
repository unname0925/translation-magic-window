#include "platform/app_paths.h"

#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

#include <stdexcept>

namespace tmw::platform {

std::filesystem::path defaultDataDirectory() {
    PWSTR folder = nullptr;
    const HRESULT hr =
        SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &folder);
    if (FAILED(hr)) {
        CoTaskMemFree(folder);
        throw std::runtime_error("cannot locate the LocalAppData folder");
    }
    std::filesystem::path path = std::filesystem::path(folder) / L"TranslationMagicWindow";
    CoTaskMemFree(folder);
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path capturesDirectory(const std::filesystem::path& dataDirectory) {
    std::filesystem::path path = dataDirectory / L"captures";
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path findModelsDirectory() {
    std::filesystem::path here = executableDirectory();
    // 往上找幾層：開發時執行檔在
    // build\<preset>in\<config>\，模型在倉庫根目錄
    for (int level = 0; level < 6 && !here.empty(); ++level) {
        const std::filesystem::path candidate = here / L"models";
        if (std::filesystem::exists(candidate / L"PP-OCRv6_medium_det")) {
            return candidate;
        }
        if (!here.has_parent_path() || here.parent_path() == here) {
            break;
        }
        here = here.parent_path();
    }
    return {};
}

}  // namespace tmw::platform
