#ifndef ORANGE_ENGINE_APP_APP_CONFIG_H
#define ORANGE_ENGINE_APP_APP_CONFIG_H

// ---------------------------------------------------------------------------
// AppConfig —— 给 AppHost::Create 用的参数包。刻意保持为 plain
// aggregate：调用方可以用 designated initializer 配置；后续添加字段
// 也不会破坏既有调用点的 ABI。
//
// 当前只声明引擎实际消费的字段。固定步长物理 dt、profiler 控制、
// asset 搜索根目录等会随对应模块上线时再补，不在这里预先占位。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/platform/Window.h>

namespace Orange::Engine
{

    struct AppConfig
    {
        Platform::WindowDesc window{};

        // 把呈现帧率限制在显示器刷新率上。当前没有别的 pacing 模式，所以
        // 这个字段更像是描述性配置；待 swap-chain 在渲染器侧接通后会实际
        // 生效。
        bool vsync{true};
    };

} // namespace Orange::Engine

#endif // ORANGE_ENGINE_APP_APP_CONFIG_H
