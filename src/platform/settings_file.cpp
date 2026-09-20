#include "platform/settings_file.h"

#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "platform/logging.h"
#include "platform/text_encoding.h"

namespace tmw::platform {
namespace {

constexpr const wchar_t* kFileName = L"settings.json";

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + wideToUtf8(path.wstring()));
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

std::filesystem::path defaultSettingsPath() {
    PWSTR folder = nullptr;
    const HRESULT hr =
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &folder);
    if (FAILED(hr)) {
        CoTaskMemFree(folder);
        throw std::runtime_error("cannot locate the AppData folder");
    }
    std::filesystem::path directory = std::filesystem::path(folder) / L"TranslationMagicWindow";
    CoTaskMemFree(folder);
    std::filesystem::create_directories(directory);
    return directory / kFileName;
}

std::filesystem::path settingsPathIn(const std::filesystem::path& dataDirectory) {
    std::filesystem::create_directories(dataDirectory);
    return dataDirectory / kFileName;
}

SettingsFileLoad loadSettings(const std::filesystem::path& path) {
    SettingsFileLoad result;
    if (!std::filesystem::exists(path)) {
        result.missing = true;
        return result;
    }

    std::string text;
    try {
        text = readFile(path);
    } catch (const std::exception& error) {
        result.problem = error.what();
        logWarn("讀不到設定檔，改用預設值：" + result.problem);
        return result;
    }

    core::SettingsLoad parsed = core::parseSettings(text);
    if (!parsed.usedDefaults()) {
        result.settings = std::move(parsed.settings);
        return result;
    }

    // 讀不懂：先把原檔留下來（使用者可能手改壞了，內容還有用），再用預設值
    result.problem = parsed.problem;
    std::filesystem::path backup = path;
    backup += L".bak";
    std::error_code code;
    std::filesystem::rename(path, backup, code);
    if (!code) {
        result.backup = backup;
    }
    logWarn("設定檔無法使用（" + result.problem + "），已備份成 " +
            (result.backup ? pathToUtf8(result.backup->filename()) : std::string("（備份失敗）")) +
            "，改用預設值");
    return result;
}

void saveSettings(const std::filesystem::path& path, const core::Settings& settings) {
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::path temporary = path;
    temporary += L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("cannot write " + wideToUtf8(temporary.wstring()));
        }
        file << core::serializeSettings(settings);
    }
    std::filesystem::rename(temporary, path);
}

}  // namespace tmw::platform
