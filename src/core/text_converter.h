// 譯文的後處理：所有譯文都要經過 OpenCC 的 s2twp（簡體轉繁體，並改成台灣慣用詞），
// 見 docs/design.md 4.5 步驟 5。
//
// 用介面隔開，是因為測試不該依賴 OpenCC 的字典檔，使用者也可以關掉這個轉換。
#pragma once

#include <string>

namespace tmw::core {

class ITextConverter {
public:
    virtual ~ITextConverter() = default;
    virtual std::string convert(const std::string& text) const = 0;
};

// 原樣回傳
class NullTextConverter final : public ITextConverter {
public:
    std::string convert(const std::string& text) const override { return text; }
};

}  // namespace tmw::core
