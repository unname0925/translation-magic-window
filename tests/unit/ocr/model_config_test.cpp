#include "ocr/model_config.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace tmw::ocr {
namespace {

// 暫存資料夾中的 inference.yml，測試結束時刪除
class TempYaml {
public:
    explicit TempYaml(const std::string& content) {
        dir_ = std::filesystem::temp_directory_path() /
               ("tmw-ocr-test-" + std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(counter_++));
        std::filesystem::create_directories(dir_);
        std::ofstream(path(), std::ios::binary) << content;
    }
    ~TempYaml() {
        std::error_code ignored;
        std::filesystem::remove_all(dir_, ignored);
    }
    std::filesystem::path path() const { return dir_ / "inference.yml"; }

private:
    static inline int counter_ = 0;
    std::filesystem::path dir_;
};

// PaddleX 寫出的字元表格式：特殊字元加引號，單引號寫成 ''''，反斜線不加引號
constexpr char kRecognitionYaml[] = R"(Global:
  model_name: test_rec
PreProcess:
  transform_ops:
  - DecodeImage:
      channel_first: false
      img_mode: BGR
  - RecResizeImg:
      image_shape:
      - 3
      - 48
      - 320
PostProcess:
  name: CTCLabelDecode
  character_dict:
  - '!'
  - '"'
  - ''''
  - \
  - '#'
  - '0'
  - A
  - あ
  - 가
  - 繁
)";

TEST(ModelConfigTest, RecognitionDictionaryMatchesPaddleXOrder) {
    const TempYaml yaml(kRecognitionYaml);
    const RecognitionModelConfig config = loadRecognitionModelConfig(yaml.path());
    const std::vector<std::string> expected = {"blank", "!", "\"", "'",  "\\", "#",
                                               "0",     "A", "あ", "가", "繁", " "};
    EXPECT_EQ(config.characters, expected);
    EXPECT_EQ(config.imageHeight, 48);
    EXPECT_EQ(config.imageWidth, 320);
}

TEST(ModelConfigTest, RecognitionRequiresCtcDecoder) {
    const TempYaml yaml("PostProcess:\n  name: AttnLabelDecode\n  character_dict:\n  - a\n");
    EXPECT_THROW(loadRecognitionModelConfig(yaml.path()), std::runtime_error);
}

TEST(ModelConfigTest, RecognitionRequiresDictionary) {
    const TempYaml yaml("PostProcess:\n  name: CTCLabelDecode\n");
    EXPECT_THROW(loadRecognitionModelConfig(yaml.path()), std::runtime_error);
}

TEST(ModelConfigTest, MissingFileThrows) {
    EXPECT_THROW(loadRecognitionModelConfig("Z:/does/not/exist/inference.yml"), std::runtime_error);
}

TEST(ModelConfigTest, InvalidYamlThrows) {
    const TempYaml yaml("PostProcess: [unclosed\n");
    EXPECT_THROW(loadRecognitionModelConfig(yaml.path()), std::runtime_error);
}

TEST(ModelConfigTest, EmptyOrNonMappingFileThrows) {
    const TempYaml empty("");
    EXPECT_THROW(loadDetectionModelConfig(empty.path()), std::runtime_error);
    const TempYaml list("- a\n- b\n");
    EXPECT_THROW(loadRecognitionModelConfig(list.path()), std::runtime_error);
}

TEST(ModelConfigTest, DetectionNormalizationIsRead) {
    const TempYaml yaml(R"(PreProcess:
  transform_ops:
  - NormalizeImage:
      mean: [0.5, 0.25, 0.125]
      std: [0.1, 0.2, 0.4]
      scale: 1./255.
      order: hwc
)");
    const DetectionModelConfig config = loadDetectionModelConfig(yaml.path());
    EXPECT_DOUBLE_EQ(config.mean[1], 0.25);
    EXPECT_DOUBLE_EQ(config.std[2], 0.4);
    EXPECT_DOUBLE_EQ(config.scale, 1.0 / 255.0);
}

TEST(ModelConfigTest, DetectionDefaultsWithoutNormalizeStep) {
    const TempYaml yaml("PostProcess:\n  name: DBPostProcess\n");
    const DetectionModelConfig config = loadDetectionModelConfig(yaml.path());
    EXPECT_DOUBLE_EQ(config.mean[0], 0.485);
    EXPECT_DOUBLE_EQ(config.scale, 1.0 / 255.0);
}

TEST(ModelConfigTest, ParseScale) {
    EXPECT_DOUBLE_EQ(parseScale("1./255."), 1.0 / 255.0);
    EXPECT_DOUBLE_EQ(parseScale("0.5"), 0.5);
    EXPECT_THROW(parseScale("abc"), std::runtime_error);
}

}  // namespace
}  // namespace tmw::ocr
