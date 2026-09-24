#include "core/pipeline.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "core/ruby.h"
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

void Pipeline::rememberScript(int lens, Language script, const std::vector<OcrLine>& lines) {
    if (script == Language::Unknown) {
        return;  // 這次也判斷不出來（畫面上只有數字和符號），維持原樣
    }
    std::string text;
    for (const OcrLine& line : lines) {
        text += line.text;
    }
    const ScriptCounts counts = countScripts(text);
    // 這次讀出來的東西裡，完全沒有「這個判定該有的文字」，就表示畫面換語言了
    // （日文漫畫翻到韓文條漫）：忘掉，下一次重新判斷。否則會一直用錯的模型讀出一堆空字串。
    //
    // 每種判定各看各的文字：日文判定不能把拉丁字母算進去——韓文頁面上常有網址浮水印，
    // 日文模型讀韓文本文只會得到空字串，卻讀得到那串網址，算進去就永遠切不過去。
    const bool readSomething = [&counts, script] {
        switch (script) {
            case Language::Korean:
                return counts.hangul > 0;
            case Language::English:
                return counts.latin > 0;
            case Language::Japanese:
                return counts.kana + counts.han > 0;
            case Language::Unknown:
                break;
        }
        return false;
    }();
    memory(lens).script = readSomething ? script : Language::Unknown;
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
    // 沿用這個透鏡上一次判斷出來的語言：只有它是 Unknown 時，兩個辨識模型才都要跑
    OcrResult recognized = ocr_.recognize(job.frame, memory(job.lens).script, cancel);
    std::vector<OcrLine> lines = std::move(recognized.lines);
    rememberScript(job.lens, recognized.script, lines);
    result.timings.ocrMs = millisecondsSince(ocrStart);
    result.lines = lines;
    if (cancel.stop_requested()) {
        return result;
    }

    const auto layoutStart = std::chrono::steady_clock::now();
    // ルビ 要先附到本文上：否則它會夾在句子中間，把「這兩欄是同一句」的判斷擋掉
    // （design.md 4.4 的實測：400 組相鄰配對有 63% 因此合併失敗）
    const RubyResult withRuby = attachRuby(lines, options_.ruby);
    std::vector<TextBlock> blocks = mergeIntoBlocks(withRuby.lines, options_.merge);
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

    // 送出去翻譯的是帶 ルビ 標記的版本：`{本文|讀音}`（design.md 4.5）。
    // LLM 會把本文和讀音分別翻譯並保留標記，一般引擎看不懂就當成一般文字。
    std::vector<std::string> sources;
    sources.reserve(blocks.size());
    for (const TextBlock& block : blocks) {
        sources.push_back(markRuby(block.text, block.ruby));
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
