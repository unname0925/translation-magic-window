#include "core/pipeline.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "core/translator.h"

namespace tmw::core {
namespace {

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

std::string joinBlocks(const std::vector<TextBlock>& blocks) {
    std::string out;
    for (const TextBlock& block : blocks) {
        out += block.text;
        out += '\n';
    }
    return out;
}

}  // namespace

Pipeline::Pipeline(IOcrService& ocr, TranslationService& translation, PipelineOptions options)
    : ocr_(ocr), translation_(translation), options_(std::move(options)) {}

Pipeline::LensMemory& Pipeline::memory(int lens) {
    for (auto& [id, state] : memories_) {
        if (id == lens) {
            return state;
        }
    }
    memories_.emplace_back(lens, LensMemory{});
    return memories_.back().second;
}

void Pipeline::forget(int lens) {
    std::erase_if(memories_, [lens](const auto& entry) { return entry.first == lens; });
}

PipelineResult Pipeline::run(const PipelineJob& job, std::stop_token cancel) {
    PipelineResult result;
    result.generation = job.generation;
    result.lens = job.lens;
    result.region = job.region;
    if (cancel.stop_requested() || job.frame.empty()) {
        return result;
    }

    const auto ocrStart = std::chrono::steady_clock::now();
    std::vector<OcrLine> lines = ocr_.recognize(job.frame, cancel);
    result.timings.ocrMs = millisecondsSince(ocrStart);
    if (cancel.stop_requested()) {
        return result;
    }

    const auto layoutStart = std::chrono::steady_clock::now();
    std::vector<TextBlock> blocks = mergeIntoBlocks(lines, options_.merge);
    if (options_.dropEdgeBlocks) {
        blocks =
            dropEdgeBlocks(blocks, SizeI{job.frame.width, job.frame.height}, options_.edgeMargin);
    }
    result.timings.layoutMs = millisecondsSince(layoutStart);
    if (blocks.empty()) {
        // 透鏡底下沒有文字。記住這件事，畫面沒變時就不會一直重跑。
        LensMemory& state = memory(job.lens);
        result.unchanged = state.text.empty();
        state.text.clear();
        return result;
    }

    // 整片一起判斷語言：單一段落常常太短（M0-11）
    result.language = detectLanguage(joinBlocks(blocks));
    if (!job.language.empty() && job.language != "auto") {
        result.language = job.language == "ja"   ? Language::Japanese
                          : job.language == "en" ? Language::English
                          : job.language == "ko" ? Language::Korean
                                                 : result.language;
    }
    for (TextBlock& block : blocks) {
        block.language = result.language;
    }

    LensMemory& state = memory(job.lens);
    const std::string text = joinBlocks(blocks);
    result.unchanged = !text.empty() && text == state.text;
    state.text = text;

    std::vector<std::string> sources;
    sources.reserve(blocks.size());
    for (const TextBlock& block : blocks) {
        sources.push_back(block.text);
    }

    TranslateRequest request;
    request.srcLang = languageCode(result.language);
    if (request.srcLang.empty()) {
        request.srcLang = "auto";
    }
    request.context = state.recent;

    const auto translationStart = std::chrono::steady_clock::now();
    std::vector<std::string> translations;
    try {
        translations = translation_.translate(sources, request, cancel);
    } catch (const TranslatorError& error) {
        if (error.kind() == TranslateError::Cancelled) {
            return result;
        }
        // 翻譯失敗時仍然顯示原文，並在結果視窗標示原因
        result.error = describeTranslateError(error.kind()) + "：" + error.what();
        translations.assign(sources.size(), std::string());
    }
    result.timings.translationMs = millisecondsSince(translationStart);

    result.groups.reserve(blocks.size());
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        result.groups.push_back(TranslatedBlock{std::move(blocks[i]), std::move(translations[i])});
    }

    if (result.error.empty()) {
        // 留最後幾組當作下一次的上下文
        for (const TranslatedBlock& group : result.groups) {
            state.recent.emplace_back(group.block.text, group.translation);
        }
        if (state.recent.size() > options_.contextGroups) {
            state.recent.erase(
                state.recent.begin(),
                state.recent.end() - static_cast<std::ptrdiff_t>(options_.contextGroups));
        }
    }
    return result;
}

}  // namespace tmw::core
