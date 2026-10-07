#include "core/pipeline.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "core/overlay_plan.h"
#include "core/ruby.h"
#include "core/translator.h"
#include "core/utf8.h"

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
    // map 新增元素不會讓已經拿到的參照失效：第一段和翻譯段可以在不同執行緒用同一個透鏡的記憶
    const std::lock_guard lock(memoryMutex_);
    return memories_[lens];
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

bool shouldRereadWithMangaOcr(const TextBlock& block) {
    return block.orientation == Orientation::Vertical && block.rect.width() > 0 &&
           block.rect.height() <= 10 * block.rect.width();
}

int mangaOcrCharacterLimit(std::string_view ppOcrText) {
    return 2 * characterCount(ppOcrText) + 16;
}

void Pipeline::rereadMangaBlocks(const PipelineJob& job, std::vector<TextBlock>& blocks,
                                 std::stop_token cancel) {
    LensMemory& state = memory(job.lens);
    // 畫面沒變的段落沿用上一次的結果，其餘一起交給 manga-ocr（一批一起讀比一段一段快很多）
    std::vector<std::size_t> fresh;
    std::vector<RereadRequest> requests;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (shouldRereadWithMangaOcr(blocks[i]) && !state.reread.contains(blocks[i].text)) {
            fresh.push_back(i);
            requests.push_back({blocks[i].rect, mangaOcrCharacterLimit(blocks[i].text)});
        }
    }
    std::vector<std::optional<std::string>> results;
    if (!requests.empty()) {
        results = ocr_.reread(job.frame, requests, cancel);
        results.resize(requests.size());
    }
    if (cancel.stop_requested()) {
        return;
    }

    std::map<std::string, std::string> kept;
    std::size_t next = 0;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        TextBlock& block = blocks[i];
        if (!shouldRereadWithMangaOcr(block)) {
            continue;
        }
        std::string reread;
        if (next < fresh.size() && fresh[next] == i) {
            std::optional<std::string>& result = results[next++];
            if (!result || result->empty()) {
                continue;  // 不支援、模型沒載入或讀不出東西：沿用 PP-OCR 的文字
            }
            reread = std::move(*result);
        } else if (const auto found = state.reread.find(block.text); found != state.reread.end()) {
            reread = found->second;  // 畫面沒變：沿用上一次的結果
        } else {
            continue;
        }
        kept.emplace(block.text, reread);
        // ルビ 的位置是 PP-OCR 那個版本的第幾個字，要搬到新的文字上（找不到本文的丟掉）
        block.ruby = remapRuby(block.text, block.ruby, reread);
        block.text = std::move(reread);
    }
    state.reread = std::move(kept);
}

void Pipeline::forget(int lens) {
    const std::lock_guard lock(memoryMutex_);
    std::erase_if(memories_, [lens](const auto& entry) { return entry.first == lens; });
}

