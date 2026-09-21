#include "core/opencc_converter.h"

#include <SimpleConverter.hpp>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace tmw::core {
namespace {

// OpenCC 的 API 只收 std::string。Windows 上的路徑可能有非 ASCII 字元
// （使用者名稱是中文時 %LOCALAPPDATA% 就會有），所以轉成 UTF-8 交給它。
std::string toUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

}  // namespace

OpenccConverter::OpenccConverter(const std::filesystem::path& configFile) {
    if (!std::filesystem::exists(configFile)) {
        throw std::runtime_error("找不到 OpenCC 設定檔：" + toUtf8(configFile));
    }
    try {
        // 把設定檔的資料夾也交給 OpenCC，它才找得到同一層的字典檔（.ocd2）
        const std::vector<std::string> paths{toUtf8(configFile.parent_path())};
        converter_ = std::make_unique<opencc::SimpleConverter>(toUtf8(configFile), paths);
    } catch (const std::exception& error) {
        throw std::runtime_error("OpenCC 初始化失敗：" + std::string(error.what()));
    }
}

OpenccConverter::~OpenccConverter() = default;

std::string OpenccConverter::convert(const std::string& text) const {
    try {
        return converter_->Convert(text);
    } catch (const std::exception&) {
        return text;
    }
}

std::filesystem::path OpenccConverter::defaultConfig(const std::filesystem::path& openccDirectory) {
    return openccDirectory / "s2twp.json";
}

}  // namespace tmw::core
