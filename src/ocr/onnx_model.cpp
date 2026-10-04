#include "ocr/onnx_model.h"

#include <windows.h>

#include <dml_provider_factory.h>
#include <dxgi.h>
#include <onnxruntime_cxx_api.h>

#include <stdexcept>
#include <string>

#include "ocr/onnx_internal.h"

namespace tmw::ocr {

Ort::Env& onnxEnvironment() {
    // 整個程式共用一個 Env（ONNX Runtime 的建議用法）。只記錄錯誤：
    // DirectML 每次建立工作階段都會警告「部分節點不在 GPU 上」，這是正常的
    // （和張量形狀有關的運算本來就放在 CPU 上）。
    static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "tmw");
    return env;
}

std::string onnxDisplayPath(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

std::string_view deviceName(Device device) {
    switch (device) {
        case Device::DirectML:
            return "dml";
        case Device::Auto:
            return "auto";
        case Device::Cpu:
            break;
    }
    return "cpu";
}

Device resolveDevice(Device requested, bool firstAdapterIsHardware) {
    if (requested != Device::Auto) {
        return requested;
    }
    return firstAdapterIsHardware ? Device::Auto : Device::Cpu;
}

bool firstAdapterIsHardware() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        return false;
    }
    bool hardware = false;
    IDXGIAdapter1* adapter = nullptr;
    // DirectML 的執行提供者用 device_id 0，也就是列舉出來的第一張
    if (SUCCEEDED(factory->EnumAdapters1(0, &adapter))) {
        DXGI_ADAPTER_DESC1 desc{};
        hardware =
            SUCCEEDED(adapter->GetDesc1(&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0;
        adapter->Release();
    }
    factory->Release();
    return hardware;
}

bool directMLAvailable() {
    // 和 ONNX Runtime 延遲載入時一樣的搜尋順序：先找程式資料夾，再找 System32。
    // 載入後就留著（之後 ONNX Runtime 會用到同一份）
    return LoadLibraryExW(L"DirectML.dll", nullptr, 0) != nullptr;
}

std::filesystem::path loadedDirectMLPath() {
    const HMODULE module = GetModuleHandleW(L"DirectML.dll");
    if (module == nullptr) {
        return {};
    }
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    return length == 0 ? std::filesystem::path() : std::filesystem::path(path);
}

struct OnnxModel::Impl {
    Device device = Device::Cpu;
    Ort::Session session{nullptr};
    std::string inputName;
    std::string outputName;
};

namespace {

// Auto：先試 DirectML。建立工作階段就會用到顯示卡和驅動，失敗表示這台電腦跑不了，改用 CPU。
Ort::Session createSession(const std::filesystem::path& onnxFile, Device device, bool optimizeGraph,
                           std::span<const FixedDimension> fixedDimensions) {
    Ort::SessionOptions options;
    if (!optimizeGraph) {
        options.SetGraphOptimizationLevel(ORT_DISABLE_ALL);
    }
    for (const FixedDimension& dimension : fixedDimensions) {
        options.AddFreeDimensionOverrideByName(dimension.name.c_str(), dimension.size);
    }
    if (device == Device::DirectML) {
        if (!directMLAvailable()) {
            // 不能讓 ONNX Runtime 去延遲載入：找不到 DLL 是無法攔截的當機
            throw Ort::Exception("DirectML.dll not found (Windows too old?)", ORT_FAIL);
        }
        // DirectML 的限制：不能用 memory pattern，也不能平行執行（見 design.md 4.4）
        options.DisableMemPattern();
        options.SetExecutionMode(ORT_SEQUENTIAL);
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, 0));
    }
    return Ort::Session(onnxEnvironment(), onnxFile.c_str(), options);
}

}  // namespace

