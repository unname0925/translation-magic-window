#include "ocr/onnx_model.h"

#include <windows.h>

#include <dml_provider_factory.h>
#include <onnxruntime_cxx_api.h>

#include <stdexcept>
#include <string>

namespace tmw::ocr {
namespace {

Ort::Env& environment() {
    // 整個程式共用一個 Env（ONNX Runtime 的建議用法）。只記錄錯誤：
    // DirectML 每次建立工作階段都會警告「部分節點不在 GPU 上」，這是正常的
    // （和張量形狀有關的運算本來就放在 CPU 上）。
    static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "tmw");
    return env;
}

std::string displayPath(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

}  // namespace

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

struct OnnxModel::Impl {
    Device device = Device::Cpu;
    Ort::Session session{nullptr};
    std::string inputName;
    std::string outputName;
};

namespace {

// Auto：先試 DirectML。建立工作階段就會用到顯示卡和驅動，失敗表示這台電腦跑不了，改用 CPU。
Ort::Session createSession(const std::filesystem::path& onnxFile, Device device) {
    Ort::SessionOptions options;
    if (device == Device::DirectML) {
        // DirectML 的限制：不能用 memory pattern，也不能平行執行（見 design.md 4.4）
        options.DisableMemPattern();
        options.SetExecutionMode(ORT_SEQUENTIAL);
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, 0));
    }
    return Ort::Session(environment(), onnxFile.c_str(), options);
}

}  // namespace

OnnxModel::OnnxModel(const std::filesystem::path& onnxFile, Device device)
    : impl_(std::make_unique<Impl>()) {
    impl_->device = device == Device::Auto ? Device::DirectML : device;
    try {
        try {
            impl_->session = createSession(onnxFile, impl_->device);
        } catch (const Ort::Exception&) {
            if (device != Device::Auto) {
                throw;
            }
            // 沒有相容的顯示卡或驅動有問題：改用 CPU（design.md 4.4）
            impl_->device = Device::Cpu;
            impl_->session = createSession(onnxFile, Device::Cpu);
        }

        if (impl_->session.GetInputCount() != 1 || impl_->session.GetOutputCount() != 1) {
            throw std::runtime_error("expected a model with one input and one output");
        }
        Ort::AllocatorWithDefaultOptions allocator;
        impl_->inputName = impl_->session.GetInputNameAllocated(0, allocator).get();
        impl_->outputName = impl_->session.GetOutputNameAllocated(0, allocator).get();
    } catch (const Ort::Exception& error) {
        throw std::runtime_error("cannot load " + displayPath(onnxFile) + " on " +
                                 std::string(deviceName(device)) + ": " + error.what());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error("cannot load " + displayPath(onnxFile) + ": " + error.what());
    }
}

OnnxModel::~OnnxModel() = default;

Device OnnxModel::device() const {
    return impl_->device;
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
