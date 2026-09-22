#include "core/translation_alignment.h"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <utility>

namespace tmw::core {
namespace {

bool isBlank(std::string_view text) {
    for (const char c : text) {
        if (static_cast<unsigned char>(c) > ' ') {
            return false;
        }
    }
    return true;
}

// 去掉 Markdown 的程式碼區塊，回傳裡面的內容；沒有區塊時原樣回傳。
std::string_view stripCodeFence(std::string_view reply) {
    const std::size_t open = reply.find("```");
    if (open == std::string_view::npos) {
        return reply;
    }
    // ``` 後面可能接著語言名稱（```json），跳到那一行的結尾
    std::size_t start = reply.find('\n', open);
    if (start == std::string_view::npos) {
        return reply;
    }
    ++start;
    const std::size_t close = reply.find("```", start);
    if (close == std::string_view::npos) {
        return reply.substr(start);  // 只有開頭的 ```（回應被截斷）
    }
    return reply.substr(start, close - start);
}

// 找出第一個完整的 [...]。會跳過字串裡的括號和跳脫字元。
std::optional<std::string_view> findArray(std::string_view text) {
    const std::size_t begin = text.find('[');
    if (begin == std::string_view::npos) {
        return std::nullopt;
    }
    int depth = 0;
    bool inString = false;
    for (std::size_t i = begin; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\') {
                ++i;  // 跳過被跳脫的字元
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '[') {
            ++depth;
        } else if (c == ']') {
            --depth;
            if (depth == 0) {
                return text.substr(begin, i - begin + 1);
            }
        }
    }
    return std::nullopt;  // 沒有收尾（回應被截斷）
}

}  // namespace

std::optional<std::vector<std::string>> parseJsonArray(std::string_view reply) {
    const std::optional<std::string_view> array = findArray(stripCodeFence(reply));
    if (!array) {
        return std::nullopt;
    }
    const nlohmann::json parsed =
        nlohmann::json::parse(*array, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_array()) {
        return std::nullopt;
    }
    std::vector<std::string> out;
    out.reserve(parsed.size());
    for (const nlohmann::json& element : parsed) {
        if (element.is_string()) {
            out.push_back(element.get<std::string>());
        } else if (element.is_number() || element.is_boolean()) {
            // 原文是 "110" 這種純數字時，LLM 偶爾會回傳數字而不是字串
            out.push_back(element.dump());
        } else if (element.is_null()) {
            out.emplace_back();
        } else {
            return std::nullopt;  // 物件或巢狀陣列：格式不對，交給逐段重送
        }
    }
    return out;
}

std::vector<std::string> splitLines(std::string_view reply) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= reply.size()) {
        std::size_t end = reply.find('\n', start);
        if (end == std::string_view::npos) {
            end = reply.size();
        }
        std::string_view line = reply.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        out.emplace_back(line);
        if (end == reply.size()) {
            break;
        }
        start = end + 1;
    }
    // 最後多出來的一個空行是行尾的換行，不是一段
    if (out.size() > 1 && out.back().empty()) {
        out.pop_back();
    }
    return out;
}

std::vector<std::string> parseJsonArrayPrefix(std::string_view partial) {
    const std::string_view text = stripCodeFence(partial);
    const std::size_t begin = text.find('[');
    if (begin == std::string_view::npos) {
        return {};
    }
    // 掃到最後一個「已經收完」的字串，再自己補上 ]，就可以交給一般的解析
    std::size_t lastComplete = std::string_view::npos;
    bool inString = false;
    for (std::size_t i = begin + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\') {
                ++i;  // 跳過被跳脫的字元
            } else if (c == '"') {
                inString = false;
                lastComplete = i;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == ']') {
            lastComplete = i - 1;  // 整個陣列都收完了
            break;
        }
    }
    if (lastComplete == std::string_view::npos) {
        return {};
    }
    std::string complete(text.substr(begin, lastComplete - begin + 1));
    complete += ']';
    return parseJsonArray(complete).value_or(std::vector<std::string>{});
}

int countRubyMarkers(std::string_view text) {
    int count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '{') {
            continue;
        }
        const std::size_t close = text.find('}', i + 1);
        if (close == std::string_view::npos) {
            break;
        }
        const std::string_view inside = text.substr(i + 1, close - i - 1);
        if (inside.find('|') != std::string_view::npos) {
            ++count;
        }
        i = close;
    }
    return count;
}

std::string describeAlignmentProblem(AlignmentProblem problem) {
    switch (problem) {
        case AlignmentProblem::None:
            return "對齊";
        case AlignmentProblem::WrongCount:
            return "譯文數量和原文不同";
        case AlignmentProblem::EmptyText:
            return "有段落的譯文是空的";
        case AlignmentProblem::RubyMismatch:
            return "ルビ 標記的數量和原文不同";
    }
    return "未知";
}

AlignmentProblem checkAlignment(std::span<const std::string> sources,
                                std::span<const std::string> translations, bool checkRuby) {
    if (sources.size() != translations.size()) {
        return AlignmentProblem::WrongCount;
    }
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (!isBlank(sources[i]) && isBlank(translations[i])) {
            return AlignmentProblem::EmptyText;
        }
        if (checkRuby && countRubyMarkers(sources[i]) != countRubyMarkers(translations[i])) {
            return AlignmentProblem::RubyMismatch;
        }
    }
    return AlignmentProblem::None;
}

std::vector<std::string> translateAligned(std::span<const std::string> sources,
                                          const BatchTranslate& batch,
                                          const AlignOptions& options) {
    if (sources.empty()) {
        return {};
    }
    AlignmentProblem lastProblem = AlignmentProblem::None;
    for (int attempt = 0; attempt < options.batchAttempts; ++attempt) {
        try {
            std::vector<std::string> out = batch(sources);
            lastProblem = checkAlignment(sources, out, options.checkRubyMarkers);
            if (lastProblem == AlignmentProblem::None) {
                return out;
            }
        } catch (const TranslatorError& error) {
            if (error.kind() != TranslateError::BadResponse) {
                throw;
            }
            lastProblem = AlignmentProblem::WrongCount;
        }
    }
    if (sources.size() == 1) {
        // 已經是一段了，逐段重送不會有任何改變
        throw TranslatorError(TranslateError::BadResponse, describeAlignmentProblem(lastProblem));
    }

    std::vector<std::string> out;
    out.reserve(sources.size());
    for (const std::string& source : sources) {
        std::vector<std::string> one = batch(std::span<const std::string>(&source, 1));
        if (one.size() != 1) {
            throw TranslatorError(TranslateError::BadResponse, "逐段重送仍然對不上");
        }
        out.push_back(std::move(one.front()));
    }
    return out;
}

}  // namespace tmw::core
