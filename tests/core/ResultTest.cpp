// Result<T, E> / Result<void, E> / ToString 的最小单元测试。
//
// 不引入测试框架；用 <cassert> + 独立 main()。assert 在 Release build
// 下会被剥掉——CI 与本地都按 Debug 跑 ctest，已经够用。

#include <orange/engine/core/Result.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using Orange::Engine::Result;
using Orange::Engine::ResultCode;
using Orange::Engine::ToString;

namespace
{

    void TestOkValue()
    {
        Result<int> r{42};
        assert(r.IsOk());
        assert(!r.IsErr());
        assert(static_cast<bool>(r));
        assert(r.Value() == 42);
        assert(r.ValueOr(-1) == 42);
        std::fprintf(stdout, "  [PASS] ok value\n");
    }

    void TestErrValue()
    {
        Result<int> r{ResultCode::NotFound};
        assert(!r.IsOk());
        assert(r.IsErr());
        assert(!static_cast<bool>(r));
        assert(r.Error() == ResultCode::NotFound);
        assert(r.ValueOr(-1) == -1);
        std::fprintf(stdout, "  [PASS] err value\n");
    }

    void TestVoidOk()
    {
        Result<void> r{};
        assert(r.IsOk());
        assert(!r.IsErr());
        std::fprintf(stdout, "  [PASS] void ok\n");
    }

    void TestVoidErr()
    {
        Result<void> r{ResultCode::IoError};
        assert(!r.IsOk());
        assert(r.IsErr());
        assert(r.Error() == ResultCode::IoError);
        std::fprintf(stdout, "  [PASS] void err\n");
    }

    void TestMoveOnlyValue()
    {
        // Result 必须能装 move-only 的值（如 unique_ptr 风格），这里用
        // std::string 验证基本的 move 行为。
        Result<std::string> r{std::string{"hello"}};
        assert(r.IsOk());
        std::string moved = std::move(r).Value();
        assert(moved == "hello");
        std::fprintf(stdout, "  [PASS] move-only value\n");
    }

    void TestToString()
    {
        // 抽样几个枚举值，确认 ToString 不返回 nullptr 且分支不漏。
        assert(std::strcmp(ToString(ResultCode::Ok), "Ok") == 0);
        assert(std::strcmp(ToString(ResultCode::InvalidArgument), "InvalidArgument") == 0);
        assert(std::strcmp(ToString(ResultCode::SchemaMismatch), "SchemaMismatch") == 0);
        assert(std::strcmp(ToString(ResultCode::InternalError), "InternalError") == 0);
        // 越界/未知值落入默认分支，绝不返回 nullptr。
        assert(ToString(static_cast<ResultCode>(0xFFFFFFFFu)) != nullptr);
        std::fprintf(stdout, "  [PASS] ToString\n");
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[ResultTest] running\n");
    TestOkValue();
    TestErrValue();
    TestVoidOk();
    TestVoidErr();
    TestMoveOnlyValue();
    TestToString();
    std::fprintf(stdout, "[ResultTest] all tests passed.\n");
    return 0;
}
