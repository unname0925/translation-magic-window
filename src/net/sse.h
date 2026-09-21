// Server-Sent Events：串流回應的格式（OpenAI 相容 API 的 stream: true）。
//
// 內容是一行一行的 `data: {...}`，空行分隔事件，最後是 `data: [DONE]`。
// 位元組是任意切開送來的（一個字元也可能被切成兩半），所以要自己接回去。
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tmw::net {

// 從收到的位元組中取出已經完整的 data: 內容，剩下不完整的留在 buffer 裡等下一批。
// `[DONE]` 和註解、其他欄位都會被略過。
std::vector<std::string> takeSseData(std::string& buffer);

}  // namespace tmw::net
