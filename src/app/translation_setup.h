// 依設定組出翻譯服務（見 docs/design.md 4.5）。
//
// 設定裡的引擎順序就是引擎鏈的順序。沒有設定任何引擎時用 Google
// （不用金鑰，M0-12 的評測中它比 LLM 差，但至少能動）。
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/clock.h"
#include "core/settings.h"
#include "core/translation_service.h"
#include "net/http_client.h"

namespace tmw::app {

struct TranslationSetup {
    std::shared_ptr<core::TranslationService> service;
    // 實際建立起來的引擎（依順序），給記錄檔和設定畫面看
    std::vector<std::string> engineIds;
    // 略過的引擎和原因（例如金鑰解不開、id 不認得）
    std::vector<std::string> problems;
};

// clock 必須活得比回傳的服務久。
// openccConfig 是 s2twp.json 的路徑；找不到時不轉繁體，並在 problems 中說明。
TranslationSetup makeTranslationService(const core::Settings& settings, const core::IClock& clock,
                                        const std::filesystem::path& openccConfig);

}  // namespace tmw::app
