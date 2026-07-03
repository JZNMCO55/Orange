// TypedHandle 的最小单元测试：验证类型隔离 / 默认 invalid / 比较运算
// / std::hash 特化。

#include <orange/engine/core/Handle.h>

#include <cassert>
#include <cstdio>
#include <type_traits>
#include <unordered_map>

using Orange::Engine::TypedHandle;

namespace
{

    struct MeshTag
    {
    };
    struct TextureTag
    {
    };

    using MeshHandle    = TypedHandle<MeshTag>;
    using TextureHandle = TypedHandle<TextureTag>;

    // 类型隔离：同一个 64-bit 值，套上不同 phantom tag 后是不同的 C++
    // 类型，不能互相赋值。这一条用 static_assert 即可在编译期保住。
    static_assert(!std::is_same_v<MeshHandle, TextureHandle>,
                  "TypedHandle 的不同 Tag 必须产出不同的类型。");
    static_assert(!std::is_convertible_v<MeshHandle, TextureHandle>,
                  "TypedHandle 不应在不同 Tag 之间隐式转换。");

    void TestDefaultInvalid()
    {
        MeshHandle h{};
        assert(!h.IsValid());
        assert(!static_cast<bool>(h));
        assert(h == MeshHandle::Invalid());
        std::fprintf(stdout, "  [PASS] default is invalid\n");
    }

    void TestExplicitValue()
    {
        MeshHandle h{123};
        assert(h.IsValid());
        assert(static_cast<bool>(h));
        assert(h.Value() == 123u);
        std::fprintf(stdout, "  [PASS] explicit value\n");
    }

    void TestComparisons()
    {
        MeshHandle a{1};
        MeshHandle b{1};
        MeshHandle c{2};
        assert(a == b);
        assert(a != c);
        assert(a < c);
        assert(!(c < a));
        std::fprintf(stdout, "  [PASS] comparisons\n");
    }

    void TestHashAndUnorderedMap()
    {
        std::unordered_map<MeshHandle, int> table;
        table[MeshHandle{10}] = 100;
        table[MeshHandle{20}] = 200;
        assert(table.size() == 2);
        assert(table.at(MeshHandle{10}) == 100);
        assert(table.at(MeshHandle{20}) == 200);
        assert(table.find(MeshHandle{30}) == table.end());
        std::fprintf(stdout, "  [PASS] std::hash + unordered_map\n");
    }

    void TestInvalidConstant()
    {
        auto invalidValue = MeshHandle::Invalid().Value();
        assert(invalidValue == MeshHandle{}.Value());
        // 任何不等于 invalid sentinel 的值都视作 valid——包含 0。
        MeshHandle zero{0};
        assert(zero.IsValid());
        std::fprintf(stdout, "  [PASS] invalid sentinel\n");
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[HandleTest] running\n");
    TestDefaultInvalid();
    TestExplicitValue();
    TestComparisons();
    TestHashAndUnorderedMap();
    TestInvalidConstant();
    std::fprintf(stdout, "[HandleTest] all tests passed.\n");
    return 0;
}
