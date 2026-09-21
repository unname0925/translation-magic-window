// 用 OpenCC 把譯文轉成台灣繁體（見 docs/design.md 4.5 步驟 5）。
//
// 翻譯引擎（尤其是 LLM 和 Google）常常回簡體或中國用語，s2twp 會一併處理：
// 「软件」→「軟體」、「服务器」→「伺服器」。
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "core/text_converter.h"

namespace opencc {
class SimpleConverter;
}

namespace tmw::core {

class OpenccConverter final : public ITextConverter {
public:
    // configFile 例如 <資料夾>/s2twp.json；字典檔（.ocd2）要和它在同一個資料夾。
    // 找不到檔案或設定壞掉時丟出 std::runtime_error。
    explicit OpenccConverter(const std::filesystem::path& configFile);
    ~OpenccConverter() override;

    OpenccConverter(const OpenccConverter&) = delete;
    OpenccConverter& operator=(const OpenccConverter&) = delete;

    // 轉換失敗時原樣回傳：簡繁轉換壞掉不應該讓整句譯文消失。
    std::string convert(const std::string& text) const override;

    // 放字典檔的資料夾底下的 s2twp.json
    static std::filesystem::path defaultConfig(const std::filesystem::path& openccDirectory);

private:
    std::unique_ptr<opencc::SimpleConverter> converter_;
};

}  // namespace tmw::core
