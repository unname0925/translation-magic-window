#include "core/context_profile.h"

namespace tmw::core {

ProfileValues defaultProfile(std::string_view id) {
    ProfileValues values;
    if (id == "manga") {
        // 依對話框分段、直排用 manga-ocr；翻頁之後很快就靜止
        values.mangaMode = true;
        values.settleMs = 300;
    } else if (id == "game") {
        // 只看文字區域；對話框常貼著透鏡邊，不能把碰到邊緣的丟掉；打字機效果要等久一點
        values.gameMode = true;
        values.dropEdgeBlocks = false;
        values.settleMs = 600;
    } else if (id == "web") {
        // 捲動時邊緣的殘句不翻
        values.dropEdgeBlocks = true;
        values.settleMs = 400;
    }
    return values;
}

std::string profileName(std::string_view id) {
    if (id == "manga") {
        return "漫畫";
    }
    if (id == "game") {
        return "遊戲";
    }
    if (id == "web") {
        return "網頁";
    }
    return std::string(id);
}

}  // namespace tmw::core