Pipeline::RecognizedPage Pipeline::recognize(const PipelineJob& job, std::stop_token cancel) {
    RecognizedPage page;
    PipelineResult& result = page.result;
    result.generation = job.generation;
    result.lens = job.lens;
    result.region = job.region;
    if (cancel.stop_requested() || job.frame.empty()) {
        return page;
    }

    const auto ocrStart = std::chrono::steady_clock::now();
    // 使用者指定了語言：只用那個模型，不判斷也不記。
    // 否則沿用這個透鏡上一次判斷出來的語言：只有它是 Unknown 時才要判斷。
    const Language forced = languageFromCode(job.language);
    OcrResult recognized;
    // 畫面等穩定時已經先做好了，之後畫面也沒變：直接沿用（速度優化 4）
    std::optional<LensMemory::Prepared> prepared = std::move(memory(job.lens).prepared);
    memory(job.lens).prepared.reset();
    const SizeI frameSize{job.frame.width, job.frame.height};
    if (job.usePrepared != 0 && prepared && prepared->ticket == job.usePrepared &&
        prepared->language == job.language && prepared->frameSize == frameSize) {
        recognized = std::move(prepared->ocr);
        result.timings.ocrPrepared = true;
    } else {
        const Language script = forced != Language::Unknown ? forced : memory(job.lens).script;
        recognized = job.manga ? ocr_.recognizeManga(job.frame, script, cancel)
                               : ocr_.recognize(job.frame, script, cancel);
        if (forced == Language::Unknown) {
            rememberScript(job.lens, recognized.script, recognized.lines);
        }
        if (job.prepareTicket != 0 && !cancel.stop_requested()) {
            memory(job.lens).prepared =
                LensMemory::Prepared{job.prepareTicket, job.language, frameSize, recognized};
        }
    }
    std::vector<OcrLine> lines = std::move(recognized.lines);
    result.timings.ocrMs = millisecondsSince(ocrStart);
    result.lines = lines;
    if (cancel.stop_requested()) {
        return page;
    }

    const auto layoutStart = std::chrono::steady_clock::now();
    // ルビ 要先附到本文上：否則它會夾在句子中間，把「這兩欄是同一句」的判斷擋掉
    // （design.md 4.4 的實測：400 組相鄰配對有 63% 因此合併失敗）
    const RubyResult withRuby = attachRuby(lines, options_.ruby);
    // 漫畫模式時 OCR 會一併找出對話框：同一個對話框裡的行就是同一段（M2-02）。
    // 沒有對話框時和只看距離的分段完全一樣。
    std::vector<TextBlock> blocks =
        mergeIntoBlocks(withRuby.lines, recognized.bubbles, options_.merge);
    // 和整個畫面比，所以在去掉邊緣的殘句之前分級（M2-17）
    classifyTextSize(blocks);
    if (options_.dropEdgeBlocks) {
        blocks =
            dropEdgeBlocks(blocks, SizeI{job.frame.width, job.frame.height}, options_.edgeMargin);
    }
    markSoundEffects(blocks, recognized.bubbles);
    if (!job.soundEffects) {
        std::erase_if(blocks, [](const TextBlock& block) { return block.soundEffect; });
    }
    if (job.overlayOnly) {
        // 花紋、雨線被讀成字的這類段落，以前都先翻譯、蓋的時候才丟掉
        // （網頁漫畫 34 頁實測 306 段裡有 62 段）
        std::erase_if(blocks, [&](const TextBlock& block) {
            switch (coverDecision(job.frame, block, job.inpainter != nullptr)) {
                case CoverDecision::Symbols:
                    ++result.overlayDrops.symbols;
                    return true;
                case CoverDecision::LowScore:
                    ++result.overlayDrops.lowScore;
                    return true;
                case CoverDecision::BusyBackground:
                    ++result.overlayDrops.busyBackground;
                    return true;
                case CoverDecision::Cover:
                    break;
            }
            return false;
        });
    }
    result.timings.layoutMs = millisecondsSince(layoutStart);
    if (job.prepareTicket != 0 && blocks.empty()) {
        return page;  // 預先做：不記「沒有文字」，正式處理時才記
    }
    if (blocks.empty()) {
        // 透鏡底下沒有文字。記住這件事，畫面沒變時就不會一直重跑。
        LensMemory& state = memory(job.lens);
        result.unchanged = state.text.empty();
        state.text.clear();
        return page;
    }

    // 整片一起判斷語言：單一段落常常太短（M0-11）
    result.language = detectLanguage(joinBlocks(blocks));
    if (forced != Language::Unknown) {
        result.language = forced;
    }
    for (TextBlock& block : blocks) {
        block.language = result.language;
    }

    if (job.prepareTicket != 0) {
        // 預先做：manga-ocr 也先重讀（結果記在 LensMemory::reread，正式處理時直接沿用），
        // 不記原文、不翻譯
        if (result.language == Language::Japanese && !recognized.bubbles.empty()) {
            rereadMangaBlocks(job, blocks, cancel);
        }
        return page;
    }

    LensMemory& state = memory(job.lens);
    const std::string text = joinBlocks(blocks);
    result.unchanged = !text.empty() && text == state.text;
    state.text = text;

    // 漫畫模式（OCR 找到了對話框）的日文：直排對白換成 manga-ocr 重讀的文字（M2-03）。
    // 在漫畫模式分出來的區塊上實測，字元錯誤率 PP-OCR 10.4%、manga-ocr 5.0%。
    if (result.language == Language::Japanese && !recognized.bubbles.empty()) {
        const auto rereadStart = std::chrono::steady_clock::now();
        rereadMangaBlocks(job, blocks, cancel);
        result.timings.rereadMs = millisecondsSince(rereadStart);
        result.timings.ocrMs += result.timings.rereadMs;
        if (cancel.stop_requested()) {
            return page;
        }
    }

    // 送出去翻譯的是帶 ルビ 標記的版本：`{本文|讀音}`（design.md 4.5）。
    // LLM 會把本文和讀音分別翻譯並保留標記，一般引擎看不懂就當成一般文字。
    // 只標記作者刻意的特殊讀音：一般的振り仮名標了沒有意義，還會害 LLM 整句不翻（M2-14）。
    std::vector<std::string> sources;
    sources.reserve(blocks.size());
    for (const TextBlock& block : blocks) {
        sources.push_back(markSpecialRuby(block.text, block.ruby, options_.furigana.get()));
    }

    TranslateRequest request;
    request.srcLang = languageCode(result.language);
    if (request.srcLang.empty()) {
        request.srcLang = "auto";
    }
    {
        const std::lock_guard lock(memoryMutex_);
        request.context = state.recent;  // 好幾頁同時翻譯時，finish 會在別的執行緒寫它
    }
    if (job.glossary != nullptr) {
        request.glossary = glossaryFor(sources, *job.glossary);
    }

    if (options_.terms != nullptr && !job.termScope.empty()) {
        std::string pageText;
        for (const TextBlock& block : blocks) {
            if (!block.soundEffect) {  // 擬聲字裡的片假名不是名字
                pageText += block.text;
                pageText += '\n';
            }
        }
        page.terms = findTerms(pageText);
    }
    page.blocks = std::move(blocks);
    page.sources = std::move(sources);
    page.request = std::move(request);
    page.done = false;
    return page;
}

