#include "ocr/model_config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace tmw::ocr {
namespace {

// 錯誤訊息用的路徑（UTF-8）。path::string() 遇到非 ASCII 字元可能丟出例外。
std::string displayPath(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

YAML::Node loadYaml(const std::filesystem::path& path) {
    // 用 std::ifstream 讀檔，路徑有中文也沒問題（YAML::LoadFile 只接受窄字元路徑）
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open " + displayPath(path));
    }
    std::ostringstream content;
    content << stream.rdbuf();
    YAML::Node root;
    try {
        root = YAML::Load(content.str());
    } catch (const YAML::Exception& error) {
        throw std::runtime_error("invalid YAML in " + displayPath(path) + ": " + error.what());
    }
    if (!root.IsMap()) {
        throw std::runtime_error(displayPath(path) + " is not a YAML mapping");
    }
    return root;
}

// PreProcess.transform_ops 中名為 name 的步驟；找不到時回傳空節點。
// yaml-cpp 對不存在的節點再用 [] 取值會丟出例外，所以每一層都要先檢查。
YAML::Node findTransform(const YAML::Node& root, const std::string& name) {
    const YAML::Node preProcess = root["PreProcess"];
    if (!preProcess || !preProcess.IsMap()) {
        return YAML::Node(YAML::NodeType::Undefined);
    }
    const YAML::Node ops = preProcess["transform_ops"];
    if (ops && ops.IsSequence()) {
        for (const YAML::Node& op : ops) {
            if (op.IsMap() && op[name]) {
                return op[name];
            }
        }
    }
    return YAML::Node(YAML::NodeType::Undefined);
}

}  // namespace

double parseScale(const std::string& text) {
    const auto slash = text.find('/');
    try {
        if (slash == std::string::npos) {
            return std::stod(text);
        }
        return std::stod(text.substr(0, slash)) / std::stod(text.substr(slash + 1));
    } catch (const std::logic_error&) {
        throw std::runtime_error("invalid scale: " + text);
    }
}

DetectionModelConfig loadDetectionModelConfig(const std::filesystem::path& inferenceYml) {
    const YAML::Node root = loadYaml(inferenceYml);
    DetectionModelConfig config;
    const YAML::Node normalize = findTransform(root, "NormalizeImage");
    if (!normalize) {
        return config;
    }
    try {
        if (normalize["mean"]) {
            const auto mean = normalize["mean"].as<std::vector<double>>();
            if (mean.size() != 3) {
                throw std::runtime_error("NormalizeImage.mean must have 3 values");
            }
            std::copy(mean.begin(), mean.end(), config.mean.begin());
        }
        if (normalize["std"]) {
            const auto std = normalize["std"].as<std::vector<double>>();
            if (std.size() != 3) {
                throw std::runtime_error("NormalizeImage.std must have 3 values");
            }
            std::copy(std.begin(), std.end(), config.std.begin());
        }
        if (normalize["scale"]) {
            config.scale = parseScale(normalize["scale"].as<std::string>());
        }
    } catch (const YAML::Exception& error) {
        throw std::runtime_error("invalid NormalizeImage in " + displayPath(inferenceYml) + ": " +
                                 error.what());
    }
    return config;
}

RecognitionModelConfig loadRecognitionModelConfig(const std::filesystem::path& inferenceYml) {
    const YAML::Node root = loadYaml(inferenceYml);
    RecognitionModelConfig config;
    try {
        const YAML::Node resize = findTransform(root, "RecResizeImg");
        if (resize && resize["image_shape"]) {
            const auto shape = resize["image_shape"].as<std::vector<int>>();
            if (shape.size() != 3 || shape[1] <= 0 || shape[2] <= 0) {
                throw std::runtime_error("RecResizeImg.image_shape must be [C, H, W]");
            }
            config.imageHeight = shape[1];
            config.imageWidth = shape[2];
        }

        const YAML::Node postProcess = root["PostProcess"];
        if (!postProcess || postProcess["name"].as<std::string>("") != "CTCLabelDecode") {
            throw std::runtime_error("PostProcess must be CTCLabelDecode");
        }
        const YAML::Node dictionary = postProcess["character_dict"];
        if (!dictionary || !dictionary.IsSequence() || dictionary.size() == 0) {
            throw std::runtime_error("PostProcess.character_dict is missing");
        }
        config.characters.reserve(dictionary.size() + 2);
        config.characters.emplace_back("blank");
        for (const YAML::Node& entry : dictionary) {
            config.characters.push_back(entry.as<std::string>());
        }
        // PaddleX 的 CTCLabelDecode 預設 use_space_char=True
        config.characters.emplace_back(" ");
    } catch (const YAML::Exception& error) {
        throw std::runtime_error("invalid recognition config in " + displayPath(inferenceYml) +
                                 ": " + error.what());
    }
    return config;
}

}  // namespace tmw::ocr
