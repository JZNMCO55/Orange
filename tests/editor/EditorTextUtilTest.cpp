// EditorTextUtil::ContainsCaseInsensitive 单测。
//
// 该谓词被 Console 日志过滤 + Asset 浏览器名称搜索共用（gap 报告 §2.8 / §2.2），
// 抽到 header-only util 后在此锁住契约。纯 std header，无 ImGui/引擎依赖。

#include "EditorTextUtil.h"

#include <cassert>
#include <cstdio>

using Orange::Editor::Util::ContainsCaseInsensitive;

int main()
{
    // 空 needle = "不过滤" → 恒 true（搜索框空串时显示全部）。
    assert(ContainsCaseInsensitive("anything", ""));
    assert(ContainsCaseInsensitive("", ""));

    // 大小写不敏感命中。
    assert(ContainsCaseInsensitive("HelloWorld", "world"));
    assert(ContainsCaseInsensitive("HelloWorld", "HELLO"));
    assert(ContainsCaseInsensitive("foo.MATERIAL", "material"));
    assert(!ContainsCaseInsensitive("abcdef", "acf")); // 子串而非子序列：非连续不算命中

    // 子串边界。
    assert(ContainsCaseInsensitive("abc", "abc"));
    assert(ContainsCaseInsensitive("abc", "a"));
    assert(ContainsCaseInsensitive("abc", "c"));

    // 不命中。
    assert(!ContainsCaseInsensitive("hello", "xyz"));
    assert(!ContainsCaseInsensitive("", "x"));     // 空 haystack + 非空 needle
    assert(!ContainsCaseInsensitive("ab", "abc")); // needle 比 haystack 长

    std::printf("editor_text_util_test: all assertions passed\n");
    return 0;
}
