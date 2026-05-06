// `find_package(OrangeEngine)` 消费者入口。
//
// 同时触碰：
//   * 一个生成头（OrangeEngineVersion.h）—— 验证 configure_file 路径
//     与"把生成头装进 <prefix>/include/orange/engine/" 两条 install
//     规则；
//   * 一个源树公共头（Result.h）—— 验证整片 `include/orange/engine/`
//     被 install(DIRECTORY ...) 真的复制了过去；
//   * 一个需要静态库符号（ToString）的调用 —— 验证 OrangeEngine
//     namespaced target 的 link 接口完整可用。

#include <orange/engine/OrangeEngineVersion.h>
#include <orange/engine/core/Result.h>

#include <cstdio>
#include <cstring>

int main()
{
    using Orange::Engine::ResultCode;
    using Orange::Engine::ToString;

    const char* okText = ToString(ResultCode::Ok);
    if (okText == nullptr || std::strcmp(okText, "Ok") != 0)
    {
        std::fprintf(stderr,
                     "config_smoke_consumer: ToString(Ok) 返回了非预期值\n");
        return 1;
    }

    std::fprintf(stdout,
                 "config_smoke_consumer: OrangeEngine %s\n",
                 ORANGE_ENGINE_VERSION_STRING);
    return 0;
}
