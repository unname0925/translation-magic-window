#include "ocr/onnx_session.h"

#include <array>
#include <stdexcept>
#include <string>

#include "ocr/onnx_internal.h"

namespace tmw::ocr {

struct OnnxSession::Impl {
    Device device = Device::Cpu;
    Ort::Session session{nullptr};
};

OnnxSession::OnnxSession(const std::filesystem::path& onnxFile, Device device)
    : impl_(std::make_unique<Impl>()) {
    impl_->device = device;
    try {
        impl_->session = createOnnxSession(onnxFile, impl_->device);
    } catch (const Ort::Exception& error) {
        throw std::runtime_error("cannot load " + onnxDisplayPath(onnxFile) + " on " +
                                 std::string(deviceName(device)) + ": " + error.what());
    }
}

OnnxSession::~OnnxSession() = default;

Device OnnxSession::device() const {
    return impl_->device;
}

std::vector<Tensor> OnnxSession::run(std::span<const NamedInput> inputs,
                                     std::span<const char* const> outputNames) {
    try {
        const Ort::MemoryInfo memory =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        // 元素數是 0 的張量（第一步的 K／V）也要給一個有效的指標
        static std::array<float, 1> emptyFloats{};
        static std::array<std::int64_t, 1> emptyIntegers{};

        std::vector<Ort::Value> values;
        std::vector<const char*> names;
        values.reserve(inputs.size());
        names.reserve(inputs.size());
        for (const NamedInput& input : inputs) {
            names.push_back(input.name);
            // ONNX Runtime 不會修改輸入，但 API 需要非 const 指標
            if (input.integer) {
                std::int64_t* data = input.integers.empty()
                                         ? emptyIntegers.data()
                                         : const_cast<std::int64_t*>(input.integers.data());
                values.push_back(Ort::Value::CreateTensor<std::int64_t>(
                    memory, data, input.integers.size(), input.shape.data(), input.shape.size()));
            } else {
                float* data = input.floats.empty() ? emptyFloats.data()
                                                   : const_cast<float*>(input.floats.data());
                values.push_back(Ort::Value::CreateTensor<float>(
                    memory, data, input.floats.size(), input.shape.data(), input.shape.size()));
            }
        }

        std::vector<Ort::Value> outputs =
            impl_->session.Run(Ort::RunOptions{nullptr}, names.data(), values.data(), values.size(),
                               outputNames.data(), outputNames.size());

        std::vector<Tensor> results;
        results.reserve(outputs.size());
        for (const Ort::Value& output : outputs) {
            const Ort::TensorTypeAndShapeInfo info = output.GetTensorTypeAndShapeInfo();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                throw std::runtime_error("model output is not float");
            }
            Tensor tensor;
            tensor.shape = info.GetShape();
            const float* data = output.GetTensorData<float>();
            tensor.data.assign(data, data + info.GetElementCount());
            results.push_back(std::move(tensor));
        }
        return results;
    } catch (const Ort::Exception& error) {
        throw std::runtime_error(std::string("inference failed: ") + error.what());
    }
}

}  // namespace tmw::ocr