Ort::Session createOnnxSession(const std::filesystem::path& onnxFile, Device& device,
                               bool optimizeGraph,
                               std::span<const FixedDimension> fixedDimensions) {
    const bool automatic = device == Device::Auto;
    device = automatic ? Device::DirectML : device;
    try {
        return createSession(onnxFile, device, optimizeGraph, fixedDimensions);
    } catch (const Ort::Exception&) {
        if (!automatic) {
            throw;  // 明確指定的裝置建立不起來就是失敗，不能默默換掉
        }
        // 沒有相容的顯示卡或驅動有問題：改用 CPU（design.md 4.4）
        device = Device::Cpu;
        return createSession(onnxFile, Device::Cpu, optimizeGraph, fixedDimensions);
    }
}

OnnxModel::OnnxModel(const std::filesystem::path& onnxFile, Device device,
                     std::string_view outputName, bool optimizeGraph,
                     std::span<const FixedDimension> fixedDimensions)
    : impl_(std::make_unique<Impl>()) {
    impl_->device = device;
    try {
        impl_->session = createOnnxSession(onnxFile, impl_->device, optimizeGraph, fixedDimensions);

        if (impl_->session.GetInputCount() != 1) {
            throw std::runtime_error("expected a model with one input");
        }
        Ort::AllocatorWithDefaultOptions allocator;
        impl_->inputName = impl_->session.GetInputNameAllocated(0, allocator).get();
        const std::size_t outputs = impl_->session.GetOutputCount();
        if (outputName.empty()) {
            if (outputs != 1) {
                throw std::runtime_error("expected a model with one output");
            }
            impl_->outputName = impl_->session.GetOutputNameAllocated(0, allocator).get();
        } else {
            for (std::size_t i = 0; i < outputs && impl_->outputName.empty(); ++i) {
                const std::string name = impl_->session.GetOutputNameAllocated(i, allocator).get();
                if (name == outputName) {
                    impl_->outputName = name;
                }
            }
            if (impl_->outputName.empty()) {
                throw std::runtime_error("the model has no output named " +
                                         std::string(outputName));
            }
        }
    } catch (const Ort::Exception& error) {
        throw std::runtime_error("cannot load " + onnxDisplayPath(onnxFile) + " on " +
                                 std::string(deviceName(device)) + ": " + error.what());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error("cannot load " + onnxDisplayPath(onnxFile) + ": " + error.what());
    }
}

OnnxModel::~OnnxModel() = default;
OnnxModel::OnnxModel(OnnxModel&&) noexcept = default;
OnnxModel& OnnxModel::operator=(OnnxModel&&) noexcept = default;

Device OnnxModel::device() const {
    return impl_->device;
}

std::vector<std::string> OnnxModel::inputDimensionNames() const {
    const Ort::TypeInfo type = impl_->session.GetInputTypeInfo(0);
    const auto info = type.GetTensorTypeAndShapeInfo();
    std::vector<const char*> names(info.GetDimensionsCount(), nullptr);
    info.GetSymbolicDimensions(names.data(), names.size());
    std::vector<std::string> out;
    out.reserve(names.size());
    for (const char* name : names) {
        out.emplace_back(name != nullptr ? name : "");
    }
    return out;
}

Tensor OnnxModel::run(std::span<const float> input, std::span<const std::int64_t> shape) {
    try {
        const Ort::MemoryInfo memory =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        // ONNX Runtime 不會修改輸入，但 API 需要非 const 指標
        const Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memory, const_cast<float*>(input.data()), input.size(), shape.data(), shape.size());
        const char* inputNames[] = {impl_->inputName.c_str()};
        const char* outputNames[] = {impl_->outputName.c_str()};
        std::vector<Ort::Value> outputs = impl_->session.Run(Ort::RunOptions{nullptr}, inputNames,
                                                             &inputTensor, 1, outputNames, 1);

        const Ort::TensorTypeAndShapeInfo info = outputs[0].GetTensorTypeAndShapeInfo();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw std::runtime_error("model output is not float");
        }
        Tensor result;
        result.shape = info.GetShape();
        const float* data = outputs[0].GetTensorData<float>();
        result.data.assign(data, data + info.GetElementCount());
        return result;
    } catch (const Ort::Exception& error) {
        throw std::runtime_error(std::string("inference failed: ") + error.what());
    }
}

}  // namespace tmw::ocr
