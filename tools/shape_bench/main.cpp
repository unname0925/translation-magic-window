// tmw_shape_bench：量測同一個推論工作階段在不同輸入形狀下的耗時。
//
//   tmw_shape_bench --model <inference.onnx> [--device cpu|dml] [--repeat N] [--rounds R]
//                   [--dim 名稱=值] <形狀> [<形狀> ...]
//
// 形狀寫成 1x3x48x160。工具會依序對每個形狀跑 repeat 次（第一次暖機不計），
// 然後整組再跑 rounds 輪，用來觀察「換過形狀之後，原本的形狀會不會變慢」。
//
// --dim 名稱=值：建立工作階段時就把自由維度固定住（ORT 的 AddFreeDimensionOverrideByName），
// 用來驗證官方文件說的「形狀在建立時就知道，DirectML 才能先最佳化」。
// PP-OCR 辨識模型的維度名稱是 DynamicDimension.0（批次）和 DynamicDimension.1（寬度）。
//
// M1-03 用它找出「一個工作階段只有第一種輸入大小跑得快」的原因（見 docs/design.md 第 5 節）。

#include <windows.h>

#include <dml_provider_factory.h>
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Arguments {
    std::wstring model;
    bool directml = true;
    int repeat = 5;
    int rounds = 2;
    std::vector<std::pair<std::string, std::int64_t>> freeDims;  // --dim 名稱=值
    std::vector<std::vector<std::int64_t>> shapes;
};

std::vector<std::int64_t> parseShape(const std::string& text) {
    std::vector<std::int64_t> shape;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('x', start);
        const std::string part = text.substr(start, end - start);
        if (!part.empty()) {
            shape.push_back(std::stoll(part));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return shape;
}

std::string narrow(const std::wstring& text) {
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size > 0 ? size - 1 : 0, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::string shapeText(const std::vector<std::int64_t>& shape) {
    std::string text;
    for (std::size_t i = 0; i < shape.size(); ++i) {
        text += (i == 0 ? "" : "x") + std::to_string(shape[i]);
    }
    return text;
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Arguments args;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == L"--model" && hasValue) {
            args.model = argv[++i];
        } else if (arg == L"--device" && hasValue) {
            args.directml = std::wstring(argv[++i]) != L"cpu";
        } else if (arg == L"--repeat" && hasValue) {
            args.repeat = std::max(1, _wtoi(argv[++i]));
        } else if (arg == L"--rounds" && hasValue) {
            args.rounds = std::max(1, _wtoi(argv[++i]));
        } else if (arg == L"--dim" && hasValue) {
            const std::string entry = narrow(argv[++i]);
            const std::size_t equals = entry.find('=');
            if (equals != std::string::npos) {
                args.freeDims.emplace_back(entry.substr(0, equals),
                                           std::stoll(entry.substr(equals + 1)));
            }
        } else {
            args.shapes.push_back(parseShape(narrow(arg)));
        }
    }
    if (args.model.empty() || args.shapes.empty()) {
        std::fputs(
            "usage: tmw_shape_bench --model <onnx> [--device cpu|dml] [--repeat N]\n"
            "                       [--rounds R] [--dim 名稱=值] 1x3x48x160 ...\n",
            stderr);
        return 2;
    }

    Ort::Env environment(ORT_LOGGING_LEVEL_ERROR, "bench");
    Ort::SessionOptions options;
    if (args.directml) {
        options.DisableMemPattern();
        options.SetExecutionMode(ORT_SEQUENTIAL);
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, 0));
    }
    for (const auto& [name, value] : args.freeDims) {
        std::printf("固定 %s = %lld\n", name.c_str(), static_cast<long long>(value));
        options.AddFreeDimensionOverrideByName(name.c_str(), value);
    }

    const auto loadStart = std::chrono::steady_clock::now();
    Ort::Session session(environment, args.model.c_str(), options);
    const double loadMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadStart)
            .count();
    Ort::AllocatorWithDefaultOptions allocator;
    const std::string inputName = session.GetInputNameAllocated(0, allocator).get();
    const std::string outputName = session.GetOutputNameAllocated(0, allocator).get();
    const char* inputNames[] = {inputName.c_str()};
    const char* outputNames[] = {outputName.c_str()};

    std::printf("model loaded in %.0f ms (device=%s, free-dims=%s)\n", loadMs,
                args.directml ? "dml" : "cpu", args.freeDims.empty() ? "no" : "yes");

    const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    for (int round = 0; round < args.rounds; ++round) {
        std::printf("-- 第 %d 輪\n", round + 1);
        for (const std::vector<std::int64_t>& shape : args.shapes) {
            const std::size_t count = static_cast<std::size_t>(
                std::accumulate(shape.begin(), shape.end(), std::int64_t{1}, std::multiplies<>()));
            std::vector<float> input(count, 0.5f);
            std::vector<double> times;
            double first = 0.0;
            for (int i = 0; i <= args.repeat; ++i) {
                Ort::Value tensor = Ort::Value::CreateTensor<float>(
                    memory, input.data(), input.size(), shape.data(), shape.size());
                const auto start = std::chrono::steady_clock::now();
                session.Run(Ort::RunOptions{nullptr}, inputNames, &tensor, 1, outputNames, 1);
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - start)
                                      .count();
                if (i == 0) {
                    first = ms;  // 暖機
                } else {
                    times.push_back(ms);
                }
            }
            std::printf("   %-16s 第一次 %7.1f ms，之後中位數 %7.1f ms\n", shapeText(shape).c_str(),
                        first, median(times));
        }
    }
    return 0;
}