std::vector<std::pair<std::string, std::string>> Pipeline::contextBefore(const PipelineJob& job) {
    LensMemory& state = memory(job.lens);  // memory() 自己會鎖，先拿參照再鎖
    const std::lock_guard lock(memoryMutex_);
    std::vector<std::pair<std::string, std::string>> context;
    const auto chapter = state.chapters.find(job.chapter);
    if (chapter == state.chapters.end()) {
        return context;  // 這一章的第一頁
    }
    // 從最靠近的前一頁往前拿，湊滿 contextGroups 組，再排回閱讀順序
    for (auto page = std::make_reverse_iterator(chapter->second.lower_bound(job.pageIndex));
         page != chapter->second.rend() && context.size() < options_.contextGroups; ++page) {
        for (auto group = page->second.rbegin();
             group != page->second.rend() && context.size() < options_.contextGroups; ++group) {
            context.push_back(*group);
        }
    }
    std::reverse(context.begin(), context.end());
    return context;
}

void Pipeline::applyTerms(const std::vector<std::string>& terms, const PipelineJob& job,
                          TranslateRequest& request, std::stop_token cancel) {
    const std::vector<std::string> unknown = options_.terms->missing(job.termScope, terms);
    if (!unknown.empty()) {
        // 名字單獨翻：不帶上下文；使用者的專有名詞表照樣用
        TranslateRequest names;
        names.srcLang = request.srcLang;
        names.dstLang = request.dstLang;
        if (job.glossary != nullptr) {
            names.glossary = glossaryFor(unknown, *job.glossary);
        }
        try {
            const std::vector<std::string> translated =
                translation_.translate(unknown, names, cancel);
            std::map<std::string, std::string> learned;
            for (std::size_t i = 0; i < unknown.size() && i < translated.size(); ++i) {
                learned.emplace(unknown[i], translated[i]);
            }
            options_.terms->remember(job.termScope, learned);
        } catch (const TranslatorError&) {
            // 翻不了就算了：這一頁照常翻，名字下次再記
        }
    }
    // 使用者的專有名詞表優先（emplace 不蓋掉已經有的）
    for (auto& [term, translation] : options_.terms->lookup(job.termScope, terms)) {
        request.glossary.emplace(term, std::move(translation));
    }
}

PipelineResult Pipeline::finish(RecognizedPage page, const PipelineJob& job,
                                std::stop_token cancel) {
    PipelineResult result = std::move(page.result);
    std::vector<TextBlock> blocks = std::move(page.blocks);
    const std::vector<std::string> sources = std::move(page.sources);
    TranslateRequest request = std::move(page.request);
    if (cancel.stop_requested()) {
        return result;
    }
    if (!page.terms.empty()) {
        applyTerms(page.terms, job, request, cancel);
    }
    result.glossary = request.glossary;
    if (job.pageIndex >= 0) {
        request.context = contextBefore(job);  // 照頁序：這一頁之前的頁
    }
    const auto translationStart = std::chrono::steady_clock::now();
    std::vector<std::string> translations;
    try {
        translations = translation_.translate(sources, request, cancel, &result.notice);
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
    const auto overlayStart = std::chrono::steady_clock::now();
    result.overlay =
        planOverlay(job.frame, result.groups, job.inpainter.get(), &result.overlayDrops);
    result.timings.overlayMs = millisecondsSince(overlayStart);

    if (result.error.empty()) {
        LensMemory& state = memory(job.lens);
        const std::lock_guard lock(memoryMutex_);
        // 留最後幾組當作下一次的上下文
        for (const TranslatedBlock& group : result.groups) {
            state.recent.emplace_back(group.block.text, group.translation);
        }
        if (state.recent.size() > options_.contextGroups) {
            state.recent.erase(
                state.recent.begin(),
                state.recent.end() - static_cast<std::ptrdiff_t>(options_.contextGroups));
        }
        if (job.pageIndex >= 0) {
            if (!state.chapters.contains(job.chapter)) {
                state.chapterOrder.push_back(job.chapter);
                if (state.chapterOrder.size() > kRememberedChapters) {
                    state.chapters.erase(state.chapterOrder.front());
                    state.chapterOrder.erase(state.chapterOrder.begin());
                }
            }
            auto& done = state.chapters[job.chapter][job.pageIndex];
            done.clear();
            for (const TranslatedBlock& group : result.groups) {
                done.emplace_back(group.block.text, group.translation);
            }
        }
    }
    return result;
}

PipelineResult Pipeline::run(const PipelineJob& job, std::stop_token cancel) {
    RecognizedPage page = recognize(job, cancel);
    if (page.done) {
        return std::move(page.result);
    }
    return finish(std::move(page), job, cancel);
}

}  // namespace tmw::core
