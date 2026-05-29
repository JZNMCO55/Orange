#ifndef ORANGE_EDITOR_EDITOR_TEXT_UTIL_H
#define ORANGE_EDITOR_EDITOR_TEXT_UTIL_H

// EditorTextUtil —— 编辑器侧无依赖的小文本工具（header-only inline）。
//
// 抽出动机：Console 日志过滤 + Asset 浏览器名称搜索都需要"大小写不敏感子串
// 匹配"，原各自在 EditorRenderLayer.cpp 内重复；集中到本头复用 + 可单测
// （editor_text_util_test）。纯 std，零 ImGui/引擎依赖。

#include <algorithm>
#include <cctype>
#include <string_view>

namespace Orange::Editor::Util
{

// 大小写不敏感子串匹配。needle 为空 → true（视为"不过滤"，便于搜索框空串时
// 显示全部）。用 std::search + tolower 比较器，不预先 lowercase 整串（无分配）。
inline bool ContainsCaseInsensitive(std::string_view haystack,
                                    std::string_view needle)
{
    if (needle.empty()) { return true; }
    const auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a))
                 == std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

}  // namespace Orange::Editor::Util

#endif  // ORANGE_EDITOR_EDITOR_TEXT_UTIL_H
