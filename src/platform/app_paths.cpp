#include "platform/app_paths.h"

#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

#include <stdexcept>

namespace tmw::platform {

std::filesystem::path localDataDirectory() {
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

std::filesystem::path capturesDirectory() {
    std::filesystem::path path = localDataDirectory() / L"captures";
    std::filesystem::create_directories(path);
    return path;
}

}  // namespace tmw::platform
